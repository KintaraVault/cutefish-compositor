// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The control centre, the status panels and the power dialog as the
// compositor draws them: their popups, places, glass, input and behaviour,
// headless on a fake renderer with fake services.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

#include <linux/input-event-codes.h>

#include <gtest/gtest.h>

#include "base/dbus.h"
#include "compositor/compositor.h"
#include "core/main_loop.h"
#include "compositor/surface.h"
#include "egl_test_util.h"
#include "fake_renderer.h"
#include "input/input_manager.h"
#include "input/seat.h"
#include "loop_test_util.h"
#include "scene/node.h"
#include "scene/scene.h"
#include "scene/surface_node.h"
#include "shell/controlcenter/control_center.h"
#include "shell/controlcenter/control_layout.h"
#include "shell/widgets/desktop_widgets.h"
#include "shell/controlcenter/control_center_view.h"
#include "shell/controlcenter/level_slider.h"
#include "shell/controlcenter/power_dialog.h"
#include "shell/controlcenter/status_panel.h"
#include "shell/controlcenter/tiles.h"
#include "shell/osd/level_track.h"
#include "shell/shell.h"
#include "shell/shell_data.h"
#include "shell/shell_window.h"
#include "toolkit/toolkit_host.h"
#include "shell/menubar/menu_bar.h"
#include "shell/menubar/menu_bar_window.h"
#include "shell/menubar/status_icon.h"
#include "shell_test_util.h"
#include "temporary_directory.h"
#include "ui/application.h"
#include "ui/bitmap.h"
#include "ui/dialog_window.h"
#include "ui/embedded_window.h"
#include "ui/icon.h"
#include "ui/image.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/search_field.h"
#include "ui/switch.h"
#include "ui/theme.h"
#include "ui/image_view.h"
#include "ui/window.h"
#include "wm/window.h"
#include "wm/window_manager.h"
#include "wm/stacking.h"

using namespace cutefish;
using Panel = shell::MenuBarPanels::Panel;

namespace {

input::Timestamp at(int ms)
{
    return input::Timestamp(std::chrono::milliseconds(ms));
}

const base::Rect kOutput(0, 0, 1440, 960);

class PreviewWallpaper final : public ui::View
{
public:
    ui::Image image;

    void paint(ui::Painter &painter) override
    {
        if (!image.isNull())
            painter.drawImage(image, localRect());
        else
            painter.fillRect(localRect(), ui::Color::rgb(0x172334));
    }
};
constexpr int kMenuBar = 32;
constexpr int kDock = 92;

class FakeServices final : public shell::ControlCenterServices
{
public:
    bool wirelessHardwareEnabled() const override { return hardware; }
    bool wirelessEnabled() const override { return wireless; }
    bool hotspotActive() const override { return hotspot; }
    std::string hotspotName() const override { return "Laptop"; }
    std::string wirelessName() const override { return wireless ? "Office 5G" : ""; }
    std::string wirelessIcon() const override { return "network-wireless-connected-100"; }
    std::vector<shell::WirelessNetwork> wirelessNetworks() const override { return wireless ? networks : decltype(networks)(); }
    void enableWireless(bool enable) override { wireless = enable; changed(); }
    void requestScan() override { ++scans; }
    void stopHotspot() override { hotspot = false; changed(); }
    bool activateNetwork(const shell::WirelessNetwork &network) override
    {
        activated.push_back(network.name);
        return network.security == shell::WirelessNetwork::Security::Password && network.connectionPath.empty();
    }
    bool bluetoothAvailable() const override { return true; }
    bool bluetoothEnabled() const override { return bluetooth; }
    void setBluetoothEnabled(bool enabled) override
    {
        // BlueZ reports other changes before the adapter's power.
        if (!slowBluetooth)
            bluetooth = enabled;
        changed();
    }
    std::vector<shell::BluetoothDevice> bluetoothDevices() const override { return devices; }
    void setBluetoothDeviceConnected(const std::string &path, bool connected) override
    {
        connections.emplace_back(path, connected);
    }
    void setDarkMode(bool dark) override { darkRequested = dark ? 1 : 0; }
    bool doNotDisturb() const override { return dnd; }
    void setDoNotDisturb(bool enabled) override { dnd = enabled; changed(); }
    bool cameraEnabled() const override { return camera; }
    void setCameraEnabled(bool enabled) override { camera = enabled; changed(); }
    bool microphoneEnabled() const override { return microphone; }
    void setMicrophoneEnabled(bool enabled) override { microphone = enabled; changed(); }
    std::vector<shell::DeviceUser> deviceUsers() const override { return users; }
    shell::MediaState media() const override { return mediaState; }
    void mediaPrevious() override { media_.push_back("previous"); }
    void mediaPlay() override { media_.push_back("play"); }
    void mediaPause() override { media_.push_back("pause"); }
    void mediaNext() override { media_.push_back("next"); }
    bool brightnessEnabled() const override { return true; }
    int brightness() const override { return brightnessValue; }
    void setBrightness(int value) override { brightnessValue = value; ++brightnessWrites; }
    bool hasSink() const override { return sink; }
    double volume() const override { return volumeValue; }
    bool muted() const override { return mutedValue; }
    void setVolume(double volume) override { volumeValue = volume; }
    void setMuted(bool muted) override { mutedValue = muted; }
    std::vector<shell::AudioOutput> outputs() const override { return outputList; }
    void setDefaultOutput(uint32_t id) override { defaultRequested = int(id); }
    int chargePercent() const override { return 80; }
    bool onBattery() const override { return false; }
    std::string batteryStatus() const override { return "Charging paused at 80%"; }
    std::vector<std::string> powerProfiles() const override { return profiles; }
    std::string powerProfile() const override { return profile; }
    void setPowerProfile(const std::string &value) override { profile = value; changed(); }
    void openSettings(const std::vector<std::string> &arguments) override { settings.push_back(arguments); }

    bool hardware = true;
    bool wireless = true;
    bool hotspot = false;
    bool bluetooth = true;
    bool slowBluetooth = false;
    bool dnd = false;
    bool camera = false;
    bool microphone = false;
    int darkRequested = -1;
    int scans = 0;
    int brightnessValue = 63;
    int brightnessWrites = 0;
    bool sink = true;
    double volumeValue = 0.36;
    bool mutedValue = false;
    int defaultRequested = -1;
    std::string profile = "balanced";
    std::vector<std::string> profiles {"power-saver", "balanced"};
    std::vector<shell::WirelessNetwork> networks;
    std::vector<std::string> activated;
    std::vector<shell::BluetoothDevice> devices;
    std::vector<std::pair<std::string, bool>> connections;
    std::vector<shell::DeviceUser> users;
    shell::MediaState mediaState;
    std::vector<std::string> media_;
    std::vector<shell::AudioOutput> outputList {{1, "Speakers", true}, {2, "HDMI", false}};
    std::vector<std::vector<std::string>> settings;
};

shell::WirelessNetwork network(const std::string &name, shell::WirelessNetwork::State state,
                               shell::WirelessNetwork::Security security, const std::string &connection = {})
{
    shell::WirelessNetwork result;
    result.key = name;
    result.name = name;
    result.icon = "network-wireless-connected-66";
    result.state = state;
    result.security = security;
    result.connectionPath = connection;
    result.devicePath = "/dev/1";
    result.specificPath = "/ap/" + name;
    return result;
}

class ControlCenterTest : public testing::Test
{
protected:
    scene::Scene scene;
    scene::Node *surfaceContainer = scene.root()->addChild(std::make_unique<scene::Node>("surfaces"));
    compositor::Compositor compositor {surfaceContainer};
    testsupport::FakeRenderer renderer;
    input::Seat seat {&compositor};
    input::InputManager input {&seat};
    wm::WindowManager manager {&compositor, &seat};
    test::TemporaryDirectory directory;
    std::unique_ptr<shell::Shell> shell;
    std::unique_ptr<shell::ShellWindow> bar;
    FakeServices *services = nullptr;
    shell::ControlCenter *center = nullptr;
    int dock = 0;
    int time = 1000;

