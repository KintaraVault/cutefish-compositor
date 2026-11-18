// SPDX-License-Identifier: BSD-3-Clause
#include <chrono>
#include <cmath>

#include <linux/input-event-codes.h>
#include <gtest/gtest.h>

#include "compositor/compositor.h"
#include "compositor/surface.h"
#include "cutefish/files/thumbnails/thumbnail_cache.h"
#include "fake_renderer.h"
#include "input/input_manager.h"
#include "input/seat.h"
#include "loop_test_util.h"
#include "scene/scene.h"
#include "shell/clipboard/clipboard_history.h"
#include "shell/spotlight/spotlight.h"
#include "shell/spotlight/spotlight_preview.h"
#include "shell/spotlight/spotlight_search.h"
#include "shell/shell_test_util.h"
#include "shell/shell_window.h"
#include "temporary_directory.h"
#include "ui/embedded_window.h"
#include "ui/scroller.h"
#include "ui/text_field.h"
#include "ui/view.h"
#include "wm/window.h"
#include "wm/window_manager.h"

using namespace cutefish;
using namespace cutefish::shell::spotlight;

TEST(SpotlightSearch, RanksNamesAndCombinesCategoryWithQuery)
{
    auto photos = std::make_shared<base::DesktopEntry>();
    photos->name = "Photos";
    photos->categories = {"Graphics"};
    auto other = std::make_shared<base::DesktopEntry>();
    other->name = "Viewer";
    other->keywords = {"photos"};
    const std::vector<shell::DesktopEntryPtr> apps{other, photos};
    EXPECT_EQ(findApps(apps, " PHOTOS "), (std::vector<shell::DesktopEntryPtr>{photos, other}));
    EXPECT_EQ(findApps(apps, "ph", Category::Creativity), (std::vector<shell::DesktopEntryPtr>{photos}));
    EXPECT_TRUE(findApps(apps, "ph", Category::Social).empty());
}

TEST(SpotlightSearch, FiltersTypesAndFolderWithoutFollowingLinks)
{
    test::TemporaryDirectory dir;
    const auto album = dir.filePath("Iceland Scans");
    std::filesystem::create_directory(album);
    dir.write("Iceland Scans/DSC06514.ARW", "raw");
    dir.write("Iceland Scans/DSC06514.jpg", "jpeg");
    dir.write("DSC06514.jpg", "unrelated");
    dir.write(".private.txt", "hidden");
    std::filesystem::create_directory_symlink(album, dir.filePath("Album link"));
    const auto files = indexFiles(dir.filePath(""));
    EXPECT_EQ(findFiles(files, "dsc06514", FileType::Images, album, 10).size(), 2u);
    EXPECT_EQ(findFiles(files, "dsc06514", FileType::All, {}, 10).size(), 3u);
    EXPECT_EQ(findFiles(files, "iceland", FileType::Folders, {}, 10).size(), 1u);
    EXPECT_TRUE(findFiles(files, "private", FileType::All, {}, 10).empty());
    EXPECT_TRUE(findFiles(files, "album link", FileType::All, {}, 10).empty());
}

TEST(SpotlightSearch, ClipboardURLShowsTheHostNotUserInfoOrPath)
{
    EXPECT_EQ(webDomain(" https://en.wikipedia.org/wiki/Test?next=elsewhere \n"), "en.wikipedia.org");
    EXPECT_EQ(webDomain("HTTPS://name:secret@example.org:8443/path"), "example.org");
    EXPECT_EQ(webDomain("http://[::1]:3000/"), "[::1]");
    EXPECT_TRUE(webDomain("http:///path").empty());
    EXPECT_TRUE(webDomain("https://example.org here").empty());
    EXPECT_TRUE(webDomain("file:///home/document").empty());
}

TEST(SpotlightSearch, DraftEncodesEachParameterAndValidatesRecipients)
{
    EXPECT_EQ(mailDraft("first@example.org, second+tag@example.net", "Q & A", "hello?"),
              "mailto:first@example.org,second%2Btag@example.net?subject=Q%20%26%20A&body=hello%3F");
    EXPECT_TRUE(mailDraft("name@", "", "").empty());
    EXPECT_TRUE(mailDraft("first@example.org, invalid", "", "").empty());
    EXPECT_TRUE(mailDraft("first@example.org", "subject\nBcc:", "").empty());
}

