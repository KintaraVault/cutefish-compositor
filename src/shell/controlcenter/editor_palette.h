// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "ui/geometry.h"

namespace cutefish::shell::editor_palette {

struct Colors {
    ui::Color ink, muted, surface, raised, border, accent, accentInk;
};

inline Colors forTheme(bool dark)
{
    using ui::Color;
    return dark ? Colors {Color::rgb(0xf5f5f5), Color::rgb(0xb8b8b8), Color::rgb(0x242424),
                          Color::rgb(0x3d3d3d), Color::rgb(0xb3b3b3), Color::rgb(0xdbdbdb), Color::rgb(0x1a1a1a)}
                : Colors {Color::rgb(0x1a1a1a), Color::rgb(0x5c5c5c), Color::rgb(0xebebeb),
                          Color::rgb(0xd1d1d1), Color::rgb(0x4d4d4d), Color::rgb(0x3d3d3d), Color::rgb(0xfafafa)};
}

} // namespace cutefish::shell::editor_palette
