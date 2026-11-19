// SPDX-License-Identifier: BSD-3-Clause
#include "shell/widgets/desktop_widgets.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <numbers>
#include <utility>
#include <sys/sysinfo.h>

#include "base/event_loop.h"
#include "base/i18n.h"
#include "base/thread_pool.h"
#include "core/main_loop.h"
#include "core/preferences.h"
#include "shell/controlcenter/control_center.h"
#include "shell/controlcenter/control_paint.h"
#include "shell/controlcenter/editor_palette.h"
#include "shell/controlcenter/panel_style.h"
#include "shell/shell_data.h"
#include "shell/shell_window.h"
#include "shell/widgets/widget_weather.h"
#include "ui/animation.h"
#include "ui/button.h"
#include "ui/embedded_window.h"
#include "ui/geometry_conversions.h"
#include "ui/image.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/menu.h"
#include "ui/popup_glass.h"
#include "ui/search_field.h"
#include "ui/text_field.h"
#include <unicode/unistr.h>
#include <unicode/timezone.h>

namespace cutefish::shell {
namespace {
using namespace control_paint;
using namespace std::chrono_literals;
using i18n::tr;

constexpr int kSize = 214;
constexpr int kGap = 21;
constexpr int kRadius = 34;
constexpr base::Point kOrigin {112, 88};
constexpr size_t kMaximumWidgets = 18;

ui::Color ink(double alpha = 1) { return ui::Color::rgb(0xffffff).withAlpha(alpha); }

render::GlassFinish widgetFinish()
{
    return panel::dark() ? render::GlassFinish::WidgetDark : render::GlassFinish::WidgetLight;
}

render::GlassParams galleryGlass()
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

void applyWidgetGlass(ShellWindow &window, base::Size size, ui::RectF body = {}, float opacity = 1)
{
    auto glass = popupGlass(0, 0, 0, 0);
    glass.blurSigma = 4;
    glass.displacement = 5.4f;
    glass.chroma = 0;
    if (body.width <= 0 || body.height <= 0)
        body = {0, 0, double(size.width()), double(size.height())};
    const double radius = std::min(body.width, body.height) * kRadius / kSize;
    if (opacity > 0)
        glass.lenses.push_back({ui::toBase(body), radius, -1, opacity, std::nullopt, widgetFinish()});
    ui::Path shape;
    if (opacity > 0)
        shape.addRoundedRect(body, radius, radius);
    window.setBackgroundBlur(opacity > 0 ? base::Rect({}, size) : base::Rect(), int(radius), glass, shape);
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
    Size size;
    base::Point position = {};
    bool inCentre = false;
    std::string value = {};
    ui::Image photo = {};
    bool completed = false;
    std::unique_ptr<ShellWindow> window = {};
    Card *card = nullptr;
    base::Job weatherJob = {};
    std::optional<widget_weather::Forecast> weather;
    bool arriving = false;
    std::unique_ptr<base::Timer> exitTimer;
};

const char *DesktopWidgets::title(Kind kind)
{
    switch (kind) {
    case Kind::Clock: return tr("Clock");
    case Kind::Calendar: return tr("Calendar");
    case Kind::Battery: return tr("Batteries");
    case Kind::WorldClock: return tr("World Clock");
    case Kind::Photos: return tr("Photos");
    case Kind::Reminders: return tr("Reminders");
    case Kind::System: return tr("System Monitor");
    case Kind::Weather: return tr("Weather");
    }
    return "";
}

const char *DesktopWidgets::sizeLabel(Size size)
{
    switch (size) {
    case Size::Small: return tr("Small");
    case Size::Medium: return tr("Medium");
    case Size::Large: return tr("Large");
    case Size::ExtraLarge: return tr("Extra Large");
    }
    return "";
}

std::vector<DesktopWidgets::Size> DesktopWidgets::sizes(Kind kind, bool inCentre)
{
    switch (kind) {
    case Kind::WorldClock: return {Size::Medium};
    case Kind::Photos:
        return inCentre ? std::vector {Size::Small, Size::Medium, Size::Large}
                        : std::vector {Size::Small, Size::Medium, Size::Large, Size::ExtraLarge};
    default: return {Size::Small, Size::Medium};
    }
}

base::Size DesktopWidgets::dimensions(Size size, int unit)
{
    const int gap = std::max(1, int(std::round(unit * double(kGap) / kSize)));
    const int wide = unit * 2 + gap;
    switch (size) {
    case Size::Small: return {unit, unit};
    case Size::Medium: return {wide, unit};
    case Size::Large: return {wide, wide};
    case Size::ExtraLarge: return {wide * 2 + gap, wide};
    }
    return {};
}

class DesktopWidgets::Card final : public ui::View
{
public:
    Card(DesktopWidgets &owner, Widget &item) : m_owner(owner), m_item(item), m_alive(owner.m_alive)
    {
        setName("desktop-widget");
        setTracksHover(true);
        setLayered(true);
    }

    Widget *widget() const { return &m_item; }
    bool leaving() const { return m_leaving; }
    // Glass is placed in window coordinates. View::visualRect() must remain local for layer damage and clipping.
    ui::RectF lensBoundsInWindow() const
    {
        const auto at = mapToWindow({0, 0});
        const auto end = mapToWindow({geometry().width, geometry().height});
        return {at.x, at.y, end.x - at.x, end.y - at.y};
    }

    void setSlot(ui::RectF rect, bool settle = false)
    {
        if (m_floating || (!settle && m_slot == rect))
            return;
        const bool slide = m_slot.width > 0 && m_slot.size() == rect.size();
        const auto offset = geometry().topLeft() + m_offset - rect.topLeft();
        m_slot = rect;
        setGeometry(rect);
        m_offset = slide ? offset : ui::PointF{};
        present();
        if (slide && m_offset != ui::PointF{}) {
            animate(m_slide, 270ms, ui::Animation::Easing::OutCubic, [this, offset](double t) {
                m_offset = offset * (1 - t);
                present();
            });
        } else if (m_slide) {
            m_slide->stop();
        }
    }

    void setEditing(bool editing)
    {
        if (m_leaving || editing == m_editing)
            return;
        m_editing = editing;
        if (!editing && m_press) {
            m_press.reset();
            m_remove = false;
            endDrag();
        }
        updateBadge();
    }

    void appear()
    {
        if (!std::exchange(m_item.arriving, false))
            return;
        m_visibility = 0;
        present();
        animate(m_lifecycle, 260ms, ui::Animation::Easing::OutCubic, [this](double t) {
            m_visibility = t;
            present();
        });
    }

    void disappear()
    {
        m_leaving = true;
        m_menu.reset();
        updateBadge();
        if (m_slide)
            m_slide->stop();
        const double from = m_visibility;
        animate(m_lifecycle, 185ms, ui::Animation::Easing::InOutQuad, [this, from](double t) {
            m_visibility = from * (1 - t);
            present();
        });
        if (m_lifecycle)
            m_lifecycle->onFinished = [this] {
                core::mainLoop()->post([owner = &m_owner, item = &m_item, alive = m_alive] {
                    if (const auto guard = alive.lock(); guard && *guard)
                        owner->finishRemoval(item);
                });
            };
    }