    void SetUp() override
    {
        shell::setShellDataDirectoryForTesting(CUTEFISH_SOURCE_DIR "/data/shell");
        scene.setSize(kOutput.size());
        manager.setOutputBounds(kOutput);
        manager.setExclusiveZone(&dock, wm::ScreenEdge::Bottom, kDock);
        input.setBounds(kOutput.size());
        input.setEventSink(&manager);
        shell::ShellEnvironment environment {&compositor, &manager, &seat};
        environment.servicesSettings = directory.filePath("services.conf");
        shell = std::make_unique<shell::Shell>(environment);
        shell->setApplications(std::make_unique<test::FakeApplications>());
        shell->setTaskWindows(std::make_unique<test::FakeTaskWindows>());

        // The menu bar's window, which the panels hang from.
        bar = std::make_unique<shell::ShellWindow>(shell.get(), wm::WindowRole::Panel);
        bar->setContentView(std::make_unique<ui::View>());
        bar->setFocusPolicy(wm::FocusPolicy::NoFocus);
        bar->setGeometry(base::Rect(0, 0, kOutput.width(), kMenuBar));
        bar->setExclusiveZone(wm::ScreenEdge::Top, kMenuBar);
        bar->show();

        auto fake = std::make_unique<FakeServices>();
        services = fake.get();
        center = shell->addComponent(std::make_unique<shell::ControlCenter>(shell.get(), std::move(fake), nullptr,
                                                                           directory.filePath("controls.conf")));
        frame();
    }

    void TearDown() override
    {
        // The bar outlives the panels hanging from it, the shell the bar.
        if (center)
            center->closePanels();
        test::runFor(std::chrono::milliseconds(5));
        center = nullptr;
        bar.reset();
        shell.reset();
        seat.shutdown();
    }

    void frame() { compositor.updateTextures(renderer); }

    // Animations advance on frames: one every 16 ms for `duration`.
    void frames(std::chrono::milliseconds duration)
    {
        for (std::chrono::milliseconds run {0}; run < duration; run += std::chrono::milliseconds(16)) {
            test::runFor(std::chrono::milliseconds(16));
            frame();
        }
    }

    void move(double x, double y) { input.onPointerMotionAbsolute(at(time += 5), {x, y}); }

    void press(double x, double y)
    {
        move(x, y);
        input.onPointerButton(at(time += 5), BTN_LEFT, true);
    }

    void release() { input.onPointerButton(at(time += 5), BTN_LEFT, false); }

    void click(double x, double y)
    {
        press(x, y);
        release();
        frame();
    }

    void rightClick(double x, double y)
    {
        move(x, y);
        input.onPointerButton(at(time += 5), BTN_RIGHT, true);
        input.onPointerButton(at(time += 5), BTN_RIGHT, false);
        frame();
    }

    void key(uint32_t code)
    {
        input.onKey(at(time += 5), code, true);
        input.onKey(at(time += 5), code, false);
    }

    // The bar items the reference captures used: x from the right edge.
    base::Rect item(int fromRight, int width = 40) const
    {
        return base::Rect(kOutput.width() - fromRight, 0, width, kMenuBar);
    }

    // The centre of a view in global logical coordinates.
    base::PointF centre(Panel panel, const ui::View *view) const
    {
        const ui::PointF origin = view->mapToWindow({0, 0});
        const base::Rect popup = center->popupWindow(panel)->drawnBounds();
        return {popup.x() + origin.x + view->geometry().width / 2, popup.y() + origin.y + view->geometry().height / 2};
    }

    void clickView(Panel panel, const ui::View *view)
    {
        const base::PointF point = centre(panel, view);
        click(point.x(), point.y());
    }

    void clickPlate(const shell::CardItem *card)
    {
        const ui::RectF plate = card->plateRect();
        const ui::PointF origin = card->mapToWindow({plate.x + plate.width / 2, plate.y + plate.height / 2});
        const base::Rect popup = center->popupWindow(Panel::ControlCenter)->drawnBounds();
        click(popup.x() + origin.x, popup.y() + origin.y);
    }

    // The footer's gaps, in the panel's coordinates: the last row's badge to
    // the line, the line to the link's capitals, the link's baseline to the
    // panel's bottom edge.
    void expectFooterGaps(Panel panel, const shell::PanelRow *lastRow)
    {
        expectFooterGaps(center->statusPanel(panel), lastRow);
    }

    void expectFooterGaps(const shell::StatusPanelView *view, const shell::PanelRow *lastRow)
    {
        const double rowBottom = lastRow->mapToWindow({0, lastRow->geometry().height}).y;
        const double badgeBottom = rowBottom - (lastRow->geometry().height - 28) / 2;
        const double line = view->mapToWindow({0, view->footerLineY()}).y;
        const auto *label = static_cast<const ui::Label *>(view->settingsLink()->children().front().get());
        const double baseline = label->mapToWindow({0, 0}).y + label->baselineOffset();
        const double capTop = baseline - label->capHeight();
        // Within the line's snap to whole pixels and the label's.
        EXPECT_NEAR(14, line - badgeBottom, 0.5);
        EXPECT_NEAR(14, capTop - (line + 1), 1);
        EXPECT_NEAR(14, view->contentHeight() - baseline, 1);
    }
};

} // namespace

// Uses the real renderer in a surfaceless EGL session. Optional artifacts are
// for comparing materials against the same wallpaper without touching the desktop.
TEST_F(ControlCenterTest, OffscreenReferenceFrames)
{
    const char *prefix = std::getenv("CUTEFISH_TAHOE_PREVIEW");
    if (!prefix)
        GTEST_SKIP() << "Set CUTEFISH_TAHOE_PREVIEW to a path prefix for PNGs";
    const base::Rect output(0, 0, 1920, 1080);
    scene.setSize(output.size());
    manager.setOutputBounds(output);
    manager.setExclusiveZone(&dock, wm::ScreenEdge::Bottom, 0);
    input.setBounds(output.size());
    bar->setGeometry({0, 0, output.width(), kMenuBar});
    auto wallpaper = std::make_unique<shell::ShellWindow>(shell.get(), wm::WindowRole::Desktop);
    auto view = std::make_unique<PreviewWallpaper>();
    if (const char *path = std::getenv("CUTEFISH_TAHOE_WALLPAPER")) {
        view->image = ui::Image::fromFile(path);
        ASSERT_FALSE(view->image.isNull());
    }
    wallpaper->setContentView(std::move(view));
    wallpaper->setGeometry(output);
    wallpaper->setFocusPolicy(wm::FocusPolicy::NoFocus);
    wallpaper->show();
    auto previewDock = std::make_unique<shell::ShellWindow>(shell.get(), wm::WindowRole::Dock, false);
    previewDock->setContentView(std::make_unique<ui::View>());
    previewDock->window()->setBackgroundColor(ui::ColorRecipe(ui::Color::rgb(0xd2dbf0)));
    previewDock->setGeometry({450, 1005, 1020, 63});
    previewDock->setExclusiveZone(wm::ScreenEdge::Bottom, 90);
    previewDock->setFocusPolicy(wm::FocusPolicy::NoFocus);
    auto *widgets = shell->addComponent(std::make_unique<shell::DesktopWidgets>(
            shell.get(), *center, directory.filePath("desktop-widgets.conf")));

    auto gpu = render::EglRendererTestAccess::create();
    ASSERT_TRUE(gpu);
    const EGLint attributes[] = {EGL_WIDTH, output.width(), EGL_HEIGHT, output.height(), EGL_NONE};
    const auto surface = eglCreatePbufferSurface(gpu->eglDisplay(), gpu->eglConfig(), attributes);
    ASSERT_NE(surface, EGL_NO_SURFACE);
    render::EglRenderTarget target(gpu->eglDisplay(), surface, nullptr, output.size());
    render::TargetRetirement retire {gpu.get(), &target};
    const auto cleanup = [&] {
        if (gpu->beginFrame(&target)) {
            wallpaper.reset();
            previewDock.reset();
            center->closePanels();
            center = nullptr;
            bar.reset();
            shell.reset();
            toolkit::PanelTexture(nullptr).releaseRenderResources();
            gpu->endFrame();
        }
    };
    uint64_t frameCount = 0;
    const auto capture = [&](const char *name) {
        gpu->setOutputContext({"preview", output, output.size(), 1, 0, ++frameCount});
        gpu->frameTime = 1000.0 * double(frameCount);
        ASSERT_TRUE(gpu->beginFrame(&target));
        compositor.updateTextures(*gpu);
        const auto image = gpu->captureFrame([&] {
            gpu->clear({0, 0, 0, 1});
            const auto draw = [&](shell::ShellWindow *window) {
                const auto *node = window->surface()->node();
                const auto bounds = window->drawnBounds();
                if (!node || !node->texture() || !window->isShown())
                    return;
                if (node->hasBackgroundBlur())
                    gpu->drawBackgroundBlur(node->backgroundBlurRect().translated(bounds.topLeft()),
                            node->backgroundBlurRadius(), bounds, node->glass(),
                            node->backgroundBlurShape().translated(bounds.x(), bounds.y()));
                gpu->drawTextureMapped(node->texture(), bounds, 1, {});
            };
            draw(wallpaper.get());
            for (auto *mapped : manager.windows())
                for (auto *window : shell->windows())
                    if (window != wallpaper.get() && window->wmWindow() == mapped) {
                        draw(window);
                        break;
                    }
        });
        gpu->cancelFrame();
        EXPECT_TRUE(image.savePng(std::string(prefix) + name + ".png"));
    };
    services->users = {{"Screen Capture", {}, false, true, true}};
    services->brightnessValue = 48;
    services->volumeValue = .64;
    center->openPanel(Panel::ControlCenter, bar.get(), {1674, 0, 38, kMenuBar});
    capture("-warmup");
    test::runFor(std::chrono::milliseconds(260));
    capture("-live");
    previewDock->show();
    const auto bounds = center->popupWindow(Panel::ControlCenter)->drawnBounds();
    press(bounds.center().x(), bounds.bottom() - 22);
    release();
    ASSERT_TRUE(center->controlCenterView()->editing());
    capture("-warmup");
    test::runFor(std::chrono::milliseconds(390));
    capture("-edit");
    key(KEY_ESC);
    capture("-editor-exit-start");
    test::runFor(std::chrono::milliseconds(125));
    capture("-editor-exit-mid");
    test::runFor(std::chrono::milliseconds(180));
    capture("-editor-exit-end");
    center->closePanels();
    test::runFor(std::chrono::milliseconds(25));
    capture("-widgets");
    widgets->edit();
    capture("-widget-gallery-start");
    test::runFor(std::chrono::milliseconds(410));
    capture("-widget-gallery");
    cleanup();
}

