// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The shell's windows rendered by Skia on the renderer's own GL context, all
// in the one pass an output frame starts with, and shown through their
// surfaces' textures.

#include <functional>

#include <gtest/gtest.h>

#include "compositor/compositor.h"
#include "compositor/output_scale.h"
#include "compositor/surface.h"
#include "egl_test_util.h"
#include "input/seat.h"
#include "render/gl_texture.h"
#include "scene/node.h"
#include "scene/scene.h"
#include "scene/surface_node.h"
#include "shell/shell.h"
#include "shell/shell_host.h"
#include "shell/shell_window.h"
#include "shell/window_renderer.h"
#include "toolkit/toolkit_host.h"
#include "ui/bitmap.h"
#include "ui/embedded_window.h"
#include "ui/frame_client.h"
#include "ui/painter.h"
#include "ui/view.h"
#include "wm/window.h"
#include "wm/window_manager.h"

using namespace cutefish;
using namespace cutefish::render;

namespace {

const base::Rect kOutput(0, 0, 400, 200);

class Fill : public ui::View
{
public:
    explicit Fill(ui::Color color)
        : m_color(color)
    {
    }

    void setColor(ui::Color color)
    {
        m_color = color;
        requestPaint();
    }

    void paint(ui::Painter &painter) override { painter.fillRect(localRect(), m_color); }

private:
    ui::Color m_color;
};

class ShellGpuPixels : public testing::TestWithParam<double>
{
protected:
    void SetUp() override
    {
        renderer = EglRendererTestAccess::create();
        ASSERT_TRUE(renderer);
        native = base::Size(base::roundToInt(kOutput.width() * scale()),
                            base::roundToInt(kOutput.height() * scale()));
        const EGLint attributes[] = {EGL_WIDTH, native.width(), EGL_HEIGHT, native.height(),
                                     EGL_NONE};
        const auto surface =
                eglCreatePbufferSurface(renderer->eglDisplay(), renderer->eglConfig(), attributes);
        ASSERT_NE(surface, EGL_NO_SURFACE);
        target = std::make_unique<EglRenderTarget>(renderer->eglDisplay(), surface, nullptr, native);

        compositor.setOutputScale(
                *compositor::OutputScale::fromNumerator(uint32_t(scale() * 120)));
        manager.setOutputBounds(kOutput);
        shell = std::make_unique<shell::Shell>(
                shell::ShellEnvironment {&compositor, &manager, &seat, nullptr});
    }

    void TearDown() override
    {
        if (renderer && target) {
            ASSERT_TRUE(renderer->beginFrame(target.get()));
            shell.reset();
            // Skia's objects go with the context current.
            toolkit::PanelTexture(nullptr).releaseRenderResources();
            renderer->endFrame();
        }
        shell.reset();
        seat.shutdown();
        if (renderer && target)
            renderer->releaseTarget(target.get());
        target.reset();
        renderer.reset();
    }

    double scale() const { return GetParam(); }

    // One output frame: the shell's pass, the surfaces' textures, then each
    // window's surface drawn where it is.
    ui::Bitmap frame()
    {
        renderer->setOutputContext({"out", kOutput, native, scale(), 0, ++frameCount});
        renderer->frameTime = 1000.0 * double(frameCount);
        EXPECT_TRUE(renderer->beginFrame(target.get()));
        const ui::Bitmap image = renderer->captureFrame([&] {
            renderer->clear({0, 0, 0, 0});
            compositor.updateTextures(*renderer);
            for (shell::ShellWindow *window : shell->windows()) {
                scene::SurfaceNode *node = window->surface()->node();
                if (!node->texture())
                    continue;
                const auto bounds = window->drawnBounds();
                if (node->hasBackgroundBlur())
                    renderer->drawBackgroundBlur(node->backgroundBlurRect().translated(bounds.topLeft()),
                            node->backgroundBlurRadius(), bounds, node->glass(),
                            node->backgroundBlurShape().translated(bounds.x(), bounds.y()));
                renderer->drawTextureMapped(node->texture(), bounds, 1.0f, base::AffineTransform());
            }
        });
        renderer->cancelFrame();
        return image.converted(ui::Bitmap::Format::Rgba8888Premultiplied);
    }

    uint32_t pixelAt(const ui::Bitmap &image, const base::Point &logical) const
    {
        const uint8_t *pixel = image.constScanLine(base::roundToInt(logical.y() * scale()))
                + size_t(base::roundToInt(logical.x() * scale())) * 4;
        return uint32_t(pixel[3]) << 24 | uint32_t(pixel[0]) << 16 | uint32_t(pixel[1]) << 8
                | pixel[2];
    }

