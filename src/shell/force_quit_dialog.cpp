// SPDX-License-Identifier: BSD-3-Clause
#include "shell/force_quit_dialog.h"

#include "base/event_loop.h"
#include "base/i18n.h"
#include "compositor/surface.h"
#include "core/main_loop.h"
#include "shell/shell.h"
#include "shell/common/task_manager.h"
#include "shell/shell_window.h"
#include "ui/color_provider.h"
#include "ui/cell_layout.h"
#include "ui/design_metrics.h"
#include "ui/dialog_window.h"
#include "ui/embedded_window.h"
#include "ui/icon.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/layout.h"
#include "ui/list_view.h"
#include "ui/painter.h"
#include "ui/window_header.h"
#include "wm/shell_surface.h"
#include "wm/window.h"
#include "wm/window_manager.h"

#include <algorithm>
#include <map>
#include <optional>
#include <unistd.h>

namespace cutefish::shell {
using i18n::tr;
using namespace std::chrono_literals;

struct ForceQuitDialog::Entry {
    uint64_t id;
    int64_t pid;
    std::string applicationId;
    std::string name;
    std::string icon;
    bool stalled;
    bool operator==(const Entry &) const = default;
};

class ForceQuitDialog::Content final : public ui::ColumnLayout
{
    class AppList final : public ui::ListView
    {
    protected:
        void paint(ui::Painter &painter) override
        {
            const auto color = ui::color(ui::ColorId::Text);
            painter.drawSmoothRectangle(localRect(), ui::design::menuItemRadius, color.withAlpha(.055));
        }
    };

public:
    explicit Content(ForceQuitDialog &dialog)
    {
        setSpacing(12);
        // Keep the close control relative to the window corner, not the
        // padded body of the dialog.
        auto *header = addChild<ui::WindowHeader>(ui::window_controls::kDialogHeaderHeight);
        header->setCloseOnly(true);
        header->setName("forceQuitHeader");
        header->setLayoutHints({.fillWidth = true});
        auto *titleLayout = header->setContent<ui::RowLayout>();
        titleLayout->setPadding({8, 0, ui::design::dialogInset, 0});
        auto *title = titleLayout->addChild<ui::Label>(tr("Force Quit Applications"));
        title->setWeight(600);
        title->setVerticallyCentered(true);
        title->setElide(ui::Label::Elide::Right);
        title->setLayoutHints({.fillWidth = true, .fillHeight = true});

        auto *body = addChild<ui::ColumnLayout>();
        body->setLayoutHints({.fillWidth = true, .fillHeight = true});
        body->setPadding({ui::design::dialogInset, 0, ui::design::dialogInset, ui::design::dialogInset});
        body->setSpacing(12);
        m_message = body->addChild<ui::Label>();
        m_message->setWrap(true);
        m_message->setLayoutHints({.fillWidth = true});
        m_list = body->addChild<AppList>();
        m_list->setName("forceQuitApplications");
        m_list->setLayoutHints({.fillWidth = true, .fillHeight = true, .preferredHeight = 152});
        m_list->setRowHeight(ui::design::sidebarRowHeight);
        m_list->setSpacing(2);
        m_list->createItem = [this] { return std::make_unique<Row>(*this); };
        m_list->bindItem = [this](ui::View *view, size_t i) {
            static_cast<Row *>(view)->bind(rows[i], selected && rows[i].id == *selected);
        };
        auto *actions = body->addChild<ui::RowLayout>();
        actions->setSpacing(8);
        actions->setLayoutHints({.horizontalAlignment = ui::Alignment::End});
        cancel = actions->addChild<ui::DialogButton>(tr("Close"));
        cancel->setName("forceQuitCancel");
        cancel->onClicked = [this, &dialog] {
            if (pending) {
                pending.reset();
                refresh();
                return;
            }
            dialog.close();
        };
        quit = actions->addChild<ui::DialogButton>(tr("Force Quit"), true);
        quit->setName("forceQuitAction");
        quit->setDestructive(true);
        quit->onClicked = [&dialog] {
            if (dialog.m_content->pending)
                dialog.confirm();
            else
                dialog.request();
        };
        refresh();
    }

    void refresh()
    {
        m_message->setText(pending
            ? i18n::format(tr("Force quit “{app}”? Unsaved changes will be lost."), {{"app", pending->name}})
            : rows.empty() ? tr("There are no applications to force quit.")
                           : tr("If an application isn't responding, select it and click Force Quit."));
        cancel->setText(pending ? tr("Cancel") : tr("Close"));
        m_list->setEnabled(!pending);
        m_list->setRowCount(rows.size());
        m_list->reload();
        quit->setEnabled(selected.has_value());
    }

    void move(int step)
    {
        if (pending || rows.empty())
            return;
        const auto current = std::ranges::find_if(rows, [this](const auto &row) { return row.id == selected; });
        const auto i = current == rows.end() ? (step < 0 ? rows.size() - 1 : 0)
                     : size_t(std::clamp(int(current - rows.begin()) + step, 0, int(rows.size()) - 1));
        selected = rows[i].id;
        refresh();
        m_list->scrollToRow(i);
    }

