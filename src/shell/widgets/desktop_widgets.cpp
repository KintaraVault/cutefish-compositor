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
#include "shell/controlcenter/panel_style.h"
#include "shell/shell_window.h"
#include "ui/animation.h"
#include "ui/embedded_window.h"
#include "ui/keys.h"
#include "ui/menu.h"
#include "ui/popup_glass.h"

namespace cutefish::shell {
namespace {
using namespace control_paint;
using namespace std::chrono_literals;
using i18n::tr;

constexpr int kSize = 214;
constexpr int kGap = 21;
constexpr int kWideSize = kSize * 2 + kGap;
constexpr int kRadius = 34;
constexpr base::Point kOrigin {112, 82};
constexpr size_t kMaximumWidgets = 18;

ui::Color accent() { return ui::Theme::current().highlight; }
ui::Color ink(double alpha = 1) { return panel::textColor(alpha); }

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
                text(painter, inset(rect, 12), date(tm, "%H:%M"), rect.width * .278, ink(.94), true, true);
                return;
            }
            const ui::PointF c {rect.x + featureWidth / 2, rect.y + rect.height / 2};
            const double radius = std::min(featureWidth, rect.height) * .38;
            const ui::RectF face {c.x - radius, c.y - radius, 2 * radius, 2 * radius};
            const auto fill = panel::dark() ? ui::Color::rgb(0x1D1D20) : ui::Color::rgb(0xFDFAF6);
            painter.fillRoundedRect(face, radius, fill);
            painter.strokeRoundedRect(inset(face, .5), radius, ink(.17));
            const auto pointAt = [&](double turns, double distance) -> ui::PointF {
                const double a = turns * 2 * std::numbers::pi;
                return {c.x + std::sin(a) * distance, c.y - std::cos(a) * distance};
            };
            for (int i = 0; i < 60; ++i)
                painter.drawLine(pointAt(i / 60.0, radius * (i % 5 ? .9 : .85)), pointAt(i / 60.0, radius * .95), ink(i % 5 ? .23 : .75), i % 5 ? 1 : 2);
            for (int i = 1; i <= 12; ++i) {
                const auto p = pointAt(i / 12.0, radius * .70);
                text(painter, {p.x - 9, p.y - 9, 18, 18}, std::to_string(i), radius > 46 ? 8 : 7, ink(.88), true, true);
            }
            const auto hand = [&](double turns, double length, double width, ui::Color color) {
                ui::Path line;
                line.moveTo(c);
                line.lineTo(pointAt(turns, radius * length));
                painter.strokePath(line, color, width);
            };
            hand((tm.tm_hour % 12 + tm.tm_min / 60.) / 12., .46, 4, ink());
            hand((tm.tm_min + tm.tm_sec / 60.) / 60., .72, 2.8, ink());
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
                const auto tint = ui::Color::rgb(charge <= 20 ? 0xE74245 : 0x38A565);
                ui::Path arc;
                arc.arcTo(ring, 90, -std::clamp(charge, 0, 100) * 3.6);
                painter.strokePath(arc, tint, 6);
            }
            const ui::RectF screen {c.x - r * .47, c.y - r * .33, r * .94, r * .61};
            painter.strokeRoundedRect(screen, 2, ink(.77), 1.5);
            painter.drawLine({screen.x - 3, screen.bottom() + 2}, {screen.right() + 3, screen.bottom() + 2}, ink(.77), 1.5);
            text(painter, {rect.x + (wide ? 8 : 16), rect.bottom() - rect.height * .42,
                           featureWidth - 24, rect.height * .38}, charge < 0 ? "—" : std::to_string(charge) + "%",
                 rect.height * (wide ? .139 : .193), ink(.95), false, wide);
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
    explicit Gallery(DesktopWidgets &owner) : m_owner(owner) { setTracksHover(true); }

    void reveal()
    {
        m_fade = std::make_unique<ui::Animation>(window());
        m_fade->setDuration(240ms);
        m_fade->setEasing(ui::Animation::Easing::OutCubic);
        setOpacity(.35);
        m_fade->onUpdate = [this](double t) { setOpacity(.35 + .65 * t); };
        m_fade->start();
    }