TEST_F(ControlCenterTest, TheControlCentreHangsBelowTheBarWithThePanelGlass)
{
    int changes = 0;
    base::Subscription subscription = center->panelsChanged.connect([&] { ++changes; });
    center->openPanel(Panel::ControlCenter, bar.get(), item(178));
    frame();
    ASSERT_TRUE(center->isOpen(Panel::ControlCenter));
    EXPECT_EQ(1, changes);
    shell::ShellWindow *window = center->popupWindow(Panel::ControlCenter);
    ASSERT_NE(nullptr, window);
    ASSERT_NE(nullptr, window->wmWindow());
    EXPECT_TRUE(window->wmWindow()->isPopup());
    // Right of the screen it does not fit: the visible tiles stop 18 px from the edge.
    const base::Rect bounds = window->drawnBounds();
    constexpr int width = int(shell::ControlCenterView::kWidth);
    EXPECT_EQ(342, width);
    EXPECT_EQ(base::Rect(1440 - width - 12, 38, width, bounds.height()), bounds);
    EXPECT_GT(bounds.height(), 400);
    EXPECT_TRUE(window->surface()->glass().enabled);
    EXPECT_GE(window->surface()->glass().lenses.size(), 18u);
    EXPECT_GT(window->surface()->glass().underlay.opacity, 0);
    EXPECT_TRUE(window->surface()->glass().underlay.bounds.contains({width / 2., 30}));
    EXPECT_EQ(base::Rect(0, 0, width, bounds.height()), window->surface()->backgroundBlurRect());
    EXPECT_EQ(16, window->surface()->backgroundBlurRadius());
    const auto mask = window->surface()->backgroundBlurShape();
    EXPECT_TRUE(mask.contains({40, 30}));
    EXPECT_FALSE(mask.contains({width / 2., 30})); // no enclosing glass in the grid gutter

    // Escape closes it; the bar lends it the keyboard.
    key(KEY_ESC);
    test::runFor(std::chrono::milliseconds(20));
    EXPECT_FALSE(center->isOpen(Panel::ControlCenter));
    EXPECT_EQ(2, changes);

    // Left of the screen it starts at the item's plate.
    center->openPanel(Panel::ControlCenter, bar.get(), base::Rect(100, 0, 40, kMenuBar));
    frame();
    EXPECT_EQ(103, center->popupWindow(Panel::ControlCenter)->drawnBounds().x());
    // A press outside closes it and goes nowhere.
    click(700, 600);
    test::runFor(std::chrono::milliseconds(20));
    EXPECT_FALSE(center->isOpen(Panel::ControlCenter));
}

TEST_F(ControlCenterTest, CardsToggleTheirServices)
{
    center->openPanel(Panel::ControlCenter, bar.get(), item(178));
    frame();
    shell::ControlCenterView *view = center->controlCenterView();
    ASSERT_NE(nullptr, view);
    EXPECT_TRUE(view->wirelessCard()->isChecked());
    EXPECT_TRUE(view->bluetoothCard()->isChecked());
    EXPECT_FALSE(view->doNotDisturbCard()->isChecked());

    // The whole card toggles, not just its icon.
    clickView(Panel::ControlCenter, view->bluetoothCard());
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_FALSE(services->bluetooth);
    EXPECT_FALSE(view->bluetoothCard()->isChecked());
    EXPECT_FALSE(center->controlCenterPager()->isShowingDetail());
    clickView(Panel::ControlCenter, view->doNotDisturbCard());
    clickView(Panel::ControlCenter, view->cameraCard());
    clickView(Panel::ControlCenter, view->darkModeCard());
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_TRUE(services->dnd);
    EXPECT_TRUE(services->camera);
    EXPECT_EQ(1, services->darkRequested);
    EXPECT_TRUE(view->doNotDisturbCard()->isChecked());

    // Unavailable hardware disables the tile without changing the user's layout.
    const int before = center->popupWindow(Panel::ControlCenter)->drawnBounds().height();
    services->hardware = false;
    services->bluetooth = false;
    services->changed();
    test::runFor(std::chrono::milliseconds(5));
    frame();
    EXPECT_TRUE(view->wirelessCard()->isVisible());
    EXPECT_FALSE(view->wirelessCard()->isEnabled());
    EXPECT_EQ(before, center->popupWindow(Panel::ControlCenter)->drawnBounds().height());
}

TEST_F(ControlCenterTest, EditorsFollowThemeAndThePanelClosesWithAFade)
{
    struct RestoreTheme {
        ui::Theme previous = ui::Theme::current();
        ~RestoreTheme() { ui::Theme::setCurrent(previous); }
    } restore;
    ui::Theme::setCurrent(ui::Theme::light());
    center->openPanel(Panel::ControlCenter, bar.get(), item(178));
    frames(std::chrono::milliseconds(300));
    auto *panel = center->popupWindow(Panel::ControlCenter);
    auto *view = center->controlCenterView();
    view->edit(true);
    frames(std::chrono::milliseconds(390));
    EXPECT_GT(panel->surface()->glass().tintRed, .9f);
    ui::Theme::setCurrent(ui::Theme::dark());
    frames(std::chrono::milliseconds(40));
    EXPECT_LT(panel->surface()->glass().tintRed, .1f);
    view->edit(false);
    frames(std::chrono::milliseconds(300));
    EXPECT_FALSE(view->editing());

    shell::DesktopWidgets widgets(shell.get(), *center, directory.filePath("theme-widgets.conf"));
    widgets.edit();
    frames(std::chrono::milliseconds(390));
    const auto gallery = std::ranges::find_if(shell->windows(), [](const auto *window) {
        return window->wmWindow() && window->wmWindow()->role() == wm::WindowRole::Picker;
    });
    ASSERT_NE(gallery, shell->windows().end());
    EXPECT_LT((*gallery)->surface()->glass().tintRed, .1f);
    ui::Theme::setCurrent(ui::Theme::light());
    frames(std::chrono::milliseconds(40));
    EXPECT_GT((*gallery)->surface()->glass().tintRed, .9f);
    widgets.done();

    center->closePanels();
    EXPECT_FALSE(center->isOpen(Panel::ControlCenter));
    frames(std::chrono::milliseconds(95));
    EXPECT_GT(panel->surface()->glass().opacity, 0);
    EXPECT_LT(panel->surface()->glass().opacity, 1);
    frames(std::chrono::milliseconds(155));
    EXPECT_EQ(nullptr, center->popupWindow(Panel::ControlCenter));
}

