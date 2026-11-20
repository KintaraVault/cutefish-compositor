// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "decoration/window_visual_style.h"

#include <algorithm>
#include <iterator>

#include "ui/theme.h"

namespace cutefish::decoration {

namespace {

constexpr int kRadius = ui::design::windowRadius;
constexpr double kOutlineWidthPx = 1.0;

constexpr render::Color kOutlineColor {0, 0, 0, 0.05f};
// Drawn just inside the outline; only shows on dark content.
constexpr render::Color kHighlightColor {1, 1, 1, 0.15f};
// Dark content needs a real dark edge to separate from the shadow, and a
// brighter rim inside it so the window still reads as raised.
constexpr render::Color kDarkOutlineColor {0, 0, 0, 0.7f};
constexpr render::Color kDarkHighlightColor {1, 1, 1, 0.2f};
constexpr render::Color kShellOutlineColor {0, 0, 0, 0.14f};
// The dock floats over the wallpaper, so it leans on the highlight instead.
constexpr render::Color kDockOutlineColor {0, 0, 0, 0.1f};
constexpr render::Color kDockHighlightColor {1, 1, 1, 0.2f};
constexpr render::Color kDarkDockOutlineColor {0, 0, 0, 0.3f};
constexpr render::Color kDarkDockHighlightColor {1, 1, 1, 0.2f};
// Light enough that the blocked window still reads; ash's 0.5 is for whole
// containers behind system modals.
constexpr float kDimOpacity = 0.20f;
constexpr float kDarkDimOpacity = 0.32f;

// Ash's phantom window: kColorAshPhantomWindowBackgroundColor is the primary
// colour at kAlpha25, edged by views::HighlightBorder's kHighlightBorderNoShadow
// (cros.sys.system-highlight-border outside, cros.sys.system-highlight inside).
constexpr float kSnapPreviewFillAlpha = 0x40 / 255.f;
constexpr render::Color kSnapPreviewOutline {0, 0, 0, 0x14 / 255.f};
constexpr render::Color kSnapPreviewHighlight {1, 1, 1, 0x28 / 255.f};
constexpr render::Color kDarkSnapPreviewHighlight {1, 1, 1, 0x0F / 255.f};

} // namespace

WindowVisualStyle::WindowVisualStyle(bool dark, int accentIndex)
    : radius(kRadius)
    , outlineWidthPx(kOutlineWidthPx)
    , activeOutline(dark ? kDarkOutlineColor : kOutlineColor)
    , inactiveOutline(dark ? kDarkOutlineColor : kOutlineColor)
    , highlight(dark ? kDarkHighlightColor : kHighlightColor)
    , shellOutline(dark ? kDarkOutlineColor : kShellOutlineColor)
    , dockOutline(dark ? kDarkDockOutlineColor : kDockOutlineColor)
    , dockHighlight(dark ? kDarkDockHighlightColor : kDockHighlightColor)
    , dimOpacity(dark ? kDarkDimOpacity : kDimOpacity)
    , snapPreviewOutline(kSnapPreviewOutline)
    , snapPreviewHighlight(dark ? kDarkSnapPreviewHighlight : kSnapPreviewHighlight)
{
    const auto accent = ui::accent::forMode(accentIndex, dark);
    snapPreviewFill = {accent.r / 255.f, accent.g / 255.f, accent.b / 255.f, kSnapPreviewFillAlpha};
}

int WindowVisualStyle::radiusFor(const base::Size &size)
{
    return std::max(0, std::min({kRadius, size.width() / 2, size.height() / 2}));
}

} // namespace cutefish::decoration
