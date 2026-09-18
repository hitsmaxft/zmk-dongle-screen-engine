/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zmk/dongle_theme/theme.h>

_Static_assert(DTE_ABI_VERSION == 1, "v1 version macro changed");
_Static_assert(DTE_ENGINE_VERSION_MAJOR == 1u, "engine major version changed");
_Static_assert(DTE_ENGINE_VERSION_MINOR == 1u, "engine minor version changed");
_Static_assert(DTE_ENGINE_VERSION_PATCH == 2u, "engine patch version changed");
_Static_assert(DTE_TAP == 1 && DTE_LONG_PRESS == 6,
               "v1 gesture values changed");
_Static_assert(sizeof(struct dte_snapshot) == 80,
               "v1 snapshot layout changed");
_Static_assert((sizeof(void *) == 4 && sizeof(struct dte_theme) == 20) ||
                   (sizeof(void *) == 8 && sizeof(struct dte_theme) == 40),
               "v1 descriptor size changed");
_Static_assert(offsetof(struct dte_theme, abi_version) == 0,
               "v1 descriptor prefix changed");
_Static_assert(offsetof(struct dte_theme, id) <
                   offsetof(struct dte_theme, mount) &&
                   offsetof(struct dte_theme, mount) <
                       offsetof(struct dte_theme, gesture) &&
                   offsetof(struct dte_theme, gesture) <
                       offsetof(struct dte_theme, render),
               "v1 descriptor field order changed");
_Static_assert(DTE_ABI_VERSION_V1_1 == 0x0101u,
               "ABI 1.1 encoding changed");
_Static_assert(DTE_STATUS_OK == 0 && DTE_STATUS_RENDER_FAILED == -8,
               "ABI 1.1 status values changed");
_Static_assert(DTE_RENDER_FRAME_CHANGED == 1 &&
                   DTE_RENDER_CONTINUOUS == 2 &&
                   DTE_RENDER_DEADLINE_VALID == 4,
               "ABI 1.1 render flag values changed");

static int mounts;
static int gestures;
static int observed_wpm;
static int observed_origin_x;
static int observed_origin_y;

static enum dte_status mount_v1_1(int32_t width, int32_t height, uint32_t now) {
  assert(width == 280 && height == 240 && now == 0);
  mounts++;
  return DTE_STATUS_OK;
}

static void gesture_v1_1(int32_t kind, uint32_t now) {
  assert(kind == DTE_TAP && now == 25);
  gestures++;
  observed_origin_x = dte_gesture_x();
  observed_origin_y = dte_gesture_y();
}

static enum dte_status render_v1_1(const struct dte_snapshot_v1_1 *snapshot,
                                 uint32_t now, uint16_t *pixels,
                                 struct dte_render_result_v1_1 *result) {
  assert(snapshot->abi_version == DTE_ABI_VERSION_V1_1);
  assert(snapshot->struct_size >= DTE_SNAPSHOT_V1_1_REQUIRED_SIZE);
  assert(pixels != NULL);
  observed_wpm = snapshot->wpm;
  pixels[0] = 0x1234;
  result->flags = DTE_RENDER_FRAME_CHANGED | DTE_RENDER_DEADLINE_VALID |
                  (UINT32_C(1) << 31); /* Unknown optional flag is ignored. */
  result->next_frame_at_ms = now + 250;
  return DTE_STATUS_OK;
}

const struct dte_theme_v1_1 dte_selected_theme_v1_1 = DTE_THEME_V1_1_INIT(
    "api-1.1-test", DTE_THEME_CAP_GESTURE, mount_v1_1, gesture_v1_1, render_v1_1);

