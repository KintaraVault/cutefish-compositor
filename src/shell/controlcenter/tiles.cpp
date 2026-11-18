// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/controlcenter/tiles.h"

#include <array>

#include <algorithm>
#include <chrono>
#include <cmath>

#include "base/i18n.h"
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
        const double size = m_small ? 30 : 48;
        return {m_small ? (geometry().width - size) / 2 : 17,
                m_small || !m_large ? (geometry().height - size) / 2 : 17, size, size};
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
    const double padding = m_controlStyle ? 17 : theme.largeSpacing;
    const double height = geometry().height;
    const ui::RectF slot = plateRect();
    const double size = m_controlStyle ? (m_small ? 28 : 25) : kIconSize;
    const ui::RectF icon {slot.x + (slot.width - size) / 2, slot.y + (slot.height - size) / 2, size, size};
    m_svg->setGeometry(icon);
    m_glyph->setSize(size);
    m_glyph->setGeometry(icon);
    m_themeGlyph->setSize(size);
    m_themeGlyph->setGeometry(icon);
    if (m_controlStyle) {
        m_title->setVisible(!m_small);
        m_title->setFontSize(15);
        m_title->setWeight(600);
        m_title->setColor(m_checked && m_large ? kLightArtwork : kWhite);
    }

    m_status->setFontSize(m_controlStyle ? 14 : theme.smallFontSize());
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
    const double x = m_large ? padding : slot.x + slot.width + theme.smallSpacing * 1.25;
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
    m_platePressed = !m_hasDetail || (!m_small && onPlate(event.position));
    requestPaint();
}

void CardItem::pointerRelease(const ui::PointerEvent &event)
{
    if (event.button != ui::MouseButton::Left || !m_pressed)
        return;
    m_pressed = false;
    requestPaint();
    const ui::RectF rect = localRect();
    const bool inside = event.position.x >= 0 && event.position.y >= 0 && event.position.x < rect.width
            && event.position.y < rect.height;
    if (!inside)
        return;
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
    m_art->setFillMode(ui::ImageView::FillMode::PreserveAspectFit);
    m_art->setCornerRadius(ui::Theme::current().smallRadius, ui::Corners::Continuous);
    m_art->setVisible(false);
    m_art->onStatusChanged = [this](ui::ImageView::Status status) {
        m_artReady = status == ui::ImageView::Status::Ready;
        m_art->setVisible(m_artReady);
        m_cover->setVisible(!m_artReady);
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

    // Album art from a file; anything else keeps the default cover.
    std::string path;
    if (m_state.hasPlayer && base::url::isLocalFile(m_state.artUrl))
        path = base::url::toLocalFile(m_state.artUrl).value_or(std::string());
    else if (m_state.hasPlayer && m_state.artUrl.starts_with("/"))
        path = m_state.artUrl;
    if (path != m_art->source()) {
        m_artReady = false;
        m_art->setVisible(false);
        m_cover->setVisible(true);
        m_art->setSource(path);
    }
}

void MediaTile::layout()
{
    const ui::Theme &theme = ui::Theme::current();
    updateArtwork();
    const double padding = theme.largeSpacing;
    if (m_controlStyle) {
        const double h = geometry().height;
        const double w = geometry().width;
        const double cover = std::min(53.0, h * .29);
        const ui::RectF art {17, 17, cover, cover};
        m_cover->setOpacity(0);
        m_cover->setGeometry(art);
        m_art->setGeometry(art);
        m_art->setSourceSize({cover, cover});
        m_title->setWeight(650);
        m_title->setFontSize(16);
        m_title->setColor(kWhite);
        m_title->setGeometry({17, h - (m_artist->text().empty() ? 88 : 100), w - 34, 23});
        m_artist->setVisible(!m_artist->text().empty());
        m_artist->setColor(kWhite.withAlpha(.74));
        m_artist->setGeometry({17, h - 77, w - 34, 17});
        for (size_t i = 0; i < 3; ++i)
            std::array {m_previous, m_play, m_next}[i]->setGeometry({w * (i + 1) / 4 - 17, h - 53, 34, 34});
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
    if (m_artReady)
        return;
    // The cards' plate colour under the default cover, inside its mask.
    const ui::Color surface = panel::tileInkColor();
    const ui::RectF art = m_cover->geometry();
    painter.fillRoundedRect(art, m_controlStyle ? 11 : ui::Theme::current().smallRadius,
                            m_controlStyle ? kWhite.withAlpha(.12) : panel::on(surface, panel::isDark(surface) ? 0.15 : 0.1),
                            ui::Corners::Continuous);
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
