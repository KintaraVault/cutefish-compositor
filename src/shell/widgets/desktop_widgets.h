// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/settings.h"
#include "base/timer.h"
#include "shell/shell.h"

namespace cutefish::shell {

class ControlCenter;
class ShellWindow;

// Live widgets belong to the desktop layer; the editor is a separate shell
// window. Positions are saved relative to the logical desktop work area.
class DesktopWidgets final : public Component
{
public:
    DesktopWidgets(Shell *shell, ControlCenter &controls, std::string file = {});
    ~DesktopWidgets() override;
    void edit();
    void done();

private:
    enum class Kind { Clock, Calendar, Battery };
    struct Widget;
    class Card;
    class Gallery;

    void updateOutput();
    void repaint();
    void add(Kind kind, bool wide, std::optional<base::Point> global = {});
    void remove(Widget *item);
    void move(Widget *item, base::Point global);
    void save();
    base::Rect area() const;
    base::Rect geometryFor(const Widget &item) const;
    static const char *title(Kind kind);

    Shell *m_shell;
    ControlCenter &m_controls;
    base::Settings m_settings;
    base::Timer m_tick;
    base::Subscription m_outputChanged;
    base::Subscription m_batteryChanged;
    std::vector<std::unique_ptr<Widget>> m_widgets;
    std::unique_ptr<ShellWindow> m_gallery;
    Gallery *m_galleryView = nullptr;
    bool m_dark = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell
