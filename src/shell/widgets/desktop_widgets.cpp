// SPDX-License-Identifier: BSD-3-Clause
#include "shell/widgets/desktop_widgets.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <numbers>

#include "base/event_loop.h"
#include "base/i18n.h"
#include "core/main_loop.h"
#include "core/preferences.h"
#include "shell/controlcenter/control_center.h"
#include "shell/controlcenter/control_paint.h"
#include "shell/controlcenter/editor_palette.h"
#include "shell/controlcenter/panel_style.h"
#include "shell/shell_data.h"
#include "shell/shell_window.h"
#include "ui/animation.h"
#include "ui/embedded_window.h"
#include "ui/keys.h"
#include "ui/menu.h"
#include "ui/popup_glass.h"
#include "ui/search_field.h"
#include <unicode/unistr.h>

namespace cutefish::shell {
namespace {
using namespace control_paint;
using namespace std::chrono_literals;
using i18n::tr;

constexpr int kSize = 214;
constexpr int kGap = 21;
constexpr int kWideSize = kSize * 2 + kGap;
constexpr int kRadius = 34;
constexpr base::Point kOrigin {112, 88};
constexpr size_t kMaximumWidgets = 18;

ui::Color ink(double alpha = 1) { return ui::Color::rgb(0xffffff).withAlpha(alpha); }

render::GlassParams widgetGlass()
{
    auto glass = popupGlass(1, 1, 1, .13);
    glass.blurSigma = 14;
    glass.saturation = 1.10f;
    glass.shadowLift = .12f;
    glass.highlightRollOff = .56f;
    glass.bevel = 15;
    glass.depth = 13;
    glass.displacement = 14;
    glass.chroma = .012f;
    glass.rim = .85f;
    glass.backdropReflection = 1.f;
    return glass;
}

std::tm localTime()
{
    const std::time_t now = std::time(nullptr);
    std::tm result {};
    localtime_r(&now, &result);
    return result;
}

std::string date(const std::tm &time, const char *format)
{
    char buffer[100] {};
    std::strftime(buffer, sizeof(buffer), format, &time);
    return buffer;
}
} // namespace

struct DesktopWidgets::Widget {
    Kind kind;
    bool wide;
    base::Point position;
    std::unique_ptr<ShellWindow> window;
    Card *card = nullptr;
};

const char *DesktopWidgets::title(Kind kind)
{
    switch (kind) {
    case Kind::Clock: return tr("Clock");
    case Kind::Calendar: return tr("Calendar");
    case Kind::Battery: return tr("Battery");
    }
    return "";
}

class DesktopWidgets::Card final : public ui::View
{
public:
    Card(DesktopWidgets &owner, Widget &item) : m_owner(owner), m_item(item)
    {
        setName("desktop-widget");
        setTracksHover(true);
    }

