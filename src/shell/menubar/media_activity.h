// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_MEDIA_ACTIVITY_H_
#define CUTEFISH_SHELL_MENUBAR_MEDIA_ACTIVITY_H_

#include <memory>

#include "base/signal.h"
#include "shell/controlcenter/control_center_services.h"
#include "ui/view.h"

namespace cutefish::ui { class Animation; }

namespace cutefish::shell {

class MediaTile;
class MprisManager;
class Shell;
class ShellWindow;

// The menu bar's single Live Activity, backed by the session's current MPRIS player.
class MediaActivity final : public ui::View
{
public:
    static constexpr int kWidth = 68;

    MediaActivity(Shell &shell, ShellWindow &bar, MprisManager &players);
    ~MediaActivity() override;

    bool isOpen() const { return m_expanded; }
    void close();
    void reanchor();
    std::function<void()> closeOtherPopups;
    bool acceptsPointer() const override { return m_state.hasPlayer; }

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;
    void hoverChanged() override;
    void pointerPress(const ui::PointerEvent &event) override;
    void pointerRelease(const ui::PointerEvent &event) override;

private:
    void refresh();
    void open();
    void finishClosing();
    void animateCapsule();
    void animatePopup();
    void applyPopup();
    ShellWindow *popupHost() const;

    Shell &m_shell;
    ShellWindow &m_bar;
    MprisManager &m_players;
    MediaState m_state;
    std::string m_track;
    std::string m_service;
    std::unique_ptr<ui::Window> m_popup;
    MediaTile *m_tile = nullptr;
    std::unique_ptr<ui::Animation> m_capsuleMotion;
    std::unique_ptr<ui::Animation> m_popupMotion;
    std::shared_ptr<char> m_popupToken;
    double m_phase = 0;
    double m_energy = 0;
    double m_visibility = 0;
    double m_press = 0;
    double m_openProgress = 0;
    bool m_pressed = false;
    bool m_expanded = false;
    base::Subscription m_currentChanged;
    base::Subscription m_playerChanged;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_MEDIA_ACTIVITY_H_
