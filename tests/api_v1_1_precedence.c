/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <zmk/dongle_theme/theme.h>

static int v1_mounts;
static int v1_1_mounts;

static void mount_v1(int width, int height, uint32_t now) {
  (void)width;(void)height;(void)now;v1_mounts++;
}
static void gesture_v1(int kind, uint32_t now) {(void)kind;(void)now;}
static int render_v1(const struct dte_snapshot *snapshot, uint32_t now,
                     uint16_t *pixels) {
  (void)snapshot;(void)now;(void)pixels;return 0;
}
static enum dte_status mount_v1_1(int32_t width, int32_t height, uint32_t now) {
  (void)width;(void)height;(void)now;v1_1_mounts++;return DTE_STATUS_OK;
}
static enum dte_status render_v1_1(const struct dte_snapshot_v1_1 *snapshot,
                                 uint32_t now, uint16_t *pixels,
                                 struct dte_render_result_v1_1 *result) {
  (void)snapshot;(void)now;(void)pixels;(void)result;return DTE_STATUS_OK;
}

const struct dte_theme dte_selected_theme = {
    DTE_ABI_VERSION, "valid-v1", mount_v1, gesture_v1, render_v1};
const struct dte_theme_v1_1 dte_selected_theme_v1_1 = DTE_THEME_V1_1_INIT(
    "valid-1.1", 0, mount_v1_1, NULL, render_v1_1);

int main(void) {
  assert(dte_init_ex(280, 240) == DTE_STATUS_OK);
  assert(dte_active_abi_version() == DTE_ABI_VERSION_V1_1);
  assert(v1_mounts == 0 && v1_1_mounts == 1);
  return 0;
}
