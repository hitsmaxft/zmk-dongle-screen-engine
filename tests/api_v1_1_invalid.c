/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <zmk/dongle_theme/theme.h>

static void mount_v1(int width, int height, uint32_t now) {
  (void)width;(void)height;(void)now;
}
static void gesture_v1(int kind, uint32_t now) {(void)kind;(void)now;}
static int render_v1(const struct dte_snapshot *snapshot, uint32_t now,
                     uint16_t *pixels) {
  (void)snapshot;(void)now;(void)pixels;return 0;
}

const struct dte_theme dte_selected_theme = {
    DTE_ABI_VERSION, "valid-v1", mount_v1, gesture_v1, render_v1};
/* Presence is intentional: an invalid ABI 1.1 selection must not silently fall
 * back to the otherwise valid v1 descriptor. */
const struct dte_theme_v1_1 dte_selected_theme_v1_1 = {
    .abi_version = DTE_ABI_VERSION_V1_1,
    .struct_size = DTE_THEME_V1_1_REQUIRED_SIZE - 1,
    .id = "invalid-1.1",
};

int main(void) {
  assert(dte_init_ex(280, 240) == DTE_STATUS_STRUCT_TOO_SMALL);
  assert(dte_active_abi_version() == 0);
  assert(dte_last_status() == DTE_STATUS_STRUCT_TOO_SMALL);
  return 0;
}
