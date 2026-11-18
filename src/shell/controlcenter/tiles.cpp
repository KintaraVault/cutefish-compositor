// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/controlcenter/tiles.h"

#include <array>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
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
        const double size = m_small ? 23 : 33;
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
    const ui::RectF slot = plateRect();
    const double size = m_controlStyle ? (m_small ? 19 : 20) : kIconSize;
    const ui::RectF icon {slot.x + (slot.width - size) / 2, slot.y + (slot.height - size) / 2, size, size};
    m_svg->setGeometry(icon);
    m_glyph->setSize(size);
    m_glyph->setGeometry(icon);
    m_themeGlyph->setSize(size);
    m_themeGlyph->setGeometry(icon);
    if (m_controlStyle) {
        m_title->setVisible(!m_small);
        m_title->setFontSize(12);
        m_title->setWeight(600);
        m_title->setColor(m_checked && m_large ? kLightArtwork : kWhite);
    }

    m_status->setFontSize(m_controlStyle ? 10 : theme.smallFontSize());
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
    m_icon = addChild<SvgIcon>();
    setTracksHover(true);
}

void IconButton::setFile(std::string file)
{
    m_icon->setFile(std::move(file));
}

void IconButton::setSurfaceColor(ui::Color color)
{
    m_surface = color;
    requestPaint();
}

void IconButton::layout()
{
    const double size = geometry().height * 0.75;
    m_icon->setGeometry({(geometry().width - size) / 2, (geometry().height - size) / 2, size, size});
}

void IconButton::paint(ui::Painter &painter)
{
    if (!isEnabled() || !isHovered())
        return;
    const bool dark = panel::isDark(m_surface);
    const bool pressed = m_pressed && m_inside;
    const ui::Color color = panel::on(m_surface, pressed ? (dark ? 0.3 : 0.2) : (dark ? 0.2 : 0.1));
    const ui::RectF rect = localRect();
    painter.fillRoundedRect(rect, rect.height / 2, color);
}

void IconButton::hoverChanged()
{
    requestPaint();
}

void IconButton::enabledChanged()
{
    if (!isEnabled())
        m_pressed = false;
    requestPaint();
}

void IconButton::pointerPress(const ui::PointerEvent &event)
{
    if (event.button != ui::MouseButton::Left || !isEnabled())
        return;
    m_pressed = true;
    m_inside = true;
    requestPaint();
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
        requestPaint();
    }
}

void IconButton::pointerRelease(const ui::PointerEvent &event)
{
    if (event.button != ui::MouseButton::Left || !m_pressed)
        return;
    pointerMove(event);
    m_pressed = false;
    requestPaint();
    if (m_inside && isEnabled() && onClicked)
        onClicked();
}

// MprisItem's buttons: 28 px each, 6 apart.
static constexpr double kButtonSize = 28;

