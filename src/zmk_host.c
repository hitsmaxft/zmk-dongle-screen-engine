/* SPDX-License-Identifier: MIT */
#include <lvgl.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/led.h>
#include <zephyr/dt-bindings/input/cst816s-gesture-codes.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/dongle_theme/raster.h>
#include <zmk/dongle_theme/theme.h>
#include <zmk/dongle_theme/transport.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/modifiers_state_changed.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/wpm.h>
#if defined(CONFIG_SOC_NRF52840)
#include <hal/nrf_clock.h>
#endif
LOG_MODULE_REGISTER(dongle_theme, LOG_LEVEL_INF);
static lv_obj_t *canvas;
static int bl = -1, br = -1, bd = -1, wpm, mods;
static struct k_spinlock state_lock;
static bool asleep;
static bool touch_release_pending;
static uint32_t touch_contacts, touch_hints, touch_dropped;
static uint32_t draw_us, draw_max_us, draw_count, refresh_us, refresh_count;
static bool animation_open;
static uint32_t animation_last, animation_us, animation_intervals;
static uint32_t last_presented, stats_deadline;
static int measured_fps_x10 = -1, published_fps_x10 = -1;
static bool transfer_failed;
static bool startup_ready, startup_visible;
static int backlight_percent = CONFIG_ZMK_DONGLE_SCREEN_BRIGHTNESS;
static int startup_phase;
static unsigned startup_attempts;
static uint32_t deadline, last_report, frames;
static uint16_t
    transfer_pixels[CONFIG_ZMK_DONGLE_SCREEN_STRIP_PIXELS] __aligned(4);
static void frame_work_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(frame_work, frame_work_cb);
static void set_backlight(int percent) {
#if DT_NODE_EXISTS(DT_NODELABEL(disp_bl))
  const struct device *led = DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(disp_bl)));
  if (device_is_ready(led))
    led_set_brightness(led, DT_NODE_CHILD_IDX(DT_NODELABEL(disp_bl)), percent);
#else
  ARG_UNUSED(percent);
#endif
}
void dte_host_backlight_set(int percent) {
  backlight_percent = CLAMP(percent, 0, 100);
  if (startup_visible && !asleep)
    set_backlight(backlight_percent);
}
static void startup_repaint_cb(struct k_work *work) {
  ARG_UNUSED(work);
  transfer_failed =
      true; /* Force all rows even when the RAM snapshot is unchanged. */
  frame_work_cb(NULL);
}
K_WORK_DELAYABLE_DEFINE(startup_repaint_work, startup_repaint_cb);
static void startup_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(startup_work, startup_cb);
static void startup_cb(struct k_work *work) {
  ARG_UNUSED(work);
  if (!startup_ready) {
    startup_ready = true;
    startup_phase = 1;
  } else if (startup_phase == 1)
    startup_phase = 2;
  else if (startup_phase == 2)
    startup_phase = 0;
  transfer_failed = IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_DIRECT_RGB565);
  LOG_INF("startup phase=%d", startup_phase);
  frame_work_cb(NULL);
  if (startup_phase == 1)
    k_work_reschedule_for_queue(zmk_display_work_q(), &startup_work,
                                K_MSEC(CONFIG_ZMK_DONGLE_SCREEN_SPLASH_MS));
  else if (startup_phase == 2)
    k_work_reschedule_for_queue(
        zmk_display_work_q(), &startup_work,
        K_MSEC(CONFIG_ZMK_DONGLE_SCREEN_DIAL_REVEAL_MS));
}
static void reveal_startup(void) {
  if (!startup_ready || startup_visible || transfer_failed)
    return;
  set_backlight(backlight_percent);
  startup_visible = true;
  LOG_INF("startup full frame sent; backlight=%d%%", backlight_percent);
  if (IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_DIRECT_RGB565))
    k_work_reschedule_for_queue(zmk_display_work_q(), &startup_repaint_work,
                                K_MSEC(200));
}
static void direct_lvgl_flush(lv_display_t *display, const lv_area_t *area,
                              uint8_t *pixels) {
  ARG_UNUSED(area);
  ARG_UNUSED(pixels);
  /* A late root invalidation must never paint black over the direct
   * framebuffer. */
  LOG_WRN("suppressed LVGL flush while direct renderer owns the panel");
  lv_display_flush_ready(display);
}
/* ABI 1.2+ renders final pixels directly into a bounded strip. The display
 * driver owns each synchronous buffer only until display_write() returns. */
static bool present_frame(uint32_t now, struct dte_frame_result *frame) {
  const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  uint32_t started = k_cycle_get_32();
  bool sent = false;
  if (transfer_failed) {
    frame->flags |= DTE_RENDER_FRAME_CHANGED;
    frame->dirty_count = 1;
    frame->dirty[0] = (struct dte_rect){0, 0, dte_width(), dte_height()};
  }
  transfer_failed = false;
  for (unsigned i = 0; i < frame->dirty_count; i++) {
    const struct dte_rect *rect = &frame->dirty[i];
    int rows = CONFIG_ZMK_DONGLE_SCREEN_STRIP_PIXELS / rect->width;
    if (rows < 1) {
      transfer_failed = true;
      LOG_ERR("render strip too small");
      break;
    }
    for (int y = rect->y; y < rect->y + rect->height; y += rows) {
      int h = MIN(rows, rect->y + rect->height - y);
      struct dte_canvas target = DTE_CANVAS_INIT;
      target.scene_width = dte_width();
      target.scene_height = dte_height();
      target.origin_x = rect->x;
      target.origin_y = y;
      target.width = rect->width;
      target.height = h;
      target.stride_pixels = rect->width;
      target.buffer_size = (uint32_t)rect->width * h * 2u;
      target.pixels = transfer_pixels;
      if (dte_draw(now, &target) != DTE_STATUS_OK) {
        transfer_failed = true;
        LOG_ERR("theme strip render failed");
        break;
      }
      size_t count = (size_t)rect->width * h;
      if (IS_ENABLED(CONFIG_LV_COLOR_16_SWAP))
        for (size_t n = 0; n < count; n++)
          transfer_pixels[n] = __builtin_bswap16(transfer_pixels[n]);
      struct display_buffer_descriptor desc = {.width = rect->width,
                                               .height = h,
                                               .pitch = rect->width,
                                               .buf_size = count * 2u};
      int rc = display_write(disp, rect->x, y, &desc, transfer_pixels);
      if (rc) {
        transfer_failed = true;
        LOG_ERR("display strip failed: %d", rc);
        break;
      }
      sent = true;
    }
    if (transfer_failed)
      break;
  }
  if (sent) {
    refresh_count++;
    refresh_us += k_cyc_to_us_floor32(k_cycle_get_32() - started);
  }
  return sent && !transfer_failed;
}
static void frame_work_cb(struct k_work *work) {
  ARG_UNUSED(work);
  if (!canvas || !startup_ready || asleep || touch_release_pending)
    return;
  uint32_t now = k_uptime_get_32();
  struct zmk_endpoint_instance ep = zmk_endpoint_get_selected();
  int layer = zmk_keymap_highest_layer_active();
  k_spinlock_key_t key = k_spin_lock(&state_lock);
  int sl = bl, sr = br, sd = bd, sw = wpm, sm = mods;
  k_spin_unlock(&state_lock, key);
  dte_set_state(sw, layer, ep.transport, zmk_ble_active_profile_index(), sm, sl,
                sr, sd, -1, -1);
  if ((int32_t)(now - stats_deadline) >= 0) {
    published_fps_x10 = measured_fps_x10;
    stats_deadline = now + 500;
  }
  dte_set_display_stats(backlight_percent, published_fps_x10);
  dte_set_startup_phase(startup_phase);
  dte_set_layer_name(
      zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(layer)));
  uint32_t started = k_cycle_get_32();
  struct dte_frame_result frame = DTE_FRAME_RESULT_INIT;
  dte_result_t frame_status = dte_frame(now, &frame);
  bool active =
      frame_status == DTE_STATUS_OK &&
      (frame.flags & (DTE_RENDER_CONTINUOUS | DTE_RENDER_DEADLINE_VALID));
  uint32_t cost = k_cyc_to_us_floor32(k_cycle_get_32() - started);
  draw_us += cost;
  draw_max_us = MAX(draw_max_us, cost);
  draw_count++;
  bool presented = frame_status == DTE_STATUS_OK && present_frame(now, &frame);
  reveal_startup();
  if (!startup_visible && transfer_failed && ++startup_attempts < 3)
    k_work_reschedule_for_queue(zmk_display_work_q(), &frame_work, K_MSEC(50));
  uint32_t completed = k_cycle_get_32();
  if (!animation_open && active)
    last_presented = 0;
  if (presented && animation_open) {
    if (last_presented) {
      uint32_t delta = k_cyc_to_us_floor32(completed - last_presented);
      if (delta > 0 && delta < 1000000) {
        int sample = (int)(10000000u / delta);
        measured_fps_x10 =
            measured_fps_x10 < 0 ? sample : (measured_fps_x10 * 3 + sample) / 4;
      }
    }
    last_presented = completed;
  }
  if (animation_open) {
    animation_us += k_cyc_to_us_floor32(completed - animation_last);
    if (presented)
      animation_intervals++;
  }
  animation_open = active;
  animation_last = completed;
  frames++;
  if (now - last_report >= 5000) {
    LOG_DBG("rendered %u frames in %u ms (transport refresh is separate)",
            frames, now - last_report);
    frames = 0;
    last_report = now;
  }
  if (active) {
    deadline = (frame.flags & DTE_RENDER_DEADLINE_VALID)
                   ? frame.next_frame_at_ms
                   : ((uint64_t)now * CONFIG_ZMK_DONGLE_SCREEN_FPS / 1000 + 1) *
                         1000 / CONFIG_ZMK_DONGLE_SCREEN_FPS;
    k_work_reschedule_for_queue(
        zmk_display_work_q(), &frame_work,
        K_MSEC(MAX(1, (int32_t)(deadline - k_uptime_get_32()))));
  }
}
static int status_listener(const zmk_event_t *eh) {
  k_spinlock_key_t key = k_spin_lock(&state_lock);
  const struct zmk_peripheral_battery_state_changed *b =
      as_zmk_peripheral_battery_state_changed(eh);
  if (b) {
    if (b->source == 0)
      bl = b->state_of_charge;
    else if (b->source == 1)
      br = b->state_of_charge;
  }
  const struct zmk_wpm_state_changed *w = as_zmk_wpm_state_changed(eh);
  if (w)
    wpm = w->state;
  const struct zmk_battery_state_changed *d = as_zmk_battery_state_changed(eh);
  if (d)
    bd = d->state_of_charge;
  /* ZMK currently declares modifiers_state_changed without raising it. Mirror
   * the working display-widget pattern: use every keycode transition as the
   * wake-up signal, then project the canonical aggregate HID modifier state. */
  if (as_zmk_keycode_state_changed(eh) || as_zmk_modifiers_state_changed(eh))
    mods = zmk_hid_get_explicit_mods();
  k_spin_unlock(&state_lock, key);
  const struct zmk_activity_state_changed *a =
      as_zmk_activity_state_changed(eh);
  if (a)
    asleep = a->state != ZMK_ACTIVITY_ACTIVE;
  if (canvas) {
    if (asleep)
      k_work_cancel_delayable(&frame_work);
    else
      k_work_reschedule_for_queue(zmk_display_work_q(), &frame_work, K_NO_WAIT);
  }
  return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(theme_state, status_listener);
ZMK_SUBSCRIPTION(theme_state, zmk_peripheral_battery_state_changed);
ZMK_SUBSCRIPTION(theme_state, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(theme_state, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(theme_state, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(theme_state, zmk_modifiers_state_changed);
ZMK_SUBSCRIPTION(theme_state, zmk_wpm_state_changed);
ZMK_SUBSCRIPTION(theme_state, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(theme_state, zmk_activity_state_changed);

#if DT_HAS_CHOSEN(zmk_touch)
struct touch_sample {
  int x, y, down, kind;
  uint32_t timestamp;
};
K_MSGQ_DEFINE(touch_samples, sizeof(struct touch_sample), 16, 4);
static int raw_x, raw_y;
static int queued_x, queued_y, last_hw_kind;
static bool input_held;
static uint32_t last_move;
static bool held;
static struct touch_sample release_sample;
static atomic_t touch_overflow;
static int sensor_gesture(int code) {
  /* Same portrait -> landscape rotation as the coordinates below. Values in
   * EV_DEVICE are zero; the gesture lives in code, not value / INPUT_KEY_*. */
  switch (code) {
  case CST816S_GESTURE_CODE_SWIPE_UP:
    return DTE_LEFT;
  case CST816S_GESTURE_CODE_SWIPE_DOWN:
    return DTE_RIGHT;
  case CST816S_GESTURE_CODE_SWIPE_LEFT:
    return DTE_DOWN;
  case CST816S_GESTURE_CODE_SWIPE_RIGHT:
    return DTE_UP;
  case CST816S_GESTURE_CODE_SINGLE_CLICK:
    return DTE_TAP;
  case CST816S_GESTURE_CODE_LONG_PRESS:
    return DTE_LONG_PRESS;
  default:
    return 0;
  }
}
static void finish_release(void) {
  if (!touch_release_pending)
    return;
  dte_touch_at(CLAMP(release_sample.y, 0, 279),
               239 - CLAMP(release_sample.x, 0, 239), 0,
               release_sample.timestamp, k_uptime_get_32());
  held = touch_release_pending = false;
}
static void release_cb(struct k_work *work) {
  ARG_UNUSED(work);
  finish_release();
  frame_work_cb(NULL);
}
K_WORK_DELAYABLE_DEFINE(release_work, release_cb);
static void hold_cb(struct k_work *work) {
  ARG_UNUSED(work);
  if (canvas && !asleep)
    frame_work_cb(NULL);
}
K_WORK_DELAYABLE_DEFINE(hold_work, hold_cb);
static void touch_work_cb(struct k_work *work) {
  ARG_UNUSED(work);
  if (!canvas)
    return;
  if (atomic_set(&touch_overflow, 0)) {
    k_msgq_purge(&touch_samples);
    dte_touch_cancel();
    held = false;
    touch_release_pending = false;
    touch_dropped++;
    k_work_cancel_delayable(&hold_work);
    k_work_cancel_delayable(&release_work);
    LOG_WRN("touch queue overflow; contact cancelled");
    return;
  }
  struct touch_sample sample;
  bool changed = false;
  while (k_msgq_get(&touch_samples, &sample, K_NO_WAIT) == 0) {
    if (sample.kind) {
      if ((held || touch_release_pending) &&
          dte_touch_hint(sample.kind, k_uptime_get_32())) {
        touch_hints++;
        changed = true;
        LOG_INF("touch gesture=%d", sample.kind);
      }
      continue;
    }
    if (!sample.down) {
      /* Stock driver reports release BEFORE its EV_DEVICE gesture and omits
       * release coordinates. Coalesce that pair before software tap/swipe. */
      if (!held)
        dte_touch_cancel();
      release_sample = sample;
      touch_release_pending = true;
      k_work_cancel_delayable(&hold_work);
      k_work_reschedule_for_queue(zmk_display_work_q(), &release_work,
                                  K_MSEC(12));
      continue;
    }
    if (touch_release_pending) {
      finish_release(); /* A second contact must not overwrite the first. */
      k_work_cancel_delayable(&release_work);
    }
    /* Sensor portrait 240x280 -> mdac 0x60 landscape 280x240. */
    dte_touch(CLAMP(sample.y, 0, 279), 239 - CLAMP(sample.x, 0, 239),
              sample.down, sample.timestamp);
    if (sample.down && !held) {
      touch_contacts++;
      LOG_INF("touch down raw=%d,%d", sample.x, sample.y);
      k_work_reschedule_for_queue(zmk_display_work_q(), &hold_work,
                                  K_MSEC(600));
    }
    if (!sample.down)
      k_work_cancel_delayable(&hold_work);
    held = sample.down;
  }
  /* Motion coordinates alone do not change this gesture-based UI. Rendering
   * each one used to block the consumer for hundreds of ms and flood its queue.
   */
  if (changed)
    frame_work_cb(NULL);
}
K_WORK_DEFINE(touch_work, touch_work_cb);
static void input_cb(struct input_event *e, void *user) {
  ARG_UNUSED(user);
  if (e->type == INPUT_EV_ABS && e->code == INPUT_ABS_X)
    raw_x = e->value;
  if (e->type == INPUT_EV_ABS && e->code == INPUT_ABS_Y)
    raw_y = e->value;
  int kind = e->type == INPUT_EV_DEVICE ? sensor_gesture(e->code) : 0;
  if (canvas && startup_ready && startup_phase == 0 &&
      ((e->type == INPUT_EV_KEY && e->code == INPUT_BTN_TOUCH) || kind)) {
    uint32_t now = k_uptime_get_32();
    if (kind) {
      if (kind == last_hw_kind)
        return;
      last_hw_kind = kind;
    } else if (e->value) {
      if (input_held) {
        /* Coalesce motion; reserve queue capacity for contact edges/gestures.
         * Release always carries the newest coordinates, not this sample. */
        if (k_msgq_num_used_get(&touch_samples) >= 8 || now - last_move < 24 ||
            (raw_x - queued_x > -3 && raw_x - queued_x < 3 &&
             raw_y - queued_y > -3 && raw_y - queued_y < 3))
          return;
      } else
        last_hw_kind = 0;
      input_held = true;
      queued_x = raw_x;
      queued_y = raw_y;
      last_move = now;
    } else
      input_held = false;
    struct touch_sample s = {raw_x, raw_y, e->value, kind, now};
    if (k_msgq_put(&touch_samples, &s, K_NO_WAIT) != 0)
      atomic_set(&touch_overflow, 1);
    k_work_submit_to_queue(zmk_display_work_q(), &touch_work);
  }
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_CHOSEN(zmk_touch)), input_cb, NULL);
#endif

#if IS_ENABLED(CONFIG_LOG)
static void diagnostic_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(diagnostic_work, diagnostic_cb);
static void diagnostic_cb(struct k_work *work) {
  ARG_UNUSED(work);
#if DT_HAS_CHOSEN(zmk_touch)
  LOG_INF("touch ready=%d contacts=%u gestures=%u dropped=%u idle=%d",
          device_is_ready(DEVICE_DT_GET(DT_CHOSEN(zmk_touch))), touch_contacts,
          touch_hints, touch_dropped, asleep);
#endif
  LOG_INF("10s frames=%u draw avg/max=%u/%u us; LVGL refresh=%u avg=%u us",
          draw_count, draw_count ? draw_us / draw_count : 0, draw_max_us,
          refresh_count, refresh_count ? refresh_us / refresh_count : 0);
  LOG_INF(
      "raster avg ring/arcs/text=%u/%u/%u us",
      draw_count ? k_cyc_to_us_floor32(dtr_profile_cycles[0]) / draw_count : 0,
      draw_count ? k_cyc_to_us_floor32(dtr_profile_cycles[1]) / draw_count : 0,
      draw_count ? k_cyc_to_us_floor32(dtr_profile_cycles[2]) / draw_count : 0);
  dtr_profile_cycles[0] = dtr_profile_cycles[1] = dtr_profile_cycles[2] = 0;
  LOG_INF("animation intervals=%u fps_x10=%u; direct=%d", animation_intervals,
          animation_us ? (uint32_t)((uint64_t)animation_intervals * 10000000 /
                                    animation_us)
                       : 0,
          IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_DIRECT_RGB565));
#if defined(CONFIG_SOC_NRF52840)
  LOG_INF("clock HFCLKSTAT=0x%08x SPIM3_FREQ=0x%08x ICACHE=0x%08x",
          NRF_CLOCK->HFCLKSTAT, NRF_SPIM3->FREQUENCY, NRF_NVMC->ICACHECNF);
#endif
  animation_us = animation_intervals = 0;
  draw_us = draw_count = draw_max_us = refresh_us = refresh_count = 0;
  k_work_reschedule_for_queue(zmk_display_work_q(), &diagnostic_work,
                              K_SECONDS(10));
}
#endif

lv_obj_t *zmk_display_status_screen(void) {
  backlight_percent = CONFIG_ZMK_DONGLE_SCREEN_BRIGHTNESS;
  set_backlight(0);
  startup_ready = startup_visible = false;
  startup_attempts = 0;
  startup_phase = 0;
  const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  struct display_capabilities caps;
  display_get_capabilities(disp, &caps);
  lv_obj_t *root = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(root, lv_color_black(), 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_set_style_border_width(root, 0, 0);
  if (!((caps.x_resolution == 280 && caps.y_resolution == 240) ||
        (caps.x_resolution == 240 &&
         (caps.y_resolution == 240 || caps.y_resolution == 280)))) {
    LOG_ERR("unsupported display size %ux%u", caps.x_resolution,
            caps.y_resolution);
    return root;
  }
  dte_init(caps.x_resolution, caps.y_resolution);
  dte_set_battery_count(
      IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_DONGLE_BATTERY) ? 3 : 2);
  canvas =
      root; /* readiness/lifetime only; LVGL does not own animation pixels */
  lv_display_enable_invalidation(lv_display_get_default(), false);
  lv_timer_pause(lv_display_get_refr_timer(lv_display_get_default()));
  lv_display_set_flush_cb(lv_display_get_default(), direct_lvgl_flush);
  transfer_failed = true; /* first presentation must cover the complete scene */
  wpm = zmk_wpm_get_state();
  k_work_reschedule_for_queue(
      zmk_display_work_q(), &startup_work,
      K_MSEC(CONFIG_ZMK_DONGLE_SCREEN_STARTUP_DELAY_MS));
#if DT_HAS_CHOSEN(zmk_touch)
  LOG_INF("CST816S ready=%d; rotation=90; hardware gestures enabled",
          device_is_ready(DEVICE_DT_GET(DT_CHOSEN(zmk_touch))));
#endif
#if IS_ENABLED(CONFIG_LOG)
  k_work_reschedule_for_queue(zmk_display_work_q(), &diagnostic_work,
                              K_SECONDS(10));
#endif
  return root;
}
