// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/settings.h"
#include "base/timer.h"
#include "render/glass.h"
#include "shell/shell.h"
#include "ui/path.h"
#include "ui/view.h"

namespace cutefish::shell {

class ControlCenter;
class ShellWindow;
class NotificationCentre;

// Live widgets belong to the desktop layer; the editor is a separate shell
// window. Positions are saved relative to the logical desktop work area.
class DesktopWidgets final : public Component
{
public:
    DesktopWidgets(Shell *shell, ControlCenter &controls, std::string file = {});
    ~DesktopWidgets() override;
    void edit(bool forCentre = false);
    void done();
    void setNotificationCentre(NotificationCentre &centre);

    class Stack : public ui::View
    {
    public:
        explicit Stack(DesktopWidgets &owner);
        double heightForWidth(double width) const override;
        void appendGlass(render::GlassParams &glass, ui::Path &mask, ui::RectF viewport) const;
        std::function<void()> onChanged;
        std::function<void()> onPresentationChanged;
    protected:
        void layout() override;
    private:
        DesktopWidgets &m_owner;
        std::weak_ptr<bool> m_alive;
        base::Subscription m_changed;
        base::Subscription m_repaint;
    };

private:
    friend class NotificationCentre;
    // First three values are the on-disk IDs used by older Koifish versions.
    enum class Kind { Clock, Calendar, Battery, WorldClock, Photos, Reminders, System, Weather };
    enum class Size { Small, Medium, Large, ExtraLarge };
    struct Widget;
    class Card;
    class Gallery;

    void updateOutput();
    void repaint();
    void add(Kind kind, Size size, std::optional<base::Point> cardOrigin = {},
             std::optional<base::Point> dropPoint = {});
    void remove(Widget *item);
    void finishRemoval(Widget *item);
    void resize(Widget *item, Size size);
    void move(Widget *item, base::Point global);
    void changed();
    void configure(Widget &item);
    void refreshWeather(Widget &item);
    void retire(std::unique_ptr<ShellWindow> window);
    void reorder(Widget *item, ui::PointF position, double width);
    void save();
    base::Rect area() const;
    base::Rect geometryFor(const Widget &item) const;
    static const char *title(Kind kind);
    static const char *sizeLabel(Size size);
    static std::vector<Size> sizes(Kind kind, bool inCentre = false);
    static base::Size dimensions(Size size, int unit);
    std::vector<std::pair<Widget *, ui::RectF>> stackLayout(double width) const;

    Shell *m_shell;
    ControlCenter &m_controls;
    base::Settings m_settings;
    base::Timer m_tick;
    base::Timer m_weatherTick;
    base::Subscription m_outputChanged;
    base::Subscription m_batteryChanged;
    std::vector<std::unique_ptr<Widget>> m_widgets;
    std::vector<std::unique_ptr<Widget>> m_exiting;
    std::unique_ptr<ShellWindow> m_gallery;
    std::unique_ptr<ShellWindow> m_configuration;
    std::vector<std::unique_ptr<ShellWindow>> m_retiring;
    Gallery *m_galleryView = nullptr;
    bool m_addToCentre = false;
    base::Subscription m_themeChanged;
    base::Signal<> m_changed;
    base::Signal<> m_repaint;
    std::function<bool(base::Point)> m_centreDrop;
    std::function<bool()> m_centreOpen;
    std::function<void(bool)> m_centreEditing;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell
