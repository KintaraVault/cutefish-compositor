// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/controlcenter/tiles.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <numbers>

#include <curl/curl.h>

#include "base/file.h"
#include "base/i18n.h"
#include "base/thread_pool.h"
#include "base/url.h"
#include "shell/controlcenter/panel_style.h"
#include "ui/animation.h"
#include "ui/icon.h"
#include "ui/geometry.h"
#include "ui/image_view.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/path.h"
#include "ui/theme.h"

using cutefish::i18n::format;
using cutefish::i18n::tr;

namespace cutefish::shell {

static constexpr std::chrono::milliseconds kChevronFade {150};

// The light/ artwork's ink, for glyphs drawn in its place.
static constexpr ui::Color kLightArtwork = ui::Color::rgb(0x383838);
static constexpr ui::Color kWhite = ui::Color::rgb(0xFFFFFF);

namespace {
std::shared_ptr<base::file::TemporaryFile> fetchCover(const std::string &url, std::weak_ptr<int> request)
{
    if (request.expired())
        return {};
    static const bool initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    if (!initialized)
        return {};
    const auto *runtime = std::getenv("XDG_RUNTIME_DIR");
    auto file = std::make_shared<base::file::TemporaryFile>(runtime ? runtime : "/tmp", "cutefish-cover-");
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl || !file->isValid())
        return {};
    struct Transfer { base::file::TemporaryFile &file; size_t bytes = 0; } transfer {*file};
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, url.starts_with("https://") ? "https" : "http,https");
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 2500L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 6500L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &request);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, +[](void *state, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
        return static_cast<std::weak_ptr<int> *>(state)->expired();
    });
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, +[](char *data, size_t size, size_t count, void *state) -> size_t {
        auto &out = *static_cast<Transfer *>(state);
        constexpr size_t limit = 8 * 1024 * 1024;
        if (size && count > (limit - out.bytes) / size)
            return 0;
        const size_t bytes = size * count;
        if (!out.file.write(std::string_view(data, bytes)))
            return 0;
        out.bytes += bytes;
        return bytes;
    });
    long status = 0;
    if (curl_easy_perform(curl.get()) != CURLE_OK || curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status) != CURLE_OK
            || status < 200 || status >= 300 || !transfer.bytes)
        return {};
    return file;
}

// Sample the same cover file as ImageView, on a worker rather than in paint().
// Prefer the strongest recurring hue so dark covers with a small bright motif
// still give the visualizer a readable colour.
ui::Color coverAccent(const std::string &path)
{
    const ui::Bitmap image = ui::Bitmap::decodeFile(path, {32, 32}, ui::Bitmap::Scaling::Cover);
    if (image.isNull())
        return ui::Color::rgb(0xcbd0d5);
    struct Hue {
        double weight = 0;
        double red = 0, green = 0, blue = 0;
    };
    std::array<Hue, 24> hues {};
    double neutralWeight = 0, neutralBrightness = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const uint32_t pixel = image.pixel(x, y);
            const double alpha = (pixel >> 24) & 255;
            if (alpha < 96)
                continue;
            const double red = std::min(255., double((pixel >> 16) & 255) * 255 / alpha);
            const double green = std::min(255., double((pixel >> 8) & 255) * 255 / alpha);
            const double blue = std::min(255., double(pixel & 255) * 255 / alpha);
            const double high = std::max({red, green, blue});
            const double low = std::min({red, green, blue});
            const double chroma = high - low;
            const double value = high / 255;
            if (value < .12)
                continue;
            const double opacity = alpha / 255;
            neutralWeight += opacity * value;
            neutralBrightness += high * opacity * value;
            const double saturation = high ? chroma / high : 0;
            if (saturation < .19)
                continue;
            double hue = 0;
            if (high == red)
                hue = std::fmod((green - blue) / chroma + 6, 6);
            else if (high == green)
                hue = (blue - red) / chroma + 2;
            else
                hue = (red - green) / chroma + 4;
            auto &bucket = hues[std::clamp(int(hue * 4), 0, 23)];
            const double weight = saturation * saturation * std::min(1., value * 2.8) * opacity;
            bucket.weight += weight;
            bucket.red += red * weight;
            bucket.green += green * weight;
            bucket.blue += blue * weight;
        }
    }
    const auto best = std::max_element(hues.begin(), hues.end(),
                                       [](const Hue &a, const Hue &b) { return a.weight < b.weight; });
    if (best->weight < .05) {
        if (neutralWeight <= 0)
            return ui::Color::rgb(0xcbd0d5);
        const auto grey = uint8_t(std::clamp(neutralBrightness / neutralWeight, 155., 230.));
        return {grey, grey, grey, 255};
    }
    const double red = best->red / best->weight;
    const double green = best->green / best->weight;
    const double blue = best->blue / best->weight;
    const double boost = std::clamp(185. / std::max({red, green, blue}), 1., 2.6);
    const auto channel = [boost](double value) {
        return uint8_t(std::clamp(value * boost + 8, 0., 255.));
    };
    return {channel(red), channel(green), channel(blue), 255};
}
} // namespace

void SvgIcon::setFile(std::string file)
{
    if (file == m_file)
        return;
    m_file = std::move(file);
    requestPaint();
}

void SvgIcon::setTint(std::optional<ui::Color> tint)
{
    m_tint = tint;
    requestPaint();
}

void SvgIcon::paint(ui::Painter &painter)
{
    if (m_file.empty())
        return;
    // On whole device pixels, as FishUI's IconItem puts its pixmap.
    const ui::Rect device = painter.snappedRect(localRect());
    const double scale = painter.scale();
    const ui::PointF offset = painter.offset();
    painter.drawSvg(m_file,
                    {device.x / scale - offset.x, device.y / scale - offset.y, device.width / scale,
                     device.height / scale},
                    m_tint);
}

DetailChevron::DetailChevron()
    : ui::Icon("chevron-right-small", kSize)
{
    setOpacity(0);
}

DetailChevron::~DetailChevron() = default;