    static void artwork(ui::Painter &painter, ui::RectF rect, Kind kind, Size size, int charge, bool discharging,
                        const Widget *item = nullptr)
    {
        const auto tm = localTime();
        const bool wide = size != Size::Small;
        const double featureWidth = wide ? std::min(rect.width * .52, rect.height + 10) : rect.width;
        const double corner = std::min(rect.width, rect.height) * kRadius / kSize;
        if (kind == Kind::WorldClock || (kind == Kind::Clock && !wide)) {
            static const auto zones = [] {
                std::array<std::unique_ptr<icu::TimeZone>, 4> result;
                const char *ids[] = {"America/New_York", "Asia/Tokyo", "Australia/Sydney", "Europe/Paris"};
                for (size_t i = 0; i < result.size(); ++i)
                    result[i].reset(icu::TimeZone::createTimeZone(icu::UnicodeString::fromUTF8(ids[i])));
                return result;
            }();
            if (kind == Kind::Clock) {
                analog(painter, center(rect), std::min(rect.width, rect.height) * .44, tm, false);
            } else {
                painter.fillRoundedRect(rect, corner, ui::Color::rgb(0x17171a).withAlpha(.91));
                const char *names[] = {tr("New York"), tr("Tokyo"), tr("Sydney"), tr("Paris")};
                const auto now = std::time(nullptr);
                const double cell = (rect.width - 16) / 4;
                for (size_t i = 0; i < zones.size(); ++i) {
                    UErrorCode error = U_ZERO_ERROR;
                    int32_t raw = 0, dst = 0;
                    zones[i]->getOffset(double(now) * 1000, false, raw, dst, error);
                    const auto utc = now + (raw + dst) / 1000;
                    std::tm zone {};
                    gmtime_r(&utc, &zone);
                    const double x = rect.x + 8 + i * cell;
                    analog(painter, {x + cell / 2, rect.y + rect.height * .37}, std::min(cell * .44, rect.height * .26),
                           zone, zone.tm_hour < 6 || zone.tm_hour >= 18);
                    text(painter, {x, rect.y + rect.height * .63, cell, rect.height * .13}, names[i], 8, ink(), true, true);
                    const int delta = (raw + dst) / 60000 - int(tm.tm_gmtoff / 60);
                    const auto minutes = std::abs(delta) % 60;
                    const auto offset = delta == 0 ? std::string(tr("Local time"))
                            : (delta < 0 ? "−" : "+") + std::to_string(std::abs(delta) / 60)
                                + (minutes ? ":" + std::to_string(minutes + 100).substr(1) : " h");
                    const auto day = zone.tm_yday == tm.tm_yday ? tr("Today") : date(zone, "%a");
                    text(painter, {x, rect.y + rect.height * .74, cell, rect.height * .10}, day, 7, ink(.64), false, true);
                    text(painter, {x, rect.y + rect.height * .83, cell, rect.height * .10}, offset, 7, ink(.64), false, true);
                }
            }
            return;
        }
        if (kind == Kind::Weather) {
            painter.fillRoundedRect(rect, corner, ui::Color::rgb(0x3e4c5b));
            const auto status = [](int code) {
                if (code == 0) return tr("Clear");
                if (code < 4) return tr("Cloudy");
                if (code < 50) return tr("Fog");
                if ((code >= 71 && code <= 77) || code == 85 || code == 86) return tr("Snow");
                if (code >= 95) return tr("Thunderstorms");
                return tr("Rain");
            };
            const auto degrees = [](int value) { return std::to_string(value) + "°"; };
            const auto w = rect.width - 26;
            const double x = rect.x + 13;
            const auto *forecast = item && item->weather ? &*item->weather : nullptr;
            text(painter, {x, rect.y + rect.height * .08, w, rect.height * .17}, tr("Weather"), 10, ink(), true);
            text(painter, {x, rect.y + rect.height * .22, w, rect.height * .35},
                 forecast ? degrees(forecast->temperature) : "–°", rect.height * .18, ink());
            if (!forecast) {
                const char *message = !item || item->value.empty() ? tr("Set Location")
                          : item->weatherJob.isPending() ? tr("Updating…") : tr("Forecast Unavailable");
                text(painter, {x, rect.y + rect.height * .61, w, rect.height * .20}, message, 8, ink(.75));
            } else if (!wide) {
                text(painter, {x, rect.y + rect.height * .61, w, rect.height * .20}, status(forecast->code), 9, ink(.8));
            } else {
                text(painter, {x + w * .40, rect.y + rect.height * .26, w * .60, rect.height * .20},
                     status(forecast->code), 9, ink(.8), true, true);
                const double cell = w / forecast->hours.size();
                for (size_t i = 0; i < forecast->hours.size(); ++i) {
                    const auto &hour = forecast->hours[i];
                    const double left = x + cell * i;
                    text(painter, {left, rect.y + rect.height * .59, cell, rect.height * .13}, hour.time, 7, ink(.72), false, true);
                    text(painter, {left, rect.y + rect.height * .74, cell, rect.height * .15}, degrees(hour.temperature), 9, ink(), true, true);
                }
            }
            return;
        }
        if (kind == Kind::Photos) {
            if (item && !item->photo.isNull()) {
                const auto &photo = item->photo;
                const double scale = std::max(rect.width / photo.width(), rect.height / photo.height());
                const auto c = center(rect);
                painter.save();
                painter.clipRoundedRect(rect, corner);
                painter.drawImage(photo, {c.x - photo.width() * scale / 2, c.y - photo.height() * scale / 2,
                                          photo.width() * scale, photo.height() * scale});
                painter.restore();
            } else {
                painter.fillRoundedRect(rect, corner, ui::Color::rgb(0x28282b));
                const auto c = center(rect);
                painter.strokeRoundedRect({c.x - 19, c.y - 29, 38, 29}, 6, ink(.7), 1.5);
                painter.drawLine({c.x - 16, c.y - 4}, {c.x, c.y - 17}, ink(.7), 1.5);
                painter.drawLine({c.x, c.y - 17}, {c.x + 16, c.y - 4}, ink(.7), 1.5);
                text(painter, {rect.x + 8, c.y + 4, rect.width - 16, 28}, tr("Choose a Photo"), 10, ink(.79), true, true);
            }
            return;
        }
        if (kind == Kind::Reminders || kind == Kind::System) {
            const bool reminders = kind == Kind::Reminders;
            const auto fg = reminders ? ui::Color::rgb(0x222228) : ink();
            const auto accent = ui::Color::rgb(reminders ? 0x187dab : 0x62db91);
            painter.fillRoundedRect(rect, corner, ui::Color::rgb(reminders ? 0xfcfcfd : 0x17181a));
            const double left = rect.x + 13;
            text(painter, {left, rect.y + 9, rect.width - 26, 22}, reminders ? tr("Today") : tr("System"), 10, accent, true);
            if (reminders) {
                const bool hasTask = item && !item->value.empty();
                text(painter, {rect.right() - 28, rect.y + 8, 17, 22}, hasTask && !item->completed ? "1" : "0", 12, fg, true, true);
                if (hasTask) {
                    painter.strokeRoundedRect({left, rect.y + 39, 12, 12}, 6, accent, 1.5);
                    if (item->completed)
                        painter.fillRoundedRect({left + 3, rect.y + 42, 6, 6}, 3, accent);
                    paragraph(painter, {left + 20, rect.y + 36, rect.width - 46, rect.height - 50}, item->value, 10,
                              fg.withAlpha(item->completed ? .48 : 1));
                } else {
                    text(painter, {left, rect.y + 33, rect.width - 26, 20}, tr("No Reminders"), 9, fg.withAlpha(.62));
                }
            } else {
                struct sysinfo data {};
                if (sysinfo(&data) == 0 && data.totalram > 0) {
                    const double ratio = 1 - double(data.freeram) / data.totalram;
                    const double w = rect.width - 26;
                    text(painter, {left, rect.y + rect.height * .27, w, rect.height * .36},
                         std::to_string(int(ratio * 100)) + "%", rect.height * .16, fg, true);
                    painter.fillRoundedRect({left, rect.y + rect.height * .69, w, 6}, 3, fg.withAlpha(.16));
                    painter.fillRoundedRect({left, rect.y + rect.height * .69, w * ratio, 6}, 3, accent);
                    text(painter, {left, rect.y + rect.height * .77, w, 21}, tr("Memory in use"), 8, fg.withAlpha(.62));
                }
            }
            return;
        }
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
            const auto col = ui::Color::rgb(0xd52945);
            if (!wide) {
                painter.fillRoundedRect(rect, corner, ui::Color::rgb(0xfcfcfd));
                const auto fg = ui::Color::rgb(0x202023);
                const double left = rect.x + rect.width * .10, top = rect.y + rect.height * .09;
                const double step = rect.width * .80 / 7, line = rect.height * .105;
                auto first = tm;
                first.tm_mday = 1;
                std::mktime(&first);
                auto last = first;
                ++last.tm_mon;
                last.tm_mday = 0;
                std::mktime(&last);
                text(painter, {left, top, step * 7, line}, date(tm, "%B"), rect.height * .066, col, true);
                for (int i = 0; i < 7; ++i) {
                    auto header = first;
                    header.tm_wday = (i + 1) % 7;
                    auto day = date(header, "%a");
                    if (!day.empty()) {
                        icu::UnicodeString letter = icu::UnicodeString::fromUTF8(day);
                        day.clear();
                        letter.tempSubString(0, U16_LENGTH(letter.char32At(0))).toUTF8String(day);
                    }
                    text(painter, {left + i * step, top + line, step, line}, day, rect.height * .056, fg.withAlpha(.7), true, true);
                }
                for (int i = 1; i <= last.tm_mday; ++i) {
                    const int slot = (first.tm_wday + 6) % 7 + i - 1;
                    const ui::RectF cell {left + slot % 7 * step, top + (2 + slot / 7) * line, step, line};
                    if (i == tm.tm_mday)
                        painter.fillRoundedRect(cell, line / 2, col);
                    text(painter, cell, std::to_string(i), rect.height * .057, i == tm.tm_mday ? ink() : fg, false, true);
                }
                return;
            }
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
            if (!wide) {
                const double side = rect.height * .36, pen = std::max(2., rect.height * .029);
                for (int i = 0; i < 4; ++i) {
                    const ui::RectF ring {rect.x + rect.width * (.10 + .44 * (i % 2)),
                                          rect.y + rect.height * (.10 + .44 * (i / 2)), side, side};
                    painter.strokeRoundedRect(ring, side / 2, ink(.22), pen);
                    if (i != 0)
                        continue;
                    if (charge > 0) {
                        ui::Path arc;
                        arc.arcTo(ring, 90, -std::clamp(charge, 0, 100) * 3.6);
                        painter.strokePath(arc, ui::Color::rgb(charge <= 20 ? 0xff787e : 0x5ee978), pen);
                    }
                    const auto c = center(ring);
                    painter.strokeRoundedRect({c.x - side * .19, c.y - side * .13, side * .38, side * .24}, 2, ink(.9), 1.5);
                    painter.drawLine({c.x - side * .25, c.y + side * .16}, {c.x + side * .25, c.y + side * .16}, ink(.9), 1.5);
                }
                return;
            }
            const double r = std::min(featureWidth, rect.height) * (wide ? .27 : .175);
            const ui::PointF c {wide ? rect.x + featureWidth / 2 : rect.x + r + 19,
                               wide ? rect.y + rect.height * .44 : rect.y + r + 17};
            const ui::RectF ring {c.x - r, c.y - r, r * 2, r * 2};
            ui::Path circle;
            circle.addEllipse(ring);
            painter.strokePath(circle, ink(.15), 6);
            if (charge > 0) {
                const auto tint = ui::Color::rgb(charge <= 20 ? 0xff787e : 0x5ee978);
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

    static void analog(ui::Painter &painter, ui::PointF c, double radius, const std::tm &tm, bool dark)
    {
        const auto dial = ui::Color::rgb(dark ? 0xfafafa : 0x1b1b1c);
        painter.fillRoundedRect({c.x - radius, c.y - radius, radius * 2, radius * 2}, radius,
                                ui::Color::rgb(dark ? 0x212126 : 0xfafafa));
        const auto at = [&](double turns, double distance) -> ui::PointF {
            const double a = turns * 2 * std::numbers::pi;
            return {c.x + std::sin(a) * radius * distance, c.y - std::cos(a) * radius * distance};
        };
        for (int i = 0; i < 60; ++i)
            painter.drawLine(at(i / 60., i % 5 ? .91 : .86), at(i / 60., .96), dial.withAlpha(i % 5 ? .42 : .9), i % 5 ? .65 : 1.2);
        for (int i = 1; i <= 12; ++i) {
            const auto p = at(i / 12., .73);
            text(painter, {p.x - radius / 4, p.y - radius / 5, radius / 2, radius * .4},
                 std::to_string(i), std::max(4., radius * .20), dial, true, true);
        }
        painter.drawLine(c, at((tm.tm_hour + tm.tm_min / 60.) / 12., .51), dial, std::max(1.5, radius * .065));
        painter.drawLine(c, at((tm.tm_min + tm.tm_sec / 60.) / 60., .74), dial, std::max(1.2, radius * .045));
        painter.drawLine(c, at(tm.tm_sec / 60., .81), ui::Color::rgb(0xd4932c), 1);
        painter.fillRoundedRect({c.x - 1.5, c.y - 1.5, 3, 3}, 1.5, ui::Color::rgb(0xd4932c));
    }

protected:
    bool acceptsPointer() const override { return !m_leaving && m_visibility > .4; }
    void paint(ui::Painter &painter) override
    {
        if (m_alive.expired())
            return;
        artwork(painter, localRect(), m_item.kind, m_item.size, m_owner.m_controls.batteryCharge(),
                m_owner.m_controls.onBattery(), &m_item);
        // Repaint the artwork underneath the badge in the same layer, including its last fade frame.
        if (m_badgeOpacity > 0) {
            constexpr double centre = 17;
            const double radius = 11 * m_badgeScale;
            const auto fill = ui::Color::rgb(panel::dark() ? 0x4F4F55 : 0xDFDFE4);
            sign(painter, {centre - radius, centre - radius, radius * 2, radius * 2}, false,
                 fill.withAlpha(m_badgeOpacity), ink(m_badgeOpacity));
        }
    }

    void pointerPress(const ui::PointerEvent &event) override
    {
        if (event.button == ui::MouseButton::Right) {
            m_menu = std::make_unique<ui::Menu>();
            if (m_item.kind == Kind::Photos || m_item.kind == Kind::Reminders || m_item.kind == Kind::Weather) {
                m_menu->addItem(m_item.kind == Kind::Photos ? tr("Edit “Featured”…")
                                : m_item.kind == Kind::Weather ? tr("Edit Location…") : tr("Edit Reminder…"),
                                [this] { m_owner.configure(m_item); });
                m_menu->addSeparator();
            }
            const auto variants = sizes(m_item.kind, m_item.inCentre);
            if (variants.size() > 1) {
                const auto heading = m_menu->addItem(tr("Size"), {});
                m_menu->setItemEnabled(heading, false);
                for (const auto size : variants) {
                    const auto row = m_menu->addItem(sizeLabel(size), [this, size] { m_owner.resize(&m_item, size); });
                    m_menu->setItemChecked(row, m_item.size == size);
                }
                m_menu->addSeparator();
            }
            m_menu->addItem(tr("Remove Widget"), [this] { scheduleRemove(); });
            m_menu->addSeparator();
            m_menu->addItem(tr("Edit Widgets…"), [this] { m_owner.edit(m_item.inCentre); });
            m_menu->popupAt(window(), mapToWindow(event.position));
        }
        if (event.button == ui::MouseButton::Left && (m_editing || m_item.kind == Kind::Reminders
                                                       || m_item.kind == Kind::Photos || m_item.kind == Kind::Weather)) {
            m_press = dragPoint(event.position);
            m_remove = m_editing && m_badgeOpacity > .45
                    && control_paint::contains(kBadgeHit, event.position);
            if (m_remove)
                updateBadge();
        }
    }

    void pointerMove(const ui::PointerEvent &event) override
    {
        if (!m_press || !m_editing || m_remove)
            return;
        const auto delta = dragPoint(event.position) - *m_press;
        if (!m_floating && std::hypot(delta.x, delta.y) < 5)
            return;
        if (!m_floating) {
            m_floating = true;
            m_dragOffset = m_offset;
            if (m_slide)
                m_slide->stop();
            if (m_item.inCentre)
                parent()->raiseChild(this);
            lift(true);
        }
        if (m_item.inCentre) {
            m_offset = m_dragOffset + delta;
            present();
            return;
        }
        const auto global = m_item.window->geometry().topLeft();
        m_owner.move(&m_item, global + base::Point(int(std::lround(delta.x)), int(std::lround(delta.y))));
    }

    void pointerRelease(const ui::PointerEvent &event) override
    {
        if (event.button != ui::MouseButton::Left || !m_press)
            return;
        m_press.reset();
        if (std::exchange(m_remove, false)) {
            updateBadge();
            if (m_editing && control_paint::contains(kBadgeHit, event.position))
                scheduleRemove();
        } else if (m_floating) {
            endDrag(dragPoint(event.position));
        } else if (!m_editing && localRect().contains(event.position)) {
            if (m_item.kind == Kind::Reminders && !m_item.value.empty()) {
                m_item.completed = !m_item.completed;
                m_owner.save();
                requestPaint();
            } else if (m_item.value.empty()) {
                m_owner.configure(m_item);
            }
        }
    }

private:
    static constexpr ui::RectF kBadgeHit {4, 4, 26, 26};

    void animate(std::unique_ptr<ui::Animation> &animation, std::chrono::milliseconds duration,
                 ui::Animation::Easing easing, std::function<void(double)> update)
    {
        if (!window()) {
            update(1);
            return;
        }
        if (!animation)
            animation = std::make_unique<ui::Animation>(window());
        animation->setDuration(duration);
        animation->setEasing(easing);
        animation->onFinished = {};
        animation->onUpdate = std::move(update);
        animation->start();
    }

    void updateBadge()
    {
        const double to = m_editing && !m_leaving ? 1 : 0;
        const double scaleTo = to ? (m_remove ? .83 : 1) : .66;
        const double from = m_badgeOpacity;
        const double scaleFrom = m_badgeScale;
        animate(m_badgeMotion, to ? 190ms : 145ms, ui::Animation::Easing::OutCubic, [=, this](double t) {
            m_badgeOpacity = std::lerp(from, to, t);
            m_badgeScale = std::lerp(scaleFrom, scaleTo, t);
            requestPaint(kBadgeHit);
        });
    }

    void lift(bool held)
    {
        const double from = m_lift;
        animate(m_liftMotion, held ? 140ms : 240ms, ui::Animation::Easing::OutCubic, [this, from, held](double t) {
            m_lift = std::lerp(from, held ? 1. : 0., t);
            present();
        });
    }

    ui::PointF dragPoint(ui::PointF local) const
    {
        const auto point = mapToWindow(local);
        return m_item.inCentre ? parent()->mapFromWindow(point) : point;
    }

    void endDrag(std::optional<ui::PointF> drop = {})
    {
        if (!std::exchange(m_floating, false))
            return;
        lift(false);
        if (m_item.inCentre) {
            const auto slot = m_slot;
            if (drop)
                m_owner.reorder(&m_item, *drop, parent()->geometry().width);
            if (slot == m_slot)
                setSlot(slot, true);
        } else {
            m_owner.save();
        }
    }

    void present()
    {
        if (m_alive.expired())
            return;
        setOpacity(m_visibility);
        const bool inCentre = m_item.inCentre;
        const double scale = std::lerp(.88, 1., m_visibility) + m_lift * (inCentre ? .035 : -.025);
        setTransform({m_offset + ui::PointF{0, inCentre ? 10 * (1 - m_visibility) : 0}, scale});
        updateGlass();
    }

public:
    void updateGlass()
    {
        if (m_item.inCentre) {
            if (auto *stack = static_cast<Stack *>(parent()); stack && stack->onPresentationChanged)
                stack->onPresentationChanged();
        } else if (m_item.window) {
            applyWidgetGlass(*m_item.window, m_item.window->geometry().size(), lensBoundsInWindow(), float(opacity()));
        }
    }

private:
    void scheduleRemove()
    {
        core::mainLoop()->post([owner = &m_owner, item = &m_item, alive = std::weak_ptr<bool>(m_owner.m_alive)] {
            if (const auto guard = alive.lock(); guard && *guard)
                owner->remove(item);
        });
    }
    DesktopWidgets &m_owner;
    Widget &m_item;
    std::weak_ptr<bool> m_alive;
    std::unique_ptr<ui::Animation> m_badgeMotion, m_lifecycle, m_slide, m_liftMotion;
    std::unique_ptr<ui::Menu> m_menu;
    std::optional<ui::PointF> m_press;
    ui::RectF m_slot;
    ui::PointF m_offset, m_dragOffset;
    double m_visibility = 1, m_lift = 0;
    double m_badgeOpacity = 0, m_badgeScale = .66;
    bool m_editing = false, m_leaving = false;
    bool m_remove = false;
    bool m_floating = false;
};

DesktopWidgets::Stack::Stack(DesktopWidgets &owner) : m_owner(owner), m_alive(owner.m_alive)
{
    setName("notification-widgets");
    m_changed = owner.m_changed.connect([this] {
        layout(); // Keep retiring views until their exit motion completes.
        requestLayout();
        if (onChanged)
            onChanged();
    });
    m_repaint = owner.m_repaint.connect([this] {
        for (const auto &card : children()) {
            static_cast<Card *>(card.get())->setEditing(bool(m_owner.m_gallery));
            card->requestPaint();
        }
    });
}

double DesktopWidgets::Stack::heightForWidth(double width) const
{
    if (m_alive.expired())
        return 0;
    double height = 0;
    for (const auto &[item, rect] : m_owner.stackLayout(width))
        height = std::max(height, rect.bottom());
    for (const auto &child : children())
        if (static_cast<Card *>(child.get())->leaving())
            height = std::max(height, child->geometry().bottom());
    return height;
}

void DesktopWidgets::Stack::layout()
{
    if (m_alive.expired())
        return;
    const auto cards = m_owner.stackLayout(geometry().width);
    for (size_t i = children().size(); i > 0; --i) {
        auto *card = static_cast<Card *>(children()[i - 1].get());
        if (std::ranges::none_of(cards, [card](const auto &pair) { return pair.first == card->widget(); })) {
            const bool exiting = std::ranges::any_of(m_owner.m_exiting, [card](const auto &item) { return item.get() == card->widget(); });
            if (!exiting)
                takeChild(card);
            else if (!card->leaving())
                card->disappear();
        }
    }
    for (const auto &[item, rect] : cards) {
        const auto it = std::ranges::find_if(children(), [item](const auto &view) { return static_cast<Card *>(view.get())->widget() == item; });
        auto *card = it == children().end() ? addChild<Card>(m_owner, *item) : static_cast<Card *>(it->get());
        card->setSlot(rect);
        card->setEditing(bool(m_owner.m_gallery));
        card->appear();
        card->requestPaint();
    }
}

void DesktopWidgets::Stack::appendGlass(render::GlassParams &glass, ui::Path &mask, ui::RectF viewport) const
{
    if (m_alive.expired())
        return;
    const auto clip = ui::toBase(viewport);
    ui::Path clipPath;
    clipPath.addRect(viewport);
    for (const auto &child : children()) {
        const auto *cardView = static_cast<const Card *>(child.get());
        const auto rect = cardView->lensBoundsInWindow();
        if (cardView->opacity() <= 0 || !clip.intersects(ui::toBase(rect)))
            continue;
        const double radius = std::min(rect.width, rect.height) * kRadius / kSize;
        ui::Path card;
        card.addRoundedRect(rect, radius, radius);
        mask.addPath(card.intersected(clipPath));
        const auto *item = cardView->widget();
        const auto finish = item->kind == Kind::Battery
                ? (panel::dark() ? render::GlassFinish::FrostedDark : render::GlassFinish::FrostedLight) : widgetFinish();
        glass.lenses.push_back({ui::toBase(rect), radius, -1, float(cardView->opacity()), clip, finish});
    }
}

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
            m_sidebarScroll = 0;
            requestLayout();
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
        syncGlass();
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
        painter.save();
        painter.clip(sidebarClip());
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
            if (kind == Kind::Clock || kind == Kind::WorldClock) {
                painter.strokeRoundedRect(inset(badge, 6), 9, ink(), 1.3);
                painter.drawLine({c.x, c.y - 5}, c, ink(), 1.4);
                painter.drawLine(c, {c.x + 4, c.y + 2}, ink(), 1.4);
            } else if (kind == Kind::Calendar) {
                painter.strokeRoundedRect(inset(badge, 7), 2, ink(), 1.4);
                painter.drawLine({badge.x + 7, badge.y + 12}, {badge.right() - 7, badge.y + 12}, ink(), 1.4);
            } else if (kind == Kind::Battery) {
                painter.strokeRoundedRect({c.x - 8, c.y - 4, 15, 8}, 2, ink(), 1.3);
                painter.fillRoundedRect({c.x - 6, c.y - 2, 10, 4}, 1, ink());
                painter.drawLine({c.x + 9, c.y - 1}, {c.x + 9, c.y + 1}, ink(), 1.2);
            } else {
                text(painter, badge, kind == Kind::Photos ? "◇" : kind == Kind::Reminders ? "☷" : "∿", 14, ink(), true, true);
            }
            text(painter, {row.x + 49, row.y, row.width - 55, row.height}, title(kind), 11, ink(), selected);
        }
        painter.restore();
        if (!featured())
            text(painter, {side + 26, 16, width - side - 52, 35},
                 !m_search->text().empty() ? tr("Search Results") : m_all ? tr("All Widgets") : title(m_kind), 14, ink(), true);
        const auto clip = previewClip();
        painter.save();
        painter.clip(clip);
        if (kinds.empty())
            text(painter, clip, tr("No Widgets Found"), 16, ink(.78), true, true);
        if (featured() && m_showHelp) {
            const auto hero = heroRect();
            painter.fillRoundedRect(hero, 10, ink(.11));
            text(painter, {hero.x + 28, hero.y + 26, hero.width * .54, 30}, tr("Widgets on Desktop"), 15, palette.ink, true);
            paragraph(painter, {hero.x + 28, hero.y + 63, hero.width * .48, 78},
                      tr("Place widgets directly on the desktop by dragging them from the widget gallery."), 12, palette.muted);
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
            const auto close = heroClose();
            painter.fillRoundedRect(close, 9, palette.raised.withAlpha(.82));
            text(painter, close, "×", 11, palette.ink, true, true);
            text(painter, {clip.x + 10, hero.bottom() + 5, clip.width - 20, 24}, tr("Suggestions"), 11, palette.ink, true);
        } else if (featured()) {
            text(painter, {clip.x + 10, heroRect().bottom() + 5, clip.width - 20, 24}, tr("Suggestions"), 11, palette.ink, true);
        }
        for (const auto &pick : previews()) {
            const auto rect = pick.rect;
            if (!ui::toBase(clip).intersects(ui::toBase(rect)))
                continue;
            Card::artwork(painter, rect, pick.kind, pick.size, m_owner.m_controls.batteryCharge(),
                          m_owner.m_controls.onBattery(), pick.kind == Kind::Photos ? &m_featuredPhoto : nullptr);
            if (!featured()) {
                const auto label = m_all ? std::string(title(pick.kind)) + " · " + sizeLabel(pick.size) : sizeLabel(pick.size);
                text(painter, {rect.x, rect.bottom() + 4, rect.width, 23}, label, 10, palette.muted, false, true);
            }
            if (contains(rect, m_hover) && contains(clip, m_hover))
                sign(painter, {rect.x + 5, rect.y + 5, 24, 24}, true, ui::Color::rgb(0x32994E), ui::Color::rgb(0xffffff));
        }
        painter.restore();
        painter.fillRect({0, height - 60, width, 60}, palette.surface.withAlpha(.30));
        painter.fillRect({0, height - 60, width, 1}, palette.border.withAlpha(.21));
        text(painter, {23, height - 56, width - 156, 52},
             tr("Drag a widget onto the desktop or into Notification Center."), 11, palette.ink, true);
        const auto r = doneRect();
        painter.fillRoundedRect(r, r.height / 2, ui::Color::rgb(0x087ff6).withAlpha(contains(r, m_hover) ? 1 : .92));
        text(painter, r, tr("Done"), 11, ui::Color::rgb(0xffffff), true, true);
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
        if (contains(previewClip(), m_origin))
            for (const auto &pick : previews())
                if (contains(pick.rect, m_origin))
                    m_held = pick;
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
                m_owner.add(m_held->kind, m_held->size);
            else if (m_drag && !contains(localRect(), p))
                m_owner.add(m_held->kind, m_held->size, dragBounds(p).topLeft(), global(p));
        } else if (featured() && m_showHelp && contains(heroClose(), m_origin) && contains(heroClose(), p)) {
            m_showHelp = false;
            m_scroll = 0;
        } else if (contains(sidebarClip(), p) && contains(sidebarClip(), m_origin)) {
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
        requestLayout();
        requestPaint();
    }
    bool keyPress(const ui::KeyEvent &event) override
    {
        if (m_closing)
            return true;
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
            requestLayout();
            requestPaint();
        } else if (event.key == ui::key::Return) {
            if (!filtered().empty())
                m_owner.add(m_kind, event.modifiers & ui::ShiftModifier ? sizes(m_kind).back() : sizes(m_kind).front());
        } else {
            return false;
        }
        return true;
    }

