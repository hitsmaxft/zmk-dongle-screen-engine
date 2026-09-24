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
#include <zmk/dongle_theme/filter.h>
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
static uint32_t plan_us,plan_max_us,plan_count;
static uint32_t region_us,region_max_us,region_count;
static uint32_t display_us,display_count,present_bytes,present_writes;
static uint32_t frame_us,frame_max_us,frame_count,profile_presented_frames;
static uint32_t hash_us,hash_max_us,hash_count,pack_us,pack_max_us,pack_count;
static uint32_t dirty_rects,dirty_rect_max,damage_frames;
static uint32_t candidate_tiles,changed_tiles;
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
#if IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_FULL_FRAMEBUFFER)
static uint16_t full_pixels[DTE_MAX_PIXELS] __aligned(4);
static bool full_hash_valid;
#else
static uint16_t
    transfer_pixels[CONFIG_ZMK_DONGLE_SCREEN_STRIP_PIXELS] __aligned(4);
#endif
static uint16_t packed_pixels[2048] __aligned(4);
static uint32_t tile_hash[18 * 18];
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
#if IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_FULL_FRAMEBUFFER)
static bool draw_full_region(uint32_t now,const struct dte_dirty_rect *rect,
                             const uint32_t *damage){
  int width=dte_width();
  struct dte_canvas target=DTE_CANVAS_INIT;
  target.scene_width=width;target.scene_height=dte_height();
  target.origin_x=rect->x;target.origin_y=rect->y;
  target.width=rect->width;target.height=rect->height;
  target.stride_pixels=width;
  target.buffer_size=((uint32_t)(rect->height-1)*width+rect->width)*2u;
  target.pixels=&full_pixels[rect->y*width+rect->x];
  dtr_set_canvas_damage(damage,(dte_height()+15)/16);
  uint32_t draw_started=k_cycle_get_32();
  dte_result_t status=dte_draw(now,&target);
  dtr_set_canvas_damage(NULL,0);
  uint32_t draw_elapsed=k_cyc_to_us_floor32(k_cycle_get_32()-draw_started);
  region_us+=draw_elapsed;region_count++;
  if(draw_elapsed>region_max_us)region_max_us=draw_elapsed;
  if(status!=DTE_STATUS_OK)return false;
  if(IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_FILTER_CRT))
    dte_filter_apply(DTE_FILTER_CRT,&target);
  return true;
}
static bool present_frame(uint32_t now, struct dte_frame_result *frame) {
  if(!transfer_failed&&!frame->dirty_count&&
     !(frame->flags&DTE_RENDER_FRAME_CHANGED))return false;
  bool force=transfer_failed||!full_hash_valid;
  const struct device *disp=DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  transfer_failed=false;
  uint32_t candidates[18]={0},changed[18]={0};
  struct dte_dirty_rect full={0,0,dte_width(),dte_height()};
  if(force||!frame->dirty_count){
    if(!draw_full_region(now,&full,NULL)){
      transfer_failed=true;LOG_ERR("theme full render failed");return false;
    }
    dte_mark_rect_tiles(candidates,dte_width(),dte_height(),&full);
  }else{
    int left=dte_width(),top=dte_height(),right=0,bottom=0;
    for(unsigned i=0;i<frame->dirty_count;i++){
      const struct dte_rect *rect=&frame->dirty[i];
      if(rect->x<left)left=rect->x;
      if(rect->y<top)top=rect->y;
      if(rect->x+rect->width>right)right=rect->x+rect->width;
      if(rect->y+rect->height>bottom)bottom=rect->y+rect->height;
    }
    for(unsigned i=0;i<frame->dirty_count;i++){
      const struct dte_rect *rect=&frame->dirty[i];
      struct dte_dirty_rect dirty={rect->x,rect->y,rect->width,rect->height};
      dte_mark_rect_tiles(candidates,dte_width(),dte_height(),&dirty);
    }
    struct dte_dirty_rect region={left,top,right-left,bottom-top};
    if(!draw_full_region(now,&region,candidates)){
      transfer_failed=true;full_hash_valid=false;
      LOG_ERR("theme incremental render failed");return false;
    }
  }
  int ncols=(dte_width()+15)/16,nrows=(dte_height()+15)/16;
  uint32_t hash_started=k_cycle_get_32();
  uint32_t candidate_count=0,changed_count=0;
  for(int ty=0;ty<nrows;ty++)for(int tx=0;tx<ncols;tx++){
    if(!(candidates[ty]&(1u<<tx)))continue;
    candidate_count++;
    uint32_t hash=dte_hash_rgb565_tile(full_pixels,dte_width(),0,0,tx*16,ty*16,
                                       dte_width(),dte_height());
    int index=ty*18+tx;
    if(force||tile_hash[index]!=hash){changed[ty]|=1u<<tx;changed_count++;}
    tile_hash[index]=hash;
  }
  uint32_t hash_elapsed=k_cyc_to_us_floor32(k_cycle_get_32()-hash_started);
  hash_us+=hash_elapsed;hash_count++;
  if(hash_elapsed>hash_max_us)hash_max_us=hash_elapsed;
  candidate_tiles+=candidate_count;changed_tiles+=changed_count;
  bool sent=false;
  uint32_t pack_elapsed=0;
  struct dte_dirty_rect rect;
  while(dte_next_dirty_rect(changed,dte_width(),dte_height(),&rect)){
    int n=rect.width*rect.height;
    if(n>(int)(sizeof(packed_pixels)/sizeof(packed_pixels[0]))){
      transfer_failed=true;LOG_ERR("full-frame tile rectangle exceeds scratch");break;
    }
    int stride=dte_width();
    uint32_t pack_started=k_cycle_get_32();
    uint16_t *dst=packed_pixels;
    const uint16_t *row=full_pixels+rect.y*stride+rect.x;
    for(int y=0;y<rect.height;y++,row+=stride)
      for(int x=0;x<rect.width;x++){
        uint16_t pixel=row[x];
        *dst++=IS_ENABLED(CONFIG_LV_COLOR_16_SWAP)?__builtin_bswap16(pixel):pixel;
      }
    pack_elapsed+=k_cyc_to_us_floor32(k_cycle_get_32()-pack_started);
    struct display_buffer_descriptor desc={.width=rect.width,.height=rect.height,
      .pitch=rect.width,.buf_size=(size_t)n*2u};
    uint32_t write_started=k_cycle_get_32();
    int rc=display_write(disp,rect.x,rect.y,&desc,packed_pixels);
    display_us+=k_cyc_to_us_floor32(k_cycle_get_32()-write_started);
    display_count++;present_writes++;present_bytes+=(uint32_t)n*2u;
    if(rc){transfer_failed=true;LOG_ERR("display full tiles failed: %d",rc);break;}
    sent=true;
  }
  pack_us+=pack_elapsed;pack_count++;
  if(pack_elapsed>pack_max_us)pack_max_us=pack_elapsed;
  full_hash_valid=!transfer_failed;
  return sent&&!transfer_failed;
}
#else
/* ABI 1.2+ renders final pixels directly into a bounded strip. The display
 * driver owns each synchronous buffer only until display_write() returns. */