TEST_F(ControlCenterTest, MediaCoverFadesInAndClearsWhenThePlayerStops)
{
    auto path = directory.filePath("cover.png");
    // The optional private fixture runs the same case over HTTP.
    if (const auto *url = std::getenv("CUTEFISH_TEST_COVER_URL")) {
        path = url;
    } else {
        ui::Bitmap bitmap(24, 24, ui::Bitmap::Format::Argb32Premultiplied);
        std::fill_n(reinterpret_cast<uint32_t *>(bitmap.bits()), bitmap.stride() * bitmap.height() / 4, 0xffc05830);
        ASSERT_TRUE(bitmap.savePng(path));
    }
    center->openPanel(Panel::ControlCenter, bar.get(), item(178));
    frame();
    auto *media = center->controlCenterView()->media();
    ASSERT_NE(media, nullptr);
    const auto findCover = std::ranges::find_if(media->children(), [](const auto &child) {
        return child->name() == "media-cover-image";
    });
    ASSERT_NE(findCover, media->children().end());
    auto *cover = static_cast<ui::ImageView *>(findCover->get());
    shell::MediaState playing;
    playing.hasPlayer = true;
    playing.artUrl = path;
    media->setState(playing);
    for (int n = 0; n < 100 && !cover->isVisible(); ++n)
        frames(std::chrono::milliseconds(16));
    ASSERT_TRUE(cover->isVisible());
    EXPECT_LT(cover->opacity(), 1);
    frames(std::chrono::milliseconds(325));
    EXPECT_DOUBLE_EQ(cover->opacity(), 1);
    media->setState({});
    EXPECT_FALSE(cover->isVisible());
}

TEST_F(ControlCenterTest, TheWifiBluetoothAndSoundTilesOpenTheirPanelsAsDetailPages)
{
    using State = shell::WirelessNetwork::State;
    using Security = shell::WirelessNetwork::Security;
    services->networks = {network("Office 5G", State::Activated, Security::Password, "/c/1"),
                          network("Cafe", State::Deactivated, Security::None)};
    center->openPanel(Panel::ControlCenter, bar.get(), item(178));
    frame();
    shell::ControlCenterView *view = center->controlCenterView();
    shell::ControlCenterPager *pager = center->controlCenterPager();
    ASSERT_NE(nullptr, pager);
    shell::CardItem *wireless = view->wirelessCard();
    EXPECT_TRUE(wireless->hasDetail());
    EXPECT_TRUE(view->bluetoothCard()->hasDetail());
    EXPECT_FALSE(view->darkModeCard()->hasDetail());
    shell::ShellWindow *window = center->popupWindow(Panel::ControlCenter);
    const int mainHeight = window->drawnBounds().height();

    // The chevron fades in while the card is hovered.
    EXPECT_EQ(0, wireless->chevron()->opacity());
    const base::PointF card = centre(Panel::ControlCenter, wireless);
    move(card.x(), card.y());
    frames(std::chrono::milliseconds(200));
    EXPECT_EQ(1, wireless->chevron()->opacity());
    // Centred on the card's height, as far from its end as the plate is from its start.
    const ui::RectF chevron = wireless->chevron()->geometry();
    EXPECT_DOUBLE_EQ(wireless->geometry().height / 2, chevron.y + chevron.height / 2);
    EXPECT_DOUBLE_EQ(10, wireless->geometry().width - chevron.x - chevron.width);

    // Right click pushes the Wi-Fi panel without toggling: the pages slide and the
    // window's height glides to the panel's.
    const base::Rect popup = window->drawnBounds();
    rightClick(card.x(), card.y());
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_TRUE(services->wireless);
    auto *panel = dynamic_cast<shell::NetworkPanel *>(pager->detailPage());
    ASSERT_NE(nullptr, panel);
    EXPECT_TRUE(pager->isShowingDetail());
    EXPECT_EQ(1, services->scans);
    frames(std::chrono::milliseconds(100));
    EXPECT_GT(pager->progress(), 0);
    EXPECT_LT(pager->progress(), 1);
    EXPECT_GT(panel->opacity(), 0);
    EXPECT_LT(view->opacity(), 1);
    // The window follows the pages frame by frame, keeping its right edge.
    EXPECT_GT(window->drawnBounds().width(), 320);
    EXPECT_LT(window->drawnBounds().width(), shell::ControlCenterView::kWidth);
    EXPECT_EQ(1440 - 12, window->drawnBounds().right());
    EXPECT_GT(window->drawnBounds().height(), std::min(mainHeight, int(std::lround(panel->shownBottom()))));
    EXPECT_LT(window->drawnBounds().height(), std::max(mainHeight, int(std::lround(panel->shownBottom()))));
    frames(std::chrono::milliseconds(400));
    EXPECT_EQ(1, pager->progress());
    EXPECT_FALSE(view->isVisible());
    EXPECT_TRUE(panel->transform().isIdentity());
    const int detailHeight = int(std::lround(panel->shownBottom()));
    EXPECT_NE(mainHeight, detailHeight);
    EXPECT_EQ(base::Rect(1440 - 320 - 12, 38, 320, detailHeight), window->drawnBounds());
    EXPECT_EQ(base::Rect(0, 0, 320, detailHeight), window->surface()->backgroundBlurRect());
    // The panel is as wide as it is from the menu bar.
    ASSERT_EQ(2u, panel->rows().size());
    EXPECT_EQ(320 - 2 * 6, panel->rows()[0]->geometry().width);
    expectFooterGaps(panel, panel->rows().back());

    // Escape goes back to the tiles, and again closes the control centre.
    key(KEY_ESC);
    test::runFor(std::chrono::milliseconds(20));
    EXPECT_TRUE(center->isOpen(Panel::ControlCenter));
    EXPECT_FALSE(pager->isShowingDetail());
    frames(std::chrono::milliseconds(400));
    EXPECT_EQ(nullptr, pager->detailPage());
    EXPECT_TRUE(view->isVisible());
    EXPECT_EQ(1, view->opacity());
    constexpr int width = int(shell::ControlCenterView::kWidth);
    EXPECT_EQ(base::Rect(1440 - width - 12, 38, width, mainHeight), window->drawnBounds());

    // The audio output button sits beside its slider; the title also opens it.
    shell::TileSlider *sound = view->soundSlider();
    EXPECT_TRUE(sound->hasDetail());
    EXPECT_FALSE(view->brightnessSlider()->hasDetail());
    const base::PointF soundCentre = centre(Panel::ControlCenter, sound);
    move(soundCentre.x(), soundCentre.y());
    frames(std::chrono::milliseconds(200));
    EXPECT_EQ(1, sound->chevron()->opacity());
    const auto *soundTitle = static_cast<const ui::Label *>(sound->children().front().get());
    const ui::RectF soundChevron = sound->chevron()->geometry();
    EXPECT_DOUBLE_EQ(sound->slider()->geometry().y + 10, soundChevron.y + soundChevron.height / 2);
    EXPECT_DOUBLE_EQ(15, sound->geometry().width - soundChevron.x - soundChevron.width);
    const ui::PointF soundOrigin = sound->mapToWindow({soundTitle->geometry().x + 4, soundTitle->geometry().y + 4});
    rightClick(popup.x() + soundOrigin.x, popup.y() + soundOrigin.y);
    frames(std::chrono::milliseconds(400));
    auto *volume = dynamic_cast<shell::VolumePanel *>(pager->detailPage());
    ASSERT_NE(nullptr, volume);
    ASSERT_EQ(2u, volume->rows().size());
    EXPECT_EQ(320 - 2 * 6, volume->rows()[0]->geometry().width);
    EXPECT_EQ(int(std::lround(volume->shownBottom())), window->drawnBounds().height());
    key(KEY_ESC);
    frames(std::chrono::milliseconds(420));
    EXPECT_EQ(nullptr, pager->detailPage());
    EXPECT_TRUE(center->isOpen(Panel::ControlCenter));

    // The Bluetooth chevron; the panel's settings link closes the control centre.
    services->devices = {{"/dev/a", "AirPods", "audio-headphones", true, false, 72}};
    shell::CardItem *bluetooth = view->bluetoothCard();
    const auto bt = centre(Panel::ControlCenter, bluetooth);
    rightClick(bt.x(), bt.y());
    frames(std::chrono::milliseconds(400));
    EXPECT_TRUE(services->bluetooth);
    auto *devices = dynamic_cast<shell::BluetoothPanel *>(pager->detailPage());
    ASSERT_NE(nullptr, devices);
    ASSERT_EQ(1u, devices->rows().size());
    clickView(Panel::ControlCenter, devices->settingsLink());
    test::runFor(std::chrono::milliseconds(20));
    EXPECT_FALSE(center->isOpen(Panel::ControlCenter));
    EXPECT_EQ((std::vector<std::string> {"-m", "bluetooth"}), services->settings.back());
}