    static void artwork(ui::Painter &painter, ui::RectF rect, Kind kind, bool wide, int charge, bool discharging)
    {
        const auto tm = localTime();
        const double featureWidth = wide ? std::min(rect.width * .52, rect.height + 10) : rect.width;
        if (kind == Kind::Clock) {
            if (!wide) {
                const auto c = center(rect);
                const double d = std::min(rect.width, rect.height) / 2 - 6;
                for (int i = 0; i < 60; ++i) {
                    const double angle = i * std::numbers::pi / 30;
                    const auto point = [&](double distance) -> ui::PointF {
                        return {c.x + std::sin(angle) * distance, c.y - std::cos(angle) * distance};
                    };
                    const double edge = d / std::max(std::abs(std::sin(angle)), std::abs(std::cos(angle)));
                    const double trimmed = std::min(edge, d + 19);
                    painter.drawLine(point(trimmed - (i % 5 ? 4 : 7)), point(trimmed), ink(i % 5 ? .16 : .28), 1.15);
                }
                const auto clock = date(tm, "%H:%M");
                ui::FontSpec font {"Nimbus Sans Narrow", rect.width * .355, true};
                const ui::FontMetrics metrics(font, painter.scale());
                const double width = rect.width - 36;
                const double advance = metrics.horizontalAdvanceF(clock);
                if (advance > width)
                    font.pointSize *= width / advance;
                ui::TextLayout line(clock, font, painter.scale());
                line.setDefaultColor(ink(.55));
                line.beginLayout();
                line.createLine(rect.width, {});
                line.endLayout();
                const auto &info = line.lineAt(0);
                line.draw(painter, {c.x - info.naturalTextWidth / 2, c.y - (info.ascent + info.descent) / 2});
                return;
            }
            const ui::PointF c {rect.x + featureWidth / 2, rect.y + rect.height / 2};
            const double radius = std::min(featureWidth, rect.height) * .38;
            const ui::RectF face {c.x - radius, c.y - radius, 2 * radius, 2 * radius};
            const bool dark = panel::dark();
            const auto fill = ui::Color::rgb(dark ? 0x1D1D20 : 0xFDFAF6);
            const auto dial = ui::Color::rgb(dark ? 0xFFFFFF : 0x1D1D20);
            painter.fillRoundedRect(face, radius, fill);
            painter.strokeRoundedRect(inset(face, .5), radius, ink(.17));
            const auto pointAt = [&](double turns, double distance) -> ui::PointF {
                const double a = turns * 2 * std::numbers::pi;
                return {c.x + std::sin(a) * distance, c.y - std::cos(a) * distance};
            };
            for (int i = 0; i < 60; ++i)
                painter.drawLine(pointAt(i / 60.0, radius * (i % 5 ? .9 : .85)), pointAt(i / 60.0, radius * .95), dial.withAlpha(i % 5 ? .23 : .75), i % 5 ? 1 : 2);
            for (int i = 1; i <= 12; ++i) {
                const auto p = pointAt(i / 12.0, radius * .70);
                text(painter, {p.x - 9, p.y - 9, 18, 18}, std::to_string(i), radius > 46 ? 8 : 7, dial.withAlpha(.88), true, true);
            }
            const auto hand = [&](double turns, double length, double width, ui::Color color) {
                ui::Path line;
                line.moveTo(c);
                line.lineTo(pointAt(turns, radius * length));
                painter.strokePath(line, color, width);
            };
            hand((tm.tm_hour % 12 + tm.tm_min / 60.) / 12., .46, 4, dial);
            hand((tm.tm_min + tm.tm_sec / 60.) / 60., .72, 2.8, dial);
            hand(tm.tm_sec / 60., .80, 1.25, ui::Color::rgb(0xF1942B));
            painter.fillRoundedRect({c.x - 2.2, c.y - 2.2, 4.4, 4.4}, 2.2, ui::Color::rgb(0xF1942B));
            if (wide) {
                const double x = rect.x + featureWidth + 5;
                text(painter, {x, rect.y + rect.height / 2 - 25, rect.right() - x - 12, 28}, date(tm, "%H:%M"), 19, ink(), true);
                text(painter, {x, rect.y + rect.height / 2 + 6, rect.right() - x - 12, 22}, date(tm, "%a, %b %e"), 9, ink(.62));
            }
        } else if (kind == Kind::Calendar) {
            const auto col = ui::Color::rgb(panel::dark() ? 0xFF7775 : 0xC82B32);
            text(painter, {rect.x + 16, rect.y + 13, featureWidth - 32, 23}, date(tm, "%A"), 10, col, true);
            text(painter, {rect.x + 15, rect.y + rect.height * .23, featureWidth - 30, rect.height * .52},
                 std::to_string(tm.tm_mday), rect.height > 120 ? 41 : 31, ink(), true);
            text(painter, {rect.x + 16, rect.bottom() - 35, featureWidth - 32, 21}, date(tm, "%B"), 10, ink(.63));
            if (wide) {
                std::tm first = tm;
                first.tm_mday = 1;
                std::mktime(&first);
                std::tm next = first;
                ++next.tm_mon;
                next.tm_mday = 0;
                std::mktime(&next);
                const double start = rect.x + featureWidth - 6;
                const double xstep = (rect.right() - start - 12) / 7;
                const double ystep = (rect.height - 27) / 7;
                const int offset = (first.tm_wday + 6) % 7;
                for (int i = 0; i < 7; ++i) {
                    auto header = first;
                    header.tm_wday = (i + 1) % 7;
                    text(painter, {start + i * xstep, rect.y + 11, xstep, ystep}, date(header, "%a"), 6, ink(.52), true, true);
                }
                for (int day = 1; day <= next.tm_mday; ++day) {
                    const int slot = offset + day - 1;
                    const ui::RectF cell {start + slot % 7 * xstep, rect.y + 11 + (slot / 7 + 1) * ystep, xstep, ystep};
                    if (day == tm.tm_mday)
                        painter.fillRoundedRect(inset(cell, 1), 8, col);
                    text(painter, cell, std::to_string(day), 7.5, day == tm.tm_mday ? ui::Color::rgb(0xFFFFFF) : ink(), day == tm.tm_mday, true);
                }
            }
        } else {
            const double r = std::min(featureWidth, rect.height) * (wide ? .27 : .175);
            const ui::PointF c {wide ? rect.x + featureWidth / 2 : rect.x + r + 19,
                               wide ? rect.y + rect.height * .44 : rect.y + r + 17};
            const ui::RectF ring {c.x - r, c.y - r, r * 2, r * 2};
            ui::Path circle;
            circle.addEllipse(ring);
            painter.strokePath(circle, ink(.15), 6);
            if (charge > 0) {
                const auto tint = charge <= 20 ? ui::Color::rgb(0xff787e) : ink(.94);
                ui::Path arc;
                arc.arcTo(ring, 90, -std::clamp(charge, 0, 100) * 3.6);
                painter.strokePath(arc, tint, 6);
            }
            const ui::RectF screen {c.x - r * .47, c.y - r * .33, r * .94, r * .61};
            painter.strokeRoundedRect(screen, 2, ink(.77), 1.5);
            painter.drawLine({screen.x - 3, screen.bottom() + 2}, {screen.right() + 3, screen.bottom() + 2}, ink(.77), 1.5);
            text(painter, {rect.x + (wide ? 8 : 16), rect.bottom() - rect.height * .42,
                           featureWidth - 24, rect.height * .38}, charge < 0 ? "—" : std::to_string(charge) + "%",
                 rect.height * (wide ? .139 : .209), ink(.78), false, wide);
            const std::string status = charge < 0 ? tr("No Battery") : discharging ? tr("On Battery") : tr("Plugged In");
            if (wide)
                text(painter, {rect.x + featureWidth, rect.y, rect.width - featureWidth - 14, rect.height}, status, 12, ink(), true);
            else if (charge < 0)
                text(painter, {rect.x + 17, rect.bottom() - 30, rect.width - 30, 20}, status, 8, ink(.66));
        }
    }

protected:
    bool acceptsPointer() const override { return true; }
    void paint(ui::Painter &painter) override
    {
        artwork(painter, localRect(), m_item.kind, m_item.wide, m_owner.m_controls.batteryCharge(), m_owner.m_controls.onBattery());
        if (m_owner.m_gallery)
            sign(painter, {6, 6, 22, 22}, false,
                 panel::dark() ? ui::Color::rgb(0x4F4F55) : ui::Color::rgb(0xDFDFE4), ink());
    }