TEST(SpotlightSearch, FilePreviewContainsTheActualImageAndSkipsDirectoriesAndLinks)
{
    test::TemporaryDirectory dir;
    files::internal::setThumbnailCacheRoot(dir.filePath("cache"));
    const auto image = dir.filePath("photo.png");
    ui::Bitmap source(240, 120, ui::Bitmap::Format::Argb32Premultiplied);
    source.fill(0xff4499cc);
    EXPECT_TRUE(source.savePng(image));
    auto preview = loadFilePreview(image);
    files::internal::setThumbnailCacheRoot({});
    ASSERT_FALSE(preview.isNull());
    EXPECT_LT(preview.width(), source.width());
    EXPECT_EQ(preview.width(), preview.height() * 2);
    EXPECT_EQ(preview.pixel(20, 20), 0xff4499ccu);
    const auto link = dir.filePath("link.png");
    std::filesystem::create_symlink(image, link);
    EXPECT_TRUE(loadFilePreview(link).isNull());
    EXPECT_TRUE(loadFilePreview(dir.filePath("")).isNull());
}

namespace {

double frameClock = 10'000;

class SpotlightWindowTest : public testing::Test
{
protected:
    scene::Scene scene;
    compositor::Compositor compositor{scene.root()->addChild(std::make_unique<scene::Node>("surfaces"))};
    testsupport::FakeRenderer renderer;
    input::Seat seat{&compositor};
    input::InputManager input{&seat};
    wm::WindowManager wm{&compositor, &seat};
    test::TemporaryDirectory dir;
    std::unique_ptr<shell::Shell> desktop;
    shell::Spotlight *spotlight = nullptr;
    shell::ClipboardHistory *clipboard = nullptr;
    int clock = 100;

    void SetUp() override
    {
        frameClock = 10'000;
        ui::Scroller::setClock([] { return frameClock; });
        const base::Rect bounds(0, 0, 1440, 960);
        scene.setSize(bounds.size());
        wm.setOutputBounds(bounds);
        input.setBounds(bounds.size());
        input.setEventSink(&wm);
        shell::ShellEnvironment env{&compositor, &wm, &seat};
        env.servicesSettings = dir.filePath("services.conf");
        desktop = std::make_unique<shell::Shell>(env);
        auto apps = std::make_unique<test::FakeApplications>();
        for (int i = 0; i < 60; ++i)
            apps->addSimple("app" + std::to_string(i), "Application " + std::to_string(i), "application-x-executable");
        apps->scan();
        desktop->setApplications(std::move(apps));
        desktop->setTaskWindows(std::make_unique<test::FakeTaskWindows>());
        auto history = std::make_unique<shell::ClipboardHistory>(nullptr, *seat.selection(), dir.filePath("clipboard"));
        clipboard = history.get();
        spotlight = desktop->addComponent(std::make_unique<shell::Spotlight>(desktop.get(), nullptr,
                std::move(history), dir.filePath("spotlight.conf")));
    }

    void TearDown() override
    {
        desktop.reset();
        seat.shutdown();
        ui::Scroller::setClock(nullptr);
    }

    void render(double step = 16)
    {
        frameClock += step;
        renderer.frameTime = frameClock;
        compositor.updateTextures(renderer);
    }

    void settle()
    {
        render();
        render(230);
    }

    input::Timestamp time() const { return input::Timestamp(std::chrono::milliseconds(int64_t(frameClock))); }

    wm::Window *picker() const
    {
        for (auto *window : wm.windows()) {
            if (!window->isPopup() && window->role() == wm::WindowRole::Picker)
                return window;
        }
        return nullptr;
    }

    void key(uint32_t code, bool pressed)
    {
        input.onKey(input::Timestamp(std::chrono::milliseconds(clock++)), code, pressed);
    }

    void tap(uint32_t code) { key(code, true); key(code, false); }

    void click(double x, double y)
    {
        input.onPointerMotionAbsolute(input::Timestamp(std::chrono::milliseconds(clock++)), {x, y});
        input.onPointerButton(input::Timestamp(std::chrono::milliseconds(clock++)), BTN_LEFT, true);
        input.onPointerButton(input::Timestamp(std::chrono::milliseconds(clock++)), BTN_LEFT, false);
    }