TEST_F(ControlCenterTest, SlidersMoveTheLevelsAndTheMediaCardControlsThePlayer)
{
    center->openPanel(Panel::ControlCenter, bar.get(), item(178));
    frame();
    shell::ControlCenterView *view = center->controlCenterView();
    shell::LevelSlider *sound = view->soundSlider()->slider();
    EXPECT_NEAR(0.36, sound->position(), 1e-9);

    // A press puts the knob's centre under the pointer.
    const ui::PointF origin = sound->mapToWindow({0, 0});
    const base::Rect popup = center->popupWindow(Panel::ControlCenter)->drawnBounds();
    const double travel = sound->geometry().width - 67;
    click(popup.x() + origin.x + 16 + travel * 0.5, popup.y() + origin.y + 10);
    EXPECT_NEAR(0.5, services->volumeValue, 0.011);
    EXPECT_FALSE(services->mutedValue);

    // A wheel notch moves five steps.
    input.onPointerAxis(at(time += 5), base::PointF(0, -10), true);
    frame();
    EXPECT_NEAR(0.55, services->volumeValue, 0.011);

    // The brightness slider writes as the knob moves.
    shell::LevelSlider *display = view->brightnessSlider()->slider();
    const ui::PointF displayOrigin = display->mapToWindow({0, 0});
    click(popup.x() + displayOrigin.x + display->geometry().height / 2, popup.y() + displayOrigin.y + 11);
    EXPECT_EQ(1, services->brightnessWrites);
    EXPECT_EQ(1, services->brightnessValue);

    // No player: "Not Playing", three dimmed, disabled buttons.
    shell::MediaTile *media = view->media();
    EXPECT_FALSE(media->playButton()->isEnabled());
    EXPECT_DOUBLE_EQ(0.68, media->playButton()->opacity());
    services->mediaState.hasPlayer = true;
    services->mediaState.playing = true;
    services->mediaState.title = "Song";
    services->mediaState.canControl = true;
    services->mediaState.canPause = true;
    services->mediaState.canGoNext = true;
    services->changed();
    test::runFor(std::chrono::milliseconds(5));
    frame();
    EXPECT_TRUE(media->playButton()->isEnabled());
    EXPECT_FALSE(media->previousButton()->isEnabled());
    clickView(Panel::ControlCenter, media->playButton());
    clickView(Panel::ControlCenter, media->nextButton());
    clickView(Panel::ControlCenter, media->previousButton());
    EXPECT_EQ((std::vector<std::string> {"pause", "next"}), services->media_);
}

TEST_F(ControlCenterTest, TheWifiPanelListsNetworksAndJoinsThem)
{
    using State = shell::WirelessNetwork::State;
    using Security = shell::WirelessNetwork::Security;
    services->networks = {network("Office 5G", State::Activated, Security::Password, "/c/1"),
                          network("Cafe", State::Deactivated, Security::None),
                          network("Home", State::Deactivated, Security::Password),
                          network("Busy", State::Activating, Security::Password, "/c/2")};
    center->openPanel(Panel::Network, bar.get(), item(293));
    frame();
    shell::ShellWindow *window = center->popupWindow(Panel::Network);
    ASSERT_NE(nullptr, window);
    // As tall as its content, glass and input over all of it, and allowed
    // to grow to the work area's height.
    const base::Rect bounds = window->drawnBounds();
    auto *panel = static_cast<shell::NetworkPanel *>(center->statusPanel(Panel::Network));
    const int shown = int(std::lround(panel->shownBottom()));
    EXPECT_EQ(int(panel->contentHeight()), shown);
    // Kept 6 inside the work area's right edge.
    EXPECT_EQ(base::Rect(1440 - 320 - 6, 38, 320, shown), bounds);
    EXPECT_EQ(base::Rect(1440 - 320 - 6, 38, 320, 960 - kMenuBar - kDock - 12),
              center->panelGeometry(Panel::Network, bar.get(), item(293)));
    EXPECT_EQ(base::Rect(0, 0, 320, shown), window->surface()->backgroundBlurRect());
    EXPECT_FALSE(window->surface()->inputRegion());
    EXPECT_EQ(1, services->scans);

    ASSERT_EQ(4u, panel->rows().size());
    EXPECT_TRUE(panel->rows()[0]->isActive());
    expectFooterGaps(Panel::Network, panel->rows().back());
    EXPECT_FALSE(panel->rows()[1]->isActive());
    clickView(Panel::Network, panel->rows()[1]);
    // A connecting network takes no clicks.
    clickView(Panel::Network, panel->rows()[3]);
    EXPECT_EQ((std::vector<std::string> {"Cafe"}), services->activated);
    EXPECT_TRUE(center->isOpen(Panel::Network));
    // A new password network closes the panel for the agent's prompt.
    clickView(Panel::Network, panel->rows()[2]);
    test::runFor(std::chrono::milliseconds(20));
    EXPECT_FALSE(center->isOpen(Panel::Network));

    // The switch turns Wi-Fi off; the body goes, the panel shrinks over 380 ms.
    center->openPanel(Panel::Network, bar.get(), item(293));
    frame();
    panel = static_cast<shell::NetworkPanel *>(center->statusPanel(Panel::Network));
    const double open = panel->shownBottom();
    clickView(Panel::Network, panel->wirelessSwitch());
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_FALSE(services->wireless);
    test::runFor(std::chrono::milliseconds(100));
    frame();
    EXPECT_LT(panel->shownBottom(), open);
    EXPECT_GT(panel->shownBottom(), panel->contentHeight());
    // The window and its glass shrink with it, frame by frame.
    window = center->popupWindow(Panel::Network);
    const int shrinking = int(std::lround(panel->shownBottom()));
    EXPECT_EQ(shrinking, window->geometry().height());
    EXPECT_EQ(base::Rect(0, 0, 320, shrinking), window->surface()->backgroundBlurRect());
    test::runFor(std::chrono::milliseconds(400));
    frame();
    EXPECT_DOUBLE_EQ(panel->contentHeight(), panel->shownBottom());
    EXPECT_EQ(int(panel->contentHeight()), window->geometry().height());

    // The settings link closes the panel and opens Settings.
    clickView(Panel::Network, panel->settingsLink());
    test::runFor(std::chrono::milliseconds(20));
    EXPECT_FALSE(center->isOpen(Panel::Network));
    EXPECT_EQ((std::vector<std::string> {"-m", "wlan"}), services->settings.back());
}