    void pointerPress(const ui::PointerEvent &event) override
    {
        if (event.button == ui::MouseButton::Right) {
            m_menu = std::make_unique<ui::Menu>();
            m_menu->addItem(tr("Edit Widgets…"), [this] { m_owner.edit(); });
            m_menu->addItem(tr("Remove Widget"), [this] { scheduleRemove(); });
            m_menu->popupAt(window(), event.position);
        }
        if (event.button == ui::MouseButton::Left && m_owner.m_gallery) {
            m_press = event.position;
            m_dragging = false;
            m_remove = control_paint::contains({4, 4, 26, 26}, event.position);
        }
    }

    void pointerMove(const ui::PointerEvent &event) override
    {
        if (!m_press || !m_owner.m_gallery || m_remove)
            return;
        const auto delta = event.position - *m_press;
        if (!m_dragging && std::hypot(delta.x, delta.y) < 5)
            return;
        m_dragging = true;
        const auto global = m_item.window->geometry().topLeft();
        m_owner.move(&m_item, global + base::Point(int(std::lround(delta.x)), int(std::lround(delta.y))));
    }

    void pointerRelease(const ui::PointerEvent &event) override
    {
        if (event.button != ui::MouseButton::Left || !m_press)
            return;
        if (m_remove && control_paint::contains({4, 4, 26, 26}, event.position))
            scheduleRemove();
        if (m_dragging)
            m_owner.save();
        m_press.reset();
    }

private:
    void scheduleRemove()
    {
        core::mainLoop()->post([owner = &m_owner, item = &m_item, alive = std::weak_ptr<bool>(m_owner.m_alive)] {
            if (alive.lock())
                owner->remove(item);
        });
    }
    DesktopWidgets &m_owner;
    Widget &m_item;
    std::unique_ptr<ui::Menu> m_menu;
    std::optional<ui::PointF> m_press;
    bool m_remove = false;
    bool m_dragging = false;
};

class DesktopWidgets::Gallery final : public ui::View
{
public:
    explicit Gallery(DesktopWidgets &owner) : m_owner(owner)
    {
        setTracksHover(true);
        m_search = addChild<ui::SearchField>(tr("Search Widgets"));
        m_search->setName("widget-search");
        m_search->setFontSize(15);
        m_search->onTextChanged = [this] {
            const auto kinds = filtered();
            if (!kinds.empty() && std::ranges::find(kinds, m_kind) == kinds.end())
                m_kind = kinds.front();
            m_scroll = 0;
            requestPaint();
        };
    }

    void setRestingGeometry(base::Rect bounds)
    {
        m_resting = bounds;
        position();
    }

    void reveal()
    {
        m_reveal = 0;
        position();
        m_fade = std::make_unique<ui::Animation>(window());
        m_fade->setDuration(340ms);
        m_fade->setEasing(ui::Animation::Easing::OutCubic);
        m_fade->onUpdate = [this](double t) { m_reveal = t; position(); };
        m_fade->start();
    }

protected:
    bool acceptsPointer() const override { return true; }
    bool acceptsFocus() const override { return true; }

    void layout() override
    {
        const auto palette = editor_palette::forTheme(panel::dark());
        m_search->setFill(ui::ColorRecipe(palette.surface.withAlpha(.76)));
        m_search->setGlyphColor(ui::ColorRecipe(palette.muted));
        m_search->setTextColor(ui::ColorRecipe(palette.ink));
        m_search->setPlaceholderColor(ui::ColorRecipe(palette.muted));
        m_search->setGeometry({20, 20, sidebar() - 40, 32});
    }