    ui::View *pickerChild(const std::string &name) const
    {
        for (auto *shellWindow : desktop->windows()) {
            if (shellWindow->role() != wm::WindowRole::Picker)
                continue;
            for (const auto &view : shellWindow->window()->contentView()->children()) {
                if (view->name() == name)
                    return view.get();
            }
        }
        return nullptr;
    }

    double iconY(size_t index = 10) const
    {
        const auto *icons = pickerChild("spotlight-result-icons");
        if (!icons || icons->children().size() <= index) {
            ADD_FAILURE() << "No visible result icons";
            return 0;
        }
        return icons->children()[index]->geometry().y;
    }
};

} // namespace

TEST_F(SpotlightWindowTest, IsFloatingAndSwitchesModesWithKeysAndMouse)
{
    using Mode = shell::Spotlight::Mode;
    spotlight->show(Mode::Search);
    render();
    ASSERT_NE(picker(), nullptr);
    EXPECT_EQ(picker()->bounds().width(), 620);
    EXPECT_EQ(picker()->bounds().height(), 54);
    EXPECT_EQ(picker()->rootSurface(), seat.focusedSurface());
    render(230);
    ASSERT_EQ(picker()->bounds().y(), 211);
    const auto collapsed = picker()->bounds();
    click(collapsed.right() - 212, collapsed.y() + 26);
    settle();
    ASSERT_GT(picker()->bounds().height(), 300);
    EXPECT_EQ(picker()->bounds().x(), collapsed.x());
    key(KEY_LEFTMETA, true);
    tap(KEY_4);
    key(KEY_LEFTMETA, false);
    settle();
    ASSERT_EQ(picker()->bounds().height(), 114);
    EXPECT_EQ(picker()->rootSurface(), seat.focusedSurface());
    tap(KEY_ESC);
    ASSERT_NE(picker(), nullptr);
    EXPECT_EQ(seat.focusedSurface(), nullptr);
    EXPECT_TRUE(picker()->rootSurface()->inputRegion()->isEmpty());
    settle();
    EXPECT_EQ(picker(), nullptr);
}

TEST_F(SpotlightWindowTest, ClipboardDiscardAndDisableDoesNotResurrectPendingCopy)
{
    ASSERT_TRUE(test::runUntil([this] { return clipboard->store().isLoaded(); }));
    ASSERT_TRUE(clipboard->store().add({{"text/plain", "do not retain"}}));
    clipboard->store().setEnabled(false);
    EXPECT_FALSE(clipboard->store().add({{"text/plain", "disabled"}}));
    test::runFor(std::chrono::milliseconds(60));
    EXPECT_TRUE(clipboard->store().entries().empty());
    clipboard->store().setEnabled(true);
    ASSERT_TRUE(clipboard->store().add({{"text/plain", "new entry"}}));
    ASSERT_TRUE(test::runUntil([this] { return clipboard->store().entries().size() == 1; }));
    EXPECT_EQ(clipboard->store().entries().front().text, "new entry");
}

TEST_F(SpotlightWindowTest, MenuKeepsPickerOpenAndOutsideClickDismissesIt)
{
    spotlight->show(shell::Spotlight::Mode::Applications);
    settle();
    ASSERT_TRUE(picker() && picker()->bounds().height() > 450);
    const auto bounds = picker()->bounds();
    click(bounds.right() - 28, bounds.y() + 26);
    render();
    const auto menuVisible = [this] {
        return std::any_of(wm.windows().begin(), wm.windows().end(), [](const wm::Window *window) {
            return window->isPopup() && window->isVisible();
        });
    };
    ASSERT_TRUE(test::runUntil(menuVisible));
    tap(KEY_ESC);
    EXPECT_TRUE(test::runUntil([&] { return !menuVisible(); }));
    ASSERT_NE(picker(), nullptr);
    click(20, 20);
    EXPECT_TRUE(test::runUntil([this] { return !seat.focusedSurface(); }));
    test::runFor(std::chrono::milliseconds(10)); // process the posted focus-loss dismissal
    settle();
    EXPECT_EQ(picker(), nullptr);
}

