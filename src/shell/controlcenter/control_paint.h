// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <algorithm>
#include <string>

#include "ui/painter.h"
#include "ui/text_layout.h"
#include "ui/theme.h"

namespace cutefish::shell::control_paint {

inline bool contains(const ui::RectF &rect, ui::PointF point)
{
    return point.x >= rect.x && point.x < rect.right() && point.y >= rect.y && point.y < rect.bottom();
}

inline ui::RectF inset(ui::RectF rect, double margin)
{
    return {rect.x + margin, rect.y + margin, std::max(0.0, rect.width - margin * 2),
            std::max(0.0, rect.height - margin * 2)};
}

inline ui::PointF center(const ui::RectF &rect) { return {rect.x + rect.width / 2, rect.y + rect.height / 2}; }
inline ui::RectF translated(ui::RectF rect, double x, double y)
{
    rect.x += x;
    rect.y += y;
    return rect;
}

inline void text(ui::Painter &painter, const ui::RectF &rect, std::string text, double size,
                 ui::Color ink, bool bold = false, bool centered = false)
{
    if (rect.width <= 0 || rect.height <= 0 || text.empty())
        return;
    ui::TextLayout line(std::move(text), {ui::Theme::current().fontFamily, size, bold}, painter.scale());
    line.setDefaultColor(ink);
    line.beginLayout();
    line.createLine(rect.width, {});
    line.endLayout();
    const auto &info = line.lineAt(0);
    if (info.naturalTextWidth > rect.width) {
        size_t end = line.xToCursor(0, std::max(0.0, rect.width - size * 1.4));
        line.setText(line.text().substr(0, end) + "…");
        line.beginLayout();
        line.createLine(rect.width, {});
        line.endLayout();
    }
    const auto &fit = line.lineAt(0);
    painter.save();
    painter.clip(rect);
    line.draw(painter, {rect.x + (centered ? std::max(0.0, (rect.width - fit.naturalTextWidth) / 2) : 0),
                       rect.y + (rect.height - fit.ascent - fit.descent) / 2});
    painter.restore();
}

inline void paragraph(ui::Painter &painter, const ui::RectF &rect, std::string text,
                      double size, ui::Color ink)
{
    if (rect.width <= 0 || rect.height <= 0 || text.empty())
        return;
    ui::TextLayout layout(std::move(text), {ui::Theme::current().fontFamily, size}, painter.scale());
    layout.setDefaultColor(ink);
    layout.beginLayout();
    double y = 0;
    while (y < rect.height) {
        const int index = layout.createLine(rect.width, {0, y});
        if (index < 0)
            break;
        const auto &line = layout.lineAt(index);
        y += std::max(1., line.ascent + line.descent);
    }
    layout.endLayout();
    painter.save();
    painter.clip(rect);
    layout.draw(painter, {rect.x, rect.y});
    painter.restore();
}

inline void sign(ui::Painter &painter, ui::RectF rect, bool plus, ui::Color fill, ui::Color ink)
{
    painter.fillRoundedRect(rect, rect.height / 2, fill);
    const auto c = center(rect);
    painter.drawLine({c.x - 4, c.y}, {c.x + 4, c.y}, ink, 2);
    if (plus)
        painter.drawLine({c.x, c.y - 4}, {c.x, c.y + 4}, ink, 2);
}

} // namespace cutefish::shell::control_paint