int main(void) {
  struct {
    struct dte_theme_v1_1 theme;
    uintptr_t future_tail[2];
  } future = {.theme = DTE_THEME_V1_1_INIT("future", 0, mount_v1_1, NULL,
                                         render_v1_1)};
  future.theme.struct_size = (uint16_t)sizeof(future);
  future.theme.capabilities = UINT32_C(1) << 31;
  assert(dte_validate_theme_v1_1(&future.theme) == DTE_STATUS_OK);

  struct dte_theme_v1_1 invalid = future.theme;
  invalid.abi_version = 99;
  assert(dte_validate_theme_v1_1(&invalid) == DTE_STATUS_UNSUPPORTED_ABI);
  invalid = future.theme;
  invalid.struct_size = DTE_THEME_V1_1_REQUIRED_SIZE - 1;
  assert(dte_validate_theme_v1_1(&invalid) == DTE_STATUS_STRUCT_TOO_SMALL);
  invalid = future.theme;
  invalid.render = NULL;
  assert(dte_validate_theme_v1_1(&invalid) == DTE_STATUS_MISSING_CALLBACK);
  invalid = future.theme;
  invalid.capabilities = DTE_THEME_CAP_GESTURE;
  invalid.gesture = NULL;
  assert(dte_validate_theme_v1_1(&invalid) == DTE_STATUS_MISSING_CALLBACK);

  assert(dte_init_ex(1, 1) == DTE_STATUS_UNSUPPORTED_DISPLAY);
  assert(dte_active_abi_version() == 0);
  assert(dte_init_ex(280, 240) == DTE_STATUS_OK);
  assert(mounts == 1);
  assert(dte_active_abi_version() == DTE_ABI_VERSION_V1_1);

  struct {
    struct dte_snapshot_v1_1 snapshot;
    uint32_t future_tail[2];
  } future_snapshot = {.snapshot = DTE_SNAPSHOT_V1_1_INIT};
  future_snapshot.snapshot.struct_size = (uint16_t)sizeof(future_snapshot);
  future_snapshot.snapshot.wpm = 123;
  future_snapshot.snapshot.battery_count = 3;
  future_snapshot.snapshot.battery_left = 80;
  future_snapshot.snapshot.battery_right = 70;
  future_snapshot.snapshot.battery_dongle = 60;
  future_snapshot.snapshot.connected_left = 1;
  future_snapshot.snapshot.connected_right = 1;
  memcpy(future_snapshot.snapshot.layer_name, "FN", 3);
  assert(dte_set_snapshot_v1_1(&future_snapshot.snapshot) == DTE_STATUS_OK);

  struct dte_snapshot_v1_1 short_snapshot = future_snapshot.snapshot;
  short_snapshot.struct_size = DTE_SNAPSHOT_V1_1_REQUIRED_SIZE - 1;
  assert(dte_set_snapshot_v1_1(&short_snapshot) ==
         DTE_STATUS_STRUCT_TOO_SMALL);
  short_snapshot = future_snapshot.snapshot;
  short_snapshot.abi_version = 1;
  assert(dte_set_snapshot_v1_1(&short_snapshot) == DTE_STATUS_UNSUPPORTED_ABI);

  dte_touch(10, 20, 1, 0);
  dte_touch(10, 20, 0, 25);
  assert(gestures == 1 && observed_origin_x == 10 && observed_origin_y == 20);
  assert(dte_gesture_x() == -1 && dte_gesture_y() == -1);

  struct dte_render_result_v1_1 result = DTE_RENDER_RESULT_V1_1_INIT;
  assert(dte_render_v1_1(1000, &result) == DTE_STATUS_OK);
  assert(observed_wpm == 123);
  assert(result.flags ==
         (DTE_RENDER_FRAME_CHANGED | DTE_RENDER_DEADLINE_VALID));
  assert(result.next_frame_at_ms == 1250);
  assert(dte_pixels()[0] == 0x1234);
  assert(dte_render(1000) == 1); /* Legacy wrapper observes the deadline. */

  result = (struct dte_render_result_v1_1)DTE_RENDER_RESULT_V1_1_INIT;
  result.struct_size = DTE_RENDER_RESULT_V1_1_REQUIRED_SIZE - 1;
  assert(dte_render_v1_1(1000, &result) == DTE_STATUS_STRUCT_TOO_SMALL);
  result = (struct dte_render_result_v1_1)DTE_RENDER_RESULT_V1_1_INIT;
  result.abi_version = 1;
  assert(dte_render_v1_1(1000, &result) == DTE_STATUS_UNSUPPORTED_ABI);
  assert(dte_last_status() == DTE_STATUS_UNSUPPORTED_ABI);
  return 0;
}