    void paint(ui::Painter &painter) override
    {
        const auto width = geometry().width;
        const auto palette = editor_palette::forTheme(panel::dark());
        const auto ink = [&](double alpha = 1.) { return palette.ink.withAlpha(alpha); };
        const auto height = geometry().height;
        const auto side = sidebar();
        painter.save();
        painter.clipRoundedRect(localRect(), kRadius);
        painter.fillRect({0, 0, side, height}, palette.surface.withAlpha(.14));
        painter.fillRect({side, 0, 1, height - 60}, palette.border.withAlpha(.20));
        const auto kinds = filtered();
        auto all = kindRect(0);
        if (m_all || contains(all, m_hover))
            painter.fillRoundedRect(all, 10, ink(m_all ? .20 : .09));
        for (int i = 0; i < 4; ++i)
            painter.fillRoundedRect({all.x + 14 + i % 2 * 8., all.y + 16 + i / 2 * 8., 5, 5}, 2.5, ink());
        text(painter, {all.x + 49, all.y, all.width - 55, all.height}, tr("All Widgets"), 11, ink());
        for (size_t i = 0; i < kinds.size(); ++i) {
            const Kind kind = kinds[i];
            const auto row = kindRect(i + 1);
            const bool selected = !m_all && m_kind == kind;
            if (selected || contains(row, m_hover))
                painter.fillRoundedRect(row, 10, ink(selected ? .20 : .09));
            const ui::RectF badge {row.x + 11, row.y + 8, 29, 29};
            painter.fillRoundedRect(badge, 6, palette.raised.withAlpha(.65));
            const auto c = center(badge);
            if (kind == Kind::Clock) {
                painter.strokeRoundedRect(inset(badge, 6), 9, ink(), 1.3);
                painter.drawLine({c.x, c.y - 5}, c, ink(), 1.4);
                painter.drawLine(c, {c.x + 4, c.y + 2}, ink(), 1.4);
            } else if (kind == Kind::Calendar) {
                painter.strokeRoundedRect(inset(badge, 7), 2, ink(), 1.4);
                painter.drawLine({badge.x + 7, badge.y + 12}, {badge.right() - 7, badge.y + 12}, ink(), 1.4);
            } else {
                painter.strokeRoundedRect({c.x - 8, c.y - 4, 15, 8}, 2, ink(), 1.3);
                painter.fillRoundedRect({c.x - 6, c.y - 2, 10, 4}, 1, ink());
                painter.drawLine({c.x + 9, c.y - 1}, {c.x + 9, c.y + 1}, ink(), 1.2);
            }
            text(painter, {row.x + 49, row.y, row.width - 55, row.height}, title(kind), 11, ink(), selected);
        }
        if (!featured())
            text(painter, {side + 26, 16, width - side - 52, 35},
                 !m_search->text().empty() ? tr("Search Results") : m_all ? tr("All Widgets") : title(m_kind), 14, ink(), true);
        const auto clip = previewClip();
        painter.save();
        painter.clip(clip);
        if (kinds.empty())
            text(painter, clip, tr("No Widgets Found"), 16, ink(.78), true, true);
        if (featured()) {
            const ui::RectF hero {clip.x + 6, 22 - m_scroll, clip.width - 12, 151};
            painter.fillRoundedRect(hero, 10, ink(.11));
            text(painter, {hero.x + 20, hero.y + 22, hero.width * .54, 30}, tr("Widgets on the Desktop"), 15, palette.ink, true);
            paragraph(painter, {hero.x + 20, hero.y + 60, hero.width * .48, 78},
                      tr("Keep useful information in view. Drag a widget from the gallery onto your desktop."), 12, palette.muted);
            const ui::RectF picture {hero.x + hero.width * .57, hero.y + 16, hero.width * .39, 119};
            painter.fillRoundedRect(picture, 8, palette.raised.withAlpha(.58));
            const double unit = (picture.width - 25) / 4;
            for (int i = 0; i < 4; ++i)
                painter.fillRoundedRect({picture.x + 8 + i * unit, picture.y + 8, unit * .55, 3}, 1.5, ink(.33));
            const double card = std::min(35., unit);
            painter.fillRoundedRect({picture.x + 13, picture.y + 24, card, card}, 8, ink(.30));
            painter.fillRoundedRect({picture.x + card + 20, picture.y + 24, card * 2 + 6, card}, 8, ink(.23));
            painter.fillRoundedRect({picture.x + card * .6, picture.y + 64, picture.width * .77, 47}, 7, palette.surface.withAlpha(.91));
            for (int i = 0; i < 3; ++i)
                painter.fillRoundedRect({picture.x + card * .6 + 9 + i * unit, picture.y + 73, unit - 4, 25}, 5, ink(.19));
            text(painter, {clip.x + 10, hero.bottom() + 8, clip.width - 20, 30}, tr("Recommendations"), 12, palette.muted, true);
        }
        int group = 0;
        for (const Kind kind : shownKinds()) {
            const auto small = previewRect(group, 0);
            if (m_all && !featured())
                text(painter, {clip.x + 12, small.y - 34, clip.width - 24, 29}, title(kind), 13, ink(), true);
            for (int i = 0; i < 2; ++i) {
                auto rect = previewRect(group, i);
                drawPreview(painter, rect, kind, i);
                const auto label = featured() ? std::string(title(kind))
                                             : std::string(i ? tr("Medium") : tr("Small"));
                text(painter, {rect.x, rect.bottom() + 4, rect.width, 29}, label, 10, palette.muted, false, true);
                if (contains(rect, m_hover) && contains(clip, m_hover))
                    sign(painter, {rect.x + 5, rect.y + 5, 24, 24}, true, ui::Color::rgb(0x32994E), ui::Color::rgb(0xffffff));
            }
            ++group;
        }
        painter.restore();
        painter.fillRect({0, height - 60, width, 60}, palette.surface.withAlpha(.30));
        painter.fillRect({0, height - 60, width, 1}, palette.border.withAlpha(.21));
        text(painter, {23, height - 56, width - 156, 52},
             tr("Click a widget to add it, or drag it onto the desktop."), 11, palette.ink, true);
        const auto r = doneRect();
        painter.fillRoundedRect(r, 10, palette.accent.withAlpha(contains(r, m_hover) ? 1 : .91));
        text(painter, r, tr("Done"), 11, palette.accentInk, true, true);
        painter.restore();
    }

