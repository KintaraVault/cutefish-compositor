// SPDX-License-Identifier: BSD-3-Clause
#pragma once

struct wl_display;
struct wl_global;

namespace cutefish::wayland {
wl_global* createWallpaperTintGlobal(wl_display* display);
}
