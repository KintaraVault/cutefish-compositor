// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/menubar/media_activity.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "base/event_loop.h"
#include "core/main_loop.h"
#include "shell/controlcenter/session_services.h"
#include "shell/controlcenter/tiles.h"
#include "shell/menubar/mpris.h"
#include "shell/shell.h"
#include "shell/shell_data.h"
#include "shell/shell_host.h"
#include "shell/shell_window.h"
#include "ui/animation.h"
#include "ui/embedded_window.h"
#include "ui/keys.h"
#include "ui/painter.h"
#include "ui/path.h"
#include "ui/popup_glass.h"

namespace cutefish::shell {

static constexpr int kCardWidth = 296;
static constexpr int kCardHeight = 160;
static constexpr int kCardRadius = 24;
static constexpr int kGap = 3;

MediaActivity::MediaActivity(Shell &shell, ShellWindow &bar, MprisManager &players)
    : m_shell(shell), m_bar(bar), m_players(players)
{
    setName("media-activity");
    setTracksHover(true);
    setVisible(false);
    m_currentChanged = players.currentChanged.connect([this] { refresh(); });
    m_playerChanged = players.currentPlayerChanged.connect([this] { refresh(); });
    refresh();
}

MediaActivity::~MediaActivity()
{
    finishClosing();
}

void MediaActivity::refresh()
{
    const auto *player = m_players.current();
    const auto state = mediaState(player);
    const auto service = player ? player->service() : std::string();
    const auto track = player ? player->trackId().path : std::string();
    const bool trackChanged = service != m_service || track != m_track
            || (track.empty() && state.title != m_state.title);
    if (m_state != state && m_tile && m_expanded)
        m_tile->setState(state);
    m_state = state;
    m_track = track;
    m_service = service;
    if (state.hasPlayer) {
        setVisible(true);
        if (m_tile && m_expanded)
            m_tile->setTimeline(player->duration(), player->canSetPosition(), trackChanged);
    } else {
        m_pressed = false;
        close();
    }
    animateCapsule();
}

void MediaActivity::layout()
{
    animateCapsule();
}

void MediaActivity::animateCapsule()
{
    if (!window() || !isVisible() || (m_capsuleMotion && m_capsuleMotion->isRunning()))
        return;
    if (!m_capsuleMotion) {
        m_capsuleMotion = std::make_unique<ui::Animation>(window());
        m_capsuleMotion->setDuration(std::chrono::seconds(4));
        m_capsuleMotion->setLoops(ui::Animation::Infinite);
        m_capsuleMotion->setEasing(ui::Animation::Easing::Linear);
        m_capsuleMotion->onUpdate = [this](double t) {
            const auto approach = [](double &v, double target, double speed) {
                v += std::clamp(target - v, -speed, speed);
            };
            const double showing = m_state.hasPlayer ? 1 : 0;
            const double energy = m_state.playing ? 1 : 0;
            const double press = m_pressed && isHovered() ? 1 : 0;
            if (energy)
                m_phase = t;
            approach(m_visibility, showing, .10);
            approach(m_energy, energy, .085);
            approach(m_press, press, press ? .22 : .11);
            requestPaint();
            if (!showing && m_visibility == 0) {
                m_capsuleMotion->stop();
                setVisible(false);
                if (parent())
                    parent()->requestLayout();
            } else if (!energy && m_energy == 0 && m_visibility == showing && m_press == press) {
                m_capsuleMotion->stop();
            }
        };
    }
    m_capsuleMotion->start();
}

void MediaActivity::paint(ui::Painter &painter)
{
    if (m_visibility <= 0)
        return;
    const double scale = .87 + m_visibility * .13 - m_press * .065;
    const double width = 58 * scale;
    const double height = 24 * scale;
    const double x = (geometry().width - width) / 2;
    const double y = (geometry().height - height) / 2;
    const double open = m_openProgress;
    // A neutral filter over the actual menu bar backdrop, not a blue/black asset.
    painter.fillRoundedRect({x, y, width, height}, height / 2,
                            ui::Color::rgb(0x000000).withAlpha(m_visibility * (.64 + .08 * m_press) * (1 - open)));
    if (open > 0) {
        const auto white = ui::Color::rgb(0xffffff);
        painter.fillRoundedRect({x, y, width, height}, height / 2, white.withAlpha(open * .13));
        const double cx = x + width / 2;
        const double cy = y + height / 2;
        ui::Path chevron;
        chevron.moveTo({cx - 5, cy + 2});
        chevron.lineTo({cx, cy - 3});
        chevron.lineTo({cx + 5, cy + 2});
        painter.strokePath(chevron, white.withAlpha(open * .9), 1.7);
    }
    if (open < 1) {
        paintLiveMediaIcon(painter, {x + 7 * scale, y + 4 * scale, 16 * scale, 16 * scale}, m_visibility * (1 - open));
        if (open < .98) {
            painter.save();
            painter.multiplyOpacity(m_visibility * (1 - open));
            paintLiveMediaWave(painter, {x + width - 25 * scale, y + 5 * scale, 18 * scale, 14 * scale}, m_phase, m_energy);
            painter.restore();
        }
    }
}

void MediaActivity::hoverChanged()
{
    animateCapsule();
}

void MediaActivity::pointerPress(const ui::PointerEvent &event)
{
    if (event.button != ui::MouseButton::Left || !m_state.hasPlayer)
        return;
    m_pressed = true;
    animateCapsule();
}

void MediaActivity::pointerRelease(const ui::PointerEvent &event)
{
    if (event.button != ui::MouseButton::Left || !m_pressed)
        return;
    m_pressed = false;
    animateCapsule();
    if (!localRect().contains(event.position))
        return;
    if (m_expanded)
        close();
    else {
        if (closeOtherPopups)
            closeOtherPopups();
        open();
    }
}

void MediaActivity::open()
{
    const auto *player = m_players.current();
    const int width = std::min(kCardWidth, m_bar.geometry().width() - 2 * kGap);
    if (!player || !m_state.hasPlayer || width < 175)
        return;
    if (m_popup && m_popup->isHidden())
        finishClosing();
    if (m_popup) {
        m_expanded = true;
        m_tile->setState(m_state);
        m_tile->setTimeline(player->duration(), player->canSetPosition(), true);
        if (auto *host = popupHost()) {
            host->setInputRegion(std::nullopt);
            host->setPopupGrab(true);
        }
        animatePopup();
        return;
    }
    m_expanded = true;
    m_popupToken = std::make_shared<char>();
    const std::weak_ptr<char> token = m_popupToken;
    const auto dismiss = [this, token] {
        core::mainLoop()->post([this, token] { if (!token.expired()) close(); });
    };
    m_popup = ui::Window::createPopup(m_bar.window(), {}, true);
    ui::PopupGlass::prepare(*m_popup);
    m_popup->setScale(m_bar.scale());
    m_popup->addShortcut(ui::NoModifier, ui::key::Escape, dismiss);
    m_popup->onCloseRequested = dismiss;
    auto tile = std::make_unique<MediaTile>();
    m_tile = tile.get();
    tile->setActivityStyle();
    tile->setOpacity(0);
    tile->setState(m_state);
    tile->position = [this] { const auto *p = m_players.current(); return p ? p->position() : std::nullopt; };
    tile->pollPosition = [this] {
        if (auto *p = m_players.current(); m_expanded && p)
            p->refreshPosition();
    };
    tile->seek = [this](double value) { if (auto *p = m_players.current()) p->setPosition(value); };
    tile->onPrevious = [this] { if (auto *p = m_players.current(); p && p->canGoPrevious()) p->previous(); };
    tile->onPlay = [this] { if (auto *p = m_players.current(); p && p->canPlay()) p->play(); };
    tile->onPause = [this] { if (auto *p = m_players.current(); p && p->canPause()) p->pause(); };
    tile->onNext = [this] { if (auto *p = m_players.current(); p && p->canGoNext()) p->next(); };
    tile->setTimeline(player->duration(), player->canSetPosition(), true);
    m_popup->setContentView(std::move(tile));
    m_popup->resize({double(width), kCardHeight});
    applyPopup();
    m_popup->show();
    if (auto *host = popupHost())
        host->onPopupDismissed = [this] { close(); };
    applyPopup();
    animatePopup();
}

void MediaActivity::close()
{
    if (!m_expanded)
        return;
    m_expanded = false;
    if (m_popup->isHidden()) {
        finishClosing();
        requestPaint();
        return;
    }
    if (auto *host = popupHost()) {
        host->setPopupGrab(false);
        host->setInputRegion(base::Region());
    }
    animatePopup();
}

void MediaActivity::reanchor()
{
    if (m_popup) {
        if (m_popup->size().width > m_bar.geometry().width() - 2 * kGap)
            close();
        applyPopup();
    }
}

void MediaActivity::animatePopup()
{
    if (!m_popup)
        return;
    if (!m_popupMotion)
        m_popupMotion = std::make_unique<ui::Animation>(m_popup.get());
    m_popupMotion->stop();
    m_popupMotion->setDuration(std::chrono::milliseconds(m_expanded ? 245 : 170));
    m_popupMotion->setEasing(m_expanded ? ui::Animation::Easing::OutCubic : ui::Animation::Easing::InQuad);
    m_popupMotion->onUpdate = [this, from = m_openProgress, to = m_expanded ? 1. : 0.](double t) {
        m_openProgress = std::lerp(from, to, t);
        applyPopup();
        requestPaint();
    };
    const std::weak_ptr<char> token = m_popupToken;
    m_popupMotion->onFinished = [this, token] {
        if (!m_expanded)
            core::mainLoop()->post([this, token] {
                if (!token.expired() && !m_expanded)
                    finishClosing();
            });
    };
    m_popupMotion->start();
}

void MediaActivity::applyPopup()
{
    const auto size = m_popup->size();
    const double maxX = std::max(double(kGap), m_bar.geometry().width() - size.width - kGap);
    const double centre = mapToWindow({geometry().width / 2, 0}).x;
    const double x = std::clamp(centre - size.width / 2, double(kGap), maxX);
    ui::PopupPlacement placement;
    placement.anchorRect = {std::round(x), m_bar.geometry().height() + kGap - std::round(7 * (1 - m_openProgress)), 1, 1};
    placement.anchor = ui::PopupAnchor::TopLeft;
    placement.gravity = ui::PopupAnchor::BottomRight;
    placement.flipX = placement.flipY = placement.slide = placement.resize = false;
    m_popup->setPopupPlacement(placement);
    m_tile->setOpacity(m_openProgress);
    if (auto *host = popupHost()) {
        // Luminance is bounded for white ink, but each channel is darkened
        // equally: the blurred/refracted app or wallpaper supplies the hue.
        auto glass = popupGlass(0, 0, 0, .49);
        glass.blurSigma = 13;
        glass.bevel = 10;
        glass.depth = 5;
        glass.displacement = 7;
        glass.chroma = 0;
        glass.saturation = .88;
        glass.shadowLift = .08;
        glass.highlightRollOff = .44;
        glass.rim = .12;
        glass.backdropReflection = .38;
        glass.opacity = float(m_openProgress);
        host->setBackgroundBlur({0, 0, int(size.width), int(size.height)}, kCardRadius, glass);
    }
}

ShellWindow *MediaActivity::popupHost() const
{
    return m_shell.host()->windowFor(static_cast<ui::EmbeddedWindow *>(m_popup.get()));
}

void MediaActivity::finishClosing()
{
    m_popupToken.reset();
    m_popupMotion.reset();
    m_tile = nullptr;
    if (m_popup) {
        if (auto *host = popupHost())
            host->onPopupDismissed = nullptr;
        m_popup->hide();
    }
    m_popup.reset();
    m_openProgress = 0;
}

} // namespace cutefish::shell