    bool scroll(const ui::ScrollEvent &event) override
    {
        if (m_drag)
            return false;
        if (contains(sidebarClip(), m_hover)) {
            m_sidebarScroll = std::clamp(m_sidebarScroll + event.deltaY, 0.,
                                         std::max(0., 42. * (filtered().size() + 1) - sidebarClip().height));
        } else if (contains(previewClip(), m_hover)) {
            m_scroll = std::clamp(m_scroll + event.deltaY, 0., std::max(0., contentHeight() - previewClip().height));
        } else {
            return false;
        }
        requestLayout();
        requestPaint();
        return true;
    }

private:
    void syncGlass()
    {
        const auto bounds = localRect();
        if (!m_owner.m_gallery || bounds.width <= 0 || bounds.height <= 0)
            return;
        const bool dark = panel::dark();
        auto glass = galleryGlass();
        glass.tintRed = glass.tintGreen = glass.tintBlue = dark ? .055f : 1.f;
        glass.tintAlpha = dark ? .72f : .53f;
        glass.shadowLift = 0;
        glass.highlightRollOff = 0;
        glass.lenses.push_back({ui::toBase(bounds), kRadius, -1, 1, std::nullopt,
                                dark ? render::GlassFinish::GalleryDark : render::GlassFinish::GalleryLight});
        const auto clip = ui::toBase(previewClip());
        for (const auto &pick : previews()) {
            const auto rect = pick.rect;
            if (clip.intersects(ui::toBase(rect)))
                glass.lenses.push_back({ui::toBase(rect), std::min(rect.width, rect.height) * kRadius / kSize,
                                        -1, 1, clip, widgetFinish()});
        }
        ui::Path shape;
        shape.addRoundedRect(bounds, kRadius, kRadius);
        m_owner.m_gallery->setBackgroundBlur(ui::toBase(bounds).toRoundedRect(), kRadius, glass, shape);
    }