    std::unique_ptr<EglRenderer> renderer;
    std::unique_ptr<EglRenderTarget> target;
    base::Size native;
    uint64_t frameCount = 0;
    scene::Scene scene;
    scene::Node *surfaces = scene.root()->addChild(std::make_unique<scene::Node>("surfaces"));
    compositor::Compositor compositor {surfaces};
    input::Seat seat {&compositor};
    wm::WindowManager manager {&compositor, &seat};
    std::unique_ptr<shell::Shell> shell;
};

} // namespace

TEST_P(ShellGpuPixels, GlassUnderlaySoftensGapsWithoutPaintingASolidPlate)
{
    class SplitBackdrop : public ui::View {
        void paint(ui::Painter &painter) override
        {
            painter.fillRect(localRect(), ui::Color::rgb(0x0000ff));
            painter.fillRect({0, 0, localRect().width / 2, localRect().height}, ui::Color::rgb(0xff0000));
        }
    };
    shell::ShellWindow wallpaper(shell.get(), wm::WindowRole::Desktop, false);
    wallpaper.setContentView(std::make_unique<SplitBackdrop>());
    wallpaper.setGeometry(kOutput);
    wallpaper.show();

    shell::ShellWindow panel(shell.get(), wm::WindowRole::Panel, false);
    panel.window()->setBackgroundColor(ui::ColorRecipe(ui::Color::rgb(0).withAlpha(0)));
    panel.setContentView(std::make_unique<ui::View>());
    panel.setGeometry({100, 10, 200, 180});
    ui::Path mask;
    mask.addRoundedRect({26, 40, 46, 100}, 12, 12);
    GlassParams material;
    material.enabled = true;
    material.blurSigma = 7.5f;
    material.lenses.push_back({{26, 40, 46, 100}, 12});
    panel.setBackgroundBlur({0, 0, 200, 180}, 12, material, mask);
    panel.show();

    const auto before = frame();
    EXPECT_EQ(0xffff0000u, pixelAt(before, {191, 100}));
    EXPECT_EQ(0xff0000ffu, pixelAt(before, {208, 100}));
    material.underlay = {{0, 0, 200, 180}, .86f};
    panel.setBackgroundBlur({0, 0, 200, 180}, 12, material, mask);
    const auto after = frame();
    EXPECT_EQ(0xffff0000u, pixelAt(after, {80, 100}));
    EXPECT_EQ(0xff0000ffu, pixelAt(after, {320, 100}));
    EXPECT_EQ(pixelAt(before, {148, 100}), pixelAt(after, {148, 100})); // the lens still reads the same scene
    EXPECT_GT(pixelAt(after, {191, 100}) & 0xff, 20u);
    EXPECT_GT((pixelAt(after, {208, 100}) >> 16) & 0xff, 20u);
    EXPECT_EQ(0u, (pixelAt(after, {191, 100}) >> 8) & 0xff); // no tinted fill
    EXPECT_EQ(0xffu, pixelAt(after, {191, 100}) >> 24);
    EXPECT_NEAR(255, (pixelAt(after, {101, 100}) >> 16) & 0xff, 3);
}