void DetailChevron::setShown(bool shown)
{
    const double target = shown ? 1 : 0;
    if (!window()) {
        setOpacity(target);
        return;
    }
    if (!m_fade) {
        m_fade = std::make_unique<ui::Animation>(window());
        m_fade->setDuration(kChevronFade);
        m_fade->setEasing(ui::Animation::Easing::OutQuad);
        m_fade->onUpdate = [this](double t) { setOpacity(m_from + (m_to - m_from) * t); };
    }
    m_fade->stop();
    m_from = opacity();
    m_to = target;
    m_fade->start();
}

CardItem::CardItem()
{
    m_svg = addChild<SvgIcon>();
    m_glyph = addChild<ui::Icon>(std::string(), kIconSize);
    m_glyph->setVisible(false);
    m_themeGlyph = addChild<ui::ThemeIcon>(std::string(), 24);
    m_themeGlyph->setVisible(false);
    m_chevron = addChild<DetailChevron>();
    m_chevron->setVisible(false);
    m_title = addChild<ui::Label>();
    m_title->setElide(ui::Label::Elide::Right);
    m_status = addChild<ui::Label>();
    m_status->setElide(ui::Label::Elide::Right);
}

CardItem::~CardItem() = default;

void CardItem::setArtwork(std::string name)
{
    m_artwork = std::move(name);
    m_glyph->setVisible(false);
    m_themeGlyph->setVisible(false);
    m_svg->setVisible(true);
    updateIcon();
}

void CardItem::setGlyph(std::string name)
{
    m_artwork.clear();
    m_glyph->setIconName(std::move(name));
    m_glyph->setVisible(true);
    m_svg->setVisible(false);
    m_themeGlyph->setVisible(false);
    updateIcon();
}

void CardItem::setThemeGlyph(std::string name, bool builtin)
{
    if (builtin) {
        setGlyph(std::move(name));
        return;
    }
    m_artwork.clear();
    m_svg->setVisible(false);
    m_glyph->setVisible(false);
    m_themeGlyph->setIconName(std::move(name));
    m_themeGlyph->setVisible(true);
    updateIcon();
}

void CardItem::setControlStyle(bool small, bool large)
{
    m_controlStyle = true;
    m_small = small;
    m_large = large;
    requestLayout();
    requestPaint();
}

void CardItem::setChecked(bool checked)
{
    if (checked == m_checked)
        return;
    m_checked = checked;
    updateIcon();
    requestPaint();
}

void CardItem::setTitle(std::string title)
{
    if (title != m_title->text()) {
        m_title->setText(std::move(title));
        requestLayout();
    }
}

void CardItem::setStatus(std::string status)
{
    if (status != m_status->text()) {
        m_status->setText(std::move(status));
        requestLayout();
    }
}

void CardItem::setHasDetail(bool detail)
{
    if (detail == m_hasDetail)
        return;
    m_hasDetail = detail;
    setTracksHover(detail);
    m_chevron->setVisible(detail);
    requestLayout();
}

ui::RectF CardItem::plateRect() const
{
    const double padding = ui::Theme::current().largeSpacing;
    if (m_controlStyle) {
        const double row = m_large ? geometry().height / 2 : geometry().height;
        const double size = std::clamp(row / 56., 1., 1.3) * (m_small ? 25 : 34);
        return {m_small ? (geometry().width - size) / 2 : 10,
                m_small || !m_large ? (geometry().height - size) / 2 : 10, size, size};
    }
    return {padding, (geometry().height - kPlateSize) / 2, kPlateSize, kPlateSize};
}

bool CardItem::onPlate(const ui::PointF &point) const
{
    const ui::RectF plate = plateRect();
    const double dx = point.x - (plate.x + plate.width / 2);
    const double dy = point.y - (plate.y + plate.height / 2);
    return dx * dx + dy * dy <= plate.width * plate.width / 4;
}

void CardItem::hoverChanged()
{
    m_chevron->setShown(m_hasDetail && isHovered());
}

void CardItem::updateIcon()
{
    const bool white = m_controlStyle ? !m_checked : panel::dark() || m_checked;
    if (!m_artwork.empty())
        m_svg->setFile(panel::artwork(m_artwork, white));
    const auto color = white ? kWhite : m_controlStyle && m_checked && !m_large
            ? ui::Color::rgb(0x087afa) : kLightArtwork;
    m_glyph->setColor(color);
    m_themeGlyph->setColor(color);
}

void CardItem::layout()
{
    const ui::Theme &theme = ui::Theme::current();
    // A theme change relays the card out; the artwork follows it here.
    updateIcon();
    const double padding = m_controlStyle ? 10 : theme.largeSpacing;
    const double height = geometry().height;
    const double scale = std::clamp((m_large ? height / 2 : height) / 56., 1., 1.3);
    const ui::RectF slot = plateRect();
    const double size = m_controlStyle ? scale * (m_small ? 23 : 20) : kIconSize;
    const ui::RectF icon {slot.x + (slot.width - size) / 2, slot.y + (slot.height - size) / 2, size, size};
    m_svg->setGeometry(icon);
    m_glyph->setSize(size);
    m_glyph->setGeometry(icon);
    m_themeGlyph->setSize(size);
    m_themeGlyph->setGeometry(icon);
    if (m_controlStyle) {
        m_title->setVisible(!m_small);
        m_title->setFontSize(std::round(12 * scale));
        m_title->setWeight(600);
        m_title->setColor(m_checked && m_large ? kLightArtwork : kWhite);
    }

    m_status->setFontSize(m_controlStyle ? std::round(10 * scale) : theme.smallFontSize());
    m_status->setColor(m_controlStyle ? (m_checked && m_large ? kLightArtwork : kWhite).withAlpha(.75)
                                      : panel::secondaryTextColor());
    m_status->setVisible(!m_small && !m_status->text().empty());
    // The chevron's slot stays clear while it is hidden, so the text never
    // elides afresh under the pointer.
    double right = geometry().width - padding;
    m_chevron->setVisible(m_hasDetail && !m_small);
    if (m_hasDetail && !m_small) {
        const double size = DetailChevron::kSize;
        m_chevron->setColor(panel::secondaryTextColor());
        m_chevron->setGeometry({right - size, (height - size) / 2, size, size});
        if (!m_controlStyle)
            right -= size + theme.smallSpacing;
    }
    const double x = m_large ? padding : slot.x + slot.width + (m_controlStyle ? 5 : theme.smallSpacing * 1.25);
    const double width = std::max(0.0, right - x);
    const double titleHeight = m_title->sizeHint().height;
    const double statusHeight = m_status->isVisible() ? m_status->sizeHint().height : 0;
    const double top = m_large ? height - titleHeight - statusHeight - padding : (height - titleHeight - statusHeight) / 2;
    m_title->setGeometry({x, top, width, titleHeight});
    m_status->setGeometry({x, top + titleHeight, width, statusHeight});
}

void CardItem::paint(ui::Painter &painter)
{
    if (m_controlStyle && (m_small || m_large)) {
        const double radius = m_small ? localRect().height / 2 : localRect().height * .24;
        if (m_checked)
            painter.fillRoundedRect(localRect(), radius, ui::Color::rgb(m_pressed ? 0xDADADD : 0xF7F7F9));
        else if (m_pressed || isHovered())
            painter.fillRoundedRect(localRect(), radius, kWhite.withAlpha(m_pressed ? .19 : .08));
        return;
    }
    if (!m_controlStyle)
        panel::paintTile(painter, localRect());
    const ui::Color surface = panel::tileInkColor();
    const bool darkSurface = panel::isDark(surface);
    const ui::Color highlight = ui::Theme::current().highlight;
    const bool pressed = m_pressed && m_platePressed;
    ui::Color plate;
    if (m_controlStyle)
        plate = m_checked ? ui::Color::rgb(pressed ? 0xe0e4eb : 0xfafcff) : kWhite.withAlpha(pressed ? .28 : .20);
    else if (m_checked)
        plate = pressed ? panel::shade(highlight, 0.2) : highlight;
    else
        plate = panel::on(surface, pressed ? (darkSurface ? 0.25 : 0.2) : (darkSurface ? 0.15 : 0.1));
    const ui::RectF rect = panel::parityPlate(plateRect(), m_controlStyle ? 25 : kIconSize, painter.scale());
    painter.fillRoundedRect(rect, rect.width / 2, plate);
}

void CardItem::pointerPress(const ui::PointerEvent &event)
{
    if (event.button != ui::MouseButton::Left)
        return;
    m_pressed = true;
    m_platePressed = m_controlStyle || !m_hasDetail || (!m_small && onPlate(event.position));
    requestPaint();
}

void CardItem::pointerRelease(const ui::PointerEvent &event)
{
    if (m_controlStyle && event.button == ui::MouseButton::Right) {
        if (m_hasDetail && onDetail)
            onDetail();
        return;
    }
    if (event.button != ui::MouseButton::Left || !m_pressed)
        return;
    m_pressed = false;
    requestPaint();
    const ui::RectF rect = localRect();
    const bool inside = event.position.x >= 0 && event.position.y >= 0 && event.position.x < rect.width
            && event.position.y < rect.height;
    if (!inside)
        return;
    if (m_controlStyle) {
        if (onClicked)
            onClicked();
        return;
    }
    if (m_platePressed) {
        if ((!m_hasDetail || onPlate(event.position)) && onClicked)
            onClicked();
    } else if ((m_small || !onPlate(event.position)) && onDetail) {
        onDetail();
    }
}

IconButton::IconButton()
    : m_surface(ui::Theme::current().background)
{
    setTracksHover(true);
}

IconButton::~IconButton() = default;

void IconButton::setTransport(Transport transport, bool white, bool playing)
{
    const ui::Color ink = white ? kWhite : kLightArtwork;
    if (m_transport != transport || m_glyphInk != ink || m_playing != playing)
        requestPaint();
    m_transport = transport;
    m_glyphInk = ink;
    if (!window())
        m_morph = playing ? 1 : 0;
    else if (transport == Transport::PlayPause && m_playing != playing) {
        m_morphMotion = std::make_unique<ui::Animation>(window());
        m_morphMotion->setDuration(std::chrono::milliseconds(230));
        m_morphMotion->setEasing(ui::Animation::Easing::InOutCubic);
        m_morphMotion->onUpdate = [this, from = m_morph, to = playing ? 1. : 0.](double t) {
            m_morph = std::lerp(from, to, t);
            requestPaint();
        };
        m_morphMotion->start();
    }
    m_playing = playing;
}

void IconButton::animateSkip()
{
    if (!window() || m_transport == Transport::PlayPause)
        return;
    m_skip = std::make_unique<ui::Animation>(window());
    m_skip->setDuration(std::chrono::milliseconds(300));
    m_skip->setEasing(ui::Animation::Easing::OutCubic);
    m_skip->onUpdate = [this](double t) { m_skipProgress = t; requestPaint(); };
    m_skip->start();
}

void IconButton::setSurfaceColor(ui::Color color)
{
    m_surface = color;
    requestPaint();
}

void IconButton::setGlyphSize(double size)
{
    if (m_size == size)
        return;
    m_size = size;
    requestPaint();
}

void IconButton::animate()
{
    const double press = isEnabled() && m_pressed && m_inside ? 1 : 0;
    const double ink = press ? 1 : isEnabled() && isHovered() ? .52 : 0;
    if (!window()) {
        m_press = press;
        m_ink = ink;
        requestPaint();
        return;
    }
    if (!m_motion)
        m_motion = std::make_unique<ui::Animation>(window());
    m_motion->stop();
    m_motion->setDuration(std::chrono::milliseconds(press ? 95 : 180));
    m_motion->setEasing(ui::Animation::Easing::OutCubic);
    m_motion->onUpdate = [this, press, ink, fromPress = m_press, fromInk = m_ink](double t) {
        m_press = std::lerp(fromPress, press, t);
        m_ink = std::lerp(fromInk, ink, t);
        requestPaint();
    };
    m_motion->start();
}

