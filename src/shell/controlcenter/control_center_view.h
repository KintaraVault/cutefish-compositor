// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_CONTROLCENTER_CONTROL_CENTER_VIEW_H_
#define CUTEFISH_SHELL_CONTROLCENTER_CONTROL_CENTER_VIEW_H_

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>

#include "render/glass.h"
#include "shell/controlcenter/control_layout.h"
#include "ui/path.h"
#include "ui/view.h"

namespace cutefish::ui {
class Animation;
} // namespace cutefish::ui

namespace cutefish::shell {

class CardItem;
class ControlCenterServices;
class DeviceUsersTile;
class MediaTile;
class StatusPanelView;
class TileSlider;

class ControlCenterView : public ui::View
{
public:
    static constexpr double kWidth = 342;
    struct State {
        bool available = false;
        bool active = false;
        std::string status;
    };

    ControlCenterView(ControlCenterServices &services, controls::Layout &model, int page,
                      int maximumWidth, int maximumHeight, int editorHeight);
    ~ControlCenterView() override;

    // Reads the services again.
    void refresh();
    // Takes its text again after the session language changed.
    void retranslate();
    // The window height at `scale`: the layout's, on whole device pixels.
    double windowHeight(double scale) const;
    double windowWidth() const;
    ui::Path glassShape() const;
    render::GlassUnderlay glassUnderlay() const;
    std::vector<render::GlassLens> glassLenses() const;
    const std::vector<ui::RectF> &glassTiles() const { return m_glassTiles; }
    bool editing() const;
    double editorProgress() const { return editing() ? m_catalogReveal : 0.; }
    double liveWidth() const { return canvasWidth(); }
    bool escapeEditor();
    void edit(bool enabled);
    void selectPage(int page);
    void addPage();
    int pageId() const { return m_page; }
    void reveal();

    ui::View *control(std::string_view type) const;
    CardItem *wirelessCard() const;
    CardItem *bluetoothCard() const;
    CardItem *darkModeCard() const;
    CardItem *doNotDisturbCard() const;
    CardItem *cameraCard() const;
    MediaTile *media() const;
    TileSlider *brightnessSlider() const;
    TileSlider *soundSlider() const;

    // The Wi-Fi or Bluetooth card's chevron: its detail page.
    std::function<void()> onWirelessDetail;
    std::function<void()> onBluetoothDetail;
    // The Sound slider's chevron: the sound panel.
    std::function<void()> onSoundDetail;
    std::function<State(std::string_view)> state;
    std::function<void(std::string_view, bool detail)> activate;
    std::function<bool(ui::PointF)> isMenuBarDrop;
    std::function<void()> onModelChanged;
    std::function<void()> onGlassChanged;
    // The layout's height may have changed.
    std::function<void()> onHeightChanged;

protected:
    void layout() override;
    bool scroll(const ui::ScrollEvent &event) override;

private:
    class Chrome;
    friend class Chrome;
    void rebuild(bool animate = false);
    void changed(bool animate = true);
    void animateTiles();
    const controls::Page &page() const;
    double cell() const;
    double gap() const;
    double canvasWidth() const;
    double catalogWidth() const;
    double gridTop() const;
    double liveGridTop() const;
    double liveHeight() const;
    double gridHeight() const;
    double gridViewport() const;
    double gridViewport(int rows, double top, int maximumHeight) const;
    double canvasX() const;
    int rows() const;
    ui::RectF gridClip() const;
    ui::RectF tileRect(controls::Size size, base::Point cell) const;
    ui::RectF catalogRect() const;
    ui::RectF editorHeaderRect() const;
    ui::RectF editRect() const;
    void scrollGrid(double delta);
    void removeTile(int id);
    ui::RectF removingRect() const;

    ControlCenterServices &m_services;
    controls::Layout &m_model;
    int m_page;
    int m_maximumWidth;
    int m_maximumHeight;
    int m_editorHeight;
    ui::View *m_grid;
    DeviceUsersTile *m_users;
    Chrome *m_chrome;
    std::unordered_map<int, ui::View *> m_tiles;
    std::unordered_map<int, ui::RectF> m_previous;
    std::vector<ui::RectF> m_glassTiles;
    std::unique_ptr<ui::Animation> m_transition;
    double m_progress = 1;
    double m_scroll = 0;
    double m_catalogReveal = 1;
    std::unique_ptr<ui::Animation> m_catalogTween;
    bool m_editTarget = false;
    struct RemovedTile { controls::Tile tile; ui::RectF rect; };
    std::optional<RemovedTile> m_removed;
    double m_removal = 1;
    std::unique_ptr<ui::Animation> m_removeTween;
};

// The control centre's pages: the tiles, and a tile's detail page (the
// Wi-Fi, Bluetooth or Sound panel) over them. Between the two the pages slide kSlide and
// cross-fade while the window's height glides from one to the other, on the
// panels' 380 ms curve; a change of direction on the way turns from where
// the pages are.
class ControlCenterPager : public ui::View
{
public:
    static constexpr double kSlide = 90;

    explicit ControlCenterPager(std::unique_ptr<ControlCenterView> main);
    ~ControlCenterPager() override;

    ControlCenterView *mainPage() const { return m_main; }
    // While shown, and while it slides away.
    StatusPanelView *detailPage() const { return m_detail; }
    bool isShowingDetail() const { return m_detail && m_target == 1; }
    bool isSliding() const;
    // 0 at the tiles, 1 at the detail page.
    double progress() const { return m_progress; }

    // `page` is shown at once at its height; its maximum height set.
    void showDetail(std::unique_ptr<StatusPanelView> page);
    void showMain();

    // The window's width: the tiles' or a status panel's, and between the
    // two while the pages slide.
    double windowWidth() const;
    // The window's height at `scale`: the tiles' or the detail page's as it
    // is drawn, and between the two while the pages slide.
    double windowHeight(double scale) const;

    // windowWidth() or windowHeight() may have changed.
    std::function<void()> onSizeChanged;

protected:
    void layout() override;

private:
    void slideTo(double target);
    void setProgress(double progress);
    void settle();
    void sizeChanged();

    ControlCenterView *m_main;
    StatusPanelView *m_detail = nullptr;
    std::unique_ptr<ui::Animation> m_tween;
    double m_progress = 0;
    double m_from = 0;
    double m_target = 0;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CONTROLCENTER_CONTROL_CENTER_VIEW_H_