TEST_F(ControlCenterTest, TheBluetoothPanelListsPairedDevicesAndConnectsThem)
{
    services->devices = {{"/dev/a", "AirPods", "audio-headphones", true, false, 72},
                         {"/dev/b", "Mouse", "input-mouse", false, false, -1},
                         {"/dev/c", "Keyboard", "input-keyboard", false, true, 40}};
    center->openPanel(Panel::Bluetooth, bar.get(), item(333));
    frame();
    auto *panel = static_cast<shell::BluetoothPanel *>(center->statusPanel(Panel::Bluetooth));
    ASSERT_NE(nullptr, panel);
    EXPECT_TRUE(panel->bluetoothSwitch()->isChecked());
    ASSERT_EQ(3u, panel->rows().size());
    EXPECT_TRUE(panel->rows()[0]->isActive());
    EXPECT_FALSE(panel->rows()[1]->isActive());
    expectFooterGaps(Panel::Bluetooth, panel->rows().back());

    // A click disconnects a connected device and connects another; a busy
    // one takes no clicks.
    clickView(Panel::Bluetooth, panel->rows()[0]);
    clickView(Panel::Bluetooth, panel->rows()[1]);
    clickView(Panel::Bluetooth, panel->rows()[2]);
    EXPECT_EQ((std::vector<std::pair<std::string, bool>> {{"/dev/a", false}, {"/dev/b", true}}),
              services->connections);

    // Off, the list goes and the switch alone is left at once, while the
    // adapter is still powering off.
    services->slowBluetooth = true;
    const double open = panel->contentHeight();
    clickView(Panel::Bluetooth, panel->bluetoothSwitch());
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_TRUE(services->bluetooth);
    EXPECT_FALSE(panel->bluetoothSwitch()->isChecked());
    EXPECT_LT(panel->contentHeight(), open);
    services->changed();
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_FALSE(panel->bluetoothSwitch()->isChecked());
    services->bluetooth = false;
    services->changed();
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_FALSE(panel->bluetoothSwitch()->isChecked());

    clickView(Panel::Bluetooth, panel->settingsLink());
    test::runFor(std::chrono::milliseconds(20));
    EXPECT_FALSE(center->isOpen(Panel::Bluetooth));
    EXPECT_EQ((std::vector<std::string> {"-m", "bluetooth"}), services->settings.back());
}

TEST_F(ControlCenterTest, TheSoundAndBatteryPanelsPickOutputsAndModes)
{
    center->openPanel(Panel::Volume, bar.get(), item(333));
    frame();
    auto *volume = static_cast<shell::VolumePanel *>(center->statusPanel(Panel::Volume));
    ASSERT_EQ(2u, volume->rows().size());
    EXPECT_TRUE(volume->rows()[0]->isActive());
    expectFooterGaps(Panel::Volume, volume->rows().back());
    clickView(Panel::Volume, volume->rows()[0]);
    EXPECT_EQ(-1, services->defaultRequested);
    clickView(Panel::Volume, volume->rows()[1]);
    EXPECT_EQ(2, services->defaultRequested);

    // Opening another panel is the bar's: it closes this one first.
    center->closePanels();
    test::runFor(std::chrono::milliseconds(5));
    center->openPanel(Panel::Battery, bar.get(), item(247));
    frame();
    EXPECT_FALSE(center->isOpen(Panel::Volume));
    auto *battery = static_cast<shell::BatteryPanel *>(center->statusPanel(Panel::Battery));
    ASSERT_EQ(3u, battery->rows().size());
    EXPECT_TRUE(battery->rows()[0]->isVisible());
    EXPECT_TRUE(battery->rows()[1]->isActive());
    EXPECT_FALSE(battery->rows()[2]->isVisible());
    expectFooterGaps(Panel::Battery, battery->rows()[1]);
    clickView(Panel::Battery, battery->rows()[0]);
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_EQ("power-saver", services->profile);
    EXPECT_TRUE(battery->rows()[0]->isActive());
}

TEST_F(ControlCenterTest, LanguageChangeKeepsTheOpenPanels)
{
    center->openPanel(Panel::ControlCenter, bar.get(), item(178));
    frame();
    shell::ControlCenterView *view = center->controlCenterView();
    ASSERT_NE(nullptr, view);
    const int height = center->popupWindow(Panel::ControlCenter)->drawnBounds().height();
    ui::Application::instance()->languageChanged.emit();
    test::runFor(std::chrono::milliseconds(5));
    frame();
    EXPECT_TRUE(center->isOpen(Panel::ControlCenter));
    EXPECT_EQ(view, center->controlCenterView());
    EXPECT_EQ(height, center->popupWindow(Panel::ControlCenter)->drawnBounds().height());
    EXPECT_TRUE(view->wirelessCard()->isChecked());
    center->closePanels();
    test::runFor(std::chrono::milliseconds(5));

    center->openPanel(Panel::Battery, bar.get(), item(247));
    frame();
    auto *battery = static_cast<shell::BatteryPanel *>(center->statusPanel(Panel::Battery));
    ASSERT_NE(nullptr, battery);
    ui::Application::instance()->languageChanged.emit();
    test::runFor(std::chrono::milliseconds(5));
    frame();
    EXPECT_TRUE(center->isOpen(Panel::Battery));
    EXPECT_EQ(battery, center->statusPanel(Panel::Battery));
    ASSERT_EQ(3u, battery->rows().size());
    EXPECT_TRUE(battery->rows()[1]->isActive());
    EXPECT_FALSE(battery->rows()[2]->isVisible());
}

TEST_F(ControlCenterTest, TheBarIndicatorsFollowTheServices)
{
    int changes = 0;
    base::Subscription subscription = center->MenuBarIndicators::changed.connect([&] { ++changes; });
    EXPECT_TRUE(center->audio().hasSink);
    EXPECT_NEAR(0.36, center->audio().volume, 1e-9);
    EXPECT_EQ("Office 5G", center->network().wirelessName);
    services->wireless = false;
    services->changed();
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_EQ(1, changes);
    EXPECT_FALSE(center->network().wirelessEnabled);
    services->changed();
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_EQ(1, changes);
}

TEST_F(ControlCenterTest, ThePowerDialogConfirmsWithReturnAndCancelsWithEscape)
{
    shell::PowerDialog dialog(shell.get());
    int accepted = 0;
    int rejected = 0;
    dialog.onAccepted = [&] { ++accepted; };
    dialog.onRejected = [&] { ++rejected; };
    dialog.open(shell::PowerDialog::Action::Logout);
    frame();
    ASSERT_TRUE(dialog.isOpen());
    wm::Window *window = dialog.window()->wmWindow();
    ASSERT_NE(nullptr, window);
    EXPECT_EQ(wm::WindowRole::Normal, window->role());
    EXPECT_TRUE(window->isAlwaysOnTop());
    EXPECT_EQ(336, window->bounds().width());
    // Asking again changes what it asks.
    dialog.open(shell::PowerDialog::Action::Reboot);
    EXPECT_EQ(shell::PowerDialog::Action::Reboot, dialog.action());
    key(KEY_ENTER);
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_EQ(1, accepted);
    EXPECT_FALSE(dialog.isOpen());

    dialog.open(shell::PowerDialog::Action::Shutdown);
    frame();
    key(KEY_ESC);
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_EQ(1, rejected);
    EXPECT_FALSE(dialog.isOpen());
}

TEST_F(ControlCenterTest, TheDeviceUsersTileShowsWhoUsesTheCamera)
{
    center->openPanel(Panel::ControlCenter, bar.get(), item(178));
    frames(std::chrono::milliseconds(280));
    shell::ControlCenterView *view = center->controlCenterView();
    const int before = center->popupWindow(Panel::ControlCenter)->drawnBounds().height();
    const double beforeY = view->wirelessCard()->mapToWindow({0, 0}).y;
    services->users = {{"Camera", "org.example.Camera", true, true, false}};
    services->changed();
    test::runFor(std::chrono::milliseconds(5));
    frame();
    // Its own glass plate sits above the scrollable grid. The bottom stays in the work area.
    const auto after = center->popupWindow(Panel::ControlCenter)->drawnBounds();
    EXPECT_GE(after.height(), before);
    EXPECT_NEAR(beforeY + 45, view->wirelessCard()->mapToWindow({0, 0}).y, 1);
    EXPECT_EQ("Camera is using the camera and microphone", shell::deviceUsageText(services->users.front()));
}