void IconButton::paint(ui::Painter &painter)
{
    const ui::RectF rect = localRect();
    if (m_ink > 0) {
        const ui::Color color = panel::on(m_surface, m_ink * (panel::isDark(m_surface) ? .23 : .18));
        painter.fillRoundedRect(rect, rect.height / 2, color);
    }
    const double unit = (m_size > 0 ? m_size : rect.height * .75) * (1 - .055 * m_press) / 24;
    const auto shape = [&](const auto &points, double dx, double direction) {
        ui::Path path;
        std::vector<ui::PointF> vertices;
        for (const auto &point : points) {
            const ui::PointF p {rect.width / 2 + unit * direction * (point.x + dx),
                                rect.height / 2 + unit * point.y};
            if (vertices.empty() || std::hypot(p.x - vertices.back().x, p.y - vertices.back().y) > .01)
                vertices.push_back(p);
        }
        if (vertices.size() < 3)
            return;
        if (m_size > 0) {
            // The large Live Activity controls have softly rounded corners,
            // including the two skip triangles and the pause bars.
            for (size_t i = 0; i < vertices.size(); ++i) {
                const auto &prev = vertices[(i + vertices.size() - 1) % vertices.size()];
                const auto &cur = vertices[i];
                const auto &next = vertices[(i + 1) % vertices.size()];
                const double in = std::hypot(cur.x - prev.x, cur.y - prev.y);
                const double out = std::hypot(next.x - cur.x, next.y - cur.y);
                const double cut = std::min({1.35 * unit, in * .3, out * .3});
                const ui::PointF entry {cur.x + (prev.x - cur.x) * cut / in,
                                        cur.y + (prev.y - cur.y) * cut / in};
                const ui::PointF exit {cur.x + (next.x - cur.x) * cut / out,
                                       cur.y + (next.y - cur.y) * cut / out};
                if (i == 0)
                    path.moveTo(entry);
                else
                    path.lineTo(entry);
                path.quadTo(cur, exit);
            }
        } else {
            path.moveTo(vertices.front());
            for (size_t i = 1; i < vertices.size(); ++i)
                path.lineTo(vertices[i]);
        }
        path.close();
        painter.fillPath(path, m_glyphInk);
    };
    using Quad = std::array<ui::PointF, 4>;
    if (m_transport == Transport::PlayPause) {
        // Two halves tile the play triangle exactly, then separate into pause bars.
        constexpr std::array<Quad, 2> play {{
            {{{-5,-8}, {0,-5}, {0,5}, {-5,8}}}, {{{0,-5}, {8,0}, {8,0}, {0,5}}},
        }};
        constexpr std::array<Quad, 2> pause {{
            {{{-6,-7.5}, {-1.8,-7.5}, {-1.8,7.5}, {-6,7.5}}},
            {{{1.8,-7.5}, {6,-7.5}, {6,7.5}, {1.8,7.5}}},
        }};
        if (m_morph == 0) {
            shape(std::array<ui::PointF, 3>{{{-5,-8}, {8,0}, {-5,8}}}, 0, 1);
        } else for (size_t part = 0; part < play.size(); ++part) {
            Quad points;
            for (size_t i = 0; i < points.size(); ++i)
                points[i] = {std::lerp(play[part][i].x, pause[part][i].x, m_morph),
                             std::lerp(play[part][i].y, pause[part][i].y, m_morph)};
            shape(points, 0, 1);
        }
    } else {
        constexpr std::array<ui::PointF, 3> arrow {{{-4,-5.7}, {5,0}, {-4,5.7}}};
        const auto travel = [this](double delay) {
            const double t = std::clamp((m_skipProgress - delay) / (1 - delay), 0., 1.);
            return 5 * std::sin(std::numbers::pi * t);
        };
        const int direction = static_cast<int>(m_transport);
        shape(arrow, -4.8 + .9 * m_press + travel(.18), direction);
        shape(arrow, 4.2 + .9 * m_press + travel(0), direction);
    }
}

void IconButton::hoverChanged()
{
    animate();
}

void IconButton::enabledChanged()
{
    if (!isEnabled())
        m_pressed = false;
    animate();
}

void IconButton::pointerPress(const ui::PointerEvent &event)
{
    if (event.button != ui::MouseButton::Left || !isEnabled())
        return;
    m_pressed = true;
    m_inside = true;
    animate();
}

void IconButton::pointerMove(const ui::PointerEvent &event)
{
    if (!m_pressed)
        return;
    const ui::RectF rect = localRect();
    const bool inside = event.position.x >= 0 && event.position.y >= 0 && event.position.x < rect.width
            && event.position.y < rect.height;
    if (inside != m_inside) {
        m_inside = inside;
        animate();
    }
}

void IconButton::pointerRelease(const ui::PointerEvent &event)
{
    if (event.button != ui::MouseButton::Left || !m_pressed)
        return;
    pointerMove(event);
    m_pressed = false;
    animate();
    if (m_inside && isEnabled() && onClicked) {
        animateSkip();
        onClicked();
    }
}

// MprisItem's buttons: 28 px each, 6 apart.
static constexpr double kButtonSize = 28;

void paintLiveMediaWave(ui::Painter &painter, const ui::RectF &rect, const MediaBands &bands, ui::Color accent)
{
    static constexpr std::array envelope {.32, .64, .98, .92, .62, .34};
    const double step = rect.width / envelope.size();
    const double width = step * .58;
    for (size_t i = 0; i < envelope.size(); ++i) {
        const double height = std::lerp(width, rect.height * envelope[i], bands[i]);
        painter.fillRoundedRect({rect.x + step * (i + .5) - width / 2,
                                 rect.y + (rect.height - height) / 2, width, height}, width / 2,
                                 accent.withAlpha(.60 + .40 * bands[i]));
    }
}

