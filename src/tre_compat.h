/* SPDX-License-Identifier: MIT */
#pragma once

#include <tre/render.h>

/* Engine-private bridge for the ABI 1.x dtr/dte_ui compatibility facade. */
int dtr_compat_begin_image(struct tre_surface *surface,
                           struct tre_render_ctx *ctx);