    void hoverMove(const ui::PointerEvent &event) override { m_hover = event.position; requestPaint(); }
    void pointerLeave() override { if (!m_drag) m_hover = {-1, -1}; requestPaint(); }
    void pointerPress(const ui::PointerEvent &event) override
    {
        if (event.button != ui::MouseButton::Left)
            return;
        setFocus();
        m_origin = m_hover = event.position;
        cancelDrag();
        if (contains(previewClip(), m_origin)) {
            const auto kinds = shownKinds();
            for (size_t group = 0; group < kinds.size(); ++group)
                for (int i = 0; i < 2; ++i) {
                    const auto rect = previewRect(group, i);
                    if (contains(rect, m_origin))
                        m_held = Pick {kinds[group], bool(i), rect};
                }
        }
    }
    void pointerMove(const ui::PointerEvent &event) override
    {
        m_hover = event.position;
        if (m_held && std::hypot(m_hover.x - m_origin.x, m_hover.y - m_origin.y) > 5)
            m_drag = true;
        if (m_drag)
            updateDrag();
        requestPaint();
    }
    void pointerRelease(const ui::PointerEvent &event) override
    {
        if (event.button != ui::MouseButton::Left)
            return;
        const auto p = event.position;
        if (control_paint::contains(doneRect(), m_origin) && control_paint::contains(doneRect(), p)) {
            scheduleDone();
        } else if (m_held) {
            if (!m_drag && contains(m_held->rect, p))
                m_owner.add(m_held->kind, m_held->wide);
            else if (m_drag && !contains(localRect(), p))
                m_owner.add(m_held->kind, m_held->wide, dragBounds(p).topLeft());
        } else {
            const auto kinds = filtered();
            for (size_t i = 0; i <= kinds.size(); ++i)
                if (contains(kindRect(i), p) && contains(kindRect(i), m_origin)) {
                    m_all = i == 0;
                    if (i)
                        m_kind = kinds[i - 1];
                    m_scroll = 0;
                }
        }
        cancelDrag();
        m_owner.m_gallery->activate();
        setFocus();
        requestPaint();
    }
    bool keyPress(const ui::KeyEvent &event) override
    {
        if (event.key == ui::key::Escape) {
            if (m_held) {
                cancelDrag();
            } else {
                scheduleDone();
            }
        } else if (event.key == ui::key::Down || event.key == ui::key::Up) {
            const auto kinds = filtered();
            if (kinds.empty())
                return true;
            const int count = int(kinds.size());
            const int current = int(std::ranges::find(kinds, m_kind) - kinds.begin());
            m_kind = kinds[(current + (event.key == ui::key::Down ? 1 : count - 1)) % count];
            m_all = false;
            m_scroll = 0;
            requestPaint();
        } else if (event.key == ui::key::Return) {
            if (!filtered().empty())
                m_owner.add(m_kind, event.modifiers & ui::ShiftModifier);
        } else {
            return false;
        }
        return true;
    }

