// SPDX-License-Identifier: BSD-3-Clause
#include "shell/controlcenter/control_layout.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <set>

#include "base/i18n.h"

namespace cutefish::shell::controls {

namespace {
constexpr unsigned bit(Size size) { return 1u << unsigned(size); }
constexpr unsigned kFlexible = bit(Size::Small) | bit(Size::Medium) | bit(Size::Large);
constexpr unsigned kCompact = bit(Size::Small) | bit(Size::Medium);

int number(std::string_view text)
{
    int value = -1;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc() && end == text.data() + text.size() ? value : -1;
}

bool freeAt(const std::vector<Tile> &tiles, const base::Rect &rect)
{
    return rect.x() >= 0 && rect.y() >= 0 && rect.x() + rect.width() <= Layout::kColumns
            && rect.y() + rect.height() <= Layout::kRows
            && std::none_of(tiles.begin(), tiles.end(), [&](const Tile &item) {
                return !item.bounds().intersected(rect).isEmpty();
            });
}

bool firstFree(const std::vector<Tile> &tiles, Tile &tile)
{
    for (int y = 0; y < Layout::kRows; ++y) {
        for (int x = 0; x < Layout::kColumns; ++x) {
            tile.cell = {x, y};
            if (freeAt(tiles, tile.bounds()))
                return true;
        }
    }
    return false;
}
} // namespace

const std::vector<Definition> &catalog()
{
    // Keep the keys stable across languages. Titles are translated by the views.
    static const std::vector<Definition> definitions {
        {"wifi", "Wi-Fi", "Connectivity", "network-wireless-connected-100", Size::Medium, kCompact, PinStyle::Visibility, true},
        {"bluetooth", "Bluetooth", "Connectivity", "bluetooth-symbolic", Size::Medium, kCompact, PinStyle::Visibility, true},
        {"airdrop", "AirDrop", "Connectivity", "network-transmit-receive-symbolic", Size::Medium, kCompact},
        {"screenshot", "Screenshot", "Screen Capture", "camera-photo-symbolic", Size::Small, kFlexible, PinStyle::Copy},
        {"record", "Screen Recording", "Screen Capture", "media-record-symbolic", Size::Small, kFlexible, PinStyle::Copy},
        {"display", "Display", "Display & Brightness", "brightness", Size::Wide, bit(Size::Wide), PinStyle::Visibility},
        {"mirror", "Screen Mirroring", "Display & Brightness", "video-display-symbolic", Size::Small, kCompact},
        {"dark", "Dark Mode", "Display & Brightness", "dark-mode", Size::Small, kCompact, PinStyle::Add, true},
        {"night", "Night Shift", "Display & Brightness", "weather-clear-night-symbolic", Size::Small, kCompact},
        {"true-tone", "True Tone", "Display & Brightness", "weather-clear-symbolic", Size::Small, kCompact},
        {"lock", "Lock Screen", "Lock Screen", "system-lock-screen-symbolic", Size::Small, kFlexible, PinStyle::Copy},
        {"screensaver", "Start Screen Saver", "Lock Screen", "preferences-desktop-screensaver-symbolic", Size::Small, kFlexible, PinStyle::Copy},
        {"display-sleep", "Sleep Display", "Lock Screen", "system-suspend-symbolic", Size::Small, kFlexible, PinStyle::Copy},
        {"home", "Scene or Accessory", "Home", "user-home-symbolic", Size::Large, kFlexible, PinStyle::Copy, true},
        {"shortcut", "Run Shortcut", "Shortcuts", "system-run-symbolic", Size::Medium, kFlexible, PinStyle::Copy, true},
        {"keyboard", "Full Keyboard Access", "Keyboard", "input-keyboard-symbolic", Size::Small, kCompact, PinStyle::Add, true},
        {"keyboard-light", "Keyboard Brightness", "Keyboard", "keyboard-brightness-symbolic", Size::Medium, kCompact},
        {"desktop", "Show Desktop", "Desktop & Windows", "user-desktop-symbolic", Size::Small, kFlexible, PinStyle::Copy},
        {"overview", "Window Overview", "Desktop & Windows", "view-grid-symbolic", Size::Large, kFlexible, PinStyle::Copy},
        {"stage", "Stage Manager", "Desktop & Windows", "view-list-symbolic", Size::Small, kFlexible},
        {"dock", "Automatically Hide the Dock", "Desktop & Windows", "go-up-symbolic", Size::Small, kFlexible},
        {"corners", "Corners", "Desktop & Windows", "view-grid-symbolic", Size::Medium, kFlexible, PinStyle::Copy},
        {"left-right", "Left & Right", "Desktop & Windows", "view-split-left-right-symbolic", Size::Medium, kFlexible, PinStyle::Copy},
        {"app-windows", "Application Windows", "Desktop & Windows", "view-restore-symbolic", Size::Small, kFlexible, PinStyle::Copy},
        {"note", "Quick Note", "Notes", "accessories-text-editor-symbolic", Size::Small, kFlexible, PinStyle::Copy, true},
        {"focus", "Focus", "Focus", "do-not-disturb", Size::Medium, kFlexible},
        {"battery", "Battery", "Battery", "battery-full-symbolic", Size::Medium, kCompact, PinStyle::Visibility},
        {"clock", "Clock", "Clock", "appointment-soon-symbolic", Size::Small, kFlexible, PinStyle::Add, true},
        {"widgets", "Edit Widgets", "Clock", "view-grid-symbolic", Size::Small, kCompact},
        {"sound", "Sound", "Sound", "audio-volume-high-symbolic", Size::Wide, bit(Size::Wide), PinStyle::Visibility},
        {"media", "Now Playing", "Now Playing", "media-playback-start-symbolic", Size::Large, bit(Size::Large), PinStyle::Visibility, true},
        {"recognize", "Recognize Music", "Now Playing", "audio-x-generic-symbolic", Size::Medium, kFlexible, PinStyle::Add, true},
        {"calculator", "Calculator", "Utilities", "accessories-calculator-symbolic", Size::Small, kFlexible, PinStyle::Copy, true},
        {"camera", "Camera", "Privacy", "camera", Size::Small, kCompact},
        {"microphone", "Microphone", "Privacy", "microphone", Size::Small, kCompact},
        {"accessibility", "Accessibility Shortcuts", "Accessibility", "preferences-desktop-accessibility-symbolic", Size::Medium, kFlexible},
        {"slow-keys", "Slow Keys", "Accessibility", "input-keyboard-symbolic", Size::Small, kCompact},
        {"contrast", "Increase Contrast", "Accessibility", "view-reveal-symbolic", Size::Small, kCompact},
        {"motion", "Reduce Motion", "Accessibility", "media-playback-pause-symbolic", Size::Small, kCompact},
        {"zoom", "Zoom", "Accessibility", "zoom-in-symbolic", Size::Small, kCompact},
        {"caption", "Live Captions", "Accessibility", "media-view-subtitles-symbolic", Size::Small, kCompact},
    };
    return definitions;
}

const Definition *find(std::string_view type)
{
    const auto &items = catalog();
    const auto it = std::find_if(items.begin(), items.end(), [&](const Definition &def) { return def.id == type; });
    return it == items.end() ? nullptr : &*it;
}

const std::vector<std::string> &pageIcons()
{
    static const std::vector<std::string> icons {"cc-grid", "cc-music", "cc-home", "cc-star", "cc-world"};
    return icons;
}

bool supports(const Definition &definition, Size size)
{
    return unsigned(size) <= unsigned(Size::Wide) && (definition.sizes & bit(size));
}

base::Size footprint(Size size)
{
    switch (size) {
    case Size::Wide: return {4, 1};
    case Size::Large: return {2, 2};
    case Size::Medium: return {2, 1};
    default: return {1, 1};
    }
}

const Tile *Page::tile(int id) const
{
    const auto it = std::find_if(tiles.begin(), tiles.end(), [id](const Tile &tile) { return tile.id == id; });
    return it == tiles.end() ? nullptr : &*it;
}

int Page::rows() const
{
    int rows = 0;
    for (const Tile &tile : tiles)
        rows = std::max(rows, tile.cell.y() + footprint(tile.size).height());
    return rows;
}

bool Layout::place(Page &page, Tile tile, base::Point target)
{
    std::vector<Tile> kept, displaced;
    for (const Tile &other : page.tiles) {
        if (other.id != tile.id)
            kept.push_back(other);
    }
    if (target == base::Point(-1, -1)) {
        if (!firstFree(kept, tile))
            return false;
    } else {
        tile.cell = target;
        if (!freeAt({}, tile.bounds()))
            return false;
        std::erase_if(kept, [&](const Tile &other) {
            if (other.bounds().intersected(tile.bounds()).isEmpty())
                return false;
            displaced.push_back(other);
            return true;
        });
    }
    kept.push_back(std::move(tile));
    for (Tile &other : displaced) {
        if (!firstFree(kept, other))
            return false;
        kept.push_back(std::move(other));
    }
    std::sort(kept.begin(), kept.end(), [](const Tile &a, const Tile &b) {
        return a.cell.y() * kColumns + a.cell.x() < b.cell.y() * kColumns + b.cell.x();
    });
    page.tiles = std::move(kept);
    return true;
}

Layout::Layout(std::string file)
    : m_settings(std::move(file))
{
    constexpr int kMaxStoredId = std::numeric_limits<int>::max() / 2;
    std::set<int> ids;
    for (const auto &key : m_settings.stringListValue("Pages/order")) {
        const int id = number(key);
        if (id < 0 || id >= kMaxStoredId || page(id) || m_pages.size() == kPages)
            continue;
        const std::string group = "Page-" + key + "/";
        std::string icon = m_settings.stringValue(group + "icon");
        if (std::find(pageIcons().begin(), pageIcons().end(), icon) == pageIcons().end())
            icon = pageIcons().front();
        Page &out = m_pages.emplace_back(Page {id, std::move(icon), {}});
        m_next = std::max(m_next, id + 1);
        for (const auto &entry : m_settings.stringListValue(group + "tiles")) {
            const int tileId = number(entry);
            if (tileId <= 0 || tileId >= kMaxStoredId || ids.contains(tileId))
                continue;
            const std::string prefix = group + entry + '-';
            const Definition *def = find(m_settings.stringValue(prefix + "type"));
            const auto point = m_settings.pointValue(prefix + "cell");
            const auto raw = m_settings.intValue(prefix + "size", -1);
            if (!def || !point || raw < 0 || raw > int(Size::Wide) || !supports(*def, Size(raw)))
                continue;
            if (point->x < 0 || point->x >= kColumns || point->y < 0 || point->y >= kRows)
                continue;
            Tile tile {tileId, def->id, Size(raw), {point->x, point->y}};
            if (freeAt(out.tiles, tile.bounds())) {
                out.tiles.push_back(std::move(tile));
                ids.insert(tileId);
                m_next = std::max(m_next, tileId + 1);
            }
        }
    }
    if (!page(0)) {
        if (m_pages.size() == kPages)
            m_pages.pop_back();
        auto &base = *m_pages.insert(m_pages.begin(), Page {0, pageIcons().front(), {}});
        const auto addAt = [&](const char *type, base::Point cell) {
            Layout::place(base, {m_next++, type, find(type)->initial, {}}, cell);
        };
        addAt("wifi", {0, 0});
        addAt("media", {2, 0});
        addAt("bluetooth", {0, 1});
        addAt("airdrop", {0, 2});
        addAt("desktop", {2, 2});
        addAt("mirror", {3, 2});
        addAt("dark", {0, 3});
        addAt("screenshot", {1, 3});
        addAt("focus", {2, 3});
        addAt("display", {0, 4});
        addAt("sound", {0, 5});
        addAt("microphone", {0, 6});
        addAt("clock", {1, 6});
        addAt("night", {2, 6});
        addAt("lock", {3, 6});
        addAt("record", {0, 7});
        addAt("camera", {1, 7});
        addAt("recognize", {2, 7});
    }
    const auto selected = m_settings.intValue("Pages/selected", 0);
    if (selected >= 0 && selected < kMaxStoredId && page(int(selected)))
        m_selected = int(selected);
}

const Page *Layout::page(int id) const
{
    const auto it = std::find_if(m_pages.begin(), m_pages.end(), [id](const Page &p) { return p.id == id; });
    return it == m_pages.end() ? nullptr : &*it;
}

Page *Layout::editablePage(int id) { return const_cast<Page *>(page(id)); }

void Layout::select(int id)
{
    if (id != m_selected && page(id)) {
        m_selected = id;
        save();
    }
}

int Layout::addPage()
{
    if (m_pages.size() == kPages)
        return -1;
    m_selected = m_next++;
    const auto &icons = pageIcons();
    m_pages.push_back({m_selected, icons[m_pages.size() % icons.size()], {}});
    save();
    return m_selected;
}

bool Layout::removePage(int id)
{
    if (!id || !std::erase_if(m_pages, [id](const Page &p) { return p.id == id; }))
        return false;
    if (m_selected == id)
        m_selected = 0;
    save();
    return true;
}

bool Layout::setIcon(int id, const std::string &icon)
{
    Page *p = editablePage(id);
    if (!p || std::find(pageIcons().begin(), pageIcons().end(), icon) == pageIcons().end())
        return false;
    p->icon = icon;
    save();
    return true;
}

bool Layout::add(int id, std::string_view type, base::Point cell)
{
    Page *p = editablePage(id);
    const auto *def = find(type);
    if (!p || !def || !place(*p, {m_next, def->id, def->initial, {}}, cell))
        return false;
    ++m_next;
    save();
    return true;
}

bool Layout::move(int id, int tile, base::Point target)
{
    Page *p = editablePage(id);
    const Tile *item = p ? p->tile(tile) : nullptr;
    if (!item || !place(*p, *item, target))
        return false;
    save();
    return true;
}

bool Layout::resize(int id, int tile, Size size)
{
    Page *p = editablePage(id);
    const Tile *item = p ? p->tile(tile) : nullptr;
    if (!item || !supports(*find(item->type), size))
        return false;
    Tile next = *item;
    next.size = size;
    next.cell.setX(std::min(next.cell.x(), kColumns - footprint(size).width()));
    next.cell.setY(std::min(next.cell.y(), kRows - footprint(size).height()));
    if (!place(*p, next, next.cell))
        return false;
    save();
    return true;
}

bool Layout::remove(int id, int tile)
{
    Page *p = editablePage(id);
    if (!p || !std::erase_if(p->tiles, [tile](const Tile &item) { return item.id == tile; }))
        return false;
    save();
    return true;
}

Pin Layout::pin(std::string_view type) const
{
    const auto raw = m_settings.intValue("Pins/" + std::string(type), -1);
    return raw >= int(Pin::Never) && raw <= int(Pin::Always) ? Pin(raw) : Pin::Never;
}

void Layout::setPin(std::string_view type, Pin pin)
{
    if (!find(type))
        return;
    m_settings.setValue("Pins/" + std::string(type), int(pin));
    m_settings.sync();
}

void Layout::save()
{
    std::vector<std::string> order;
    for (const std::string &group : m_settings.childGroups()) {
        if (group.starts_with("Page-"))
            m_settings.remove(group);
    }
    for (const Page &page : m_pages) {
        order.push_back(std::to_string(page.id));
        const std::string group = "Page-" + order.back() + '/';
        m_settings.setValue(group + "icon", page.icon);
        std::vector<std::string> tiles;
        for (const Tile &tile : page.tiles) {
            tiles.push_back(std::to_string(tile.id));
            const std::string prefix = group + tiles.back() + '-';
            m_settings.setValue(prefix + "type", tile.type);
            m_settings.setValue(prefix + "size", int(tile.size));
            m_settings.setValue(prefix + "cell", base::Settings::Point {tile.cell.x(), tile.cell.y()});
        }
        m_settings.setValue(group + "tiles", tiles);
    }
    m_settings.setValue("Pages/order", order);
    m_settings.setValue("Pages/selected", m_selected);
    m_settings.sync();
}

} // namespace cutefish::shell::controls