void paintLiveMediaIcon(ui::Painter &painter, const ui::RectF &rect, double opacity)
{
    painter.fillRoundedRect(rect, rect.width * .24, ui::Color::rgb(0x0868f8).withAlpha(opacity), ui::Corners::Continuous);
    painter.drawSvg(panel::artwork("media-cover", true), rect, kWhite.withAlpha(opacity));
}

class MediaTile::Progress final : public ui::View
{
public:
    bool acceptsPointer() const override { return m_seekable; }
    std::function<void()> changed;
    std::function<void(double)> committed;

    void setTrack(double duration, bool seekable, bool reset)
    {
        m_duration = duration;
        if (m_seekable != seekable) {
            m_seekable = seekable;
            requestPaint();
        }
        if (reset || !seekable)
            m_preview.reset();
        if (reset)
            m_position.reset();
    }

    std::optional<double> updatePosition(std::optional<double> position, std::chrono::steady_clock::time_point now)
    {
        const auto target = m_preview ? m_preview : position;
        const auto oldEmphasis = m_emphasis;
        const auto elapsed = std::chrono::duration<double>(now - std::exchange(m_frameTime, now)).count();
        // Follow the latest target from the already painted position. This
        // also handles a new seek or a drag reversal mid-animation.
        const auto approach = [elapsed](double from, double to, double timeConstant, double snap) {
            const double next = std::lerp(from, to, -std::expm1(-elapsed / timeConstant));
            return std::abs(next - to) <= snap ? to : next;
        };
        if (target && m_position && m_duration > 0) {
            const double snap = std::clamp(m_duration * .0002, .001, .05);
            m_position = approach(*m_position, *target, m_preview ? .032 : .048, snap);
        } else {
            m_position = target;
        }
        m_emphasis = approach(m_emphasis, m_seekable && (m_preview || isHovered()) ? 1. : 0., .035, .01);
        const double fraction = m_position && m_duration > 0 ? std::clamp(*m_position / m_duration, 0., 1.) : 0;
        if (fraction != m_fraction || oldEmphasis != m_emphasis) {
            m_fraction = fraction;
            requestPaint();
        }
        return m_position;
    }

protected:
    void paint(ui::Painter &painter) override
    {
        const double h = 5 + 2 * m_emphasis;
        const ui::RectF track {0, (geometry().height - h) / 2, geometry().width, h};
        painter.fillRoundedRect(track, h / 2, kWhite.withAlpha(.23));
        if (m_fraction > 0) {
            const double width = std::min(track.width, std::max(h, track.width * m_fraction));
            painter.fillRoundedRect({0, track.y, width, h}, h / 2, kWhite.withAlpha(.87));
        }
        if (m_emphasis > 0) {
            const double radius = 3.5 + m_emphasis;
            const double cx = std::lerp(radius, std::max(radius, track.width - radius), m_fraction);
            painter.fillRoundedRect({cx - radius, geometry().height / 2 - radius, 2 * radius, 2 * radius},
                                    radius, kWhite.withAlpha(m_emphasis));
        }
    }
    void pointerPress(const ui::PointerEvent &event) override
    {
        if (event.button == ui::MouseButton::Left && m_seekable)
            update(event.position.x);
    }
    void pointerMove(const ui::PointerEvent &event) override
    {
        if (m_preview)
            update(event.position.x);
    }
    void pointerRelease(const ui::PointerEvent &event) override
    {
        if (event.button != ui::MouseButton::Left || !m_preview)
            return;
        m_preview.reset();
        committed(offset(event.position.x));
        changed();
    }

private:
    double offset(double x) const { return std::clamp(x / std::max(1., geometry().width), 0., 1.) * m_duration; }
    void update(double x) { m_preview = offset(x); changed(); }
    std::optional<double> m_preview;
    std::optional<double> m_position;
    std::chrono::steady_clock::time_point m_frameTime;
    double m_duration = 0;
    double m_fraction = 0;
    double m_emphasis = 0;
    bool m_seekable = false;
};

MediaTile::MediaTile()
{
    m_cover = addChild<MediaCover>();
    m_cover->onAccentChanged = [this] { requestPaint(); };
    m_title = addChild<ui::Label>();
    m_title->setElide(ui::Label::Elide::Right);
    m_artist = addChild<ui::Label>();
    m_artist->setElide(ui::Label::Elide::Right);
    m_artist->setOpacity(0.6);
    m_previous = addChild<IconButton>();
    m_play = addChild<IconButton>();
    m_next = addChild<IconButton>();
    m_previous->onClicked = [this] {
        if (onPrevious)
            onPrevious();
    };
    m_play->onClicked = [this] {
        if (m_state.playing ? bool(onPause) : bool(onPlay))
            (m_state.playing ? onPause : onPlay)();
    };
    m_next->onClicked = [this] {
        if (onNext)
            onNext();
    };
    setState({});
}

MediaTile::~MediaTile() = default;

void MediaTile::setActivityStyle()
{
    m_controlStyle = true;
    m_progress = addChild<Progress>();
    m_progress->setName("media-progress");
    m_progress->setTracksHover(true);
    m_progress->changed = [this] { updateTimeline(); };
    m_progress->committed = [this](double seconds) { if (seek) seek(seconds); };
    const auto timeLabel = [this](const char *name, ui::Label::TextAlignment alignment) {
        auto *label = addChild<ui::Label>();
        label->setName(name);
        label->setFontSize(10);
        label->setWeight(600);
        label->setVerticallyCentered(true);
        label->setTextAlignment(alignment);
        label->setFontFeatures({{"tnum", 1}});
        label->setColor(kWhite.withAlpha(.79));
        return label;
    };
    m_elapsed = timeLabel("media-elapsed", ui::Label::TextAlignment::Left);
    m_remaining = timeLabel("media-remaining", ui::Label::TextAlignment::Right);
    m_cover->setStyle(MediaCover::Style::Activity);
}

