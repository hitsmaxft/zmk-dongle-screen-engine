/* SPDX-License-Identifier: MIT */
#pragma once
#include <zmk/dongle_theme/raster.h>
#include <zmk/dongle_theme/theme.h>

#define DTE_RASTER_SCHEDULE_INACTIVE UINT32_C(0)
#define DTE_RASTER_SCHEDULE_CONTINUOUS UINT32_MAX

/* Transitional adapter for existing allocation-free raster Themes. */
#define DTE_THEME_RASTER_ADAPTER(theme_id, mount_fn, gesture_fn, render_fn)    \
  static dte_result_t dte_adapter_mount(int32_t w, int32_t h, uint32_t now) {  \
    mount_fn(w, h, now);                                                       \
    return DTE_STATUS_OK;                                                      \
  }                                                                            \
  static dte_result_t dte_adapter_frame(const struct dte_snapshot *snapshot,   \
                                        uint32_t now,                          \
                                        struct dte_frame_result *result) {     \
    int active = render_fn(snapshot, now, NULL);                               \
    dte_frame_from_raster(result, dte_width(), dte_height(), active);          \
    return DTE_STATUS_OK;                                                      \
  }                                                                            \
  static dte_result_t dte_adapter_draw(const struct dte_snapshot *snapshot,    \
                                       uint32_t now,                           \
                                       const struct dte_canvas *canvas) {      \
    dtr_begin_canvas(canvas);                                                  \
    (void)render_fn(snapshot, now, canvas->pixels);                            \
    dtr_end_canvas();                                                          \
    return DTE_STATUS_OK;                                                      \
  }                                                                            \
  const struct dte_theme dte_selected_theme =                                  \
      DTE_THEME_INIT(theme_id, DTE_THEME_CAP_GESTURE, dte_adapter_mount,       \
                     gesture_fn, dte_adapter_frame, dte_adapter_draw)

/* Raster adapter with an explicit presentation deadline. schedule_fn runs
 * after the dry render pass and returns zero when idle, UINT32_MAX for the
 * legacy continuous grid, or an absolute monotonic deadline. */
#define DTE_THEME_RASTER_DEADLINE_ADAPTER(theme_id, mount_fn, gesture_fn,      \
                                          render_fn, schedule_fn)              \
  static dte_result_t dte_adapter_mount(int32_t w, int32_t h, uint32_t now) {  \
    mount_fn(w, h, now);                                                       \
    return DTE_STATUS_OK;                                                      \
  }                                                                            \
  static dte_result_t dte_adapter_frame(const struct dte_snapshot *snapshot,   \
                                        uint32_t now,                          \
                                        struct dte_frame_result *result) {     \
    (void)render_fn(snapshot, now, NULL);                                      \
    uint32_t raster_schedule = schedule_fn(snapshot, now);                     \
    if (raster_schedule == DTE_RASTER_SCHEDULE_CONTINUOUS)                     \
      dte_frame_from_raster(result, dte_width(), dte_height(), 1);             \
    else                                                                       \
      dte_frame_from_raster_deadline(result, dte_width(), dte_height(),        \
                                     raster_schedule);                         \
    return DTE_STATUS_OK;                                                      \
  }                                                                            \
  static dte_result_t dte_adapter_draw(const struct dte_snapshot *snapshot,    \
                                       uint32_t now,                           \
                                       const struct dte_canvas *canvas) {      \
    dtr_begin_canvas(canvas);                                                  \
    (void)render_fn(snapshot, now, canvas->pixels);                            \
    dtr_end_canvas();                                                          \
    return DTE_STATUS_OK;                                                      \
  }                                                                            \
  const struct dte_theme dte_selected_theme =                                  \
      DTE_THEME_INIT(theme_id, DTE_THEME_CAP_GESTURE, dte_adapter_mount,       \
                     gesture_fn, dte_adapter_frame, dte_adapter_draw)