TEST_P(ShellGpuPixels, FrostedSheetDiffusesTheSceneWithoutChangingNearbyLenses)
{
    class CatalogBackdrop : public ui::View {
        void paint(ui::Painter &painter) override
        {
            painter.fillRect(localRect(), ui::Color::rgb(0x272727));
            painter.fillRect({200, 0, 200, 200}, ui::Color::rgb(0x164fa5));
            painter.fillRect({0, 148, 400, 35}, ui::Color::rgb(0xffffff));
        }
    };
    shell::ShellWindow wallpaper(shell.get(), wm::WindowRole::Desktop, false);
    wallpaper.setContentView(std::make_unique<CatalogBackdrop>());
    wallpaper.setGeometry(kOutput);
    wallpaper.show();

    shell::ShellWindow panel(shell.get(), wm::WindowRole::Panel, false);
    panel.window()->setBackgroundColor(ui::ColorRecipe(ui::Color::rgb(0).withAlpha(0)));
    panel.setContentView(std::make_unique<ui::View>());
    panel.setGeometry(kOutput);
    ui::Path mask;
    mask.addRoundedRect({20, 14, 270, 180}, 22, 22);
    mask.addRoundedRect({308, 35, 65, 50}, 20, 20);
    GlassParams material;
    material.enabled = true;
    material.blurSigma = 7.5f;
    material.lenses = {{{20, 14, 270, 180}, 22, .48f}, {{308, 35, 65, 50}, 20}};
    const auto draw = [&] {
        panel.setBackgroundBlur(kOutput, 22, material, mask);
        return frame();
    };
    panel.show();
    const auto standard = draw();
    material.lenses.front().finish = GlassFinish::FrostedLight;
    const auto light = draw();
    material.lenses.front().finish = GlassFinish::FrostedDark;
    const auto dark = draw();

    EXPECT_EQ(pixelAt(standard, {344, 56}), pixelAt(light, {344, 56}));
    EXPECT_EQ(pixelAt(standard, {344, 56}), pixelAt(dark, {344, 56}));
    EXPECT_EQ(pixelAt(standard, {303, 56}), pixelAt(light, {303, 56}));
    EXPECT_EQ(0xff272727u, pixelAt(light, {20, 14})); // the mask, not a rectangular quad
    const auto gray = [](uint32_t color) { return int(color & 255); };
    const auto neutral = pixelAt(light, {66, 66});
    EXPECT_EQ(gray(neutral), int((neutral >> 8) & 255));
    EXPECT_EQ(gray(neutral), int((neutral >> 16) & 255));
    EXPECT_GT(gray(neutral), 135);
    EXPECT_LT(gray(pixelAt(dark, {66, 66})), 65);
    for (const auto *image : {&light, &dark}) {
        const auto blue = pixelAt(*image, {263, 64});
        EXPECT_GT(gray(blue), int((blue >> 16) & 255) + 24);
    }
    const auto delta = [&](const ui::Bitmap &image) {
        return gray(pixelAt(image, {70, 162})) - gray(pixelAt(image, {70, 120}));
    };
    EXPECT_GT(delta(light), 0); // the bright scene still contributes to the material
    EXPECT_LT(delta(light) * 2, delta(standard));
    EXPECT_LT(delta(dark) * 2, delta(standard));
}

TEST_P(ShellGpuPixels, GlassCrestReflectsTheBackdropOnDarkAndColoredScenes)
{
    shell::ShellWindow wallpaper(shell.get(), wm::WindowRole::Desktop, false);
    auto fill = std::make_unique<Fill>(ui::Color::rgb(0x080808));
    auto *color = fill.get();
    wallpaper.setContentView(std::move(fill));
    wallpaper.setGeometry(kOutput);
    wallpaper.show();

    shell::ShellWindow panel(shell.get(), wm::WindowRole::Panel, false);
    panel.window()->setBackgroundColor(ui::ColorRecipe(ui::Color::rgb(0).withAlpha(0)));
    panel.setContentView(std::make_unique<ui::View>());
    panel.setGeometry({100, 40, 200, 120});
    ui::Path mask;
    mask.addRoundedRect({0, 0, 200, 120}, 30, 30);
    GlassParams material;
    material.enabled = true;
    material.blurSigma = 7.5f;
    material.rim = .94f;
    material.lightAngle = 237;
    material.backdropReflection = 1;
    material.tintAlpha = .024f;
    panel.setBackgroundBlur({0, 0, 200, 120}, 30, material, mask);
    panel.show();

    const auto rgb = [](uint32_t pixel) {
        return std::array<int, 3>{int((pixel >> 16) & 255), int((pixel >> 8) & 255), int(pixel & 255)};
    };
    const auto crest = [&](const ui::Bitmap &image) {
        auto best = rgb(pixelAt(image, {200, 40}));
        for (int y = base::roundToInt(40 * scale()); y < base::roundToInt(43 * scale()); ++y) {
            const auto *sample = image.constScanLine(y) + 4 * base::roundToInt(200 * scale());
            const std::array<int, 3> next {sample[0], sample[1], sample[2]};
            if (next[0] + next[1] + next[2] > best[0] + best[1] + best[2])
                best = next;
        }
        return best;
    };
    const auto gray = frame();
    const auto neutral = crest(gray);
    const auto body = rgb(pixelAt(gray, {200, 45}));
    EXPECT_GT(neutral[0], body[0] + 18);
    EXPECT_EQ(neutral[0], neutral[1]);
    EXPECT_EQ(neutral[1], neutral[2]);
    EXPECT_EQ(0xff080808u, pixelAt(gray, {200, 38}));

    color->setColor(ui::Color::rgb(0x102060));
    const auto blue = crest(frame());
    EXPECT_GT(blue[2], blue[0] + 24);
    color->setColor(ui::Color::rgb(0x602010));
    const auto red = crest(frame());
    EXPECT_GT(red[0], red[2] + 24);
}