static bool present_frame(uint32_t now, struct dte_frame_result *frame) {
  const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  bool sent = false;
  bool force=transfer_failed;
  bool coherent=(frame->flags&DTE_RENDER_CONTINUOUS)!=0;
  if (transfer_failed) {
    frame->flags |= DTE_RENDER_FRAME_CHANGED;
    frame->dirty_count = 1;
    frame->dirty[0] = (struct dte_rect){0, 0, dte_width(), dte_height()};
  }
  if(coherent&&frame->dirty_count>1){
    int left=dte_width(),top=dte_height(),right=0,bottom=0;
    for(unsigned i=0;i<frame->dirty_count;i++){
      const struct dte_rect *r=&frame->dirty[i];
      if(r->x<left)left=r->x;
      if(r->y<top)top=r->y;
      if(r->x+r->width>right)right=r->x+r->width;
      if(r->y+r->height>bottom)bottom=r->y+r->height;
    }
    frame->dirty_count=1;
    frame->dirty[0]=(struct dte_rect){left,top,right-left,bottom-top};
  }
  transfer_failed = false;
  for (unsigned i = 0; i < frame->dirty_count; i++) {
    const struct dte_rect *rect = &frame->dirty[i];
    int rows = CONFIG_ZMK_DONGLE_SCREEN_STRIP_PIXELS / rect->width;
    rows=(rows/16)*16;
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
      uint32_t draw_started=k_cycle_get_32();
      dte_result_t draw_status=dte_draw(now,&target);
      uint32_t draw_elapsed=k_cyc_to_us_floor32(k_cycle_get_32()-draw_started);
      region_us+=draw_elapsed;region_count++;
      if(draw_elapsed>region_max_us)region_max_us=draw_elapsed;
      if (draw_status != DTE_STATUS_OK) {
        transfer_failed = true;
        LOG_ERR("theme strip render failed");
        break;
      }
      if(IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_FILTER_CRT))
        dte_filter_apply(DTE_FILTER_CRT,&target);
      uint32_t changed[18]={0};
      int tx0=rect->x/16,tx1=(rect->x+rect->width-1)/16;
      int ty0=y/16,ty1=(y+h-1)/16;
      for(int ty=ty0;ty<=ty1;ty++)for(int tx=tx0;tx<=tx1;tx++){
        uint32_t hash=dte_hash_rgb565_tile(transfer_pixels,rect->width,rect->x,y,
                                           tx*16,ty*16,rect->width,h);
        int index=ty*18+tx;
        if(force||tile_hash[index]!=hash)changed[ty]|=1u<<tx;
        tile_hash[index]=hash;
      }
      if(coherent&&!IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_REGION_TILE_HASH)){
        int n=rect->width*h;
        if(IS_ENABLED(CONFIG_LV_COLOR_16_SWAP))for(int p=0;p<n;p++)
          transfer_pixels[p]=__builtin_bswap16(transfer_pixels[p]);
        struct display_buffer_descriptor desc={.width=rect->width,.height=h,
          .pitch=rect->width,.buf_size=(size_t)n*2u};
        uint32_t write_started=k_cycle_get_32();
        int rc=display_write(disp,rect->x,y,&desc,transfer_pixels);
        display_us+=k_cyc_to_us_floor32(k_cycle_get_32()-write_started);
        display_count++;present_writes++;present_bytes+=(uint32_t)n*2u;
        if(rc){transfer_failed=true;LOG_ERR("display strip failed: %d",rc);break;}
        sent=true;continue;
      }
      struct dte_dirty_rect changed_rect;
      while(dte_next_dirty_rect(changed,dte_width(),dte_height(),&changed_rect)){
        struct dte_dirty_rect strip={rect->x,y,rect->width,h},packet;
        if(!dte_intersect_dirty_rect(&changed_rect,&strip,&packet))continue;
        int sx=packet.x-rect->x,sy=packet.y-y,n=packet.width*packet.height;
        if(n>(int)(sizeof(packed_pixels)/sizeof(packed_pixels[0]))){
          transfer_failed=true;LOG_ERR("packed rectangle exceeds scratch");break;
        }
        n=0;
        for(int yy=0;yy<packet.height;yy++)for(int xx=0;xx<packet.width;xx++)
          packed_pixels[n++]=transfer_pixels[(sy+yy)*rect->width+sx+xx];
        if(IS_ENABLED(CONFIG_LV_COLOR_16_SWAP))for(int i=0;i<n;i++)
          packed_pixels[i]=__builtin_bswap16(packed_pixels[i]);
        struct display_buffer_descriptor desc={.width=packet.width,.height=packet.height,
          .pitch=packet.width,.buf_size=(size_t)n*2u};
        uint32_t write_started=k_cycle_get_32();
        int rc=display_write(disp,packet.x,packet.y,&desc,packed_pixels);
        display_us+=k_cyc_to_us_floor32(k_cycle_get_32()-write_started);
        display_count++;present_writes++;present_bytes+=(uint32_t)n*2u;
        if(rc){transfer_failed=true;LOG_ERR("display strip failed: %d",rc);break;}
        sent=true;
      }
      if(transfer_failed)break;
    }
    if (transfer_failed)
      break;
  }
  return sent && !transfer_failed;
}
#endif
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
  if(frame_status==DTE_STATUS_OK){
    dirty_rects+=frame.dirty_count;damage_frames++;
    if(frame.dirty_count>dirty_rect_max)dirty_rect_max=frame.dirty_count;
  }
  bool active =
      frame_status == DTE_STATUS_OK &&
      (frame.flags & (DTE_RENDER_CONTINUOUS | DTE_RENDER_DEADLINE_VALID));
  uint32_t cost = k_cyc_to_us_floor32(k_cycle_get_32() - started);
  plan_us += cost;
  plan_max_us = MAX(plan_max_us, cost);
  plan_count++;
  bool presented = frame_status == DTE_STATUS_OK && present_frame(now, &frame);
  reveal_startup();
  if (!startup_visible && transfer_failed && ++startup_attempts < 3)
    k_work_reschedule_for_queue(zmk_display_work_q(), &frame_work, K_MSEC(50));
  uint32_t completed = k_cycle_get_32();
  uint32_t frame_elapsed=k_cyc_to_us_floor32(completed-started);
  frame_us+=frame_elapsed;frame_count++;
  if(frame_elapsed>frame_max_us)frame_max_us=frame_elapsed;
  if(presented)profile_presented_frames++;
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
    uint32_t schedule_now=k_uptime_get_32();
    uint32_t period=MAX(1,1000/CONFIG_ZMK_DONGLE_SCREEN_FPS);
    if(IS_ENABLED(CONFIG_ZMK_DONGLE_SCREEN_MAX_THROUGHPUT)&&
       (frame.flags&DTE_RENDER_CONTINUOUS)&&
       schedule_now-now>=period){
      k_work_reschedule_for_queue(zmk_display_work_q(),&frame_work,K_MSEC(1));
      return;
    }
    deadline=(frame.flags&DTE_RENDER_DEADLINE_VALID)?frame.next_frame_at_ms:0;
    /* Continuous work skips missed grid slots. An explicit Theme deadline is
     * an authored keyframe and therefore runs promptly even when already late. */
    uint32_t grid_deadline=
      ((uint64_t)schedule_now*CONFIG_ZMK_DONGLE_SCREEN_FPS/1000+1)*
      1000/CONFIG_ZMK_DONGLE_SCREEN_FPS;
    if(frame.flags&DTE_RENDER_DEADLINE_VALID){
      if((int32_t)(deadline-schedule_now)<=0)deadline=schedule_now+1;
    }else{
      deadline=grid_deadline;
    }
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
  LOG_INF("10s frame plan=%u avg/max=%u/%u us; region draw=%u avg/max=%u/%u us",
          plan_count,plan_count?plan_us/plan_count:0,plan_max_us,
          region_count,region_count?region_us/region_count:0,region_max_us);
  LOG_INF("frame total=%u avg/max=%u/%u us; dirty rect avg_x10/max=%u/%u",
          frame_count,frame_count?frame_us/frame_count:0,frame_max_us,
          damage_frames?dirty_rects*10/damage_frames:0,dirty_rect_max);
  LOG_INF("hash frames=%u avg/max=%u/%u us; tile candidate/changed avg_x10=%u/%u",
          hash_count,hash_count?hash_us/hash_count:0,hash_max_us,
          hash_count?candidate_tiles*10/hash_count:0,
          hash_count?changed_tiles*10/hash_count:0);
  LOG_INF("pack frames=%u avg/max=%u/%u us",pack_count,
          pack_count?pack_us/pack_count:0,pack_max_us);
  LOG_INF("display frames=%u busy/frame=%u us; writes/frame_x10=%u; bytes/frame=%u",
          profile_presented_frames,
          profile_presented_frames?display_us/profile_presented_frames:0,
          profile_presented_frames?present_writes*10/profile_presented_frames:0,
          profile_presented_frames?present_bytes/profile_presented_frames:0);
  LOG_INF(
      "raster/draw ring/arcs/text/clear/shapes=%u/%u/%u/%u/%u us",
      region_count?k_cyc_to_us_floor32(dtr_profile_cycles[0])/region_count:0,
      region_count?k_cyc_to_us_floor32(dtr_profile_cycles[1])/region_count:0,
      region_count?k_cyc_to_us_floor32(dtr_profile_cycles[2])/region_count:0,
      region_count?k_cyc_to_us_floor32(dtr_profile_cycles[3])/region_count:0,
      region_count?k_cyc_to_us_floor32(dtr_profile_cycles[4])/region_count:0);
  LOG_INF("raster calls ring/arcs/text/clear/shapes=%u/%u/%u/%u/%u",
          dtr_profile_calls[0],dtr_profile_calls[1],dtr_profile_calls[2],
          dtr_profile_calls[3],dtr_profile_calls[4]);
  for(int i=0;i<5;i++)dtr_profile_cycles[i]=dtr_profile_calls[i]=0;
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
  plan_us=plan_count=plan_max_us=0;
  region_us=region_count=region_max_us=0;
  display_us=display_count=present_bytes=present_writes=0;
  frame_us=frame_max_us=frame_count=profile_presented_frames=0;
  hash_us=hash_max_us=hash_count=pack_us=pack_max_us=pack_count=0;
  dirty_rects=dirty_rect_max=damage_frames=candidate_tiles=changed_tiles=0;
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