TEST_F(SpotlightWindowTest, CommandConventionSwitchesModeWithoutSendingDigitsToSearch)
{
    seat.setShortcutConvention(input::ShortcutConvention::Command);
    spotlight->show(shell::Spotlight::Mode::Search);
    render();
    key(KEY_LEFTALT, true); // The physical key next to Space is Command.
    tap(KEY_4);
    key(KEY_LEFTALT, false);
    settle();
    ASSERT_TRUE(picker() && picker()->bounds().height() == 114);
    // Toggling Clipboard closes it only if the chord actually took us there.
    spotlight->toggle(shell::Spotlight::Mode::Clipboard);
    settle();
    EXPECT_EQ(picker(), nullptr);
}

TEST_F(SpotlightWindowTest, PanelFadesResizesAndCanReopenWhileClosing)
{
    using Mode = shell::Spotlight::Mode;
    spotlight->show(Mode::Search);
    ASSERT_NE(picker(), nullptr);
    EXPECT_LT(picker()->rootSurface()->opacity(), 1);
    settle();
    auto *surface = picker()->rootSurface();
    EXPECT_FLOAT_EQ(surface->opacity(), 1);
    EXPECT_FALSE(surface->inputRegion()->contains(base::Point(375, 27)));
    EXPECT_EQ(picker()->bounds().height(), 54);

    spotlight->show(Mode::Applications);
    render();
    render(48);
    EXPECT_GT(picker()->bounds().height(), 54);
    EXPECT_LT(picker()->bounds().height(), 508);
    render(230);
    EXPECT_EQ(picker()->bounds().height(), 508);
    EXPECT_TRUE(surface->inputRegion()->contains(base::Point(375, 27)));

    spotlight->show(Mode::Search);
    render();
    render(48);
    EXPECT_GT(picker()->bounds().height(), 54);
    EXPECT_LT(picker()->bounds().height(), 508);
    render(230);
    EXPECT_EQ(picker()->bounds().height(), 54);

    spotlight->hide();
    EXPECT_NE(seat.focusedSurface(), surface);
    EXPECT_TRUE(surface->inputRegion()->isEmpty());
    render();
    render(48);
    EXPECT_GT(surface->opacity(), 0);
    EXPECT_LT(surface->opacity(), 1);
    spotlight->toggle(Mode::Search);
    EXPECT_EQ(seat.focusedSurface(), surface);
    EXPECT_FALSE(surface->inputRegion()->isEmpty());
    settle();
    ASSERT_NE(picker(), nullptr);
    EXPECT_EQ(picker()->rootSurface(), surface);
    EXPECT_FLOAT_EQ(surface->opacity(), 1);
    spotlight->hide();
    settle();
    EXPECT_EQ(picker(), nullptr);
    spotlight->show(Mode::Search);
    settle();
    ASSERT_NE(picker(), nullptr);
    EXPECT_TRUE(surface->inputRegion()->contains(base::Point(28, 27)));
}

TEST_F(SpotlightWindowTest, DirectApplicationLauncherRevealsHeightWithoutMovingDown)
{
    spotlight->show(shell::Spotlight::Mode::Applications);
    ASSERT_NE(picker(), nullptr);
    EXPECT_EQ(picker()->bounds().y(), 211);
    EXPECT_LT(picker()->bounds().height(), 100);
    render();
    int previous = picker()->bounds().height();
    render(-8); // another output can supply an earlier sample time
    EXPECT_EQ(picker()->bounds().height(), previous);
    for (int i = 0; i < 6; ++i) {
        render();
        EXPECT_EQ(picker()->bounds().y(), 211);
        EXPECT_GT(picker()->bounds().height(), previous);
        EXPECT_LT(picker()->bounds().height(), 508);
        previous = picker()->bounds().height();
    }
    render(230);
    EXPECT_EQ(picker()->bounds().height(), 508);
}

