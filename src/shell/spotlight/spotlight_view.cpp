// SPDX-License-Identifier: BSD-3-Clause
#include "shell/spotlight/spotlight_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>

#include "base/i18n.h"
#include "base/paths.h"
#include "base/strings.h"
#include "base/transform.h"
#include "base/url.h"
#include "input/seat.h"
#include "shell/clipboard/clipboard_history.h"
#include "shell/common/app_launcher.h"
#include "shell/common/task_manager.h"
#include "shell/common/text_match.h"
#include "shell/dock/dock.h"
#include "shell/shell_data.h"
#include "shell/spotlight/spotlight_preview.h"
#include "ui/drag.h"
#include "ui/image_view.h"
#include "ui/icon.h"
#include "ui/keys.h"
#include "ui/painter.h"
#include "ui/path.h"
#include "ui/text_field.h"
#include "ui/text_layout.h"
#include "ui/theme.h"
#include "ui/window.h"

using cutefish::i18n::tr;

namespace cutefish::shell {
namespace {

using Mode = Spotlight::Mode;
using spotlight::Category;
using spotlight::FileType;
constexpr int kChipBand = 34;
constexpr int kRadius = 24;
constexpr int kGap = 8;
constexpr int kColumns = 5;
constexpr double kBodyInset = 7;

std::string nameFor(Mode mode)
{
    switch (mode) {
    case Mode::Search: return tr("Spotlight Search");
    case Mode::Applications: return tr("Applications");
    case Mode::Files: return tr("Files");
    case Mode::Actions: return tr("Actions");
    case Mode::Clipboard: return tr("Clipboard");
    }
    return {};
}

std::string timeLabel(int64_t seconds, const char *format)
{
    const time_t time = time_t(seconds);
    struct tm local {};
    char output[64] {};
    if (localtime_r(&time, &local))
        std::strftime(output, sizeof(output), format, &local);
    return output;
}

constexpr std::array searchEngines{
    std::pair{"DuckDuckGo", "https://duckduckgo.com/?q="},
    std::pair{"Google", "https://www.google.com/search?q="},
    std::pair{"Wikipedia", "https://en.wikipedia.org/w/index.php?search="},
};

void plate(ui::Painter &p, ui::RectF box, double radius, ui::Color color)
{
    p.drawRoundedRect(box, {radius, 0, ui::Corners::Circular}, color);
}

ui::FontSpec font(double px, bool bold = false)
{
    return {ui::Theme::current().fontFamily, px * 0.75, bold};
}

void text(ui::Painter &p, const std::string &value, ui::RectF box, double size, ui::Color color,
          bool centered = false, bool bold = false)
{
    if (value.empty() || box.width <= 0)
        return;
    const auto face = font(size, bold);
    ui::TextLayout layout(value, face, p.scale());
    layout.setWrap(ui::TextLayout::Wrap::None);
    const auto shape = [&] { layout.beginLayout(); layout.createLine(100000, {}); layout.endLayout(); };
    shape();
    if (layout.maximumWidth() > box.width) {
        const double ellipsis = ui::FontMetrics(face, p.scale()).horizontalAdvanceF("…");
        const auto cut = layout.xToCursor(0, std::max(0.0, box.width - ellipsis - 2));
        layout.setText(value.substr(0, cut) + "…");
        shape();
    }
    const auto &line = layout.lineAt(0);
    const double x = box.x + (centered ? std::max(0.0, (box.width - layout.maximumWidth()) / 2) : 0);
    layout.setDefaultColor(color);
    p.save();
    p.clip(box);
    layout.draw(p, {x, box.y + (box.height - line.ascent - line.descent) / 2});
    p.restore();
}

enum class Symbol { Search, Applications, Files, Actions, Clipboard, More };

// A single optical grid, with rounded strokes and no doubled edges at overlaps.
// These use logical coordinates and the current theme ink at any output scale.
void glyph(ui::Painter &p, Symbol symbol, ui::PointF centre, ui::Color ink, double size = 24)
{
    if (size <= 0)
        return;
    ui::Path path;
    switch (symbol) {
    case Symbol::Search:
        path.addEllipse({2.6, 2.6, 14.8, 14.8});
        path.moveTo(15.6, 15.6); path.lineTo(21.4, 21.4);
        break;
    case Symbol::Applications:
        path.moveTo(5.7, 20.3); path.lineTo(6.4, 19);
        path.moveTo(8, 16.1); path.lineTo(14.8, 3.9);
        path.moveTo(9.1, 3.9); path.lineTo(10.6, 6.6);
        path.moveTo(13.3, 11.3); path.lineTo(18.3, 20.3);
        path.moveTo(2.8, 16.1); path.lineTo(12.4, 16.1);
        path.moveTo(16, 16.1); path.lineTo(21.2, 16.1);
        break;
    case Symbol::Files:
        path.moveTo(2.1, 18.6); path.lineTo(2.1, 6.6); path.quadTo(2.1, 4.7, 4.1, 4.7);
        path.lineTo(8.2, 4.7); path.quadTo(9.1, 4.7, 9.8, 5.5);
        path.lineTo(11.1, 6.9); path.lineTo(19.9, 6.9); path.quadTo(21.9, 6.9, 21.9, 8.9);
        path.lineTo(21.9, 18.6); path.quadTo(21.9, 20.6, 19.9, 20.6);
        path.lineTo(4.1, 20.6); path.quadTo(2.1, 20.6, 2.1, 18.6); path.close();
        path.moveTo(5.2, 9.9); path.lineTo(18.8, 9.9);
        break;
    case Symbol::Actions:
        path.moveTo(10.3, 2.9); path.quadTo(12, 1.8, 13.7, 2.9);
        path.lineTo(20.3, 7.4); path.quadTo(22.4, 8.8, 20.3, 10.2);
        path.lineTo(13.7, 14.7); path.quadTo(12, 15.8, 10.3, 14.7);
        path.lineTo(3.7, 10.2); path.quadTo(1.6, 8.8, 3.7, 7.4); path.close();
        path.moveTo(6.5, 12.1); path.lineTo(3.7, 14); path.quadTo(1.6, 15.4, 3.7, 16.8);
        path.lineTo(10.3, 21.3); path.quadTo(12, 22.4, 13.7, 21.3);
        path.lineTo(20.3, 16.8); path.quadTo(22.4, 15.4, 20.3, 14); path.lineTo(17.5, 12.1);
        break;
    case Symbol::Clipboard:
        path.moveTo(7.5, 7); path.lineTo(7.5, 3.8); path.quadTo(7.5, 2, 9.4, 2);
        path.lineTo(14.8, 2); path.lineTo(20.2, 7.4); path.lineTo(20.2, 15.1);
        path.quadTo(20.2, 17, 18.3, 17); path.lineTo(16.3, 17);
        path.moveTo(14.8, 2.4); path.lineTo(14.8, 6.1); path.quadTo(14.8, 7.4, 16.1, 7.4); path.lineTo(19.8, 7.4);
        path.moveTo(3.8, 20.1); path.lineTo(3.8, 8.9); path.quadTo(3.8, 7, 5.7, 7);
        path.lineTo(11.1, 7); path.lineTo(16.3, 12.4); path.lineTo(16.3, 20.1);
        path.quadTo(16.3, 22, 14.4, 22); path.lineTo(5.7, 22); path.quadTo(3.8, 22, 3.8, 20.1); path.close();
        path.moveTo(11.1, 7.4); path.lineTo(11.1, 11.1); path.quadTo(11.1, 12.4, 12.4, 12.4); path.lineTo(15.9, 12.4);
        break;
    case Symbol::More:
        path.addEllipse({3.2, 3.2, 17.6, 17.6});
        break;
    }
    p.save();
    p.translate({centre.x - size / 2, centre.y - size / 2});
    const double scale = size / 24;
    if (scale != 1) {
        base::AffineTransform transform;
        transform.scale(scale, scale);
        path = path.transformed(transform);
    }
    p.strokePath(path, ink, (symbol == Symbol::Applications ? 2.05 : 1.8) * scale);
    if (symbol == Symbol::More) {
        for (double x : {7.8, 12.0, 16.2})
            plate(p, {(x - 1) * scale, 11 * scale, 2 * scale, 2 * scale}, scale, ink);
    }
    p.restore();
}

} // namespace

class SpotlightView::Field final : public ui::TextField
{
public:
    explicit Field(SpotlightView *owner) : m_owner(owner) {}
protected:
    bool keyPress(const ui::KeyEvent &event) override
    {
        return m_owner->key(event, this) || ui::TextField::keyPress(event);
    }
    void pointerPress(const ui::PointerEvent &event) override
    {
        if (event.button == ui::MouseButton::Left)
            m_owner->actionFieldPressed(this);
        ui::TextField::pointerPress(event);
    }
private:
    SpotlightView *m_owner;
};

class SpotlightView::WheelAnimation final : public ui::Animation
{
public:
    using Animation::Animation;
private:
    void interrupt(Interruption) override { stop(); }
};

SpotlightView::SpotlightView(Shell &shell, Dock *dock, ClipboardHistory &clipboard, std::string settingsPath)
    : m_shell(shell), m_dock(dock), m_clipboard(clipboard), m_settings(std::move(settingsPath)),
      m_field(addChild<Field>(this)), m_parameter(addChild<Field>(this)), m_subject(addChild<Field>(this)),
      m_message(addChild<Field>(this)), m_icons(addChild<ui::View>())
{
    m_list = m_settings.boolValue("Applications/List");
    m_recent = m_settings.stringListValue("Applications/Recent");
    m_clipboard.store().setEnabled(m_settings.boolValue("Clipboard/Enabled", true));
    m_field->setName("spotlight-search");
    m_field->setFrameVisible(false);
    m_field->setFontSize(24);
    m_field->setTextMargins(0, 8);
    m_field->setMaximumLength(256);
    m_field->setSelectByMouse(true);
    for (auto *input : {m_parameter, m_subject, m_message}) {
        input->setSelectByMouse(true);
        input->setVisible(false);
        input->setMaximumLength(2048);
        input->onTextChanged = [this] { actionEdited(); };
    }
    m_parameter->setName("spotlight-action-parameter");
    m_subject->setName("spotlight-action-subject");
    m_message->setName("spotlight-action-message");
    m_icons->setClipsChildren(true);
    m_icons->setName("spotlight-result-icons");
    setTracksHover(true);
    m_queryTimer.setSingleShot(true);
    m_queryTimer.onTimeout = [this] { refreshResults(); };
    m_field->onTextChanged = [this] {
        if (m_updating)
            return;
        stopScrolling();
        m_scroll = 0;
        m_selected = -1;
        if (m_mode == Mode::Files || (m_mode == Mode::Search && !idle())) {
            requestFiles();
            m_queryTimer.start(std::chrono::milliseconds(55));
        } else {
            refreshResults();
        }
    };
    m_subscriptions.push_back(shell.applications().changed.connect([this] {
        if (m_mode == Mode::Applications || m_mode == Mode::Actions || !idle())
            refreshResults();
    }));
    m_subscriptions.push_back(clipboard.store().changed.connect([this] {
        if (m_mode == Mode::Clipboard || (m_mode == Mode::Search && !idle()))
            refreshResults();
        if (m_thumbnails.size() > 200)
            m_thumbnails.clear();
    }));
    reset();
}

SpotlightView::~SpotlightView() = default;

void SpotlightView::reset()
{
    setMode(Mode::Search);
}

void SpotlightView::setMode(Mode mode)
{
    // System Settings edits the same file. Pick up those changes when the
    // panel next opens or changes mode, before rebuilding the results.
    m_settings.reload();
    m_list = m_settings.boolValue("Applications/List");
    m_menu.close();
    stopScrolling();
    const bool changed = m_mode != mode;
    m_mode = mode;
    m_action.reset();
    m_actionCard.reset();
    m_suggestions.clear();
    m_confirmClear = false;
    m_status.clear();
    m_folder.clear();
    m_category = Category::All;
    m_fileType = FileType::All;
    m_updating = true;
    m_field->clear();
    m_field->setPlaceholder(nameFor(mode));
    m_updating = false;
    m_hoverMode = -1;
    m_dragPath.clear();
    m_hot = -1;
    m_selected = -1;
    m_scroll = m_chipScroll = 0;
    if (mode == Mode::Files)
        requestFiles();
    refreshResults();
    if (changed)
        fadeBody();
    m_field->setVisible(true);
    focusSearch();
}

void SpotlightView::focusSearch()
{
    if (!m_action)
        m_field->setFocus();
}

bool SpotlightView::idle() const
{
    return m_mode == Mode::Search && m_field->text().empty() && !m_action;
}

double SpotlightView::fieldWidth(double width) const
{
    return width - 4 * (std::min<double>(kHeader, std::floor(width / 10)) + kGap) * (1 - m_expansion);
}

ui::RectF SpotlightView::modeButton(double width, int index) const
{
    const double field = fieldWidth(width);
    const double gap = kGap * (1 - m_expansion);
    const double diameter = (width - field) / 4 - gap;
    return {field + gap + index * (diameter + gap), (kHeader - diameter) / 2, diameter, diameter};
}

void SpotlightView::setExpansion(double value)
{
    if (m_expansion == value)
        return;
    m_expansion = value;
    requestLayout();
    requestPaint();
}

ui::Path SpotlightView::glassShape(int width, int height) const
{
    ui::Path shape;
    if (m_expansion == 1)
        return shape; // Standard FishUI rounded rectangle.
    const double radius = std::lerp(kHeader / 2.0, double(kRadius), m_expansion);
    shape.addRoundedRect({0, 0, fieldWidth(width), double(height)}, radius, radius);
    for (int i = 0; i < 4; ++i) {
        const auto button = modeButton(width, i);
        if (button.width >= 1)
            shape.addEllipse(button);
    }
    return shape;
}

base::Region SpotlightView::inputRegion(int width, int height) const
{
    if (m_expansion == 1)
        return base::Region({0, 0, width, height});
    base::Region region({0, 0, int(std::lround(fieldWidth(width))), height});
    for (int i = 0; i < 4; ++i) {
        const auto button = modeButton(width, i);
        if (button.width >= 1)
            region |= base::Region({int(std::lround(button.x)), int(std::lround(button.y)),
                                    int(std::lround(button.width)), int(std::lround(button.height))});
    }
    return region;
}

int SpotlightView::preferredHeight() const
{
    if (idle())
        return kHeader;
    if (m_confirmClear)
        return 160;
    if (m_action)
        return m_savingQuick ? 156 : int(m_phraseBottom) + 43 +
            (m_actionCard ? 62 : m_suggestions.empty() ? 0 : 6 + 29 * int(m_suggestions.size()));
    return int(bodyTop()) + std::clamp(int(std::ceil(m_bodyHeight)) + 5, 60, grid() ? 420 : 376);
}

double SpotlightView::bodyTop() const
{
    return kHeader + (m_chips.empty() ? 0 : kChipBand);
}

void SpotlightView::requestFiles()
{
    if (m_indexJob.isPending() || std::chrono::steady_clock::now() - m_indexed < std::chrono::seconds(60))
        return;
    m_indexJob = base::runInThread([] { return spotlight::indexFiles(base::paths::home()); }, [this](auto files) {
        m_indexed = std::chrono::steady_clock::now();
        m_files = std::move(files);
        if (m_mode == Mode::Files || (m_mode == Mode::Search && !idle()))
            refreshResults();
    });
}

void SpotlightView::appendApps(const std::string &query)
{
    const auto apps = spotlight::findApps(m_shell.applications().applications(), query, m_category);
    std::vector<std::string> used;
    const auto append = [&](const DesktopEntryPtr &app, const std::string &section) {
        m_results.push_back({Mode::Applications, app->name, query.empty() ? std::string() : app->genericName,
                             app->icon, app->id, app, {}, false, section});
    };
    if (m_mode == Mode::Applications && query.empty() && m_category == Category::All) {
        std::vector<std::string> candidates = m_recent;
        for (const auto &id : m_shell.tasks().applications()) {
            if (const auto entry = m_shell.tasks().entry(id))
                candidates.push_back(entry->id);
        }
        for (const auto &id : candidates) {
            if (used.size() >= kColumns)
                break;
            if (std::find(used.begin(), used.end(), id) != used.end())
                continue;
            const auto found = std::find_if(apps.begin(), apps.end(), [&](const auto &app) { return app->id == id; });
            if (found != apps.end()) {
                used.push_back(id);
                append(*found, "recent-apps");
            }
        }
    }
    const size_t limit = m_mode == Mode::Search ? 7 : apps.size();
    for (const auto &app : apps) {
        if (m_results.size() >= limit)
            break;
        if (std::find(used.begin(), used.end(), app->id) == used.end())
            append(app, used.empty() ? "" : "all-apps");
    }
}

void SpotlightView::appendFiles(const std::string &query)
{
    const bool home = m_mode == Mode::Files && query.empty() && m_folder.empty() && m_fileType == FileType::All;
    if (home) {
        for (const auto &name : {tr("Home"), tr("Downloads")}) {
            const std::string path = name == tr("Home") ? base::paths::home() : base::paths::userDirectory(base::paths::UserDirectory::Downloads);
            m_results.push_back({Mode::Files, name, path, "folder", path, {}, {}, true, tr("Suggestions")});
        }
    }
    const auto rows = spotlight::findFiles(m_files, query, m_fileType, m_folder, m_mode == Mode::Search ? 7 : 80);
    for (const auto *file : rows) {
        std::string detail;
        if (file->type == FileType::Folders)
            detail = tr("Folder");
        else {
            const auto ext = std::filesystem::path(file->name).extension().string();
            detail = (ext.empty() ? tr("File") : ext.substr(1)) + " · ";
            detail += file->size < 1024 ? std::to_string(file->size) + " B" : std::to_string(file->size / 1024) + " KB";
        }
        if (file->modified)
            detail += " · " + timeLabel(file->modified, "%x %H:%M");
        detail += " · " + file->parent;
        m_results.push_back({Mode::Files, file->name, std::move(detail), file->icon, file->path, {}, {},
                             file->type == FileType::Folders, home ? tr("Recents") : std::string()});
        if (file->type != FileType::Folders && file->type != FileType::All && file->size > 0
            && file->size < 1024ULL * 1024 * 1024)
            m_results.back().filePreview = std::to_string(file->modified) + ':' + std::to_string(file->size) + ':' + file->path;
    }
}

void SpotlightView::appendActions(const std::string &query)
{
    const size_t first = m_results.size();
    auto append = [&](Result result) {
        result.quick = m_settings.stringValue("QuickKeys/" + result.key);
        if (query.empty() || containsIgnoringCase(result.title, query) || containsIgnoringCase(result.detail, query)
            || (!result.quick.empty() && toLower(query) == result.quick))
            m_results.push_back(std::move(result));
    };
    const auto suggested = query.empty() ? tr("Suggestions") : std::string();
    const auto system = tr("System");
    append({Mode::Actions, tr("Open Home Folder"), system, "user-home", "home", {}, {}, false, suggested});
    append({Mode::Actions, tr("Open System Settings"), system, "preferences-system", "settings", {}, {}, false, suggested});
    append({Mode::Actions, tr("Search the Web"), tr("Web Browser"), "web-browser", "web", {}, {}, false, suggested});
    append({Mode::Actions, tr("Compose Email"), tr("Mail"), "mail-message-new", "email", {}, {}, false, suggested});
    for (const auto &app : m_shell.applications().applications()) {
        for (const auto &action : app->actions) {
            const auto command = base::DesktopEntry::parseExec(action.exec);
            if (!action.name.empty() && !command.empty())
                append({Mode::Actions, action.name, app->name, action.icon.empty() ? app->icon : action.icon,
                        app->id + ":" + action.id, app, command, false, query.empty() ? app->name : std::string()});
        }
    }
    if (!query.empty())
        std::stable_partition(m_results.begin() + first, m_results.end(), [&](const Result &result) {
            return result.quick == toLower(query);
        });
    if (m_mode == Mode::Search && m_results.size() > first + 4)
        m_results.resize(first + 4);
}

void SpotlightView::appendClipboard(const std::string &query)
{
    auto &model = m_clipboard.model();
    model.setFilterText(query);
    const int count = std::min(model.count(), m_mode == Mode::Search ? 4 : model.count());
    for (int i = 0; i < count; ++i) {
        const auto &entry = model.entry(i);
        const bool image = entry.kind == ClipboardEntry::Kind::Image;
        const bool files = entry.kind == ClipboardEntry::Kind::Files;
        const auto domain = spotlight::webDomain(entry.text);
        const bool link = !image && !files && !domain.empty();
        std::string detail = image ? entry.imageType : files ? tr("Files") : link ? tr("URL") : tr("Text");
        if (link)
            detail += " · " + domain;
        if (entry.copied)
            detail += std::string(" · ") + tr("Copied") + " " + timeLabel(*entry.copied / 1000, "%H:%M");
        m_results.push_back({Mode::Clipboard, model.title(i), std::move(detail), image ? "image-x-generic"
                : files ? "folder" : link ? "text-html" : "text-x-generic", entry.key});
    }
}

void SpotlightView::refreshResults()
{
    m_queryTimer.stop();
    m_hot = -1;
    m_results.clear();
    m_chips.clear();
    const auto &query = m_field->text();
    if (!m_action) {
        switch (m_mode) {
        case Mode::Search:
            if (!idle()) {
                appendApps(query);
                appendActions(query);
                appendFiles(query);
                if (m_clipboard.store().isEnabled())
                    appendClipboard(query);
            }
            break;
        case Mode::Applications:
            m_chips = {{int(Category::Utilities), tr("Utilities"), m_category == Category::Utilities},
                       {int(Category::Productivity), tr("Productivity & Finance"), m_category == Category::Productivity},
                       {int(Category::Creativity), tr("Creativity"), m_category == Category::Creativity},
                       {int(Category::Entertainment), tr("Entertainment"), m_category == Category::Entertainment},
                       {int(Category::Social), tr("Social"), m_category == Category::Social},
                       {int(Category::Information), tr("Information"), m_category == Category::Information}};
            appendApps(query);
            break;
        case Mode::Files:
            if (!m_folder.empty())
                m_chips.push_back({-1, std::filesystem::path(m_folder).filename().string() + "  ×", true});
            for (const auto &[type, label] : std::array{
                         std::pair{FileType::Folders, tr("Folders")}, std::pair{FileType::Documents, tr("Documents")},
                         std::pair{FileType::Pdf, tr("PDF")}, std::pair{FileType::Images, tr("Images")},
                         std::pair{FileType::Screenshots, tr("Screenshots")}, std::pair{FileType::Audio, tr("Audio")},
                         std::pair{FileType::Video, tr("Video")}})
                m_chips.push_back({int(type), label, type == m_fileType});
            appendFiles(query);
            break;
        case Mode::Actions: appendActions(query); break;
        case Mode::Clipboard:
            if (m_clipboard.store().isEnabled())
                appendClipboard(query);
            break;
        }
    }
    m_selected = std::min(m_selected, int(m_results.size()) - 1);
    if (m_mode == Mode::Search && !m_results.empty() && m_selected == -1)
        m_selected = 0;
    refreshLayout();
}

void SpotlightView::refreshLayout()
{
    stopScrolling();
    const double width = geometry().width > 0 ? geometry().width : kWidth;
    buildTiles(grid(), width);
    if (inlineAction())
        layoutAction(width);
    if (onResize)
        onResize();
    requestLayout();
    requestPaint();
}

void SpotlightView::buildTiles(bool grid, double width)
{
    m_tilesWidth = width;
    constexpr double inset = 10;
    const double cell = (width - 2 * inset) / kColumns;
    const double rowHeight = grid ? 100 : 52;
    m_tiles.clear();
    m_sections.clear();
    double y = 4;
    int column = 0;
    std::string section;
    for (int i = 0; i < int(m_results.size()); ++i) {
        const auto &tag = m_results[i].section;
        if (tag != section) {
            if (column) {
                column = 0;
                y += rowHeight;
            }
            section = tag;
            if (tag == "all-apps") {
                m_sections.push_back({y + 3, {}});
                y += 12;
            } else if (!tag.empty() && tag != "recent-apps") {
                m_sections.push_back({y, tag});
                y += 23;
            }
        }
        m_tiles.push_back({{inset + column * cell, y, grid ? cell : width - 2 * inset, rowHeight}, i});
        if (!grid || ++column == kColumns) {
            y += rowHeight;
            column = 0;
        }
    }
    m_bodyHeight = y + (column ? rowHeight : 0);
}

ui::Scroller::Axis SpotlightView::horizontalAxis() const
{
    const double width = geometry().width;
    const double maximum = m_chips.empty() ? 0 : std::max(0.0, m_chipsWidth - width);
    return {0, maximum, std::max(0.0, width - 28), maximum > 0};
}

ui::Scroller::Axis SpotlightView::verticalAxis() const
{
    const double viewport = std::max(0.0, geometry().height - bodyTop() - kBodyInset);
    const double maximum = viewport > 0 && !idle() && !m_action && !m_confirmClear
        ? std::max(0.0, m_bodyHeight - viewport) : 0;
    return {0, maximum, viewport, maximum > 0};
}

ui::PointF SpotlightView::boundedPosition(ui::PointF pos) const
{
    return {std::clamp(pos.x, 0.0, horizontalAxis().maximum), std::clamp(pos.y, 0.0, verticalAxis().maximum)};
}

void SpotlightView::applyPosition(ui::PointF pos)
{
    pos = boundedPosition(pos);
    if (pos == position())
        return;
    m_chipScroll = pos.x;
    m_scroll = pos.y;
    m_hot = -1;
    requestLayout();
    requestPaint();
}

void SpotlightView::stopScrolling()
{
    if (m_wheel)
        m_wheel->stop();
    if (m_scroller)
        m_scroller->cancel(false);
}

void SpotlightView::fadeBody()
{
    if (!window())
        return;
    if (!m_bodyFade) {
        m_bodyFade = std::make_unique<ui::Animation>(window());
        m_bodyFade->setDuration(std::chrono::milliseconds(150));
        m_bodyFade->setEasing(ui::Animation::Easing::OutCubic);
        m_bodyFade->onUpdate = [this](double t) {
            m_bodyOpacity = std::lerp(.30, 1.0, t);
            requestLayout();
            requestPaint();
        };
    }
    m_bodyOpacity = .30;
    m_bodyFade->start();
}

void SpotlightView::layoutAction(double width)
{
    const ui::FontMetrics metrics(font(20));
    const bool email = m_action->key == "email";
    double x = 53, y = 11;
    const double right = width - 16;
    m_phrase.clear();
    auto add = [&](std::string label, int token = -1, Field *input = nullptr) {
        const double available = std::max(1.0, right - 53);
        const double natural = std::ceil(metrics.horizontalAdvanceF(input && !input->text().empty() ? input->text() : label));
        const double length = std::min(available, token < 0 ? natural + 2 : std::max(54.0, natural + 18));
        if (x > 53 && x + length > right) {
            x = 53;
            y += 31;
        }
        const ui::RectF rect{x, y, length, 28};
        m_phrase.push_back({input ? std::string{} : std::move(label), rect, token});
        if (input)
            input->setGeometry(rect);
        x += length + 6;
    };
    if (email) {
        add(tr("Send"));
        add(tr("Body"), Message, m_message);
        add(tr("with"));
        add(tr("Subject"), Subject, m_subject);
        add(tr("to"));
        add(tr("Recipients"), Recipient, m_parameter);
    } else {
        add(tr("Search for"));
        add(tr("Search terms"), SearchTerms, m_parameter);
        add(tr("using"));
        add(std::string(searchEngines[m_engine].first) + " ▾", SearchEngine);
    }
    m_phraseBottom = std::max<double>(kHeader, y + 35);
    m_message->setVisible(email);
    m_subject->setVisible(email);
    m_parameter->setVisible(true);
}

void SpotlightView::actionFieldPressed(Field *field)
{
    if (!inlineAction())
        return;
    m_activeToken = field == m_subject ? Subject : field == m_message ? Message : field == m_parameter
        ? (m_action->key == "email" ? Recipient : SearchTerms) : -1;
    m_suggestionDismissed = false;
    updateSuggestions();
    refreshLayout();
}

void SpotlightView::focusToken(int token)
{
    if (token == SearchEngine)
        m_actionCard.reset();
    m_activeToken = token;
    m_suggestionDismissed = false;
    auto *input = token == Message ? m_message : token == Subject ? m_subject : m_parameter;
    input->setFocus();
    updateSuggestions();
    refreshLayout();
}

void SpotlightView::actionEdited()
{
    if (m_updating || !m_action)
        return;
    m_status.clear();
    m_actionCard.reset();
    m_suggestionDismissed = false;
    if (m_activeToken == SearchEngine)
        m_activeToken = SearchTerms;
    updateSuggestions();
    refreshLayout();
}

void SpotlightView::updateSuggestions()
{
    m_suggestions.clear();
    m_suggestionIndex = -1;
    if (!inlineAction() || m_suggestionDismissed || m_actionCard)
        return;
    if (m_activeToken == SearchEngine) {
        for (size_t i = 0; i < searchEngines.size(); ++i)
            m_suggestions.push_back({searchEngines[i].first, std::to_string(i)});
        m_suggestionIndex = m_engine;
    } else if (m_activeToken == Recipient) {
        // Complete the last address, without reading system contacts or making a network request.
        const std::string &value = m_parameter->text();
        const auto comma = value.rfind(',');
        const std::string prefix = comma == value.npos ? "" : value.substr(0, comma + 1) + ' ';
        const auto address = base::strings::trimmed(std::string_view(value).substr(prefix.empty() ? 0 : comma + 1));
        const auto at = address.find('@');
        if (at == address.npos || at == 0 || address.find_first_of(" \t\r\n") != address.npos)
            return;
        const auto partial = toLower(std::string(address.substr(at + 1)));
        for (std::string_view domain : {"gmail.com", "outlook.com", "icloud.com"}) {
            if (domain.starts_with(partial) && domain != partial) {
                std::string completion = std::string(address.substr(0, at + 1)) + std::string(domain);
                m_suggestions.push_back({completion, prefix + completion});
            }
        }
    }
}

void SpotlightView::chooseSuggestion(int index)
{
    if (index < 0 || index >= int(m_suggestions.size()))
        return;
    const auto value = m_suggestions[index].value;
    if (m_activeToken == SearchEngine) {
        m_engine = value.front() - '0';
        m_actionCard.reset();
        m_activeToken = SearchTerms;
    } else {
        m_updating = true;
        m_parameter->setText(value);
        m_updating = false;
    }
    m_suggestionDismissed = true;
    m_status.clear();
    updateSuggestions();
    refreshLayout();
}

ui::RectF SpotlightView::suggestionRect(int index, double width) const
{
    return {12, m_phraseBottom + 4 + 29 * index, width - 24, 28};
}

ui::RectF SpotlightView::actionButton(double width, double height) const
{
    const double size = std::min(m_actionCard ? 150.0 : 88.0, width - 26);
    return {width - size - 12, height - 39, size, 28};
}

void SpotlightView::layout()
{
    const double w = geometry().width;
    const double header = fieldWidth(w);
    const bool offersOpen = m_mode == Mode::Search && m_selected >= 0 && header >= 360;
    const double reserve = std::lerp(m_hoverMode > 0 && header > 280 ? 70.0 : 9.0,
                                     offersOpen ? 114.0 : 50.0, m_expansion);
    m_field->setGeometry({54, 0, std::max(0.0, header - 54 - reserve), kHeader});
    m_field->setVisible(!m_action);
    if (inlineAction()) {
        layoutAction(w);
    } else {
        m_parameter->setFrameVisible(true);
        m_parameter->setGeometry({18, 75, w - 36, 34});
        m_parameter->setVisible(bool(m_action));
        m_subject->setVisible(false);
        m_message->setVisible(false);
    }

    double x = 17;
    const ui::FontMetrics chipMetrics(font(12));
    for (auto &chip : m_chips) {
        const double chipWidth = std::ceil(chipMetrics.horizontalAdvanceF(chip.label)) + 14;
        chip.rect = {x, kHeader + 7, chipWidth, 21};
        x += chipWidth + 7;
    }
    m_chipsWidth = x + 10;
    if (m_tilesWidth != w)
        buildTiles(grid(), w);
    const auto pos = boundedPosition(position());
    m_chipScroll = pos.x;
    m_scroll = pos.y;
    for (auto &chip : m_chips)
        chip.rect.x -= m_chipScroll;
    const double body = verticalAxis().viewport;
    m_icons->setGeometry({0, bodyTop(), header, body});
    m_icons->setOpacity(m_bodyOpacity * m_expansion);
    const double iconSize = grid() ? std::min(60.0, std::max(26.0, (w - 20) / kColumns * 0.72)) : 30;
    const double scale = window() ? window()->scale() : 1;
    const auto first = std::partition_point(m_tiles.begin(), m_tiles.end(), [this](const Tile &t) {
        return t.rect.bottom() <= m_scroll;
    });
    const auto end = m_confirmClear || body <= 0 ? first : std::partition_point(first, m_tiles.end(), [this, body](const Tile &t) {
        return t.rect.y < m_scroll + body;
    });
    const int firstResult = int(first - m_tiles.begin());
    const int endResult = int(end - m_tiles.begin());
    for (auto &slot : m_images) {
        if (slot.result < firstResult || slot.result >= endResult) {
            slot.result = -1;
            slot.view->setVisible(false);
        }
    }
    for (auto it = first; it != end; ++it) {
        const auto &tile = *it;
        const double y = tile.rect.y - m_scroll;
        // Keep a visible row's ImageView as it moves. When a row leaves the
        // viewport, only its slot is recycled; the other icons stay decoded.
        auto slotIt = std::ranges::find(m_images, tile.result, &ResultIcon::result);
        if (slotIt == m_images.end()) {
            slotIt = std::ranges::find(m_images, -1, &ResultIcon::result);
            if (slotIt == m_images.end()) {
                auto *icon = m_icons->addChild<ui::ImageView>();
                icon->setFillMode(ui::ImageView::FillMode::PreserveAspectFit);
                m_images.push_back({icon, -1, {}, {}});
                slotIt = std::prev(m_images.end());
            }
        }
        auto &slot = *slotIt;
        slot.result = tile.result;
        auto *icon = slot.view;
        const auto &result = m_results[tile.result];
        icon->setVisible(true);
        const ui::RectF box{grid() ? tile.rect.x + (tile.rect.width - iconSize) / 2 : tile.rect.x + 12,
                            y + (grid() ? 8 : 11), iconSize, iconSize};
        icon->setGeometry(box);
        if ((result.type == Mode::Clipboard && result.icon == "image-x-generic" && updateClipboardPreview(result, slot))
            || (!result.filePreview.empty() && updateFilePreview(result, slot))) {
            icon->setCornerRadius(4);
            icon->setGeometry({tile.rect.x + 8, y + 8, 38, 36});
            continue;
        }
        icon->setCornerRadius(0);
        icon->setSourceSize({iconSize, iconSize});
        auto path = ui::themeIconPath(result.icon, int(std::ceil(iconSize)), scale);
        if (path.empty())
            path = ui::themeIconPath(result.type == Mode::Applications ? "application-x-executable" : "text-x-generic",
                                     int(std::ceil(iconSize)), scale);
        if (path.empty())
            path = shellDataFile("application-missing.svg");
        if (slot.source != path || !slot.previewKey.empty()) {
            slot.previewKey.clear();
            slot.source = path;
            icon->setSource(std::move(path));
        }
    }
}

bool SpotlightView::updateClipboardPreview(const Result &result, ResultIcon &icon)
{
    const auto [it, added] = m_thumbnails.try_emplace(result.key);
    if (added) {
        const auto path = m_clipboard.store().bodyPath(result.key);
        base::runInThread([path] {
            for (const auto &[type, bytes] : ClipboardStore::readBody(path)) {
                if (!type.starts_with("image/"))
                    continue;
                auto image = ui::Bitmap::decode(bytes);
                if (!image.isNull())
                    return image.scaled(ui::scaledSize(image.size(), {84, 84}, ui::Bitmap::Scaling::Fit));
            }
            return ui::Bitmap();
        }, [this, key = result.key, alive = std::weak_ptr<bool>(m_alive)] (ui::Bitmap image) {
            if (alive.expired())
                return;
            if (auto found = m_thumbnails.find(key); found != m_thumbnails.end())
                found->second = std::move(image);
            requestLayout();
        }).detach();
    } else if (it->second) {
        return applyPreview("clipboard:" + result.key, *it->second, icon);
    }
    return false;
}

bool SpotlightView::updateFilePreview(const Result &result, ResultIcon &icon)
{
    if (auto it = m_fileThumbnails.find(result.filePreview); it != m_fileThumbnails.end())
        return applyPreview(result.filePreview, it->second, icon);
    if (m_pendingThumbnails.size() >= 4 || std::ranges::find(m_pendingThumbnails, result.filePreview) != m_pendingThumbnails.end())
        return false;
    m_pendingThumbnails.push_back(result.filePreview);
    static base::ThreadPool pool(2);
    pool.run([path = result.key] { return spotlight::loadFilePreview(path); },
             [this, key = result.filePreview, alive = std::weak_ptr<bool>(m_alive)](ui::Bitmap image) {
        if (alive.expired())
            return;
        std::erase(m_pendingThumbnails, key);
        if (m_fileThumbnailOrder.size() >= 192) {
            m_fileThumbnails.erase(m_fileThumbnailOrder.front());
            m_fileThumbnailOrder.pop_front();
        }
        m_fileThumbnailOrder.push_back(key);
        m_fileThumbnails.emplace(key, std::move(image));
        requestLayout();
        requestPaint();
    }).detach();
    return false;
}

bool SpotlightView::applyPreview(const std::string &key, const ui::Bitmap &bitmap, ResultIcon &icon)
{
    if (bitmap.isNull())
        return false;
    if (icon.previewKey != key) {
        icon.previewKey = key;
        const auto pixels = bitmap.converted(ui::Bitmap::Format::Argb32Premultiplied);
        icon.view->setPixels(pixels.width(), pixels.height(), reinterpret_cast<const uint32_t *>(pixels.constBits()),
                             pixels.stride(), true);
    }
    return true;
}

void SpotlightView::paint(ui::Painter &p)
{
    if (onPaint)
        onPaint();
    const auto &theme = ui::Theme::current();
    const auto ink = theme.text;
    const auto secondary = ink.withAlpha(.65);
    const auto hairline = ink.withAlpha(.14);
    const auto subtle = ink.withAlpha(.075);
    const auto selected = theme.highlight.withAlpha(theme.darkMode ? .30 : .22);
    const double w = geometry().width, h = geometry().height;
    const auto over = [w, h](double y, double height) { return ui::RectF{0, y, w, std::max(0.0, std::min(height, h-y))}; };
    const auto field = fieldWidth(w);
    if (m_expansion < 1) {
        p.save();
        p.multiplyOpacity(1 - m_expansion);
        glyph(p, Symbol::Search, {28, kHeader / 2.0}, ink);
        for (int i = 0; i < 4; ++i) {
            const auto button = modeButton(w, i);
            if (idle() && m_hoverMode == i + 1)
                plate(p, button, button.width / 2, selected);
            glyph(p, Symbol(i + 1), {button.x + button.width / 2, kHeader / 2.0}, ink, std::min(24.0, button.width * .67));
        }
        if (idle() && m_hoverMode >= 1 && field > 280) {
            const ui::RectF modifier{field - 65, 18, 36, 18};
            const ui::RectF key{field - 25, 18, 18, 18};
            plate(p, modifier, 5, subtle);
            plate(p, key, 5, subtle);
            text(p, modeModifierLabel(), modifier, 11, secondary, true);
            text(p, std::to_string(m_hoverMode), key, 11, secondary, true);
        }
        p.restore();
    }
    if (m_expansion == 0 || idle())
        return;
    p.save();
    p.clipRoundedRect({0, 0, field, h}, kRadius);
    p.multiplyOpacity(m_expansion);
    glyph(p, Symbol(m_mode), {28, kHeader / 2.0}, ink);
    glyph(p, Symbol::More, {field - 28, kHeader / 2.0}, ink, 22);
    if (m_mode == Mode::Search && m_selected >= 0 && w >= 360) {
        const ui::RectF open{field - 110, 15, 61, 24};
        plate(p, open, 9, subtle);
        text(p, tr("Open"), open, 12, ink, true);
    }
    if (!inlineAction())
        p.drawHorizontalLine(17, kHeader, field - 34, hairline);
    p.multiplyOpacity(m_bodyOpacity);
    if (m_action || m_confirmClear) {
        if (inlineAction()) {
            for (const auto &part : m_phrase) {
                if (part.token >= 0)
                    plate(p, part.rect, 8, part.token == m_activeToken ? selected : subtle);
                text(p, part.label, part.rect.adjusted(part.token >= 0 ? 7 : 0, 0, -1, 0), 20,
                     part.token >= 0 ? theme.highlight : ink);
            }
            if (!m_suggestions.empty() || m_actionCard)
                p.drawHorizontalLine(16, m_phraseBottom, w - 32, hairline);
            for (int i = 0; i < int(m_suggestions.size()); ++i) {
                const auto row = suggestionRect(i, w);
                plate(p, row, 8, i == m_suggestionIndex ? selected : subtle);
                text(p, m_suggestions[i].label, row.adjusted(13, 0, -10, 0), 14, ink);
            }
            if (m_actionCard) {
                const ui::RectF card{12, m_phraseBottom + 6, w - 24, 54};
                plate(p, card, 12, subtle);
                glyph(p, m_action->key == "email" ? Symbol::Actions : Symbol::Search,
                      {card.x + 19, card.y + 26}, secondary, 17);
                text(p, m_actionCard->title, {card.x + 35, card.y + 5, card.width - 47, 23}, 14, ink, false, true);
                text(p, m_actionCard->detail, {card.x + 35, card.y + 28, card.width - 47, 20}, 12, secondary);
            }
        } else if (m_action) {
            text(p, std::string(tr("Add quick keys")) + " — " + m_action->title, {54, 0, w - 105, kHeader}, 21, ink);
        }
        if (m_confirmClear) {
            plate(p, {12, 65, w - 24, h - 76}, 16, subtle);
            text(p, tr("Clear the clipboard history?"), {20, 70, w - 40, 42}, 16, ink, true);
        }
        const auto button = actionButton(w, h);
        if (!inlineAction() || h >= m_phraseBottom + 43) {
            plate(p, button, 14, selected);
            text(p, m_confirmClear ? tr("Clear") : m_savingQuick ? tr("Save") : !m_actionCard ? tr("Run")
                 : m_action->key == "email" ? tr("Open in Mail") : tr("Open in Browser"), button, 14, ink, true, true);
            const auto hint = inlineAction() && m_status.empty() ? tr("Ctrl+↵") : m_status;
            text(p, hint, {18, button.y, button.x - 25, button.height}, 12, secondary);
        }
        p.restore();
        return;
    }
    if (!m_chips.empty()) {
        p.save();
        p.clip({14, kHeader + 2, w - 28, kChipBand - 3});
        for (const auto &chip : m_chips) {
            if (chip.rect.right() < 14 || chip.rect.x > w - 14)
                continue;
            plate(p, chip.rect, 8, chip.active ? selected : subtle);
            text(p, chip.label, chip.rect.adjusted(4, 0, -4, 0), 12, chip.active ? ink : secondary, true);
        }
        p.restore();
        p.drawHorizontalLine(17, bodyTop(), w - 34, hairline);
    }
    const double top = bodyTop();
    const double shown = verticalAxis().viewport;
    p.clip(over(top, shown));
    if (m_results.empty()) {
        std::string message = tr("No results found.");
        if (m_mode == Mode::Clipboard && !m_clipboard.store().isEnabled())
            message = tr("Clipboard Search is off. Enable it from the menu.");
        else if (m_mode == Mode::Clipboard && m_field->text().empty())
            message = tr("Clipboard history is empty.");
        else if (m_mode == Mode::Files && m_indexJob.isPending())
            message = tr("Looking for files…");
        text(p, message, {14, top, w - 28, h - top - 5}, 14, secondary, true);
    }
    for (const auto &section : m_sections) {
        const double y = top + section.y - m_scroll;
        if (y + 23 < top || y > h)
            continue;
        if (section.name.empty())
            p.drawHorizontalLine(0, y, w, hairline);
        else
            text(p, section.name, {22, y, w - 44, 22}, 12, secondary, false, true);
    }
    for (const auto &tile : m_tiles) {
        auto r = tile.rect;
        r.y += top - m_scroll;
        if (r.bottom() <= top || r.y >= h)
            continue;
        const auto &result = m_results[tile.result];
        if (m_selected == tile.result || m_hot == tile.result)
            plate(p, r.adjusted(1, 1, -1, -1), 13, m_selected == tile.result ? selected : subtle);
        if (grid()) {
            text(p, result.title, {r.x+5, r.y+72, r.width-10, 20}, 13, ink, true);
            continue;
        }
        const bool copy = result.type == Mode::Clipboard;
        const bool quick = result.type == Mode::Actions && m_mode == Mode::Actions;
        const double right = copy ? 46 : quick ? 133 : 10;
        const double titleY = r.y + (result.detail.empty() ? 0 : 6);
        text(p, result.title, {r.x+56, titleY, r.width-56-right, result.detail.empty() ? r.height : 21}, 15, ink);
        text(p, result.detail, {r.x+56, r.y+28, r.width-56-right, 17}, 12, secondary);
        if (copy || quick) {
            const ui::RectF button{copy ? r.right()-34 : r.right()-right, r.y+14, copy ? 24.0 : right-10, 24};
            plate(p, button, copy ? 12 : 9, subtle);
            if (copy)
                glyph(p, Symbol::Clipboard, {button.x + 12, button.y + 12}, secondary, 17);
            else
                text(p, result.quick.empty() ? tr("Add quick keys") : result.quick, button, 11, secondary, true);
        }
    }
    if (verticalAxis().enabled) {
        const auto thumb = std::min(shown, std::max(16.0, shown * shown / m_bodyHeight));
        plate(p, {field - 5, top + m_scroll / verticalAxis().maximum * (shown - thumb), 3, thumb}, 1.5, hairline);
    }
    p.restore();
}

int SpotlightView::hitMode(ui::PointF point) const
{
    if (idle()) {
        for (int i = 0; i < 4; ++i) {
            if (modeButton(geometry().width, i).contains(point))
                return i + 1;
        }
    }
    return -1;
}

std::string SpotlightView::modeModifierLabel() const
{
    return m_shell.seat() && m_shell.seat()->shortcutConvention() == input::ShortcutConvention::Command ? "⌘" : "Super";
}

int SpotlightView::hitResult(ui::PointF point) const
{
    if (idle() || point.y <= bodyTop() || point.y >= geometry().height - kBodyInset || m_action || m_confirmClear)
        return -1;
    point.y += m_scroll - bodyTop();
    for (const auto &tile : m_tiles) {
        if (tile.rect.contains(point))
            return tile.result;
    }
    return -1;
}

void SpotlightView::hoverMove(const ui::PointerEvent &event)
{
    const auto mode = hitMode(event.position);
    if (mode != m_hoverMode) {
        m_hoverMode = mode;
        m_field->setPlaceholder(nameFor(mode < 0 ? m_mode : Mode(mode)));
        requestLayout();
    }
    m_hot = hitResult(event.position);
    requestPaint();
}

void SpotlightView::pointerLeave()
{
    m_hoverMode = m_hot = -1;
    m_field->setPlaceholder(nameFor(m_mode));
    requestLayout();
    requestPaint();
}

void SpotlightView::pointerPress(const ui::PointerEvent &event)
{
    m_dragPath.clear();
    const int row = hitResult(event.position);
    if (event.button == ui::MouseButton::Right) {
        if (row >= 0)
            openMenu(event.position, row);
    } else if (event.button == ui::MouseButton::Left && row >= 0 && m_results[row].type == Mode::Files) {
        m_dragPath = m_results[row].key;
        m_dragIcon = m_results[row].icon;
        m_press = event.position;
    }
}

void SpotlightView::pointerMove(const ui::PointerEvent &event)
{
    if (m_dragPath.empty() || std::hypot(event.position.x - m_press.x, event.position.y - m_press.y) < 9)
        return;
    ui::DragSource source;
    source.mimeTypes = {"text/uri-list"};
    source.data = [uri = base::url::fromLocalFile(std::exchange(m_dragPath, {})) + "\r\n"](const std::string &) { return uri; };
    source.iconScale = window() ? window()->scale() : 1;
    source.icon = ui::themeIconBitmap(m_dragIcon, 40, source.iconScale);
    source.hotSpot = {20, 20};
    source.onFinished = [this, alive = std::weak_ptr<bool>(m_alive)](ui::DropAction action) {
        if (alive.expired())
            return;
        m_dragging = false;
        if (action != ui::NoAction && onDismiss)
            onDismiss();
    };
    m_dragging = startDrag(std::move(source));
}

void SpotlightView::pointerRelease(const ui::PointerEvent &event)
{
    m_dragPath.clear();
    if (m_dragging)
        return;
    if (event.button != ui::MouseButton::Left)
        return;
    const auto pos = event.position;
    const auto w = fieldWidth(geometry().width), h = geometry().height;
    if (m_confirmClear) {
        if (actionButton(w, h).contains(pos))
            m_clipboard.clear();
        m_confirmClear = false;
        refreshResults();
        return;
    }
    if (const int mode = hitMode(pos); mode >= 1) {
        setMode(Mode(mode));
        return;
    }
    if (inlineAction()) {
        for (const auto &part : m_phrase) {
            if (part.token == SearchEngine && part.rect.contains(pos)) {
                focusToken(SearchEngine);
                return;
            }
        }
        for (int i = 0; i < int(m_suggestions.size()); ++i) {
            if (suggestionRect(i, geometry().width).contains(pos)) {
                chooseSuggestion(i);
                return;
            }
        }
        if (h >= m_phraseBottom + 43 && actionButton(geometry().width, h).contains(pos)) {
            runAction();
            return;
        }
    }
    if (pos.y < kHeader) {
        if (!idle() && pos.x > w - 49)
            openMenu({w - 198, kHeader - 6});
        else if (m_mode == Mode::Search && m_selected >= 0 && w >= 360 && pos.x >= w - 110)
            activate(m_selected);
        else if (pos.x < 52 && m_action)
            exitAction();
        else if (pos.x < 52 && m_mode != Mode::Search)
            setMode(Mode::Search);
        else
            focusSearch();
        return;
    }
    if (m_action) {
        if (!inlineAction() && actionButton(w, h).contains(pos))
            runAction();
        return;
    }
    for (const auto &chip : m_chips) {
        if (pos.x >= 14 && pos.x <= w - 14 && chip.rect.contains(pos)) {
            switchChip(chip.value);
            return;
        }
    }
    const int row = hitResult(pos);
    if (row >= 0) {
        const Mode type = m_results[row].type;
        const auto &tile = m_tiles[row].rect;
        const ui::RectF copy{tile.right() - 34, bodyTop() + tile.y - m_scroll + 14, 24, 24};
        activate(row, (type == Mode::Clipboard && copy.contains(pos))
                 || (type == Mode::Actions && m_mode == Mode::Actions && pos.x > w - 150));
    }
}

bool SpotlightView::scroll(const ui::ScrollEvent &event)
{
    if (idle() || m_action || m_confirmClear || !window())
        return false;
    auto delta = event;
    if (!m_chips.empty() && (event.modifiers & ui::ShiftModifier) && event.deltaX == 0 && event.value120X == 0) {
        delta.deltaX = std::exchange(delta.deltaY, 0);
        delta.value120X = std::exchange(delta.value120Y, 0);
    }
    if (event.precise) {
        if (m_wheel)
            m_wheel->stop();
        if (!m_scroller) {
            m_scroller = std::make_unique<ui::Scroller>(window(), static_cast<ui::Scroller::Target *>(this));
            m_scroller->setElastic(false);
        }
        delta.deltaX *= 1.65;
        delta.deltaY *= 1.65;
        return m_scroller->handleScroll(delta);
    }

    if (m_scroller)
        m_scroller->cancel(false);
    if (!m_wheel) {
        m_wheel = std::make_unique<WheelAnimation>(window());
        m_wheel->setDuration(std::chrono::milliseconds(120));
        m_wheel->setEasing(ui::Animation::Easing::OutCubic);
    }
    // Move roughly one row per detent in a list, or one grid row in Apps.
    const double stride = grid() ? 100 : 76;
    const ui::PointF step{delta.value120X ? delta.value120X * 72.0 / 120 : delta.deltaX * 7.2,
                          delta.value120Y ? delta.value120Y * stride / 120 : delta.deltaY * stride / 10};
    const auto from = position();
    const auto target = boundedPosition((m_wheel->isRunning() ? m_wheelTarget : from) + step);
    if (target == from) {
        m_wheel->stop();
        return false;
    }
    if (m_wheel->isRunning() && target == m_wheelTarget)
        return true;
    m_wheelTarget = target;
    m_wheel->onUpdate = [this, from, target](double t) { applyPosition(from + (target - from) * t); };
    m_wheel->start();
    return true;
}

void SpotlightView::switchChip(int value)
{
    if (m_mode == Mode::Applications)
        m_category = int(m_category) == value ? Category::All : Category(value);
    if (m_mode == Mode::Files) {
        if (value < 0)
            m_folder.clear();
        else
            m_fileType = int(m_fileType) == value ? FileType::All : FileType(value);
    }
    m_scroll = 0;
    m_selected = -1;
    refreshResults();
    focusSearch();
}

void SpotlightView::setFileFolder(std::string folder)
{
    setMode(Mode::Files);
    m_folder = std::move(folder);
    refreshResults();
}

void SpotlightView::select(int row)
{
    if (m_tiles.empty())
        return;
    stopScrolling();
    m_selected = std::clamp(row, 0, int(m_tiles.size()) - 1);
    const auto &tile = m_tiles[m_selected].rect;
    const double lowest = std::clamp(tile.bottom() - verticalAxis().viewport, 0.0, tile.y);
    applyPosition({m_chipScroll, std::clamp(m_scroll, lowest, tile.y)});
    requestLayout();
    requestPaint();
}

bool SpotlightView::key(const ui::KeyEvent &event, Field *field)
{
    using namespace ui::key;
    const bool command = m_shell.seat() && m_shell.seat()->shortcutConvention() == input::ShortcutConvention::Command;
    const auto modeModifier = command ? ui::ControlModifier : ui::SuperModifier;
    if (event.modifiers == modeModifier && event.key >= '1' && event.key <= '4') {
        setMode(Mode(event.key - '0'));
        return true;
    }
    if (event.key == Escape) {
        if (!field->preedit().empty())
            return false;
        if (m_confirmClear) {
            m_confirmClear = false;
            refreshResults();
        } else if (inlineAction() && !m_suggestions.empty()) {
            m_suggestionDismissed = true;
            if (m_activeToken == SearchEngine)
                m_activeToken = SearchTerms;
            updateSuggestions();
            refreshLayout();
        } else if (m_action) {
            exitAction();
        } else if (!m_folder.empty() && field->text().empty()) {
            m_folder.clear();
            refreshResults();
        } else if (onDismiss) {
            onDismiss();
        }
        return true;
    }
    if (m_confirmClear) {
        if (event.key == Return || event.key == KeypadEnter) {
            m_confirmClear = false;
            m_clipboard.clear();
            refreshResults();
        }
        return true;
    }
    if (m_action) {
        if (!field->preedit().empty())
            return false;
        const bool enter = event.key == Return || event.key == KeypadEnter;
        if (enter && (m_savingQuick || (event.modifiers & ui::ControlModifier))) {
            runAction();
            return true;
        }
        if (!inlineAction())
            return false;
        if (!m_suggestions.empty() && (event.key == Up || event.key == Down)) {
            const int count = int(m_suggestions.size());
            m_suggestionIndex = m_suggestionIndex < 0 ? (event.key == Down ? 0 : count - 1)
                : (m_suggestionIndex + (event.key == Down ? 1 : count - 1)) % count;
            requestPaint();
            return true;
        }
        if (enter && m_suggestionIndex >= 0) {
            chooseSuggestion(m_suggestionIndex);
            return true;
        }
        if (enter || event.key == Tab) {
            const bool reverse = event.modifiers & ui::ShiftModifier;
            const bool email = m_action->key == "email";
            if (email && (enter && m_activeToken == Recipient))
                runAction();
            else if (!email && enter)
                runAction();
            else if (!email)
                focusToken(m_activeToken == SearchEngine ? SearchTerms : SearchEngine);
            else
                focusToken((m_activeToken + (reverse ? 2 : 1)) % 3);
            return true;
        }
        return false;
    }
    if (event.key == Menu || (event.key == F4 && (event.modifiers & ui::ShiftModifier))) {
        openMenu({geometry().width - 198, kHeader - 6}, event.key == Menu ? m_selected : -1);
        return true;
    }
    if (event.key == Tab && (idle() || (event.modifiers & ui::ControlModifier))) {
        setMode(Mode(int(m_mode) == 4 ? 0 : int(m_mode) + 1));
        return true;
    }
    if (event.key == Down || event.key == Up || event.key == PageUp || event.key == PageDown) {
        const int step = grid() ? kColumns : 1;
        const int delta = event.key == Down ? step : event.key == Up ? -step : event.key == PageDown ? 4 * step : -4 * step;
        select(m_selected < 0 ? (delta > 0 ? 0 : int(m_tiles.size()) - 1) : m_selected + delta);
        return true;
    }
    if (grid() && field->text().empty() && (event.key == Left || event.key == Right)) {
        select(m_selected < 0 ? 0 : m_selected + (event.key == Left ? -1 : 1));
        return true;
    }
    if (event.key == Tab && m_selected >= 0 && m_results[m_selected].folder) {
        setFileFolder(m_results[m_selected].key);
        return true;
    }
    if ((event.key == Return || event.key == KeypadEnter) && field->preedit().empty()) {
        if (m_queryTimer.isActive())
            refreshResults();
        activate(std::max(m_selected, 0), bool(event.modifiers & ui::ControlModifier));
        return true;
    }
    return false;
}

void SpotlightView::rememberApp(const std::string &id)
{
    std::erase(m_recent, id);
    m_recent.insert(m_recent.begin(), id);
    if (m_recent.size() > 12)
        m_recent.resize(12);
    m_settings.setValue("Applications/Recent", m_recent);
    m_settings.sync();
}

void SpotlightView::openTarget(const std::string &target)
{
    if (target.empty())
        return;
    m_shell.launcher().launchCommand({"xdg-open", target}, {}, "org.cutefish.Spotlight");
    if (onDismiss)
        onDismiss();
}

void SpotlightView::activate(int row, bool alternate)
{
    if (row < 0 || row >= int(m_results.size()))
        return;
    const Result result = m_results[row];
    if (result.type == Mode::Applications) {
        rememberApp(result.app->id);
        m_shell.launcher().launch(*result.app);
        if (onDismiss)
            onDismiss();
    } else if (result.type == Mode::Files) {
        if (alternate && result.folder)
            setFileFolder(result.key);
        else
            openTarget(result.key);
    } else if (result.type == Mode::Actions) {
        if (alternate || result.key == "email" || result.key == "web") {
            showAction(result, alternate);
        } else if (result.key == "home") {
            openTarget(base::paths::home());
        } else if (result.key == "settings") {
            m_shell.openSettings({});
            if (onDismiss)
                onDismiss();
        } else if (result.app && !result.command.empty()) {
            m_shell.launcher().launchCommand(result.command, result.app->workingDirectory, result.app->id);
            if (onDismiss)
                onDismiss();
        }
    } else if (result.type == Mode::Clipboard) {
        auto &model = m_clipboard.model();
        for (int i = 0; i < model.count(); ++i) {
            if (model.keyAt(i) == result.key) {
                alternate ? m_clipboard.copy(i) : m_clipboard.paste(i);
                break;
            }
        }
    }
}

void SpotlightView::openMenu(ui::PointF point, int row)
{
    m_menu.close();
    m_menu.clear();
    if (row >= 0 && row < int(m_results.size())) {
        const Result result = m_results[row];
        switch (result.type) {
        case Mode::Applications:
            m_menu.addItem(tr("Open"), [this, app = result.app] {
                rememberApp(app->id);
                m_shell.launcher().launch(*app);
                if (onDismiss)
                    onDismiss();
            });
            if (m_dock) {
                const bool pinned = m_dock->applications().isDesktopPinned(result.key);
                m_menu.addItem(pinned ? tr("Remove from Dock") : tr("Keep in Dock"), [this, pinned, id = result.key] {
                    if (pinned)
                        m_dock->applications().removeItem(id);
                    else
                        m_dock->applications().addItem(id);
                });
            }
            break;
        case Mode::Files:
            m_menu.addItem(tr("Open"), [this, path = result.key] { openTarget(path); });
            if (result.folder)
                m_menu.addItem(tr("Search in This Folder"), [this, path = result.key] { setFileFolder(path); }, "Tab");
            else
                m_menu.addItem(tr("Open Containing Folder"), [this, path = result.key] {
                    openTarget(std::filesystem::path(path).parent_path().string());
                });
            break;
        case Mode::Actions:
            m_menu.addItem(tr("Add quick keys"), [this, result] { showAction(result, true); });
            break;
        case Mode::Clipboard: {
            auto &model = m_clipboard.model();
            const auto select = [this, k = result.key](bool copy) {
                auto &current = m_clipboard.model();
                for (int i = 0; i < current.count(); ++i) {
                    if (current.keyAt(i) == k) {
                        copy ? m_clipboard.copy(i) : m_clipboard.paste(i);
                        return;
                    }
                }
            };
            m_menu.setItemIcon(m_menu.addItem(tr("Paste"), [select] { select(false); }), "edit-paste");
            m_menu.setItemIcon(m_menu.addItem(tr("Copy"), [select] { select(true); }), "edit-copy");
            for (int i = 0; i < model.count(); ++i) {
                if (model.keyAt(i) != result.key)
                    continue;
                const auto &entry = model.entry(i);
                const std::string target = entry.files.size() == 1 ? entry.files.front() :
                    !spotlight::webDomain(entry.text).empty() ? std::string(base::strings::trimmed(entry.text)) : "";
                if (!target.empty())
                    m_menu.setItemIcon(m_menu.addItem(tr("Open"), [this, target] { openTarget(target); }), "document-open");
                break;
            }
            m_menu.addSeparator();
            m_menu.setItemIcon(m_menu.addItem(tr("Delete Item"), [this, k = result.key] { m_clipboard.store().remove(k); }),
                               "edit-delete");
            break;
        }
        default: break;
        }
    } else {
        if (m_mode == Mode::Applications) {
            auto *view = m_menu.addSubmenu(tr("View content as"));
            const auto as = [this](bool list) {
                if (m_list == list)
                    return;
                m_list = list;
                m_scroll = 0;
                m_settings.setValue("Applications/List", list);
                m_settings.sync();
                refreshLayout();
                fadeBody();
            };
            view->setItemChecked(view->addItem(tr("Grid"), [as] { as(false); }), !m_list);
            view->setItemChecked(view->addItem(tr("List"), [as] { as(true); }), m_list);
            m_menu.addSeparator();
        } else if (m_mode == Mode::Clipboard) {
            m_menu.setItemEnabled(m_menu.addItem(tr("Clear History…"), [this] {
                m_confirmClear = true;
                refreshLayout();
            }), !m_clipboard.model().historyEmpty());
            m_menu.setItemChecked(m_menu.addItem(tr("Clipboard Search (8 hours)"), [this] {
                const bool enable = !m_clipboard.store().isEnabled();
                m_settings.setValue("Clipboard/Enabled", enable);
                m_settings.sync();
                m_clipboard.store().setEnabled(enable);
                refreshResults();
            }), m_clipboard.store().isEnabled());
            m_menu.addSeparator();
        }
        for (int i = 0; i <= 4; ++i) {
            if (Mode(i) != m_mode)
                m_menu.addItem(nameFor(Mode(i)), [this, i] { setMode(Mode(i)); }, i ? modeModifierLabel() + "+" + std::to_string(i) : "");
        }
    }
    const auto size = m_menu.implicitSize(window());
    point.x = std::clamp(point.x, 0.0, std::max(0.0, geometry().width - size.width - 6));
    m_menu.popupAt(window(), point);
}

void SpotlightView::showAction(const Result &result, bool quick)
{
    m_action = result;
    m_actionCard.reset();
    m_status.clear();
    m_savingQuick = quick;
    m_updating = true;
    m_activeToken = -1;
    m_engine = 0;
    m_suggestionDismissed = false;
    m_suggestions.clear();
    for (auto *input : {m_parameter, m_subject, m_message}) {
        input->setFrameVisible(quick);
        input->setTextMargins(quick ? 8 : 6, quick ? 8 : 6);
        input->setFontSize(quick ? 14 : 20);
    }
    m_parameter->setPlaceholder(quick ? tr("Type a short key, or leave blank to remove")
                              : result.key == "email" ? tr("Recipients") : tr("Search terms"));
    m_parameter->setText(quick ? result.quick : "");
    m_subject->clear();
    m_subject->setPlaceholder(tr("Subject"));
    m_message->clear();
    m_message->setPlaceholder(tr("Body"));
    m_updating = false;
    refreshResults();
    fadeBody();
    if (quick) {
        m_parameter->setVisible(true);
        m_parameter->setFocus();
        m_parameter->selectAll();
    } else {
        focusToken(result.key == "email" ? Message : SearchTerms);
    }
}

void SpotlightView::exitAction()
{
    m_action.reset();
    m_actionCard.reset();
    m_suggestions.clear();
    m_status.clear();
    refreshResults();
    fadeBody();
    m_field->setVisible(true);
    focusSearch();
}

void SpotlightView::runAction()
{
    if (!m_action)
        return;
    const auto value = m_parameter->text();
    if (m_savingQuick) {
        const auto quick = toLower(value);
        if (quick.size() > 8 || !std::all_of(quick.begin(), quick.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        })) {
            m_status = tr("Use up to 8 letters or digits.");
        } else {
            const auto &id = m_action->key;
            for (const auto &key : m_settings.childKeys("QuickKeys")) {
                if (key != id && !quick.empty() && m_settings.stringValue("QuickKeys/" + key) == quick) {
                    m_status = tr("That quick key is already used.");
                    requestPaint();
                    return;
                }
            }
            m_settings.setValue("QuickKeys/" + id, quick);
            m_settings.sync();
            exitAction();
        }
        requestPaint();
        return;
    }
    if (m_actionCard) {
        openTarget(m_actionCard->target);
        return;
    }
    if (m_action->key == "email") {
        auto draft = spotlight::mailDraft(value, m_subject->text(), m_message->text());
        if (draft.empty()) {
            focusToken(Recipient);
            m_status = tr("Enter valid addresses, separated by commas.");
            requestPaint();
            return;
        }
        m_actionCard = ActionCard{m_subject->text().empty() ? tr("Email draft") : m_subject->text(),
                                  std::string(tr("Draft · To: ")) + value, std::move(draft)};
    } else {
        if (base::strings::trimmed(value).empty()) {
            focusToken(SearchTerms);
            m_status = tr("Enter a search term.");
            requestPaint();
            return;
        }
        const auto &[engine, url] = searchEngines[m_engine];
        m_actionCard = ActionCard{value, std::string(tr("Web search · ")) + engine,
                                  url + base::url::percentEncoded(base::strings::trimmed(value))};
    }
    m_status.clear();
    updateSuggestions();
    refreshLayout();
}

} // namespace cutefish::shell