    static bool contains(const ui::RectF &rect, ui::PointF point) { return control_paint::contains(rect, point); }
    struct Pick { Kind kind; Size size; ui::RectF rect; };
    class LiftedCard final : public ui::View
    {
    public:
        LiftedCard(DesktopWidgets &owner, Kind kind, Size size) : m_owner(owner), m_kind(kind), m_size(size) {}
        void paint(ui::Painter &painter) override
        {
            Card::artwork(painter, localRect(), m_kind, m_size,
                          m_owner.m_controls.batteryCharge(), m_owner.m_controls.onBattery());
        }
    private:
        DesktopWidgets &m_owner;
        Kind m_kind;
        Size m_size;
    };
    base::Point global(ui::PointF p) const
    {
        return m_owner.m_gallery->geometry().topLeft() + base::Point(int(std::lround(p.x)), int(std::lround(p.y)));
    }
    base::Rect dragBounds(ui::PointF p) const
    {
        const bool centre = m_owner.m_centreDrop && m_owner.m_centreDrop(global(p));
        const auto size = dimensions(centre && m_held->size == Size::ExtraLarge ? Size::Large : m_held->size,
                                     centre ? 170 : kSize);
        const int w = size.width(), h = size.height();
        const auto r = m_held->rect;
        const auto origin = m_owner.m_gallery->geometry().topLeft();
        return {origin.x() + int(std::lround(p.x - (m_origin.x - r.x) * w / r.width)),
                origin.y() + int(std::lround(p.y - (m_origin.y - r.y) * h / r.height)), w, h};
    }
    void updateDrag()
    {
        const auto rect = dragBounds(m_hover);
        if (!m_lifted) {
            m_lifted = std::make_unique<ShellWindow>(m_owner.m_shell, wm::WindowRole::Picker, false);
            m_lifted->setFocusPolicy(wm::FocusPolicy::NoFocus);
            m_lifted->setInputRegion(base::Region());
            ui::PopupGlass::prepare(*m_lifted->window());
            m_lifted->setContentView(std::make_unique<LiftedCard>(m_owner, m_held->kind, m_held->size));
            applyWidgetGlass(*m_lifted, rect.size());
            m_lifted->setGeometry(rect);
            m_lifted->show();
        } else {
            if (m_lifted->geometry().size() != rect.size())
                applyWidgetGlass(*m_lifted, rect.size());
            m_lifted->setGeometry(rect);
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
    ui::RectF kindRect(int kind) const { return {14, 64. + 42 * kind - m_sidebarScroll, sidebar() - 28, 40}; }
    ui::RectF sidebarClip() const { return {0, 63, sidebar(), std::max(0., geometry().height - 125)}; }
    ui::RectF heroRect() const
    {
        const auto clip = previewClip();
        return {clip.x + 3, 19 - m_scroll, clip.width - 6, m_showHelp ? 169. : 0.};
    }
    ui::RectF heroClose() const { const auto hero = heroRect(); return {hero.x - 4, hero.y - 3, 19, 19}; }
    ui::RectF previewClip() const
    {
        const double top = featured() ? 16 : 61;
        return {sidebar() + 17, top, geometry().width - sidebar() - 34, std::max(0., geometry().height - top - 66)};
    }
    double contentHeight() const
    {
        double bottom = previewClip().y;
        for (const auto &pick : previews())
            bottom = std::max(bottom, pick.rect.bottom() + m_scroll + (featured() ? 12. : 31.));
        return bottom - previewClip().y;
    }
    std::vector<Pick> previews() const
    {
        const auto clip = previewClip();
        std::vector<Pick> result;
        if (featured()) {
            constexpr double gap = 10;
            const double unit = (clip.width - 20 - 5 * gap) / 6;
            const double step = unit + gap;
            const double top = heroRect().bottom() + 38;
            const auto add = [&](Kind kind, Size size, int col, int row, int columns, int rows) {
                result.push_back({kind, size, {clip.x + 10 + col * step, top + row * step,
                                              columns * step - gap, rows * step - gap}});
            };
            add(Kind::Clock, Size::Small, 0, 0, 1, 1);
            add(Kind::Calendar, Size::Small, 1, 0, 1, 1);
            add(Kind::Photos, Size::Large, 2, 0, 2, 2);
            add(Kind::Battery, Size::Small, 4, 0, 1, 1);
            add(Kind::System, Size::Small, 5, 0, 1, 1);
            add(Kind::Weather, Size::Medium, 0, 1, 2, 1);
            add(Kind::Reminders, Size::Medium, 4, 1, 2, 1);
            return result;
        }
        const int unit = std::max(15, int(std::min(214., (clip.width - 24) / 3.4)));
        double y = 93 - m_scroll;
        for (const auto kind : shownKinds()) {
            const double left = clip.x + 12;
            double x = left, bottom = y;
            for (const auto size : sizes(kind)) {
                auto dims = dimensions(size, unit);
                if (dims.width() > clip.width - 24)
                    dims = dimensions(size, std::max(1, int(unit * (clip.width - 25) / dims.width())));
                if (x > left && x + dims.width() > clip.right() - 12) {
                    y = bottom + 31;
                    x = left;
                }
                result.push_back({kind, size, {x, y, double(dims.width()), double(dims.height())}});
                x += dims.width() + 12;
                bottom = std::max(bottom, y + dims.height());
            }
            y = bottom + 39;
        }
        return result;
    }
    std::vector<Kind> filtered() const
    {
        const auto query = icu::UnicodeString::fromUTF8(m_search->text()).foldCase();
        std::vector<Kind> kinds;
        for (const Kind kind : {Kind::Battery, Kind::Calendar, Kind::Clock, Kind::Photos, Kind::Reminders, Kind::System, Kind::Weather, Kind::WorldClock})
            if (query.isEmpty() || icu::UnicodeString::fromUTF8(title(kind)).foldCase().indexOf(query) >= 0
                || (kind == Kind::Battery && icu::UnicodeString::fromUTF8(tr("Battery")).foldCase().indexOf(query) >= 0))
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
    void scheduleDone()
    {
        if (m_closing)
            return;
        m_closing = true;
        m_owner.m_gallery->setInputRegion(base::Region());
        m_search->setEnabled(false);
        m_fade = std::make_unique<ui::Animation>(window());
        const double progress = m_reveal;
        m_fade->setDuration(200ms);
        m_fade->setEasing(ui::Animation::Easing::InOutCubic);
        m_fade->onUpdate = [this, progress](double t) { m_reveal = progress * (1 - t); position(); };
        m_fade->onFinished = [this] {
            core::mainLoop()->post([owner = &m_owner, gallery = this, alive = std::weak_ptr<bool>(m_owner.m_alive)] {
                if (const auto guard = alive.lock(); guard && *guard && owner->m_galleryView == gallery)
                    owner->done();
            });
        };
        m_fade->start();
    }
    DesktopWidgets &m_owner;
    ui::SearchField *m_search;
    Kind m_kind = Kind::Clock;
    bool m_all = true;
    bool m_showHelp = true;
    bool m_closing = false;
    double m_scroll = 0;
    double m_sidebarScroll = 0;
    double m_reveal = 1;
    base::Rect m_resting;
    ui::PointF m_hover {-1, -1}, m_origin;
    std::optional<Pick> m_held;
    bool m_drag = false;
    std::unique_ptr<ui::Animation> m_fade;
    std::unique_ptr<ShellWindow> m_lifted;
    Widget m_featuredPhoto {Kind::Photos, Size::Large};
};

DesktopWidgets::DesktopWidgets(Shell *shell, ControlCenter &controls, std::string file)
    : m_shell(shell), m_controls(controls)
    , m_settings(file.empty() ? preferencesFile("desktop-widgets") : std::move(file))
{
    if (!m_settings.contains("Widgets/count")) {
        m_widgets.push_back(std::make_unique<Widget>(Widget {Kind::Calendar, Size::Medium, kOrigin}));
        m_widgets.push_back(std::make_unique<Widget>(Widget {Kind::Clock, Size::Small, kOrigin + base::Point(0, kSize + kGap)}));
        m_widgets.push_back(std::make_unique<Widget>(Widget {Kind::Battery, Size::Small, kOrigin + base::Point(kSize + kGap, kSize + kGap)}));
    }
    const int count = int(std::clamp<int64_t>(m_settings.intValue("Widgets/count"), 0, kMaximumWidgets));
    for (int i = 0; i < count; ++i) {
        const auto prefix = "Widgets/" + std::to_string(i) + '-';
        const auto kind = m_settings.intValue(prefix + "kind", -1);
        const auto position = m_settings.pointValue(prefix + "position");
        if (kind < 0 || kind > int(Kind::Weather))
            continue;
        const bool inCentre = m_settings.boolValue(prefix + "inCentre");
        const auto variants = sizes(Kind(kind), inCentre);
        auto size = Size(m_settings.intValue(prefix + "size", m_settings.boolValue(prefix + "wide") ? 1 : 0));
        if (std::ranges::find(variants, size) == variants.end())
            size = variants.front();
        auto item = std::make_unique<Widget>(Widget {Kind(kind), size});
        item->inCentre = inCentre;
        if (position)
            item->position = {position->x, position->y};
        item->value = m_settings.stringValue(prefix + "value");
        item->completed = m_settings.boolValue(prefix + "completed");
        if (item->kind == Kind::Photos && !item->value.empty())
            item->photo = ui::Image::fromFile(item->value);
        m_widgets.push_back(std::move(item));
    }
    if (!m_settings.boolValue("Widgets/hasCentreLayout")) {
        m_widgets.push_back(std::make_unique<Widget>(Widget {Kind::Battery, Size::Small, {}, true}));
        m_widgets.push_back(std::make_unique<Widget>(Widget {Kind::Calendar, Size::Small, {}, true}));
        m_widgets.push_back(std::make_unique<Widget>(Widget {Kind::WorldClock, Size::Medium, {}, true}));
    }
    m_outputChanged = m_shell->outputsChanged.connect([this] { updateOutput(); });
    m_themeChanged = ui::Theme::changed().connect([this] { updateOutput(); repaint(); });
    m_batteryChanged = m_controls.dataChanged.connect([this] { repaint(); });
    m_tick.onTimeout = [this] { repaint(); };
    m_tick.start(1s);
    m_weatherTick.onTimeout = [this] {
        for (const auto &item : m_widgets)
            refreshWeather(*item);
    };
    m_weatherTick.onTimeout();
    m_weatherTick.start(15min);
    updateOutput();
}

DesktopWidgets::~DesktopWidgets()
{
    *m_alive = false;
}

base::Rect DesktopWidgets::area() const
{
    const auto *output = m_shell->output({});
    return output ? m_shell->workArea(*output) : base::Rect();
}

base::Rect DesktopWidgets::geometryFor(const Widget &item) const
{
    const auto r = area();
    const auto size = dimensions(item.size, kSize);
    const int w = std::min(size.width(), r.width());
    const int h = std::min(size.height(), r.height());
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
    for (auto &item : m_widgets) {
        if (item->inCentre)
            continue;
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
        item->card->setEditing(bool(m_gallery));
        item->card->appear();
        item->card->updateGlass();
        item->window->show();
    }
    if (m_gallery) {
        const auto screen = output->bounds;
        const int w = std::min(1028, screen.width() - 24);
        const int h = std::min(552, screen.height() - 48);
        m_gallery->setOutput(output->name);
        m_galleryView->setRestingGeometry({screen.x() + (screen.width() - w) / 2, screen.bottom() - h - 8, w, h});
        m_galleryView->requestLayout();
    }
}

void DesktopWidgets::repaint()
{
    for (const auto &item : m_widgets)
        if (item->card) {
            item->card->setEditing(bool(m_gallery));
            item->card->requestPaint();
        }
    if (m_galleryView)
        m_galleryView->requestPaint();
    m_repaint();
}

void DesktopWidgets::edit(bool forCentre)
{
    m_addToCentre = forCentre;
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
    if (m_centreEditing)
        m_centreEditing(true);
    repaint();
}

void DesktopWidgets::done()
{
    if (!m_gallery)
        return;
    m_galleryView = nullptr;
    retire(std::move(m_gallery));
    if (m_centreEditing)
        m_centreEditing(false);
    repaint();
}

void DesktopWidgets::retire(std::unique_ptr<ShellWindow> window)
{
    if (!window)
        return;
    window->hide();
    auto *address = window.get();
    m_retiring.push_back(std::move(window));
    core::mainLoop()->post([this, address, alive = std::weak_ptr<bool>(m_alive)] {
        if (const auto guard = alive.lock(); guard && *guard)
            std::erase_if(m_retiring, [address](const auto &candidate) { return candidate.get() == address; });
    });
}

void DesktopWidgets::add(Kind kind, Size size, std::optional<base::Point> global,
                         std::optional<base::Point> dropPoint)
{
    if (m_widgets.size() >= kMaximumWidgets)
        return;
    const bool toCentre = dropPoint ? m_centreDrop && m_centreDrop(*dropPoint)
                                    : m_addToCentre && m_centreOpen && m_centreOpen();
    if (toCentre) {
        auto item = std::make_unique<Widget>(Widget {kind, size == Size::ExtraLarge ? Size::Large : size, {}, true});
        item->arriving = true;
        m_widgets.push_back(std::move(item));
        changed();
        return;
    }
    const auto r = area();
    if (r.isEmpty() || (dropPoint && !r.contains(*dropPoint)))
        return;
    auto item = std::make_unique<Widget>(Widget {kind, size});
    if (global) {
        item->position = *global - r.topLeft();
    } else {
        const auto dims = geometryFor(*item).size();
        const int width = dims.width(), height = dims.height();
        bool found = false;
        for (int y = std::min(kOrigin.y(), std::max(0, r.height() - height)); y + height <= r.height() && !found; y += kSize + kGap) {
            for (int x = std::min(kOrigin.x(), std::max(0, r.width() - width)); x + width <= r.width(); x += kSize + kGap) {
                item->position = {x, y};
                if (std::ranges::none_of(m_widgets, [&](const auto &other) {
                        return !other->inCentre && geometryFor(*other).intersects(geometryFor(*item));
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
    item->arriving = true;
    m_widgets.push_back(std::move(item));
    updateOutput();
    changed();
}

void DesktopWidgets::remove(Widget *item)
{
    const auto it = std::ranges::find_if(m_widgets, [item](const auto &candidate) { return candidate.get() == item; });
    if (it == m_widgets.end())
        return;
    m_exiting.push_back(std::move(*it));
    m_widgets.erase(it);
    if (item->window) {
        item->window->setInputRegion(base::Region());
        item->card->disappear();
    }
    // Allow completion even if the output or notification column stops drawing.
    item->exitTimer = std::make_unique<base::Timer>();
    item->exitTimer->setSingleShot(true);
    item->exitTimer->onTimeout = [this, item] { finishRemoval(item); };
    item->exitTimer->start(500ms);
    changed();
}

void DesktopWidgets::finishRemoval(Widget *item)
{
    const auto it = std::ranges::find_if(m_exiting, [item](const auto &candidate) { return candidate.get() == item; });
    if (it == m_exiting.end())
        return;
    const auto keepAlive = std::move(*it);
    m_exiting.erase(it);
    if (item->inCentre)
        m_changed(); // Detach views before their Widget is destroyed.
}

void DesktopWidgets::resize(Widget *item, Size size)
{
    if (item->size == size)
        return;
    item->size = size;
    if (!item->inCentre)
        updateOutput();
    changed();
}

void DesktopWidgets::configure(Widget &item)
{
    const bool photo = item.kind == Kind::Photos;
    const bool weather = item.kind == Kind::Weather;
    if (!photo && !weather && item.kind != Kind::Reminders)
        return;
    const auto bounds = area();
    if (bounds.width() < 360 || bounds.height() < 150)
        return;
    retire(std::move(m_configuration));
    m_configuration = std::make_unique<ShellWindow>(m_shell, wm::WindowRole::Picker, false);
    auto panel = std::make_unique<ui::View>();
    auto *title = panel->addChild<ui::Label>(photo ? tr("Featured Photo") : weather ? tr("Weather Location") : tr("Reminder"));
    title->setWeight(650);
    title->setGeometry({20, 10, 320, 28});
    auto *field = panel->addChild<ui::TextField>(item.value);
    field->setMaximumLength(photo ? 4096 : 144);
    field->setPlaceholder(photo ? tr("Path to a local image") : weather ? tr("Latitude, longitude") : tr("What do you want to remember?"));
    field->setGeometry({20, 42, 320, 32});
    auto *error = panel->addChild<ui::Label>(weather ? tr("Forecast coordinates are sent to Open-Meteo.")
                                                   : tr("The image could not be opened."));
    error->setGeometry({20, 74, 320, 22});
    error->setVisible(weather);
    if (weather) {
        field->onTextChanged = [error] { error->setText(tr("Forecast coordinates are sent to Open-Meteo.")); };
    }
    auto *cancel = panel->addChild<ui::Button>(tr("Cancel"));
    cancel->setGeometry({163, 105, 83, 30});
    auto *done = panel->addChild<ui::Button>(tr("Done"));
    done->setGeometry({256, 105, 84, 30});
    auto dismiss = [this] { retire(std::move(m_configuration)); };
    cancel->onClicked = dismiss;
    m_configuration->onCloseRequested = dismiss;
    auto apply = [this, item = &item, field, error, photo, weather, dismiss] {
        if (std::ranges::none_of(m_widgets, [item](const auto &candidate) { return candidate.get() == item; })) {
            dismiss();
            return;
        }
        if (weather && !field->text().empty() && !widget_weather::coordinates(field->text())) {
            error->setText(tr("Enter valid coordinates, e.g. 21.03, 105.85."));
            return;
        }
        if (photo) {
            auto image = field->text().empty() ? ui::Image() : ui::Image::fromFile(field->text());
            if (!field->text().empty() && image.isNull()) {
                error->setVisible(true);
                return;
            }
            item->photo = std::move(image);
        }
        item->completed = item->value == field->text() && item->completed;
        item->value = field->text();
        if (weather) {
            item->weatherJob.cancel();
            item->weather.reset();
            refreshWeather(*item);
        }
        changed();
        dismiss();
    };
    done->onClicked = apply;
    field->onAccepted = apply;
    m_configuration->setContentView(std::move(panel));
    m_configuration->setGeometry({bounds.x() + (bounds.width() - 360) / 2,
                                   bounds.y() + (bounds.height() - 150) / 2, 360, 150});
    m_configuration->show();
    m_configuration->activate();
    field->setFocus();
    field->selectAll();
}

void DesktopWidgets::refreshWeather(Widget &item)
{
    if (item.kind != Kind::Weather || item.weatherJob.isPending())
        return;
    if (const auto location = widget_weather::coordinates(item.value)) {
        static base::ThreadPool pool(2);
        item.weatherJob = pool.run([location = *location] { return widget_weather::fetch(location); },
                                   [this, &item](std::optional<widget_weather::Forecast> forecast) {
            item.weather = std::move(forecast);
            repaint();
        });
    }
}

void DesktopWidgets::changed()
{
    save();
    m_changed();
    repaint();
}

std::vector<std::pair<DesktopWidgets::Widget *, ui::RectF>> DesktopWidgets::stackLayout(double width) const
{
    std::vector<std::pair<Widget *, ui::RectF>> result;
    if (width < 30)
        return result;
    const int unit = int(std::floor(width / (2. + double(kGap) / kSize)));
    const auto gap = dimensions(Size::Medium, unit).width() - 2 * unit;
    double x = 0, y = 0, row = 0;
    for (const auto &item : m_widgets) {
        if (!item->inCentre)
            continue;
        const auto dims = dimensions(item->size, unit);
        if (x && x + dims.width() > width) {
            y += row + gap;
            x = row = 0;
        }
        result.emplace_back(item.get(), ui::RectF {x, y, double(dims.width()), double(dims.height())});
        x += dims.width() + gap;
        row = std::max(row, double(dims.height()));
    }
    return result;
}

void DesktopWidgets::reorder(Widget *item, ui::PointF position, double width)
{
    for (const auto &[target, rect] : stackLayout(width)) {
        if (target == item || !rect.contains(position))
            continue;
        const auto index = [this](Widget *widget) {
            return std::ranges::find_if(m_widgets, [widget](const auto &candidate) { return candidate.get() == widget; });
        };
        const auto from = index(item), to = index(target);
        if (from < to)
            std::rotate(from, from + 1, to + 1);
        else
            std::rotate(to, from, from + 1);
        changed();
        return;
    }
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
    m_settings.setValue("Widgets/hasCentreLayout", true);
    for (size_t i = 0; i < m_widgets.size(); ++i) {
        const auto &item = m_widgets[i];
        const auto prefix = "Widgets/" + std::to_string(i) + '-';
        m_settings.setValue(prefix + "kind", int(item->kind));
        m_settings.setValue(prefix + "size", int(item->size));
        m_settings.setValue(prefix + "inCentre", item->inCentre);
        m_settings.setValue(prefix + "value", item->value);
        m_settings.setValue(prefix + "completed", item->completed);
        m_settings.setValue(prefix + "position", base::Settings::Point {item->position.x(), item->position.y()});
    }
    m_settings.sync();
}

} // namespace cutefish::shell