static std::string formatMediaTime(double time, bool remaining = false)
{
    const auto seconds = int64_t(remaining ? std::ceil(time) : time);
    const auto value = seconds % 60;
    return std::string(remaining ? "−" : "") + std::to_string(seconds / 60) + ':'
            + (value < 10 ? "0" : "") + std::to_string(value);
}

void MediaTile::setTimeline(double duration, bool seekable, bool trackChanged)
{
    m_duration = duration;
    m_progress->setTrack(duration, seekable, trackChanged);
    if (trackChanged)
        m_nextPoll = {};
    updateTimeline();
}

void MediaTile::updateTimeline()
{
    if (!m_progress)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (pollPosition && now >= m_nextPoll) {
        pollPosition();
        m_nextPoll = now + std::chrono::seconds(1);
    }
    const auto pos = m_progress->updatePosition(position ? position() : std::nullopt, now);
    m_elapsed->setText(pos ? formatMediaTime(*pos) : "–:––");
    m_remaining->setText(pos && m_duration > 0 ? formatMediaTime(std::max(0., m_duration - *pos), true) : "–:––");
}

void MediaTile::animateTimeline()
{
    if (!window() || !m_progress || (m_timeline && m_timeline->isRunning()))
        return;
    m_timeline = std::make_unique<ui::Animation>(window());
    m_timeline->setDuration(std::chrono::seconds(4));
    m_timeline->setLoops(ui::Animation::Infinite);
    m_timeline->setEasing(ui::Animation::Easing::Linear);
    m_timeline->onUpdate = [this](double) { updateTimeline(); };
    m_timeline->start();
}

void MediaTile::setState(const MediaState &state)
{
    const bool control = state.hasPlayer && state.canControl;
    m_previous->setEnabled(control && state.canGoPrevious);
    m_play->setEnabled(control && (state.playing ? state.canPause : state.canPlay));
    m_next->setEnabled(control && state.canGoNext);
    for (IconButton *button : {m_previous, m_play, m_next})
        button->setOpacity(button->isEnabled() ? 1 : 0.35);
    std::string title = tr("Not Playing");
    if (state.hasPlayer)
        title = !state.title.empty() ? state.title : state.identity;
    if (m_progress && window() && (title != m_title->text() || state.artist != m_artist->text())) {
        m_metadataMotion = std::make_unique<ui::Animation>(window());
        m_metadataMotion->setDuration(std::chrono::milliseconds(185));
        m_metadataMotion->setEasing(ui::Animation::Easing::OutCubic);
        m_metadataMotion->onUpdate = [this](double t) {
            m_title->setOpacity(t);
            m_artist->setOpacity(t);
        };
        m_title->setOpacity(0);
        m_artist->setOpacity(0);
        m_metadataMotion->start();
    }
    m_state = state;
    m_title->setText(std::move(title));
    m_artist->setText(state.hasPlayer ? state.artist : std::string());
    updateArtwork();
    m_cover->setUrl(state.hasPlayer ? state.artUrl : std::string());
    requestLayout();
}

void MediaTile::updateArtwork()
{
    const bool white = m_controlStyle || panel::dark();
    m_previous->setTransport(IconButton::Transport::Previous, white);
    m_play->setTransport(IconButton::Transport::PlayPause, white, m_state.playing);
    m_next->setTransport(IconButton::Transport::Next, white);
    for (IconButton *button : {m_previous, m_play, m_next})
        button->setSurfaceColor(m_progress ? ui::Color::rgb(0x242424) : panel::tileInkColor());
}

MediaCover::MediaCover(Style style)
    : m_style(style)
{
    m_image = addChild<ui::ImageView>();
    m_image->setName("media-cover-image");
    m_image->onStatusChanged = [this](ui::ImageView::Status status) {
        m_ready = status == ui::ImageView::Status::Ready;
        m_loading = status == ui::ImageView::Status::Loading;
        animate();
        requestPaint();
    };
}

MediaCover::~MediaCover() = default;

void MediaCover::setStyle(Style style)
{
    if (m_style == style)
        return;
    m_style = style;
    requestLayout();
}

void MediaCover::setUrl(const std::string &url)
{
    if (url == m_url)
        return;
    m_url = url;
    m_request.reset();
    m_paletteRequest.reset();
    m_accent = ui::Color::rgb(0xcbd0d5);
    if (onAccentChanged)
        onAccentChanged();
    m_image->setSource({});
    m_file.reset();
    m_ready = false;
    m_loading = !url.empty();
    std::string path;
    if (base::url::isLocalFile(url))
        path = base::url::toLocalFile(url).value_or(std::string());
    else if (url.starts_with('/'))
        path = url;
    if (!path.empty()) {
        m_image->setSource(path);
        loadAccent(path);
    } else if (url.starts_with("https://") || url.starts_with("http://")) {
        // Compact and expanded views can reuse the downloaded file while
        // either one still has it. The ImageView caches each decoded size.
        static std::map<std::string, std::weak_ptr<base::file::TemporaryFile>> covers;
        std::erase_if(covers, [](const auto &entry) { return entry.second.expired(); });
        if (auto it = covers.find(url); it != covers.end()) {
            m_file = it->second.lock();
            m_image->setSource(m_file->path());
            loadAccent(m_file->path());
            return;
        }
        static base::ThreadPool pool(2);
        m_request = std::make_shared<int>();
        std::weak_ptr<int> request = m_request;
        pool.run([url, request] { return fetchCover(url, request); },
                 [this, request](std::shared_ptr<base::file::TemporaryFile> file) {
            if (request.expired())
                return;
            m_file = file;
            if (file) {
                covers[m_url] = file;
                m_image->setSource(file->path());
                loadAccent(file->path());
            } else {
                m_loading = false;
                animate();
                requestPaint();
            }
        }).detach();
    } else {
        m_loading = false;
    }
    animate();
    requestPaint();
}

