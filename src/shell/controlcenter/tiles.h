// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_CONTROLCENTER_TILES_H_
#define CUTEFISH_SHELL_CONTROLCENTER_TILES_H_

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "shell/controlcenter/control_center_services.h"
#include "shell/menubar/media_spectrum.h"
#include "ui/icon.h"
#include "ui/view.h"

namespace cutefish::base::file { class TemporaryFile; }

namespace cutefish::ui {
class Animation;
class ImageView;
class Label;
} // namespace cutefish::ui

namespace cutefish::shell {

// An SVG file drawn at its rect's device size on whole device pixels, as
// FishUI's IconItem draws a file; tinted when given a colour.
class SvgIcon : public ui::View
{
public:
    void setFile(std::string file);
    void setTint(std::optional<ui::Color> tint);

protected:
    void paint(ui::Painter &painter) override;

private:
    std::string m_file;
    std::optional<ui::Color> m_tint;
};

// The chevron of a tile with a detail page, in the secondary text colour:
// shown while the tile is hovered, fading in and out over 150 ms.
class DetailChevron : public ui::Icon
{
public:
    static constexpr double kSize = 12;

    DetailChevron();
    ~DetailChevron() override;

    void setShown(bool shown);

private:
    std::unique_ptr<ui::Animation> m_fade;
    double m_from = 0;
    double m_to = 0;
};

// A toggle tile: a round plate with the glyph, a
// title and a status line. In Control Center the whole card toggles on left
// click; right click opens its detail page where available.
class CardItem : public ui::View
{
public:
    static constexpr double kPlateSize = 28;
    static constexpr double kIconSize = 16;
    static constexpr double kHeight = kPlateSize + 12 * 2 + 4;

    CardItem();
    ~CardItem() override;

    // `name`'s light/ artwork, dark/ when dark or checked.
    void setArtwork(std::string name);
    // A FishUI.Icon glyph in place of artwork.
    void setGlyph(std::string name);
    void setThemeGlyph(std::string name, bool builtin = false);
    void setControlStyle(bool small, bool large);
    void setChecked(bool checked);
    bool isChecked() const { return m_checked; }
    void setTitle(std::string title);
    void setStatus(std::string status);
    void setHasDetail(bool detail);
    bool hasDetail() const { return m_hasDetail; }
    // The round plate's square, in the card's coordinates.
    ui::RectF plateRect() const;
    DetailChevron *chevron() const { return m_chevron; }

    std::function<void()> onClicked;
    std::function<void()> onDetail;

    ui::SizeF sizeHint() const override { return {0, kHeight}; }
    bool acceptsPointer() const override { return true; }

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;
    void hoverChanged() override;
    void pointerPress(const ui::PointerEvent &event) override;
    void pointerRelease(const ui::PointerEvent &event) override;

private:
    void updateIcon();
    bool onPlate(const ui::PointF &point) const;

    std::string m_artwork;
    bool m_checked = false;
    bool m_pressed = false;
    bool m_hasDetail = false;
    bool m_platePressed = false;
    bool m_controlStyle = false;
    bool m_small = false;
    bool m_large = false;
    SvgIcon *m_svg;
    ui::Icon *m_glyph;
    ui::ThemeIcon *m_themeGlyph;
    DetailChevron *m_chevron;
    ui::Label *m_title;
    ui::Label *m_status;
};

// An icon at three quarters of the button, a
// round ink plate under it while hovered and enabled.
class IconButton : public ui::View
{
public:
    enum class Transport { Previous = -1, PlayPause = 0, Next = 1 };
    IconButton();
    ~IconButton() override;

    // What the button sits on, for the ink of its plate.
    void setSurfaceColor(ui::Color color);
    void setGlyphSize(double size);
    void setTransport(Transport transport, bool white, bool playing = false);

    std::function<void()> onClicked;