TEST(ControlCenterLayout, PagesKeepIndependentAtomicLayoutsAcrossRestarts)
{
    using namespace shell::controls;
    test::TemporaryDirectory directory;
    const auto file = directory.filePath("layout.conf");
    int pageId = 0;
    int largeId = 0;
    {
        Layout layout(file);
        EXPECT_EQ(8, layout.page(0)->rows());
        pageId = layout.addPage();
        ASSERT_GT(pageId, 0);
        ASSERT_TRUE(layout.add(pageId, "wifi"));
        ASSERT_TRUE(layout.add(pageId, "screenshot", {2, 0}));
        largeId = layout.page(pageId)->tiles.back().id;
        ASSERT_TRUE(layout.resize(pageId, largeId, Size::Large));
        ASSERT_TRUE(layout.move(pageId, largeId, {0, 0}));
        EXPECT_EQ(base::Point(0, 0), layout.page(pageId)->tile(largeId)->cell);
        const auto *page = layout.page(pageId);
        EXPECT_EQ(2u, page->tiles.size());
        EXPECT_TRUE(page->tiles[0].bounds().intersected(page->tiles[1].bounds()).isEmpty());
        EXPECT_FALSE(layout.move(pageId, largeId, {3, 0}));
        EXPECT_EQ(base::Point(0, 0), layout.page(pageId)->tile(largeId)->cell);
        EXPECT_FALSE(layout.resize(pageId, page->tiles[1].id, Size::Large));
        EXPECT_FALSE(layout.removePage(0));
        EXPECT_TRUE(layout.setIcon(pageId, "cc-home"));
        layout.setPin("display", Pin::Active);
        EXPECT_EQ(18u, layout.page(0)->tiles.size());
    }
    Layout loaded(file);
    ASSERT_EQ(2u, loaded.pages().size());
    EXPECT_EQ(pageId, loaded.selected());
    EXPECT_EQ("cc-home", loaded.page(pageId)->icon);
    EXPECT_EQ(Size::Large, loaded.page(pageId)->tile(largeId)->size);
    EXPECT_EQ(base::Point(0, 0), loaded.page(pageId)->tile(largeId)->cell);
    EXPECT_EQ(Pin::Active, loaded.pin("display"));
    EXPECT_TRUE(loaded.removePage(pageId));
    EXPECT_EQ(0, loaded.selected());
}

TEST_F(ControlCenterTest, EditorAddsDragsCancelsAndRemovesControls)
{
    using namespace shell::controls;
    center->openPanel(Panel::ControlCenter, bar.get(), item(178));
    frames(std::chrono::milliseconds(300));
    auto *view = center->controlCenterView();
    auto *window = center->popupWindow(Panel::ControlCenter);
    const base::Rect shown = window->drawnBounds();
    click(shown.center().x(), shown.bottom() - 24);
    ASSERT_TRUE(view->editing());
    frames(std::chrono::milliseconds(370));
    const base::Rect expanded = window->drawnBounds();
    EXPECT_GT(expanded.width(), 1000);
    const auto shape = window->surface()->backgroundBlurShape();
    EXPECT_TRUE(shape.contains({60, 80}));
    EXPECT_FALSE(shape.contains({963, 111})); // catalog and canvas are separate glass shapes

    center->openItem("new-page", bar.get(), item(178));
    const int page = view->pageId();
    ASSERT_GT(page, 0);
    ASSERT_TRUE(center->layoutModel().page(page)->tiles.empty());
    frames(std::chrono::milliseconds(270));
    click(expanded.x() + 74, expanded.y() + 159); // Connectivity category
    click(expanded.x() + 401, expanded.y() + 118); // Wi-Fi preview
    frames(std::chrono::milliseconds(270));
    ASSERT_EQ(1u, center->layoutModel().page(page)->tiles.size());
    const int id = center->layoutModel().page(page)->tiles.front().id;
    const auto first = centre(Panel::ControlCenter, view->wirelessCard());
    press(first.x(), first.y());
    move(first.x() + 66, first.y() + 66);
    key(KEY_ESC);
    test::runFor(std::chrono::milliseconds(10));
    release();
    EXPECT_TRUE(view->editing());
    EXPECT_EQ(base::Point(0, 0), center->layoutModel().page(page)->tile(id)->cell);

    press(first.x(), first.y());
    move(first.x() + 66, first.y() + 66);
    release();
    frames(std::chrono::milliseconds(270));
    EXPECT_EQ(base::Point(1, 1), center->layoutModel().page(page)->tile(id)->cell);
    const auto tile = view->wirelessCard()->mapToWindow({11, 12});
    click(expanded.x() + tile.x, expanded.y() + tile.y);
    EXPECT_TRUE(center->layoutModel().page(page)->tiles.empty());
    key(KEY_ESC);
    frames(std::chrono::milliseconds(300));
    EXPECT_FALSE(view->editing());
    EXPECT_TRUE(center->isOpen(Panel::ControlCenter));
}

TEST_F(ControlCenterTest, ControlCatalogRisesOverTheDockAndReachesTheScreenEdge)
{
    shell::ShellWindow testDock(shell.get(), wm::WindowRole::Dock, false);
    testDock.setContentView(std::make_unique<ui::View>());
    testDock.setGeometry({400, kOutput.bottom() - kDock, 600, kDock});
    testDock.show();
    frames(std::chrono::milliseconds(35));
    center->openPanel(Panel::ControlCenter, bar.get(), item(178));
    frames(std::chrono::milliseconds(300));
    auto *window = center->popupWindow(Panel::ControlCenter);
    ASSERT_NE(nullptr, window->wmWindow());
    EXPECT_LT(wm::layerFor(window->wmWindow()), wm::layerFor(testDock.wmWindow()));
    const auto live = window->drawnBounds();
    const auto getUnderlay = [&] { return window->surface()->glass().underlay.bounds; };
    const auto liveUnderlay = getUnderlay();
    EXPECT_EQ(live.height(), liveUnderlay.height());
    const auto header = [&]() -> std::optional<render::GlassLens> {
        const auto &lenses = window->surface()->glass().lenses;
        const auto match = std::ranges::find_if(lenses, [](const auto &lens) {
            return !lens.viewport && lens.radius == 22 && lens.bounds.height() == 44;
        });
        return match == lenses.end() ? std::nullopt : std::optional(*match);
    };
    EXPECT_FALSE(header());
    click(live.center().x(), live.bottom() - 24);
    ASSERT_TRUE(center->controlCenterView()->editing());
    const auto expanded = window->drawnBounds();
    EXPECT_EQ(kOutput.bottom(), expanded.bottom());
    EXPECT_EQ(liveUnderlay.height(), getUnderlay().height());
    EXPECT_GT(wm::layerFor(window->wmWindow()), wm::layerFor(testDock.wmWindow()));
    EXPECT_FALSE(window->surface()->backgroundBlurShape().contains({30, 80}));
    frames(std::chrono::milliseconds(130));
    ASSERT_TRUE(header());
    EXPECT_GT(header()->opacity, 0);
    EXPECT_LT(header()->opacity, 1);
    EXPECT_TRUE(window->surface()->backgroundBlurShape().contains({30, double(expanded.height() - 2)}));
    const auto &risingLenses = window->surface()->glass().lenses;
    const auto sheet = std::ranges::find_if(risingLenses, [](const auto &lens) {
        return !lens.viewport && lens.bounds.x() == 0 && lens.radius == 31;
    });
    ASSERT_NE(risingLenses.end(), sheet);
    // The frame clock can be delayed under load; test the mask against the
    // actual animated sheet instead of assuming a particular y at 130 ms.
    EXPECT_FALSE(window->surface()->backgroundBlurShape().contains({30, sheet->bounds.y() - 1}));
    frames(std::chrono::milliseconds(260));
    const auto &mask = window->surface()->backgroundBlurShape();
    const auto &lenses = window->surface()->glass().lenses;
    ASSERT_TRUE(header());
    const auto settledHeader = *header();
    EXPECT_EQ(1, settledHeader.opacity);
    EXPECT_EQ(liveUnderlay.height(), getUnderlay().height());
    EXPECT_GT(getUnderlay().x(), liveUnderlay.x());
    EXPECT_TRUE(std::ranges::any_of(lenses, [](const auto &lens) { return lens.viewport.has_value(); }));
    EXPECT_TRUE(mask.contains({30, 80}));
    EXPECT_TRUE(mask.contains({1, double(expanded.height() - 2)})); // no bottom corner above the dock
    const auto overlap = expanded.intersected(testDock.geometry());
    ASSERT_FALSE(overlap.isEmpty());
    move(overlap.center().x(), overlap.center().y());
    EXPECT_EQ(window->surface(), seat.pointerFocus());
    key(KEY_ESC);
    frames(std::chrono::milliseconds(125));
    ASSERT_TRUE(header());
    EXPECT_GT(header()->opacity, 0);
    EXPECT_LT(header()->opacity, settledHeader.opacity);
    EXPECT_LT(header()->bounds.y(), settledHeader.bounds.y());
    EXPECT_EQ(liveUnderlay.height(), getUnderlay().height());
    EXPECT_GT(wm::layerFor(window->wmWindow()), wm::layerFor(testDock.wmWindow()));
    frames(std::chrono::milliseconds(180));
    EXPECT_FALSE(header());
    EXPECT_EQ(liveUnderlay, getUnderlay());
    EXPECT_EQ(live.bottom(), window->drawnBounds().bottom());
    EXPECT_LT(wm::layerFor(window->wmWindow()), wm::layerFor(testDock.wmWindow()));
}