protected:
    bool acceptsPointer() const override { return true; }
    bool acceptsFocus() const override { return true; }

    void paint(ui::Painter &painter) override
    {
        text(painter, {22, 12, geometry().width - 110, 35}, tr("Desktop Widgets"), 17, ink(), true);
        const auto r = doneRect();
        painter.fillRoundedRect(r, r.height / 2, accent());
        text(painter, r, tr("Done"), 10, ui::Color::rgb(0xFFFFFF), true, true);
        painter.fillRect({0, 55, geometry().width, 1}, ink(.13));
        painter.fillRect({139, 56, 1, geometry().height - 56}, ink(.13));
        for (int i = 0; i < 3; ++i) {
            const auto row = kindRect(i);
            if (int(m_kind) == i || control_paint::contains(row, m_hover))
                painter.fillRoundedRect(row, 8, ink(int(m_kind) == i ? .14 : .07));
            text(painter, inset(row, 9), title(Kind(i)), 10, ink(), int(m_kind) == i);
        }
        const auto fill = panel::dark() ? ui::Color::rgb(0x353538) : ui::Color::rgb(0xF1F0F1);
        for (int i = 0; i < 2; ++i) {
            const auto rect = previewRect(i);
            painter.fillRoundedRect(rect, 21, fill);
            Card::artwork(painter, rect, m_kind, i, m_owner.m_controls.batteryCharge(), m_owner.m_controls.onBattery());
            const ui::RectF label {rect.x, rect.bottom() + 5, rect.width, 26};
            text(painter, label, i ? tr("Medium") : tr("Small"), 9.5, ink(.77), false, true);
            if (control_paint::contains(rect, m_hover))
                sign(painter, {rect.x + 5, rect.y + 5, 22, 22}, true, ui::Color::rgb(0x32994E), ui::Color::rgb(0xFFFFFF));
        }
        if (m_held && m_drag) {
            auto r = previewRect(*m_held);
            r.x = m_hover.x - r.width / 2;
            r.y = m_hover.y - r.height / 2;
            painter.drawShadow(r, 21, 12, ui::Color {0, 0, 0, 90}, {0, 5});
            painter.fillRoundedRect(r, 21, fill);
            Card::artwork(painter, r, m_kind, *m_held, m_owner.m_controls.batteryCharge(), m_owner.m_controls.onBattery());
        }
        text(painter, {155, geometry().height - 40, geometry().width - 172, 25},
             tr("Click a widget to add it, or drag it onto the desktop."), 9, ink(.65), false, true);
    }

    void hoverMove(const ui::PointerEvent &event) override { m_hover = event.position; requestPaint(); }
    void pointerLeave() override { if (!m_drag) m_hover = {-1, -1}; requestPaint(); }
    void pointerPress(const ui::PointerEvent &event) override
    {
        if (event.button != ui::MouseButton::Left)
            return;
        setFocus();
        m_origin = m_hover = event.position;
        m_held.reset();
        m_drag = false;
        for (int i = 0; i < 2; ++i)
            if (control_paint::contains(previewRect(i), m_origin))
                m_held = i;
    }
    void pointerMove(const ui::PointerEvent &event) override
    {
        m_hover = event.position;
        if (m_held && std::hypot(m_hover.x - m_origin.x, m_hover.y - m_origin.y) > 5)
            m_drag = true;
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
            if (!m_drag && control_paint::contains(previewRect(*m_held), p))
                m_owner.add(m_kind, *m_held);
            else if (m_drag && !control_paint::contains(localRect(), p)) {
                const auto origin = m_owner.m_gallery->geometry().topLeft();
                const int w = *m_held ? kWideSize : kSize;
                m_owner.add(m_kind, *m_held, origin + base::Point(int(std::lround(p.x)) - w / 2, int(std::lround(p.y)) - kSize / 2));
            }
        } else {
            for (int i = 0; i < 3; ++i)
                if (control_paint::contains(kindRect(i), p) && control_paint::contains(kindRect(i), m_origin))
                    m_kind = Kind(i);
        }
        m_held.reset();
        m_drag = false;
        requestPaint();
    }
    bool keyPress(const ui::KeyEvent &event) override
    {
        if (event.key == ui::key::Escape) {
            if (m_held) {
                m_held.reset();
                m_drag = false;
                requestPaint();
            } else {
                scheduleDone();
            }
        } else if (event.key == ui::key::Down || event.key == ui::key::Up) {
            m_kind = Kind((int(m_kind) + (event.key == ui::key::Down ? 1 : 2)) % 3);
            requestPaint();
        } else if (event.key == ui::key::Return) {
            m_owner.add(m_kind, event.modifiers & ui::ShiftModifier);
        } else {
            return false;
        }
        return true;
    }

private:
    ui::RectF doneRect() const { return {geometry().width - 78, 14, 62, 29}; }
    ui::RectF kindRect(int kind) const { return {12, 68. + 35 * kind, 115, 33}; }
    ui::RectF previewRect(int wide) const
    {
        const double size = std::min({132., (geometry().width - 182) / 3, geometry().height - 145});
        const double x = (geometry().width + 140 - size * 3 - 14) / 2;
        return {x + (wide ? size + 14 : 0), 75, size * (wide ? 2 : 1), size};
    }
    void scheduleDone()
    {
        core::mainLoop()->post([owner = &m_owner, alive = std::weak_ptr<bool>(m_owner.m_alive)] {
            if (alive.lock())
                owner->done();
        });
    }
    DesktopWidgets &m_owner;
    Kind m_kind = Kind::Clock;
    ui::PointF m_hover {-1, -1}, m_origin;
    std::optional<int> m_held;
    bool m_drag = false;
    std::unique_ptr<ui::Animation> m_fade;
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
    auto material = panel::panelGlass();
    material.tintAlpha = panel::dark() ? .57f : .45f;
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
        const int w = std::min(700, r.width() - 24);
        const int h = std::min(293, r.height() - 24);
        m_gallery->setOutput(output->name);
        m_gallery->setGeometry({r.x() + (r.width() - w) / 2, r.bottom() - h - 12, w, h});
        m_gallery->setBackgroundBlur({0, 0, w, h}, kRadius, panel::panelGlass());
    }
}

void DesktopWidgets::repaint()
{
    if (m_dark != panel::dark()) {
        m_dark = panel::dark();
        updateOutput();
    }
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
    m_gallery = std::make_unique<ShellWindow>(m_shell, wm::WindowRole::Panel, false);
    ui::PopupGlass::prepare(*m_gallery->window());
    auto gallery = std::make_unique<Gallery>(*this);
    m_galleryView = gallery.get();
    m_gallery->setContentView(std::move(gallery));
    m_gallery->onCloseRequested = [this] { done(); };
    updateOutput();
    m_gallery->show();
    m_gallery->activate();
    m_galleryView->setFocus();
    m_galleryView->reveal();
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