    bool acceptsPointer() const override { return true; }

protected:
    void paint(ui::Painter &painter) override;
    void hoverChanged() override;
    void enabledChanged() override;
    void pointerPress(const ui::PointerEvent &event) override;
    void pointerMove(const ui::PointerEvent &event) override;
    void pointerRelease(const ui::PointerEvent &event) override;

private:
    void animate();
    void animateSkip();
    std::unique_ptr<ui::Animation> m_motion;
    std::unique_ptr<ui::Animation> m_morphMotion;
    std::unique_ptr<ui::Animation> m_skip;
    ui::Color m_surface;
    ui::Color m_glyphInk;
    Transport m_transport = Transport::PlayPause;
    bool m_playing = false;
    double m_morph = 0;
    double m_skipProgress = 0;
    double m_size = 0;
    double m_press = 0;
    double m_ink = 0;
    bool m_pressed = false;
    bool m_inside = false;
};

class MediaCover final : public ui::View
{
public:
    enum class Style { Legacy, Control, Activity };

    explicit MediaCover(Style style = Style::Legacy);
    ~MediaCover() override;
    void setStyle(Style style);
    void setUrl(const std::string &url);

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;

private:
    void animate();

    Style m_style;
    ui::ImageView *m_image;
    bool m_ready = false;
    bool m_loading = false;
    double m_phase = 0;
    std::string m_url;
    std::shared_ptr<base::file::TemporaryFile> m_file;
    std::shared_ptr<int> m_request;
    std::unique_ptr<ui::Animation> m_animation;
};

// The media card, 60 tall. Always shown; without a
// playing player it reads "Not Playing" with the default cover and three
// dimmed, disabled buttons.
class MediaTile : public ui::View
{
public:
    static constexpr double kHeight = 60;

    MediaTile();
    ~MediaTile() override;

    void setControlStyle(bool enabled) { m_controlStyle = enabled; }
    void setActivityStyle();
    void setState(const MediaState &state);
    IconButton *previousButton() const { return m_previous; }
    IconButton *playButton() const { return m_play; }
    IconButton *nextButton() const { return m_next; }

    std::function<void()> onPrevious;
    std::function<void()> onPlay;
    std::function<void()> onPause;
    std::function<void()> onNext;
    // The expanded Live Activity samples the common MPRIS clock on its frames.
    std::function<std::optional<double>()> position;
    std::function<void()> pollPosition;
    std::function<void(double)> seek;
    void setTimeline(double duration, bool seekable, bool trackChanged);
    void setBands(const MediaBands &bands);

    ui::SizeF sizeHint() const override { return {0, kHeight}; }

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;

private:
    class Progress;
    void updateArtwork();
    void updateTimeline();
    void animateTimeline();

    MediaState m_state;
    MediaCover *m_cover;
    ui::Label *m_title;
    ui::Label *m_artist;
    IconButton *m_previous;
    IconButton *m_play;
    IconButton *m_next;
    Progress *m_progress = nullptr;
    ui::Label *m_elapsed = nullptr;
    ui::Label *m_remaining = nullptr;
    std::unique_ptr<ui::Animation> m_timeline;
    std::unique_ptr<ui::Animation> m_metadataMotion;
    double m_duration = 0;
    MediaBands m_bands {};
    std::chrono::steady_clock::time_point m_nextPoll;
    bool m_controlStyle = false;
};

void paintLiveMediaWave(ui::Painter &painter, const ui::RectF &rect, const MediaBands &bands);
void paintLiveMediaIcon(ui::Painter &painter, const ui::RectF &rect, double opacity = 1);

// The applications behind the bar's privacy dot, with one compact row per app.
class DeviceUsersTile : public ui::View
{
public:
    static constexpr double kIconSize = 20;

    DeviceUsersTile();

    void setUsers(const std::vector<DeviceUser> &users);
    const std::vector<DeviceUser> &users() const { return m_users; }
    void retranslate();

    double heightForWidth(double width) const override;
    ui::SizeF sizeHint() const override;

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;

private:
    struct Row {
        ui::Label *title;
    };

    std::vector<DeviceUser> m_users;
    std::vector<Row> m_rows;
};

// DeviceUsers' sentence naming an application and what it uses.
std::string deviceUsageText(const DeviceUser &user);

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CONTROLCENTER_TILES_H_
