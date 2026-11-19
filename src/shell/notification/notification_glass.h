// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "render/glass.h"

namespace cutefish::shell {

inline render::GlassFinish notificationFinish(bool dark)
{
    return dark ? render::GlassFinish::NotificationDark : render::GlassFinish::NotificationLight;
}

inline render::GlassParams notificationGlass()
{
    render::GlassParams glass;
    glass.enabled = true;
    glass.blurSigma = 11;
    glass.bevel = 9;
    glass.depth = 8;
    glass.displacement = 6;
    glass.chroma = 0;
    glass.backdropReflection = 0;
    return glass;
}

} // namespace cutefish::shell