TEST_P(ShellGpuPixels, NestedLensRoundsItsCornersAndClipsToItsViewport)
{
    shell::ShellWindow wallpaper(shell.get(), wm::WindowRole::Desktop, false);
    wallpaper.setContentView(std::make_unique<Fill>(ui::Color::rgb(0x192d45)));
    wallpaper.setGeometry(kOutput);
    wallpaper.show();

    shell::ShellWindow sheet(shell.get(), wm::WindowRole::Panel, false);
    sheet.window()->setBackgroundColor(ui::ColorRecipe(ui::Color::rgb(0).withAlpha(0)));
    sheet.setContentView(std::make_unique<ui::View>());
    sheet.setGeometry({60, 20, 240, 160});
    ui::Path mask;
    mask.addRoundedRect({0, 0, 240, 160}, 20, 20);
    GlassParams material;
    material.enabled = true;
    material.backdropReflection = 1;
    material.tintAlpha = .55f;
    material.lenses.push_back({{0, 0, 240, 160}, 20});
    sheet.setBackgroundBlur({0, 0, 240, 160}, 20, material, mask);
    sheet.show();
    const auto before = frame();

    material.lenses.push_back({{40, 40, 124, 74}, 29, .15f, 1, base::RectF(40, 40, 90, 74)});
    sheet.setBackgroundBlur({0, 0, 240, 160}, 20, material, mask);
    const auto after = frame();
    EXPECT_NE(pixelAt(before, {138, 93}), pixelAt(after, {138, 93}));
    EXPECT_EQ(pixelAt(before, {102, 62}), pixelAt(after, {102, 62}));
    EXPECT_EQ(pixelAt(before, {204, 93}), pixelAt(after, {204, 93}));
}

TEST_P(ShellGpuPixels, WindowsRenderOnTheGpuInOnePassAndOnlyWhatChanged)
{
    shell::ShellWindow panel(shell.get(), wm::WindowRole::Panel);
    auto red = std::make_unique<Fill>(ui::Color::rgb(0xff0000));
    panel.setContentView(std::move(red));
    panel.setGeometry(base::Rect(0, 0, 200, 40));
    panel.show();
    shell::ShellWindow osd(shell.get(), wm::WindowRole::Osd);
    auto blue = std::make_unique<Fill>(ui::Color::rgb(0x0000ff));
    Fill *osdFill = blue.get();
    osd.setContentView(std::move(blue));
    osd.setGeometry(base::Rect(300, 100, 100, 50));
    osd.show();
    EXPECT_EQ(scale(), panel.scale());

    const uint64_t passes = shell->renderer()->passCount();
    ui::Bitmap image = frame();
    EXPECT_EQ(passes + 1, shell->renderer()->passCount());
    EXPECT_EQ(2u, shell->renderer()->lastPass().size());
    EXPECT_TRUE(panel.lastFrameOnGpu());
    EXPECT_TRUE(osd.lastFrameOnGpu());
    auto *texture = dynamic_cast<GlTexture *>(osd.surface()->node()->texture());
    ASSERT_NE(nullptr, texture);
    EXPECT_EQ(base::Size(base::roundToInt(100 * scale()), base::roundToInt(50 * scale())),
              texture->size());
    EXPECT_EQ(0xffff0000u, pixelAt(image, {100, 20}));
    EXPECT_EQ(0xff0000ffu, pixelAt(image, {350, 125}));
    EXPECT_EQ(0u, pixelAt(image, {250, 20}));

    // One window changed: rendered alone, into the same texture, the other
    // kept as it was.
    osdFill->setColor(ui::Color::rgb(0x00ff00));
    image = frame();
    ASSERT_EQ(1u, shell->renderer()->lastPass().size());
    EXPECT_EQ(&osd, shell->renderer()->lastPass().front());
    EXPECT_EQ(texture, osd.surface()->node()->texture());
    EXPECT_EQ(0xff00ff00u, pixelAt(image, {350, 125}));
    EXPECT_EQ(0xffff0000u, pixelAt(image, {100, 20}));

    // A new size takes a new texture in the middle of the pass; the batch
    // around the other window's render survives it.
    osd.setSize(base::Size(120, 60));
    osdFill->setColor(ui::Color::rgb(0xffffff));
    panel.window()->contentView()->requestPaint();
    image = frame();
    EXPECT_EQ(2u, shell->renderer()->lastPass().size());
    EXPECT_EQ(0xffffffffu, pixelAt(image, {390, 150}));
    EXPECT_EQ(0xffff0000u, pixelAt(image, {100, 20}));
}