    std::optional<Entry> pending;
    std::optional<uint64_t> selected;
    std::vector<Entry> rows;
    ui::DialogButton *cancel;
    ui::DialogButton *quit;

private:
    class Row : public ui::View
    {
    public:
        explicit Row(Content &owner) : m_owner(owner)
        {
            m_icon = addChild<ui::ThemeIcon>("application-x-desktop", 20);
            m_label = addChild<ui::Label>();
            m_label->setVerticallyCentered(true);
            m_label->setElide(ui::Label::Elide::Middle);
        }
        void bind(const Entry &entry, bool selected)
        {
            m_id = entry.id;
            m_selected = selected;
            m_icon->setIconName(entry.icon);
            m_label->setText(entry.stalled ? i18n::format(tr("{app} (Not Responding)"), {{"app", entry.name}})
                                           : entry.name);
            m_label->setColor(selected ? ui::ColorId::HighlightedText : ui::ColorId::Text);
            requestPaint();
        }
    protected:
        bool acceptsPointer() const override { return true; }
        void layout() override
        {
            m_icon->setGeometry({8, (localRect().height - 20) / 2, 20, 20});
            m_label->setGeometry(localRect().adjusted(36, 0, -8, 0));
        }
        void paint(ui::Painter &painter) override
        {
            if (m_selected)
                painter.drawSmoothRectangle(localRect(), ui::design::menuItemRadius, ui::color(ui::ColorId::MenuHighlight));
        }
        void pointerPress(const ui::PointerEvent &event) override
        {
            if (event.button == ui::MouseButton::Left) {
                m_owner.selected = m_id;
                m_owner.refresh();
            }
        }
    private:
        Content &m_owner;
        ui::ThemeIcon *m_icon;
        ui::Label *m_label;
        uint64_t m_id = 0;
        bool m_selected = false;
    };

    ui::Label *m_message;
    ui::ListView *m_list;
};

ForceQuitDialog::ForceQuitDialog(Shell *shell) : m_shell(shell)
{
    m_refresh.onTimeout = [this] { update(); };
}

ForceQuitDialog::~ForceQuitDialog() = default;

std::vector<ForceQuitDialog::Entry> ForceQuitDialog::entries()
{
    std::vector<Entry> result;
    std::map<std::pair<int64_t, std::string>, size_t> groups;
    for (auto *window : m_shell->windowManager()->windows()) {
        if (window->isPopup() || window->isShellWindow() || window->skipTaskbar()
            || window->processId() == getpid() || !window->rootSurface() || !window->rootSurface()->isMapped()
            || (window->applicationId().empty() && window->title().empty())) {
            continue;
        }
        auto desktop = m_shell->tasks().entryForWindow(window->id());
        Entry value {window->id(), window->processId(), window->applicationId(),
                     desktop ? desktop->name : window->applicationId(),
                     desktop && !desktop->icon.empty() ? desktop->icon : "application-x-desktop",
                     !window->isResponsive()};
        if (value.name.empty())
            value.name = window->title();
        const auto key = std::pair(value.pid, value.applicationId);
        if (value.pid > 0) {
            const auto [it, inserted] = groups.emplace(key, result.size());
            if (!inserted) {
                result[it->second].stalled |= value.stalled;
                continue;
            }
        }
        result.push_back(std::move(value));
    }
    std::ranges::sort(result, {}, &Entry::name);
    return result;
}

void ForceQuitDialog::show()
{
    if (m_window) {
        m_window->activate();
        return;
    }
    m_window = std::make_unique<ShellWindow>(m_shell, wm::WindowRole::Normal);
    m_window->setDialog();
    m_window->setSize(base::Size(388, 316));
    m_window->surface()->setAlwaysOnTop(true);
    m_window->shellSurface()->setTitle(tr("Force Quit Applications"));
    m_window->onCloseRequested = [this] { close(); };
    m_window->window()->onCloseRequested = [this] { close(); };
    m_window->window()->setBackgroundColor(ui::ColorId::PanelBackground);
    auto content = std::make_unique<Content>(*this);
    m_content = content.get();
    m_window->setContentView(std::move(content));
    auto *window = m_window->window();
    window->addShortcut(ui::NoModifier, ui::key::Escape, [this] {
        m_content->cancel->onClicked();
    });
    window->addShortcut(ui::NoModifier, ui::key::Up, [this] { m_content->move(-1); });
    window->addShortcut(ui::NoModifier, ui::key::Down, [this] { m_content->move(1); });
    update();
    m_window->show();
    m_window->activate();
    m_content->cancel->setFocus();
    m_refresh.start(250ms);
}

void ForceQuitDialog::update()
{
    auto rows = entries();
    if (m_content->rows == rows)
        return;
    const auto exists = [&](uint64_t id) {
        return std::ranges::any_of(rows, [id](const auto &row) { return row.id == id; });
    };
    if (m_content->pending && !exists(m_content->pending->id))
        m_content->pending.reset();
    if (m_content->selected && !exists(*m_content->selected))
        m_content->selected.reset();
    m_content->rows = std::move(rows);
    m_content->refresh();
}

void ForceQuitDialog::request()
{
    if (!m_content->selected)
        return;
    const auto row = std::ranges::find_if(m_content->rows, [this](const auto &entry) {
        return entry.id == m_content->selected;
    });
    if (row == m_content->rows.end())
        return;
    m_content->pending = *row;
    m_content->refresh();
    m_content->cancel->setFocus();
}

void ForceQuitDialog::confirm()
{
    auto target = std::exchange(m_content->pending, std::nullopt);
    if (target) {
        auto *manager = m_shell->windowManager();
        auto *window = manager->windowById(target->id);
        if (window && window->processId() != getpid() && window->processId() == target->pid
            && window->applicationId() == target->applicationId && !window->isPopup()
            && !window->isShellWindow() && !window->skipTaskbar()
            && window->rootSurface() && window->rootSurface()->isMapped()) {
            manager->forceQuit(window);
        }
        update();
        m_content->refresh();
    }
}

void ForceQuitDialog::close()
{
    if (!m_window)
        return;
    m_refresh.stop();
    m_window->hide();
    std::shared_ptr<ShellWindow> gone(std::move(m_window));
    m_content = nullptr;
    core::mainLoop()->post([gone] {});
}

} // namespace cutefish::shell