TEST_F(ControlCenterTest, WidgetsCanBeAddedDraggedAndRemovedAndSurviveRestart)
{
    const auto file = directory.filePath("desktop-widgets.conf");
    const auto widgetWindows = [&] {
        std::vector<wm::Window *> result;
        for (auto *window : manager.windows()) {
            if (window->role() == wm::WindowRole::DesktopWidget && window->isVisible())
                result.push_back(window);
        }
        return result;
    };
    base::Rect relocated;
    {
        shell::DesktopWidgets widgets(shell.get(), *center, file);
        ASSERT_EQ(3u, widgetWindows().size());
        EXPECT_EQ(wm::StackingLayer::DesktopWidget, wm::layerFor(widgetWindows()[0]));
        ASSERT_FALSE(widgetWindows()[0]->rootSurface()->backgroundBlurShape().isEmpty());
        widgets.edit();
        frames(std::chrono::milliseconds(280));
        key(KEY_ENTER); // Add the selected Clock, small.
        frames(std::chrono::milliseconds(55));
        ASSERT_EQ(4u, widgetWindows().size());
        const auto from = widgetWindows().back()->bounds();
        press(from.center().x(), from.center().y());
        move(from.center().x() + 53, from.center().y() + 36);
        release();
        frame();
        relocated = from.translated(base::Point(53, 36));
        EXPECT_EQ(relocated, widgetWindows().back()->bounds());
        key(KEY_ESC);
        test::runFor(std::chrono::milliseconds(15));
    }
    shell::DesktopWidgets restored(shell.get(), *center, file);
    ASSERT_EQ(4u, widgetWindows().size());
    EXPECT_EQ(relocated, widgetWindows().back()->bounds());
    restored.edit();
    frames(std::chrono::milliseconds(280));
    click(relocated.x() + 15, relocated.y() + 15);
    test::runFor(std::chrono::milliseconds(15));
    EXPECT_EQ(3u, widgetWindows().size());
}

TEST_F(ControlCenterTest, WidgetGalleryRisesFromTheScreenEdgeAndSearches)
{
    shell::DesktopWidgets widgets(shell.get(), *center, directory.filePath("widget-gallery.conf"));
    widgets.edit();
    const auto locate = [&]() -> shell::ShellWindow * {
        for (auto *window : shell->windows()) {
            auto *view = window->window()->contentView();
            if (!view || !window->isShown())
                continue;
            for (const auto &child : view->children())
                if (child->name() == "widget-search")
                    return window;
        }
        return nullptr;
    };
    auto *gallery = locate();
    ASSERT_NE(nullptr, gallery);
    auto *search = static_cast<ui::SearchField *>(gallery->window()->contentView()->children().front().get());
    EXPECT_GE(gallery->geometry().y(), kOutput.bottom() - 8);
    frames(std::chrono::milliseconds(430));
    const auto bounds = gallery->geometry();
    EXPECT_EQ(kOutput.bottom() - 8, bounds.bottom());
    EXPECT_GT(bounds.bottom(), kOutput.bottom() - kDock);
    EXPECT_GT(wm::layerFor(gallery->wmWindow()), wm::StackingLayer::Dock);
    search->setText("does not exist");
    key(KEY_ENTER);
    const auto countWidgets = [&] {
        return std::ranges::count_if(shell->windows(), [](auto *window) {
            return window->wmWindow() && window->wmWindow()->role() == wm::WindowRole::DesktopWidget;
        });
    };
    EXPECT_EQ(3, countWidgets());
    search->setText("bAtTeRy");
    key(KEY_ENTER);
    frames(std::chrono::milliseconds(60));
    EXPECT_EQ(4, countWidgets());
    // Drag the filtered Battery preview out of the gallery. The lift is a
    // separate glass window and the desktop widget keeps the grab point.
    press(bounds.x() + 420, bounds.y() + 177);
    move(715, 350);
    release();
    frames(std::chrono::milliseconds(60));
    EXPECT_EQ(5, countWidgets());
    key(KEY_ESC);
    test::runFor(std::chrono::milliseconds(15));
    EXPECT_EQ(nullptr, locate());
}

// The real bar opening the panels with clicks on its items, as live.
TEST_F(ControlCenterTest, TheBarsItemsOpenEveryPanel)
{
    // The fixture's stand-in bar gives way to the real one.
    bar->hide();
    directory.write("shell.conf", "");
    auto backend = std::make_unique<shell::MenuBar>(base::dbus::Connection::session(core::mainLoop()),
                                                    base::dbus::Connection::system(core::mainLoop()), &shell->tasks(),
                                                    directory.filePath("shell.conf"));
    auto *menuBar = shell->addComponent(std::make_unique<shell::MenuBarWindow>(shell.get(), std::move(backend)));
    menuBar->addPanels(center);
    menuBar->setIndicators(center);
    menuBar->menuBar().setControlShown(shell::MenuBar::Control::Bluetooth, true);
    frame();
    for (const auto &[panel, icon] : {std::pair {Panel::Volume, menuBar->volumeIcon()},
                                      std::pair {Panel::Bluetooth, menuBar->bluetoothIcon()},
                                      std::pair {Panel::Network, menuBar->networkIcon()},
                                      std::pair {Panel::ControlCenter, menuBar->controlIcon()}}) {
        ASSERT_TRUE(icon->isVisible());
        const ui::PointF origin = icon->mapToWindow({0, 0});
        click(origin.x + icon->geometry().width / 2, 16);
        test::runFor(std::chrono::milliseconds(50));
        frame();
        EXPECT_TRUE(center->isOpen(panel)) << int(panel);
        ASSERT_NE(nullptr, center->popupWindow(panel)) << int(panel);
        EXPECT_NE(nullptr, center->popupWindow(panel)->wmWindow()) << int(panel);
        EXPECT_TRUE(icon->isChecked());
    }
    const auto findView = [](auto &self, ui::View *root, const std::string &name) -> ui::View * {
        if (root->name() == name)
            return root;
        for (const auto &child : root->children()) {
            if (auto *found = self(self, child.get(), name))
                return found;
        }
        return nullptr;
    };
    const auto clickBarItem = [&](const ui::View *view) {
        const auto p = view->mapToWindow({view->geometry().width / 2, view->geometry().height / 2});
        click(p.x, p.y);
        test::runFor(std::chrono::milliseconds(20));
        frame();
    };
    auto shown = center->popupWindow(Panel::ControlCenter)->drawnBounds();
    click(shown.center().x(), shown.bottom() - 24);
    auto *root = menuBar->window()->window()->contentView();
    auto *plus = findView(findView, root, "new-page");
    ASSERT_NE(nullptr, plus);
    clickBarItem(plus);
    const int page = center->controlCenterView()->pageId();
    ASSERT_GT(page, 0);
    key(KEY_ESC);
    frames(std::chrono::milliseconds(300));
    clickBarItem(menuBar->controlIcon());
    ASSERT_NE(nullptr, center->controlCenterView());
    EXPECT_EQ(0, center->controlCenterView()->pageId());
    auto *pageItem = findView(findView, root, "page-" + std::to_string(page));
    ASSERT_NE(nullptr, pageItem);
    clickBarItem(pageItem);
    ASSERT_NE(nullptr, center->controlCenterView());
    EXPECT_EQ(page, center->controlCenterView()->pageId());
    EXPECT_FALSE(menuBar->controlIcon()->isChecked());
    clickBarItem(pageItem);
    EXPECT_FALSE(center->isOpen(Panel::ControlCenter));
    menuBar->removePanels(center);
    menuBar->setIndicators(nullptr);
}