    bool scroll(const ui::ScrollEvent &event) override
    {
        if (!contains(previewClip(), m_hover) || m_drag)
            return false;
        m_scroll = std::clamp(m_scroll + event.deltaY, 0., std::max(0., contentHeight() - previewClip().height));
        requestPaint();
        return true;
    }

private:
    static bool contains(const ui::RectF &rect, ui::PointF point) { return control_paint::contains(rect, point); }
    struct Pick { Kind kind; bool wide; ui::RectF rect; };
    class LiftedCard final : public ui::View
    {
    public:
        LiftedCard(DesktopWidgets &owner, Kind kind, bool wide) : m_owner(owner), m_kind(kind), m_wide(wide) {}
        void paint(ui::Painter &painter) override
        {
            Card::artwork(painter, localRect(), m_kind, m_wide,
                          m_owner.m_controls.batteryCharge(), m_owner.m_controls.onBattery());
        }
    private:
        DesktopWidgets &m_owner;
        Kind m_kind;
        bool m_wide;
    };
    base::Rect dragBounds(ui::PointF p) const
    {
        const int w = m_held->wide ? kWideSize : kSize;
        const auto r = m_held->rect;
        const auto origin = m_owner.m_gallery->geometry().topLeft();
        return {origin.x() + int(std::lround(p.x - (m_origin.x - r.x) * w / r.width)),
                origin.y() + int(std::lround(p.y - (m_origin.y - r.y) * kSize / r.height)), w, kSize};
    }
    void updateDrag()
    {
        const auto rect = dragBounds(m_hover);
        if (!m_lifted) {
            m_lifted = std::make_unique<ShellWindow>(m_owner.m_shell, wm::WindowRole::Picker, false);
            m_lifted->setFocusPolicy(wm::FocusPolicy::NoFocus);
            m_lifted->setInputRegion(base::Region());
            ui::PopupGlass::prepare(*m_lifted->window());
            m_lifted->setContentView(std::make_unique<LiftedCard>(m_owner, m_held->kind, m_held->wide));
            ui::Path shape;
            shape.addRoundedRect({0, 0, double(rect.width()), double(rect.height())}, kRadius, kRadius);
            m_lifted->setBackgroundBlur({0, 0, rect.width(), rect.height()}, kRadius, widgetGlass(), shape);
            m_lifted->setGeometry(rect);
            m_lifted->show();
        } else {
            m_lifted->setPosition(rect.topLeft());
        }
    }
    void cancelDrag()
    {
        m_lifted.reset();
        m_held.reset();
        m_drag = false;
        requestPaint();
    }
    double sidebar() const { return std::clamp(geometry().width * .24, 132., 240.); }
    bool featured() const { return m_all && m_search->text().empty() && geometry().width >= 640 && geometry().height >= 400; }
    ui::RectF doneRect() const { return {geometry().width - 116, geometry().height - 45, 96, 30}; }
    ui::RectF kindRect(int kind) const { return {14, 64. + 42 * kind, sidebar() - 28, 40}; }
    ui::RectF previewClip() const
    {
        const double top = featured() ? 16 : 61;
        return {sidebar() + 17, top, geometry().width - sidebar() - 34, std::max(0., geometry().height - top - 66)};
    }
    double previewSize() const { return std::max(25., std::min(214., (previewClip().width - 40) / 3.1)); }
    double groupPitch() const { return previewSize() + 93; }
    double contentHeight() const
    {
        const auto count = shownKinds().size();
        double bottom = previewClip().y;
        for (size_t i = 0; i < count; ++i)
            for (int wide : {0, 1})
                bottom = std::max(bottom, previewRect(int(i), wide).bottom() + m_scroll + 36.);
        return bottom - previewClip().y;
    }
    ui::RectF previewRect(int group, int wide) const
    {
        if (featured()) {
            // A compact mixed-size spread rather than three identical rows.
            const auto clip = previewClip();
            const double size = std::clamp((clip.width - 64) / 4., 100., 156.);
            const double gap = size * kGap / kSize;
            const double x = clip.x + (clip.width - (4 * size + 3 * gap)) / 2;
            const double step = size + gap;
            static constexpr int columns[3][2] = {{0, 0}, {1, 2}, {3, 1}};
            static constexpr int rows[3][2] = {{0, 1}, {0, 0}, {1, 2}};
            return {x + step * columns[group][wide], 218 + rows[group][wide] * (size + 37) - m_scroll,
                    wide ? size * 2 + gap : size, size};
        }
        const double size = previewSize();
        const double gap = size * kGap / kSize;
        const double x = previewClip().x + (previewClip().width - size * 3 - gap * 2) / 2;
        return {x + (wide ? size + gap : 0), 103 + groupPitch() * group - m_scroll,
                wide ? size * 2 + gap : size, size};
    }
    std::vector<Kind> filtered() const
    {
        const auto query = icu::UnicodeString::fromUTF8(m_search->text()).foldCase();
        std::vector<Kind> kinds;
        for (const Kind kind : {Kind::Clock, Kind::Calendar, Kind::Battery})
            if (query.isEmpty() || icu::UnicodeString::fromUTF8(title(kind)).foldCase().indexOf(query) >= 0)
                kinds.push_back(kind);
        return kinds;
    }
    std::vector<Kind> shownKinds() const
    {
        auto kinds = filtered();
        if (!m_all && std::ranges::find(kinds, m_kind) != kinds.end())
            return {m_kind};
        return kinds;
    }
    void position()
    {
        m_owner.m_gallery->setGeometry(m_resting.translated({0, int(std::lround(m_resting.height() * (1 - m_reveal)))}));
    }
    void drawPreview(ui::Painter &painter, ui::RectF rect, Kind kind, bool wide) const
    {
        const double radius = rect.height * kRadius / kSize;
        const auto fill = panel::dark() ? ink(.17) : ui::Color::rgb(0x111111).withAlpha(.53);
        const auto shade = panel::dark() ? ink(.08) : ui::Color::rgb(0x111111).withAlpha(.69);
        painter.fillRoundedRect(rect, radius, ui::LinearGradient::vertical(rect, fill, shade));
        Card::artwork(painter, rect, kind, wide, m_owner.m_controls.batteryCharge(), m_owner.m_controls.onBattery());
    }
    void scheduleDone()
    {
        core::mainLoop()->post([owner = &m_owner, alive = std::weak_ptr<bool>(m_owner.m_alive)] {
            if (alive.lock())
                owner->done();
        });
    }
    DesktopWidgets &m_owner;
    ui::SearchField *m_search;
    Kind m_kind = Kind::Clock;
    bool m_all = true;
    double m_scroll = 0;
    double m_reveal = 1;
    base::Rect m_resting;
    ui::PointF m_hover {-1, -1}, m_origin;
    std::optional<Pick> m_held;
    bool m_drag = false;
    std::unique_ptr<ui::Animation> m_fade;
    std::unique_ptr<ShellWindow> m_lifted;
};

DesktopWidgets::DesktopWidgets(Shell *shell, ControlCenter &controls, std::string file)
    : m_shell(shell), m_controls(controls)
    , m_settings(file.empty() ? preferencesFile("desktop-widgets") : std::move(file))
{
    if (!m_settings.contains("Widgets/count")) {
        m_widgets.push_back(std::make_unique<Widget>(Widget {Kind::Calendar, true, kOrigin, {}, nullptr}));
        m_widgets.push_back(std::make_unique<Widget>(Widget {Kind::Clock, false, kOrigin + base::Point(0, kSize + kGap), {}, nullptr}));
        m_widgets.push_back(std::make_unique<Widget>(Widget {Kind::Battery, false, kOrigin + base::Point(kSize + kGap, kSize + kGap), {}, nullptr}));
    }
    const int count = int(std::clamp<int64_t>(m_settings.intValue("Widgets/count"), 0, kMaximumWidgets));
    for (int i = 0; i < count; ++i) {
        const auto prefix = "Widgets/" + std::to_string(i) + '-';
        const auto kind = m_settings.intValue(prefix + "kind", -1);
        const auto position = m_settings.pointValue(prefix + "position");
        if (kind >= 0 && kind <= int(Kind::Battery) && position)
            m_widgets.push_back(std::make_unique<Widget>(Widget {Kind(kind), m_settings.boolValue(prefix + "wide"),
                                                                {position->x, position->y}, {}, nullptr}));
    }
    m_outputChanged = m_shell->outputsChanged.connect([this] { updateOutput(); });
    m_themeChanged = ui::Theme::changed().connect([this] { updateOutput(); repaint(); });
    m_batteryChanged = m_controls.dataChanged.connect([this] { repaint(); });
    m_tick.onTimeout = [this] { repaint(); };
    m_tick.start(1s);
    updateOutput();
}

DesktopWidgets::~DesktopWidgets() = default;

base::Rect DesktopWidgets::area() const
{
    const auto *output = m_shell->output({});
    return output ? m_shell->workArea(*output) : base::Rect();
}

base::Rect DesktopWidgets::geometryFor(const Widget &item) const
{
    const auto r = area();
    const int w = std::min(item.wide ? kWideSize : kSize, r.width());
    const int h = std::min(kSize, r.height());
    return {r.x() + std::clamp(item.position.x(), 0, std::max(0, r.width() - w)),
            r.y() + std::clamp(item.position.y(), 0, std::max(0, r.height() - h)), w, h};
}

void DesktopWidgets::updateOutput()
{
    const auto *output = m_shell->output({});
    const auto r = area();
    if (!output || r.isEmpty()) {
        done();
        for (auto &item : m_widgets)
            if (item->window)
                item->window->hide();
        return;
    }
    const auto material = widgetGlass();
    for (auto &item : m_widgets) {
        if (!item->window) {
            item->window = std::make_unique<ShellWindow>(m_shell, wm::WindowRole::DesktopWidget, false);
            item->window->setFocusPolicy(wm::FocusPolicy::NoFocus);
            ui::PopupGlass::prepare(*item->window->window());
            auto card = std::make_unique<Card>(*this, *item);
            item->card = card.get();
            item->window->setContentView(std::move(card));
        }
        const auto geometry = geometryFor(*item);
        item->window->setOutput(output->name);
        item->window->setGeometry(geometry);
        ui::Path shape;
        shape.addRoundedRect({0, 0, double(geometry.width()), double(geometry.height())}, kRadius, kRadius);
        item->window->setBackgroundBlur({0, 0, geometry.width(), geometry.height()}, kRadius, material, shape);
        item->window->show();
    }
    if (m_gallery) {
        const auto screen = output->bounds;
        const int w = std::min(1028, screen.width() - 24);
        const int h = std::min(552, screen.height() - 48);
        m_gallery->setOutput(output->name);
        m_galleryView->setRestingGeometry({screen.x() + (screen.width() - w) / 2, screen.bottom() - h - 8, w, h});
        const bool dark = panel::dark();
        auto galleryGlass = widgetGlass();
        galleryGlass.tintRed = galleryGlass.tintGreen = galleryGlass.tintBlue = dark ? .055f : 1.f;
        galleryGlass.tintAlpha = dark ? .72f : .53f;
        galleryGlass.shadowLift = 0;
        galleryGlass.highlightRollOff = 0;
        galleryGlass.lenses = {{{0, 0, double(w), double(h)}, kRadius, -1, 1, std::nullopt,
                                dark ? render::GlassFinish::GalleryDark : render::GlassFinish::GalleryLight}};
        ui::Path shape;
        shape.addRoundedRect({0, 0, double(w), double(h)}, kRadius, kRadius);
        m_gallery->setBackgroundBlur({0, 0, w, h}, kRadius, galleryGlass, shape);
    }
}

void DesktopWidgets::repaint()
{
    for (const auto &item : m_widgets)
        if (item->card)
            item->card->requestPaint();
    if (m_galleryView)
        m_galleryView->requestPaint();
}

void DesktopWidgets::edit()
{
    if (m_gallery) {
        m_gallery->activate();
        return;
    }
    if (area().width() < 360 || area().height() < 215)
        return;
    m_gallery = std::make_unique<ShellWindow>(m_shell, wm::WindowRole::Picker, false);
    ui::PopupGlass::prepare(*m_gallery->window());
    auto gallery = std::make_unique<Gallery>(*this);
    m_galleryView = gallery.get();
    m_gallery->setContentView(std::move(gallery));
    m_gallery->onCloseRequested = [this] { done(); };
    updateOutput();
    m_galleryView->reveal();
    m_gallery->show();
    m_gallery->activate();
    m_galleryView->setFocus();
    repaint();
}

void DesktopWidgets::done()
{
    if (!m_gallery)
        return;
    auto retiring = std::shared_ptr<ShellWindow>(std::move(m_gallery));
    m_galleryView = nullptr;
    retiring->hide();
    core::mainLoop()->post([retiring] {});
    repaint();
}

void DesktopWidgets::add(Kind kind, bool wide, std::optional<base::Point> global)
{
    if (m_widgets.size() >= kMaximumWidgets)
        return;
    const auto r = area();
    auto item = std::make_unique<Widget>(Widget {kind, wide, {}, {}, nullptr});
    if (global) {
        item->position = *global - r.topLeft();
    } else {
        const int width = wide ? kWideSize : kSize;
        bool found = false;
        for (int y = std::min(kOrigin.y(), std::max(0, r.height() - kSize)); y + kSize <= r.height() && !found; y += kSize + kGap) {
            for (int x = std::min(kOrigin.x(), std::max(0, r.width() - width)); x + width <= r.width(); x += kSize + kGap) {
                item->position = {x, y};
                if (std::ranges::none_of(m_widgets, [&](const auto &other) {
                        return !geometryFor(*other).intersected(geometryFor(*item)).isEmpty();
                    })) {
                    found = true;
                    break;
                }
            }
        }
        if (!found)
            return;
    }
    item->position = geometryFor(*item).topLeft() - r.topLeft();
    m_widgets.push_back(std::move(item));
    updateOutput();
    save();
}

void DesktopWidgets::remove(Widget *item)
{
    if (std::erase_if(m_widgets, [item](const auto &candidate) { return candidate.get() == item; }))
        save();
}

void DesktopWidgets::move(Widget *item, base::Point global)
{
    const auto origin = area().topLeft();
    item->position = global - origin;
    const auto rect = geometryFor(*item);
    item->position = rect.topLeft() - origin;
    item->window->setPosition(rect.topLeft());
}

void DesktopWidgets::save()
{
    m_settings.remove("Widgets");
    m_settings.setValue("Widgets/count", m_widgets.size());
    for (size_t i = 0; i < m_widgets.size(); ++i) {
        const auto &item = m_widgets[i];
        const auto prefix = "Widgets/" + std::to_string(i) + '-';
        m_settings.setValue(prefix + "kind", int(item->kind));
        m_settings.setValue(prefix + "wide", item->wide);
        m_settings.setValue(prefix + "position", base::Settings::Point {item->position.x(), item->position.y()});
    }
    m_settings.sync();
}

} // namespace cutefish::shell
