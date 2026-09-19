/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <zmk/dongle_theme/theme.h>

static int mounts, gestures, frames, draws;
static dte_result_t mount_theme(int32_t w, int32_t h, uint32_t now) {
  assert(w == 280 && h == 240 && now == 0);
  mounts++;
  return DTE_STATUS_OK;
}
static void gesture_theme(int32_t kind, uint32_t now) {
  assert(kind == DTE_TAP && now == 25);
  gestures++;
}
static dte_result_t frame_theme(const struct dte_snapshot *snapshot,
                                uint32_t now, struct dte_frame_result *result) {
  assert(snapshot->abi_version == DTE_ABI_VERSION_V1_2 && snapshot->wpm == 123);
  frames++;
  result->flags = DTE_RENDER_FRAME_CHANGED | DTE_RENDER_DEADLINE_VALID;
  result->next_frame_at_ms = now + 250;
  result->dirty_count = 1;
  result->dirty[0] = (struct dte_rect){10, 20, 4, 3};
  return DTE_STATUS_OK;
}
static dte_result_t draw_theme(const struct dte_snapshot *snapshot,
                               uint32_t now, const struct dte_canvas *canvas) {
  (void)snapshot;
  (void)now;
  draws++;
  assert(canvas->origin_x == 10 && canvas->origin_y == 20 &&
         canvas->width == 4 && canvas->height == 3);
  for (int y = 0; y < 3; y++)
    for (int x = 0; x < 4; x++)
      canvas->pixels[y * canvas->stride_pixels + x] = 0x1234;
  return DTE_STATUS_OK;
}
const struct dte_theme dte_selected_theme =
    DTE_THEME_INIT("api-1.2-test", DTE_THEME_CAP_GESTURE, mount_theme,
                   gesture_theme, frame_theme, draw_theme);

int main(void) {
  assert(DTE_ABI_VERSION_V1_2 == 0x0102u);
  assert(dte_validate_theme(&dte_selected_theme) == DTE_STATUS_OK);
  assert(dte_init_ex(1, 1) == DTE_STATUS_UNSUPPORTED_DISPLAY);
  assert(dte_init_ex(280, 240) == DTE_STATUS_OK && mounts == 1);
  struct dte_snapshot snapshot = DTE_SNAPSHOT_INIT;
  snapshot.wpm = 123;
  snapshot.battery_count = 3;
  memcpy(snapshot.layer_name, "FN", 3);
  assert(dte_set_snapshot(&snapshot) == DTE_STATUS_OK);
  union {
    struct dte_snapshot align;
    struct {
      uint8_t prefix[DTE_SNAPSHOT_REQUIRED_SIZE];
      uint32_t canary;
    } data;
  } short_snapshot = {0};
  short_snapshot.data.canary = 0xa5a55a5au;
  struct dte_snapshot *short_snapshot_api =
      (struct dte_snapshot *)short_snapshot.data.prefix;
  short_snapshot_api->abi_version = DTE_ABI_VERSION_V1_2;
  short_snapshot_api->struct_size = DTE_SNAPSHOT_REQUIRED_SIZE;
  short_snapshot_api->valid_mask = DTE_SNAPSHOT_VALID_WPM;
  short_snapshot_api->wpm = 123;
  assert(dte_set_snapshot(short_snapshot_api) == DTE_STATUS_OK);
  assert(short_snapshot.data.canary == 0xa5a55a5au);
  dte_touch(10, 20, 1, 0);
  dte_touch(10, 20, 0, 25);
  assert(gestures == 1);
  struct dte_frame_result result = DTE_FRAME_RESULT_INIT;
  assert(dte_frame(1000, &result) == DTE_STATUS_OK && frames == 1);
  assert(result.dirty_count == 1 && result.next_frame_at_ms == 1250);
  uint16_t pixels[12] = {0};
  struct dte_canvas canvas = DTE_CANVAS_INIT;
  canvas.scene_width = 280;
  canvas.scene_height = 240;
  canvas.origin_x = 10;
  canvas.origin_y = 20;
  canvas.width = 4;
  canvas.height = 3;
  canvas.stride_pixels = 4;
  canvas.buffer_size = sizeof(pixels);
  canvas.pixels = pixels;
  assert(dte_draw(1000, &canvas) == DTE_STATUS_OK && draws == 1);
  for (int i = 0; i < 12; i++)
    assert(pixels[i] == 0x1234);
  canvas.buffer_size = 2;
  assert(dte_draw(1000, &canvas) == DTE_STATUS_CAPACITY);
  union {
    struct dte_frame_result align;
    struct {
      uint8_t prefix[DTE_FRAME_RESULT_REQUIRED_SIZE];
      uint32_t canary;
    } data;
  } short_result = {0};
  short_result.data.canary = 0x5aa5a55au;
  struct dte_frame_result *short_result_api =
      (struct dte_frame_result *)short_result.data.prefix;
  short_result_api->abi_version = DTE_ABI_VERSION_V1_2;
  short_result_api->struct_size = DTE_FRAME_RESULT_REQUIRED_SIZE;
  assert(dte_frame(2000, short_result_api) == DTE_STATUS_OK && frames == 2);
  assert(short_result_api->struct_size == DTE_FRAME_RESULT_REQUIRED_SIZE);
  assert(short_result.data.canary == 0x5aa5a55au);
  return 0;
}
