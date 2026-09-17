/* SPDX-License-Identifier: MIT */
#include <lvgl.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/led.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/dt-bindings/input/cst816s-gesture-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/dongle_theme/theme.h>
#include <zmk/dongle_theme/raster.h>
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
static uint32_t draw_us, draw_max_us, draw_count, refresh_us, refresh_count, refresh_start;
static bool animation_open;
static uint32_t animation_last, animation_us, animation_intervals;
static uint32_t last_presented, stats_deadline;
static int measured_fps_x10=-1, published_fps_x10=-1;
static bool transfer_failed;
static bool startup_ready, startup_visible;
static int startup_phase;
static unsigned startup_attempts;
static uint32_t tile_hash[18 * 18], deadline, last_report, frames;
#if IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_PACKED_RECTS)
static uint16_t transfer_pixels[2048] __aligned(4);
#endif
static void frame_work_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(frame_work, frame_work_cb);
static void set_backlight(int percent) {
#if DT_NODE_EXISTS(DT_NODELABEL(disp_bl))
  const struct device *led=DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(disp_bl)));
  if(device_is_ready(led))led_set_brightness(led,DT_NODE_CHILD_IDX(DT_NODELABEL(disp_bl)),percent);
#else
  ARG_UNUSED(percent);
#endif
}
static void startup_repaint_cb(struct k_work *work) {
  ARG_UNUSED(work);
  transfer_failed=true; /* Force all rows even when the RAM snapshot is unchanged. */
  frame_work_cb(NULL);
}
K_WORK_DELAYABLE_DEFINE(startup_repaint_work, startup_repaint_cb);
static void startup_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(startup_work,startup_cb);
static void startup_cb(struct k_work *work) {
  ARG_UNUSED(work);
  if(!startup_ready){startup_ready=true;startup_phase=1;}
  else if(startup_phase==1)startup_phase=2;
  else if(startup_phase==2)startup_phase=0;
  transfer_failed=IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_DIRECT_RGB565);
  LOG_INF("startup phase=%d",startup_phase);
  frame_work_cb(NULL);
  if(startup_phase==1)
    k_work_reschedule_for_queue(zmk_display_work_q(),&startup_work,
                                K_MSEC(CONFIG_ZMK_DONGLE_SCREEN_SPLASH_MS));
  else if(startup_phase==2)
    k_work_reschedule_for_queue(zmk_display_work_q(),&startup_work,
                                K_MSEC(CONFIG_ZMK_DONGLE_SCREEN_DIAL_REVEAL_MS));
}
static void reveal_startup(void) {
  if(!startup_ready||startup_visible||transfer_failed)return;
  set_backlight(CONFIG_ZMK_DONGLE_SCREEN_BRIGHTNESS);startup_visible=true;
  LOG_INF("startup full frame sent; backlight=%d%%",CONFIG_ZMK_DONGLE_SCREEN_BRIGHTNESS);
  if(IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_DIRECT_RGB565))
    k_work_reschedule_for_queue(zmk_display_work_q(),&startup_repaint_work,K_MSEC(200));
}
static void direct_lvgl_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
  ARG_UNUSED(area);ARG_UNUSED(pixels);
  /* A late root invalidation must never paint black over the direct framebuffer. */
  LOG_WRN("suppressed LVGL flush while direct renderer owns the panel");
  lv_display_flush_ready(display);
}
/* Hash tiles instead of holding a second 134400-byte frame buffer. */
static bool invalidate_changed(void) {
  int w = dte_width(), h = dte_height(), idx = 0;
  uint16_t *p = dte_pixels();
  bool rows[18]={false};
  uint32_t changed[18]={0};
  const uint32_t *damage=dtr_dirty_tiles();
  bool any=false;
  for (int y = 0; y < h; y += 16)
    for (int x = 0; x < w; x += 16) {
      if(damage&&!transfer_failed&&!(damage[y/16]&(1u<<(x/16)))){idx++;continue;}
      uint32_t hash = 2166136261u;
      for (int j = y; j < MIN(y + 16, h); j++)
        for (int i = x; i < MIN(x + 16, w); i++)
          hash = (hash ^ p[j * w + i]) * 16777619u;
      if (tile_hash[idx] != hash || transfer_failed) {
        any=true;changed[y/16]|=1u<<(x/16);
        lv_area_t a = {x, y, MIN(x + 15, w - 1), MIN(y + 15, h - 1)};
        if (IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_DIRECT_RGB565)) rows[y/16]=true;
        else lv_obj_invalidate_area(canvas, &a);
        tile_hash[idx] = hash;
      }
      idx++;
    }
  if (IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_DIRECT_RGB565)) {
    const struct device *disp=DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    uint32_t started=k_cycle_get_32();
    bool sent=false;
    transfer_failed=false;
#if IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_PACKED_RECTS)
    struct dte_dirty_rect rect;
    while(dte_next_dirty_rect(changed,w,h,&rect)) {
      int n=0;
      for(int yy=0;yy<rect.height;yy++)for(int xx=0;xx<rect.width;xx++){
        uint16_t c=p[(rect.y+yy)*w+rect.x+xx];
        transfer_pixels[n++]=IS_ENABLED(CONFIG_LV_COLOR_16_SWAP)?__builtin_bswap16(c):c;
      }
      struct display_buffer_descriptor desc={.width=rect.width,.height=rect.height,.pitch=rect.width,.buf_size=n*2};
      int rc=display_write(disp,rect.x,rect.y,&desc,transfer_pixels);
      if(rc){transfer_failed=true;LOG_ERR("display rectangle failed: %d",rc);}
      sent=true;
    }
#else
    int band=0,y,height;
    while (dte_next_dirty_band(rows,h,&band,&y,&height)) {
      uint16_t *start=p+y*w;
      size_t count=(size_t)height*w;
      /* Same byte order as LVGL flush; restore even when display_write fails.
       * Synchronous driver owns the DMA buffer until return. No second frame. */
      if (IS_ENABLED(CONFIG_LV_COLOR_16_SWAP))
        for(size_t i=0;i<count;i++)start[i]=__builtin_bswap16(start[i]);
      struct display_buffer_descriptor desc={.width=w,.height=height,.pitch=w,.buf_size=count*2};
      int rc=display_write(disp,0,y,&desc,start);
      if (IS_ENABLED(CONFIG_LV_COLOR_16_SWAP))
        for(size_t i=0;i<count;i++)start[i]=__builtin_bswap16(start[i]);
      if (rc) {transfer_failed=true;LOG_ERR("display write failed: %d",rc);}
      sent=true;
    }
#endif
    if(sent) {refresh_count++;refresh_us+=k_cyc_to_us_floor32(k_cycle_get_32()-started);}
    return sent&&!transfer_failed;
  }
  return any;
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
  if ((int32_t)(now-stats_deadline)>=0) {
    published_fps_x10=measured_fps_x10;
    stats_deadline=now+500;
  }
  dte_set_display_stats(CONFIG_ZMK_DONGLE_SCREEN_BRIGHTNESS,published_fps_x10);
  dte_set_startup_phase(startup_phase);
  dte_set_layer_name(
      zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(layer)));
  uint32_t started = k_cycle_get_32();
  bool active = dte_render(now);
  uint32_t cost = k_cyc_to_us_floor32(k_cycle_get_32() - started);
  draw_us += cost;
  draw_max_us = MAX(draw_max_us, cost);
  draw_count++;
  bool presented=invalidate_changed();
  if(IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_DIRECT_RGB565)) {
    reveal_startup();
    if(!startup_visible&&transfer_failed&&++startup_attempts<3)
      k_work_reschedule_for_queue(zmk_display_work_q(),&frame_work,K_MSEC(50));
  }
  uint32_t completed=k_cycle_get_32();
  if(!animation_open&&active)last_presented=0;
  if(presented&&animation_open) {
    if(last_presented) {
      uint32_t delta=k_cyc_to_us_floor32(completed-last_presented);
      if(delta>0&&delta<1000000) {
        int sample=(int)(10000000u/delta);
        measured_fps_x10=measured_fps_x10<0?sample:(measured_fps_x10*3+sample)/4;
      }
    }
    last_presented=completed;
  }
  if (animation_open) {
    animation_us+=k_cyc_to_us_floor32(completed-animation_last);
    if(presented)animation_intervals++;
  }
  animation_open=active;
  animation_last=completed;
  frames++;
  if (now - last_report >= 5000) {
    LOG_DBG("rendered %u frames in %u ms (transport refresh is separate)",
            frames, now - last_report);
    frames = 0;
    last_report = now;
  }
  if (active) { /* Rational 60Hz grid: 16/17ms, no accumulated 16ms cadence
                   drift. */
    deadline = ((uint64_t)now * CONFIG_ZMK_DONGLE_SCREEN_FPS / 1000 + 1) * 1000 /
               CONFIG_ZMK_DONGLE_SCREEN_FPS;
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
  case CST816S_GESTURE_CODE_SWIPE_UP: return DTE_LEFT;
  case CST816S_GESTURE_CODE_SWIPE_DOWN: return DTE_RIGHT;
  case CST816S_GESTURE_CODE_SWIPE_LEFT: return DTE_DOWN;
  case CST816S_GESTURE_CODE_SWIPE_RIGHT: return DTE_UP;
  case CST816S_GESTURE_CODE_SINGLE_CLICK: return DTE_TAP;
  case CST816S_GESTURE_CODE_LONG_PRESS: return DTE_LONG_PRESS;
  default: return 0;
  }
}
static void finish_release(void) {
  if (!touch_release_pending) return;
  dte_touch_at(CLAMP(release_sample.y, 0, 279),
            239 - CLAMP(release_sample.x, 0, 239), 0, release_sample.timestamp,k_uptime_get_32());
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
      if ((held || touch_release_pending) && dte_touch_hint(sample.kind, k_uptime_get_32())) {
        touch_hints++;
        changed = true;
        LOG_INF("touch gesture=%d", sample.kind);
      }
      continue;
    }
    if (!sample.down) {
      /* Stock driver reports release BEFORE its EV_DEVICE gesture and omits
       * release coordinates. Coalesce that pair before software tap/swipe. */
      if (!held) dte_touch_cancel();
      release_sample = sample;
      touch_release_pending = true;
      k_work_cancel_delayable(&hold_work);
      k_work_reschedule_for_queue(zmk_display_work_q(), &release_work, K_MSEC(12));
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
   * each one used to block the consumer for hundreds of ms and flood its queue. */
  if (changed) frame_work_cb(NULL);
}
K_WORK_DEFINE(touch_work, touch_work_cb);
static void input_cb(struct input_event *e, void *user) {
  ARG_UNUSED(user);
  if (e->type == INPUT_EV_ABS && e->code == INPUT_ABS_X)
    raw_x = e->value;
  if (e->type == INPUT_EV_ABS && e->code == INPUT_ABS_Y)
    raw_y = e->value;
  int kind = e->type == INPUT_EV_DEVICE ? sensor_gesture(e->code) : 0;
  if (canvas && startup_ready && startup_phase==0 &&
      ((e->type == INPUT_EV_KEY && e->code == INPUT_BTN_TOUCH) || kind)) {
    uint32_t now = k_uptime_get_32();
    if (kind) {
      if (kind == last_hw_kind) return;
      last_hw_kind = kind;
    } else if (e->value) {
      if (input_held) {
        /* Coalesce motion; reserve queue capacity for contact edges/gestures.
         * Release always carries the newest coordinates, not this sample. */
        if (k_msgq_num_used_get(&touch_samples) >= 8 || now - last_move < 24 ||
            (raw_x - queued_x > -3 && raw_x - queued_x < 3 &&
             raw_y - queued_y > -3 && raw_y - queued_y < 3)) return;
      } else last_hw_kind = 0;
      input_held = true;
      queued_x = raw_x; queued_y = raw_y; last_move = now;
    } else input_held = false;
    struct touch_sample s = {raw_x, raw_y, e->value, kind, now};
    if (k_msgq_put(&touch_samples, &s, K_NO_WAIT) != 0)
      atomic_set(&touch_overflow, 1);
    k_work_submit_to_queue(zmk_display_work_q(), &touch_work);
  }
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_CHOSEN(zmk_touch)), input_cb, NULL);
#endif

static void refresh_event(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_REFR_START)
    refresh_start = k_cycle_get_32();
  else if (lv_event_get_code(event) == LV_EVENT_REFR_READY) {
    refresh_us += k_cyc_to_us_floor32(k_cycle_get_32() - refresh_start);
    refresh_count++;
    if(draw_count)reveal_startup();
  }
}
#if IS_ENABLED(CONFIG_LOG)
static void diagnostic_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(diagnostic_work, diagnostic_cb);
static void diagnostic_cb(struct k_work *work) {
  ARG_UNUSED(work);
#if DT_HAS_CHOSEN(zmk_touch)
  LOG_INF("touch ready=%d contacts=%u gestures=%u dropped=%u idle=%d",
          device_is_ready(DEVICE_DT_GET(DT_CHOSEN(zmk_touch))),
          touch_contacts, touch_hints, touch_dropped, asleep);
#endif
  LOG_INF("10s frames=%u draw avg/max=%u/%u us; LVGL refresh=%u avg=%u us",
          draw_count, draw_count ? draw_us / draw_count : 0, draw_max_us,
          refresh_count, refresh_count ? refresh_us / refresh_count : 0);
  LOG_INF("raster avg ring/arcs/text=%u/%u/%u us",
          draw_count ? k_cyc_to_us_floor32(dtr_profile_cycles[0])/draw_count : 0,
          draw_count ? k_cyc_to_us_floor32(dtr_profile_cycles[1])/draw_count : 0,
          draw_count ? k_cyc_to_us_floor32(dtr_profile_cycles[2])/draw_count : 0);
  dtr_profile_cycles[0]=dtr_profile_cycles[1]=dtr_profile_cycles[2]=0;
  LOG_INF("animation intervals=%u fps_x10=%u; direct=%d",
          animation_intervals,animation_us?(uint32_t)((uint64_t)animation_intervals*10000000/animation_us):0,
          IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_DIRECT_RGB565));