TEST_F(SpotlightWindowTest, EmailEditsInlineAndBuildsAResultCard)
{
    spotlight->show(shell::Spotlight::Mode::Actions);
    settle();
    auto *search = static_cast<ui::TextField *>(pickerChild("spotlight-search"));
    ASSERT_NE(search, nullptr);
    search->setText("Compose Email");
    tap(KEY_ENTER);
    settle();
    auto *body = static_cast<ui::TextField *>(pickerChild("spotlight-action-message"));
    auto *subject = static_cast<ui::TextField *>(pickerChild("spotlight-action-subject"));
    auto *recipient = static_cast<ui::TextField *>(pickerChild("spotlight-action-parameter"));
    ASSERT_TRUE(body && subject && recipient);
    EXPECT_TRUE(body->hasFocus());
    EXPECT_LT(body->geometry().y, 50);
    EXPECT_LT(body->geometry().x, subject->geometry().x);
    body->setText("Hi there");
    tap(KEY_TAB);
    EXPECT_TRUE(subject->hasFocus());
    subject->setText("Hello");
    const auto bounds = picker()->bounds();
    click(bounds.x() + recipient->geometry().x + 8, bounds.y() + recipient->geometry().y + 12);
    EXPECT_TRUE(recipient->hasFocus());
    settle();
    const auto emptyHeight = picker()->bounds().height();
    recipient->setText("person@out");
    settle();
    EXPECT_GT(picker()->bounds().height(), emptyHeight);
    tap(KEY_DOWN);
    tap(KEY_ENTER);
    EXPECT_EQ(recipient->text(), "person@outlook.com");
    settle();
    EXPECT_EQ(picker()->bounds().height(), emptyHeight);
    tap(KEY_ENTER);
    settle();
    EXPECT_GT(picker()->bounds().height(), emptyHeight + 50);
    subject->setText("Edited");
    settle();
    EXPECT_EQ(picker()->bounds().height(), emptyHeight);
}

TEST_F(SpotlightWindowTest, WebActionHasAKeystrokeDrivenProviderPopup)
{
    spotlight->show(shell::Spotlight::Mode::Actions);
    settle();
    auto *search = static_cast<ui::TextField *>(pickerChild("spotlight-search"));
    ASSERT_NE(search, nullptr);
    search->setText("Search the Web");
    tap(KEY_ENTER);
    settle();
    auto *terms = static_cast<ui::TextField *>(pickerChild("spotlight-action-parameter"));
    ASSERT_NE(terms, nullptr);
    EXPECT_TRUE(terms->hasFocus());
    terms->setText("Koifish");
    settle();
    const int before = picker()->bounds().height();
    tap(KEY_TAB);
    settle();
    EXPECT_GT(picker()->bounds().height(), before + 80);
    tap(KEY_DOWN);
    tap(KEY_ENTER);
    settle();
    EXPECT_EQ(picker()->bounds().height(), before);
    EXPECT_EQ(terms->text(), "Koifish");
    tap(KEY_ENTER);
    settle();
    EXPECT_GT(picker()->bounds().height(), before + 50);
}

TEST_F(SpotlightWindowTest, WheelEasesThroughAccumulatedSteps)
{
    spotlight->show(shell::Spotlight::Mode::Applications);
    settle();
    ASSERT_NE(picker(), nullptr);
    const auto bounds = picker()->bounds();
    input.onPointerMotionAbsolute(time(), {double(bounds.x() + 200), double(bounds.y() + 150)});
    const double start = iconY();
    input.onPointerAxis(time(), {0, 10}, true);
    input.onPointerAxis(time(), {0, 10}, true);
    EXPECT_DOUBLE_EQ(iconY(), start);
    render();
    render(35);
    EXPECT_LT(iconY(), start - 100);
    EXPECT_GT(iconY(), start - 200);
    render(120);
    EXPECT_NEAR(iconY(), start - 200, .01);
}

TEST_F(SpotlightWindowTest, TouchpadCoastsAndStopsOnNewInput)
{
    spotlight->show(shell::Spotlight::Mode::Applications);
    settle();
    ASSERT_NE(picker(), nullptr);
    const auto bounds = picker()->bounds();
    input.onPointerMotionAbsolute(time(), {double(bounds.x() + 200), double(bounds.y() + 150)});
    const double start = iconY();
    for (int i = 0; i < 3; ++i) {
        input.onPointerAxis(time(), {0, 9}, false);
        render(10);
    }
    EXPECT_LT(iconY(), start - 30);
    input.onPointerAxisStop(time());
    const double release = iconY();
    render(35);
    EXPECT_LT(iconY(), release - 6);
    const double coast = iconY();
    tap(KEY_LEFTSHIFT);
    render(35);
    render(35);
    EXPECT_NEAR(iconY(), coast, .01);
}