void MediaCover::loadAccent(const std::string &path)
{
    static base::ThreadPool palettePool(2);
    m_paletteRequest = std::make_shared<int>();
    const std::weak_ptr<int> request = m_paletteRequest;
    palettePool.run([path, request] {
        return request.expired() ? ui::Color::rgb(0xcbd0d5) : coverAccent(path);
    }, [this, request](ui::Color accent) {
        if (request.expired() || m_accent == accent)
            return;
        m_accent = accent;
        if (onAccentChanged)
            onAccentChanged();
    }).detach();
}

void MediaCover::animate()
{
    m_animation.reset();
    m_phase = 0;
    // Keep ImageView visible while loading: its layout starts the decoder.
    m_image->setOpacity(m_ready && !window() ? 1 : 0);
    if (!window())
        return;
    if (!m_ready && !m_loading)
        return;
    m_animation = std::make_unique<ui::Animation>(window());
    if (m_ready) {
        m_animation->setDuration(std::chrono::milliseconds(280));
        m_animation->setEasing(ui::Animation::Easing::OutCubic);
        m_animation->onUpdate = [this](double t) { m_image->setOpacity(t); m_phase = t; requestPaint(); };
    } else {
        m_animation->setDuration(std::chrono::milliseconds(850));
        m_animation->setEasing(ui::Animation::Easing::Linear);
        m_animation->setLoops(ui::Animation::Infinite);
        m_animation->onUpdate = [this](double t) { m_phase = t; requestPaint(); };
    }
    m_animation->start();
}

void MediaCover::layout()
{
    const auto rect = localRect();
    m_image->setFillMode(m_style == Style::Activity ? ui::ImageView::FillMode::PreserveAspectCrop
                                                    : ui::ImageView::FillMode::PreserveAspectFit);
    const double radius = m_style == Style::Activity ? rect.width * .20 : ui::Theme::current().smallRadius;
    m_image->setCornerRadius(radius, ui::Corners::Continuous);
    m_image->setGeometry(rect);
    m_image->setSourceSize(m_style == Style::Activity && rect.width < 25 ? ui::SizeF{16, 16} : rect.size());
    if (!m_animation && (m_ready || m_loading))
        animate();
}

void MediaTile::layout()
{
    const ui::Theme &theme = ui::Theme::current();
    updateArtwork();
    const double padding = theme.largeSpacing;
    if (m_progress) {
        animateTimeline();
        const double w = localRect().width;
        const double inset = 16;
        const ui::RectF art {inset, 16, 56, 56};
        m_cover->setGeometry(art);
        const double x = art.x + art.width + 10;
        const double textWidth = std::max(0., w - x - 47);
        m_title->setFontSize(14);
        m_title->setWeight(700);
        m_title->setColor(kWhite);
        m_title->setGeometry({x, m_artist->text().empty() ? 32. : 27., textWidth, 18});
        m_artist->setVisible(!m_artist->text().empty());
        m_artist->setFontSize(13);
        m_artist->setColor(kWhite);
        m_artist->setGeometry({x, 43, textWidth, 17});
        m_elapsed->setGeometry({inset, 75, 36, 23});
        m_remaining->setGeometry({w - inset - 39, 75, 39, 23});
        m_progress->setGeometry({inset + 31, 75, std::max(0., w - 2 * inset - 69), 23});
        constexpr double slot = 44;
        for (size_t i = 0; i < 3; ++i) {
            auto *button = std::array {m_previous, m_play, m_next}[i];
            button->setGlyphSize(i == 1 ? 40 : 31);
            button->setGeometry({w / 2 + (double(i) - 1) * w * .25 - slot / 2, 104, slot, slot});
        }
        return;
    }
    if (m_controlStyle) {
        const double h = geometry().height;
        const double w = geometry().width;
        const double scale = std::clamp(h / 121., 1., 1.3);
        const double cover = std::min(53.0, h * .29);
        const ui::RectF art {10, 10, cover, cover};
        m_cover->setStyle(MediaCover::Style::Control);
        m_cover->setGeometry(art);
        m_title->setWeight(650);
        m_title->setFontSize(std::round(12 * scale));
        m_title->setColor(kWhite);
        m_title->setGeometry({10, h - (m_artist->text().empty() ? 53 : 64) * scale, w - 20, 18 * scale});
        m_artist->setVisible(!m_artist->text().empty());
        m_artist->setColor(kWhite.withAlpha(.74));
        m_artist->setFontSize(std::round(10 * scale));
        m_artist->setGeometry({10, h - 46 * scale, w - 20, 14 * scale});
        const double slot = std::min(34., w / 5);
        for (size_t i = 0; i < 3; ++i)
            std::array {m_previous, m_play, m_next}[i]->setGeometry({w * (i + 1) / 4 - slot / 2, h - 19 * scale - slot / 2, slot, slot});
        m_play->setOpacity(m_play->isEnabled() ? 1 : .68);
        return;
    }
    const double content = geometry().height - 2 * padding;
    const ui::RectF art {padding, padding, content, content};
    m_cover->setGeometry(art);

    // The last glyph lines up with the sliders' tracks: the artwork keeps 3.125
    // of its 16 units clear, and 2px more make up for the tracks' rounded ends.
    const double iconInset = (kButtonSize - kButtonSize * 0.75) / 2;
    const double rightMargin = -iconInset - (kButtonSize - iconInset * 2) * 3.125 / 16 + 2;
    const double spacing = theme.smallSpacing;
    const double buttonsWidth = 3 * kButtonSize + 2 * spacing;
    const double buttonsX = geometry().width - padding - rightMargin - buttonsWidth;
    const double buttonY = padding + (content - kButtonSize) / 2;
    m_previous->setGeometry({buttonsX, buttonY, kButtonSize, kButtonSize});
    m_play->setGeometry({buttonsX + kButtonSize + spacing, buttonY, kButtonSize, kButtonSize});
    m_next->setGeometry({buttonsX + 2 * (kButtonSize + spacing), buttonY, kButtonSize, kButtonSize});

    m_artist->setFontSize(theme.smallFontSize());
    m_artist->setVisible(!m_artist->text().empty());
    const double x = art.x + art.width + padding;
    const double width = std::max(0.0, buttonsX - padding - x);
    const double titleHeight = m_title->sizeHint().height;
    const double artistHeight = m_artist->isVisible() ? m_artist->sizeHint().height : 0;
    const double top = padding + (content - titleHeight - artistHeight) / 2;
    m_title->setGeometry({x, top, width, titleHeight});
    m_artist->setGeometry({x, top + titleHeight, width, artistHeight});
}