#if defined(CONFIG_SOC_NRF52840)
  LOG_INF("clock HFCLKSTAT=0x%08x SPIM3_FREQ=0x%08x ICACHE=0x%08x",
          NRF_CLOCK->HFCLKSTAT,NRF_SPIM3->FREQUENCY,NRF_NVMC->ICACHECNF);
#endif
  animation_us=animation_intervals=0;
  draw_us = draw_count = draw_max_us = refresh_us = refresh_count = 0;
  k_work_reschedule_for_queue(zmk_display_work_q(), &diagnostic_work, K_SECONDS(10));
}
#endif

lv_obj_t *zmk_display_status_screen(void) {
  set_backlight(0);
  startup_ready=startup_visible=false;startup_attempts=0;startup_phase=0;
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
    lv_obj_t *l = lv_label_create(root);
    lv_label_set_text(l, "Unsupported display size");
    return root;
  }
  dte_init(caps.x_resolution, caps.y_resolution);
  dte_set_battery_count(IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_DONGLE_BATTERY)?3:2);
  if (IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_DIRECT_RGB565)) {
    canvas=root; /* readiness/lifetime only; LVGL does not own the framebuffer */
    lv_display_enable_invalidation(lv_display_get_default(),false);
    lv_timer_pause(lv_display_get_refr_timer(lv_display_get_default()));
    lv_display_set_flush_cb(lv_display_get_default(),direct_lvgl_flush);
    transfer_failed=true; /* first full presentation */
  } else {
    canvas = lv_canvas_create(root);
    lv_display_add_event_cb(lv_display_get_default(), refresh_event, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(lv_display_get_default(), refresh_event, LV_EVENT_REFR_READY, NULL);
    lv_canvas_set_buffer(canvas, dte_pixels(), caps.x_resolution,
                         caps.y_resolution, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas, 0, 0);
  }
  wpm = zmk_wpm_get_state();
  k_work_reschedule_for_queue(zmk_display_work_q(),&startup_work,K_MSEC(CONFIG_ZMK_DONGLE_SCREEN_STARTUP_DELAY_MS));
#if DT_HAS_CHOSEN(zmk_touch)
  LOG_INF("CST816S ready=%d; rotation=90; hardware gestures enabled",
          device_is_ready(DEVICE_DT_GET(DT_CHOSEN(zmk_touch))));
#endif
#if IS_ENABLED(CONFIG_LOG)
  k_work_reschedule_for_queue(zmk_display_work_q(), &diagnostic_work, K_SECONDS(10));
#endif
  return root;
}
