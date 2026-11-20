// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <algorithm>
#include <array>

#include "base/geometry.h"
#include "ui/bitmap.h"
#include "ui/wallpaper_tint.h"

namespace cutefish::shell {

// Low-pass desktop colours from the wallpaper already cropped for this
// output. Neither the rendered scene nor any intervening window is read.
class TintField
{
public:
    struct Sum {
        double r = 0, g = 0, b = 0, weight = 0;

        void add(ui::Color color, double amount)
        {
            r += color.r * amount;
            g += color.g * amount;
            b += color.b * amount;
            weight += amount;
        }

        // Add another output's mean, weighted by its visible area in the window.
        void add(const Sum &other, double area)
        {
            if (other.weight <= 0)
                return;
            r += other.r / other.weight * area;
            g += other.g / other.weight * area;
            b += other.b / other.weight * area;
            weight += area;
        }

        ui::WallpaperTint tone() const
        {
            return weight > 0 ? ui::WallpaperTint::fromSample(r / weight, g / weight, b / weight)
                              : ui::WallpaperTint{};
        }
    };

    static TintField solid(ui::Color color)
    {
        TintField field;
        field.m_cells.fill(color);
        field.m_valid = true;
        return field;
    }

    static TintField fromImage(const ui::Bitmap &image)
    {
        TintField field;
        if (image.isNull())
            return field;

        // Stratified sampling keeps the worker's cost bounded without a copy
        // or mipmap of the full-size wallpaper. Offset the samples within
        // their strata to avoid locking onto periodic pixel patterns.
        constexpr int strata = 8;
        constexpr int count = strata * strata;
        for (int cy = 0; cy < grid; ++cy) {
            for (int cx = 0; cx < grid; ++cx) {
                int r = 0, g = 0, b = 0;
                for (int iy = 0; iy < strata; ++iy) {
                    for (int ix = 0; ix < strata; ++ix) {
                        const int px = cx * strata + ix;
                        const int py = cy * strata + iy;
                        const double dx = ((13 * px + 7 * py) % 32 + 0.5) / 32;
                        const double dy = ((17 * px + 5 * py) % 32 + 0.5) / 32;
                        const int x = int((px + dx) * image.width() / (grid * strata));
                        const int y = int((py + dy) * image.height() / (grid * strata));
                        const ui::Color rgb = ui::Color::fromArgb(image.pixel(x, y));
                        // Premultiplied RGB is the wallpaper as seen over black.
                        r += rgb.r;
                        g += rgb.g;
                        b += rgb.b;
                    }
                }
                field.m_cells[cy * grid + cx] = {uint8_t((r + count / 2) / count),
                                                uint8_t((g + count / 2) / count),
                                                uint8_t((b + count / 2) / count), 255};
            }
        }
        field.m_valid = true;
        return field;
    }

    Sum sample(const base::Rect &output, const base::Rect &window) const
    {
        Sum sum;
        const base::Rect visible = output.intersected(window);
        if (!m_valid || visible.isEmpty())
            return sum;

        // Average the footprint, feathered by six percent of the output on
        // either side. Integrate partial cells so moving a window doesn't
        // cause sudden jumps at cell boundaries.
        const auto low = [](int edge, int origin, int size) {
            return std::max(0.0, (double(edge - origin) / size - 0.06) * grid);
        };
        const auto high = [](int edge, int origin, int size) {
            return std::min(double(grid), (double(edge - origin) / size + 0.06) * grid);
        };
        const double left = low(visible.x(), output.x(), output.width());
        const double right = high(visible.right(), output.x(), output.width());
        const double top = low(visible.y(), output.y(), output.height());
        const double bottom = high(visible.bottom(), output.y(), output.height());
        for (int y = int(top); y < bottom; ++y) {
            const double h = std::min(bottom, y + 1.0) - std::max(top, double(y));
            for (int x = int(left); x < right; ++x) {
                const double w = std::min(right, x + 1.0) - std::max(left, double(x));
                sum.add(m_cells[y * grid + x], w * h);
            }
        }
        return sum;
    }

private:
    static constexpr int grid = 16;
    std::array<ui::Color, grid * grid> m_cells{};
    bool m_valid = false;
};

} // namespace cutefish::shell