MediaTile::MediaTile()
{
    m_cover = addChild<SvgIcon>();
    m_art = addChild<ui::ImageView>();
    m_art->setName("media-cover-image");
    m_art->setFillMode(ui::ImageView::FillMode::PreserveAspectFit);
    m_art->setCornerRadius(ui::Theme::current().smallRadius, ui::Corners::Continuous);
    m_art->setVisible(false);
    m_art->onStatusChanged = [this](ui::ImageView::Status status) {
        if (status == ui::ImageView::Status::Null)
            return;
        m_artReady = status == ui::ImageView::Status::Ready;
        m_artLoading = status == ui::ImageView::Status::Loading;
        m_art->setVisible(m_artReady);
        animateArtwork();
        requestPaint();
    };
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

void MediaTile::setState(const MediaState &state)
{
    m_state = state;
    const bool control = state.hasPlayer && state.canControl;
    m_previous->setEnabled(control && state.canGoPrevious);
    m_play->setEnabled(control && (state.playing ? state.canPause : state.canPlay));
    m_next->setEnabled(control && state.canGoNext);
    for (IconButton *button : {m_previous, m_play, m_next})
        button->setOpacity(button->isEnabled() ? 1 : 0.35);
    std::string title = tr("Not Playing");
    if (state.hasPlayer)
        title = !state.title.empty() ? state.title : state.identity;
    m_title->setText(std::move(title));
    m_artist->setText(state.hasPlayer ? state.artist : std::string());
    updateArtwork();
    loadArtwork();
    requestLayout();
}

void MediaTile::updateArtwork()
{
    const bool white = m_controlStyle || panel::dark();
    m_cover->setFile(panel::artwork("media-cover", white));
    m_previous->setFile(panel::artwork("media-skip-backward-symbolic", white));
    m_play->setFile(panel::artwork(m_state.playing ? "media-playback-pause-symbolic"
                                                   : "media-playback-start-symbolic",
                                   white));
    m_next->setFile(panel::artwork("media-skip-forward-symbolic", white));
    for (IconButton *button : {m_previous, m_play, m_next})
        button->setSurfaceColor(panel::tileInkColor());

}

void MediaTile::loadArtwork()
{
    const std::string url = m_state.hasPlayer ? m_state.artUrl : std::string();
    if (url == m_artUrl)
        return;
    m_artUrl = url;
    m_artRequest.reset();
    m_art->setSource({});
    m_artFile.reset();
    m_artReady = false;
    m_artLoading = !url.empty();
    m_art->setVisible(false);
    m_cover->setVisible(true);
    std::string path;
    if (base::url::isLocalFile(url))
        path = base::url::toLocalFile(url).value_or(std::string());
    else if (url.starts_with('/'))
        path = url;
    if (!path.empty()) {
        m_art->setSource(path);
    } else if (url.starts_with("https://") || url.starts_with("http://")) {
        static base::ThreadPool pool(2);
        m_artRequest = std::make_shared<int>();
        std::weak_ptr<int> request = m_artRequest;
        pool.run([url, request] { return fetchCover(url, request); },
                 [this, request](std::shared_ptr<base::file::TemporaryFile> file) {
            if (request.expired())
                return;
            m_artFile = file;
            if (file) {
                m_art->setSource(file->path());
            } else {
                m_artLoading = false;
                animateArtwork();
                requestPaint();
            }
        }).detach();
    } else {
        m_artLoading = false;
    }
    animateArtwork();
}

void MediaTile::animateArtwork()
{
    if (!window())
        return;
    m_artAnimation = std::make_unique<ui::Animation>(window());
    m_artPhase = 0;
    if (m_artReady) {
        m_art->setOpacity(0);
        m_artAnimation->setDuration(std::chrono::milliseconds(280));
        m_artAnimation->setEasing(ui::Animation::Easing::OutCubic);
        m_artAnimation->onUpdate = [this](double t) { m_art->setOpacity(t); m_artPhase = t; requestPaint(); };
        m_artAnimation->onFinished = [this] { m_cover->setVisible(false); };
    } else if (m_artLoading) {
        m_artAnimation->setDuration(std::chrono::milliseconds(850));
        m_artAnimation->setEasing(ui::Animation::Easing::Linear);
        m_artAnimation->setLoops(ui::Animation::Infinite);
        m_artAnimation->onUpdate = [this](double t) { m_artPhase = t; requestPaint(); };
    } else {
        return;
    }
    m_artAnimation->start();
}

void MediaTile::layout()
{
    if (!m_artAnimation && (m_artReady || m_artLoading))
        animateArtwork();
    const ui::Theme &theme = ui::Theme::current();
    updateArtwork();
    const double padding = theme.largeSpacing;
    if (m_controlStyle) {
        const double h = geometry().height;
        const double w = geometry().width;
        const double cover = std::min(53.0, h * .29);
        const ui::RectF art {10, 10, cover, cover};
        m_cover->setOpacity(0);
        m_cover->setGeometry(art);
        m_art->setGeometry(art);
        m_art->setSourceSize({cover, cover});
        m_title->setWeight(650);
        m_title->setFontSize(12);
        m_title->setColor(kWhite);
        m_title->setGeometry({10, h - (m_artist->text().empty() ? 53 : 64), w - 20, 18});
        m_artist->setVisible(!m_artist->text().empty());
        m_artist->setColor(kWhite.withAlpha(.74));
        m_artist->setFontSize(10);
        m_artist->setGeometry({10, h - 46, w - 20, 14});
        const double slot = std::min(34., w / 5);
        for (size_t i = 0; i < 3; ++i)
            std::array {m_previous, m_play, m_next}[i]->setGeometry({w * (i + 1) / 4 - slot / 2, h - 19 - slot / 2, slot, slot});
        m_play->setOpacity(m_play->isEnabled() ? 1 : .68);
        return;
    }
    const double content = geometry().height - 2 * padding;
    const ui::RectF art {padding, padding, content, content};
    m_cover->setGeometry(art);
    m_art->setGeometry(art);
    m_art->setSourceSize({content, content});

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
    if (m_artReady && m_artPhase >= 1)
        return;
    // The cards' plate colour under the default cover, inside its mask.
    const ui::Color surface = panel::tileInkColor();
    const ui::RectF art = m_cover->geometry();
    painter.fillRoundedRect(art, m_controlStyle ? 11 : ui::Theme::current().smallRadius,
                            m_controlStyle ? kWhite.withAlpha(.12) : panel::on(surface, panel::isDark(surface) ? 0.15 : 0.1),
                            ui::Corners::Continuous);
    if (m_artLoading && m_controlStyle) {
        const ui::PointF c {art.x + art.width / 2, art.y + art.height / 2};
        for (int i = 0; i < 12; ++i) {
            const double angle = (i / 12. + m_artPhase) * 6.283185307;
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