TEST_P(ShellGpuPixels, APopupGrowingInItsOwnFrameShowsItsNewSizeInThatFrame)
{
    // Runs once on the next frame of its window.
    class Step : public ui::FrameClient
    {
    public:
        using ui::FrameClient::FrameClient;

        void start() { startTicking(); }

        std::function<void()> onTick;

    protected:
        void tick(uint64_t) override
        {
            stopTicking();
            onTick();
        }
    };

    shell::ShellWindow panel(shell.get(), wm::WindowRole::Panel);
    panel.setContentView(std::make_unique<Fill>(ui::Color::rgb(0xff0000)));
    panel.setGeometry(base::Rect(0, 0, 400, 20));
    panel.show();
    ui::PopupPlacement placement;
    placement.anchorRect = {100, 20, 1, 1};
    placement.anchor = ui::PopupAnchor::TopLeft;
    placement.gravity = ui::PopupAnchor::BottomRight;
    placement.flipX = placement.flipY = placement.slide = false;
    std::unique_ptr<ui::Window> popup = ui::Window::createPopup(panel.window(), placement, false);
    auto content = std::make_unique<Fill>(ui::Color::rgb(0x0000ff));
    Fill *fill = content.get();
    popup->setContentView(std::move(content));
    popup->resize({100, 40});
    popup->show();
    shell::ShellWindow *window = shell->host()->windowFor(static_cast<ui::EmbeddedWindow *>(popup.get()));
    ASSERT_NE(nullptr, window);
    window->setBackgroundBlur(base::Rect(0, 0, 100, 40), 8);
    ui::Bitmap image = frame();
    EXPECT_TRUE(window->lastFrameOnGpu());
    EXPECT_EQ(0xff0000ffu, pixelAt(image, {150, 50}));
    EXPECT_EQ(0u, pixelAt(image, {150, 90}));

    // Its own animation grows it, recolours it and moves its glass, as a
    // status panel's height does: all of it in this frame, drawn once.
    Step step(popup.get());
    step.onTick = [&] {
        popup->resize({100, 80});
        fill->setColor(ui::Color::rgb(0x00ff00));
        window->setBackgroundBlur(base::Rect(0, 0, 100, 80), 8);
    };
    step.start();
    image = frame();
    EXPECT_TRUE(window->lastFrameOnGpu());
    EXPECT_EQ(base::Rect(100, 20, 100, 80), window->geometry());
    EXPECT_EQ(base::Size(base::roundToInt(100 * scale()), base::roundToInt(80 * scale())),
              window->surface()->node()->texture()->size());
    EXPECT_EQ(base::Rect(0, 0, 100, 80), window->surface()->backgroundBlurRect());
    EXPECT_EQ(0xff00ff00u, pixelAt(image, {150, 30}));
    EXPECT_EQ(0xff00ff00u, pixelAt(image, {150, 90}));
    popup.reset();
}

TEST_P(ShellGpuPixels, SeparateGlassLensesShareTheBackdropAndKeepTheirGutterClear)
{
    const base::Rect bounds {50, 40, 180, 60};
    ui::Path mask;
    mask.addRoundedRect({50, 40, 60, 60}, 30, 30);
    mask.addRoundedRect({170, 40, 60, 60}, 30, 30);
    GlassParams glass;
    glass.enabled = true;
    glass.tintAlpha = .25f;
    glass.lenses = {{{0, 0, 60, 60}, 30}, {{120, 0, 60, 60}, 30, .75f, .5f}};

    renderer->setOutputContext({"out", kOutput, native, scale(), 0, ++frameCount});
    ASSERT_TRUE(renderer->beginFrame(target.get()));
    const auto image = renderer->captureFrame([&] {
        renderer->clear({0, 0, 0, 1});
        renderer->drawBackgroundBlur(bounds, 30, bounds, glass, mask);
    }).converted(ui::Bitmap::Format::Rgba8888Premultiplied);
    renderer->cancelFrame();
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(0xff000000u, pixelAt(image, {140, 70})); // unfiltered gutter
    EXPECT_EQ(0xff000000u, pixelAt(image, {52, 42})); // rounded corner
    const auto left = pixelAt(image, {80, 70});
    const auto right = pixelAt(image, {200, 70});
    EXPECT_GT(left, 0xff000000u);
    EXPECT_GT(right, left); // .75 tint at .5 opacity, versus .25 at full opacity
    EXPECT_LT(right, 0xffc0c0c0u);
}

INSTANTIATE_TEST_SUITE_P(Scales, ShellGpuPixels, testing::Values(1.0, 1.5));