void MediaTile::paint(ui::Painter &painter)
{
    if (!m_controlStyle)
        panel::paintTile(painter, localRect());
    if (m_progress)
        paintLiveMediaWave(painter, {localRect().width - 39, 34, 23, 13}, m_bands, m_cover->accentColor());
}

void MediaTile::setBands(const MediaBands &bands)
{
    if (m_bands != bands) {
        m_bands = bands;
        requestPaint();
    }
}

void MediaCover::paint(ui::Painter &painter)
{
    if (m_ready && m_phase >= 1)
        return;
    // The cards' plate colour under the default cover, inside its mask.
    const ui::Color surface = panel::tileInkColor();
    const ui::RectF art = localRect();
    if (m_style == Style::Activity && (!m_loading || art.width < 25)) {
        paintLiveMediaIcon(painter, art);
        return;
    }
    const bool control = m_style != Style::Legacy;
    painter.fillRoundedRect(art, control ? 11 : ui::Theme::current().smallRadius,
                            control ? kWhite.withAlpha(.12) : panel::on(surface, panel::isDark(surface) ? 0.15 : 0.1),
                            ui::Corners::Continuous);
    if (!m_loading && control) {
        const double size = art.width * .42;
        painter.drawSvg(panel::artwork("media-playback-start-symbolic", true),
                        {art.x + (art.width - size) / 2, art.y + (art.height - size) / 2, size, size},
                        kWhite.withAlpha(.66));
    }
    if (!control)
        painter.drawSvg(panel::artwork("media-cover", panel::dark()), art);
    if (m_loading && control) {
        const ui::PointF c {art.x + art.width / 2, art.y + art.height / 2};
        for (int i = 0; i < 12; ++i) {
            const double angle = (i / 12. + m_phase) * 2 * std::numbers::pi;
            painter.fillRoundedRect({c.x + 9 * std::cos(angle) - 1.4, c.y + 9 * std::sin(angle) - 1.4, 2.8, 2.8},
                                    1.4, kWhite.withAlpha(.15 + .7 * i / 11));
        }
    }
}

std::string deviceUsageText(const DeviceUser &user)
{
    // Whole sentences rather than joined parts, so each translates as one.
    const std::string pattern = [&user] {
        if (user.camera && user.microphone && user.systemAudio)
            // TRANSLATORS: {name} is an application's name, as in the other "is using" sentences.
            return tr("{name} is using the camera and microphone and recording system audio");
        if (user.camera && user.microphone)
            return tr("{name} is using the camera and microphone");
        if (user.camera && user.systemAudio)
            return tr("{name} is using the camera and recording system audio");
        if (user.microphone && user.systemAudio)
            return tr("{name} is using the microphone and recording system audio");
        if (user.camera)
            return tr("{name} is using the camera");
        if (user.microphone)
            return tr("{name} is using the microphone");
        return tr("{name} is recording system audio");
    }();
    return format(pattern, {{"name", user.name}});
}

DeviceUsersTile::DeviceUsersTile() = default;

void DeviceUsersTile::setUsers(const std::vector<DeviceUser> &users)
{
    if (users == m_users)
        return;
    m_users = users;
    for (Row &row : m_rows)
        takeChild(row.title);
    m_rows.clear();
    for (const DeviceUser &user : m_users) {
        Row row;
        row.title = addChild<ui::Label>(user.name);
        row.title->setElide(ui::Label::Elide::Right);
        row.title->setColor(kWhite);
        row.title->setFontSize(15);
        row.title->setWeight(600);
        m_rows.push_back(row);
    }
    retranslate();
    requestLayout();
}

void DeviceUsersTile::retranslate()
{
    std::string tip;
    for (const auto &user : m_users) {
        if (!tip.empty())
            tip += '\n';
        tip += deviceUsageText(user);
    }
    setToolTip(std::move(tip));
}

ui::SizeF DeviceUsersTile::sizeHint() const
{
    double width = 0;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        const auto &user = m_users[i];
        width = std::max(width, 31 + (user.camera + user.microphone + user.systemAudio) * 25
                               + m_rows[i].title->sizeHint().width);
    }
    return {std::max(width, 96.), heightForWidth(0)};
}

double DeviceUsersTile::heightForWidth(double) const
{
    return m_rows.size() * 36;
}

void DeviceUsersTile::layout()
{
    for (size_t i = 0; i < m_rows.size(); ++i) {
        const auto &user = m_users[i];
        const double x = 13 + (user.camera + user.microphone + user.systemAudio) * 25;
        m_rows[i].title->setGeometry({x, i * 36., std::max(0., geometry().width - x - 10), 36});
    }
}

void DeviceUsersTile::paint(ui::Painter &painter)
{
    for (size_t i = 0; i < m_users.size(); ++i) {
        const auto &user = m_users[i];
        double x = 12;
        const auto badge = [&](const char *icon, ui::Color color) {
            const double y = i * 36. + 8;
            painter.fillRoundedRect({x, y, 21, 21}, 10.5, color);
            painter.drawSvg(panel::artwork(icon, true), {x + 4, y + 4, 13, 13}, kWhite);
            x += 25;
        };
        if (user.microphone)
            badge("microphone", ui::Color::rgb(0xfc9d2f));
        if (user.camera)
            badge("camera", ui::Color::rgb(0x439f66));
        if (user.systemAudio)
            badge("audio-volume-high-symbolic", ui::Color::rgb(0x7765ef));
    }
}

} // namespace cutefish::shell
