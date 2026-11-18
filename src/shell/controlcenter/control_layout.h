// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "base/geometry.h"
#include "base/settings.h"

namespace cutefish::shell::controls {

enum class Size { Small, Medium, Large, Wide };
enum class Pin { Never, Active, Always };
enum class PinStyle { Add, Copy, Visibility };

struct Definition {
    const char *id;
    const char *title;
    const char *category;
    const char *icon;
    Size initial;
    unsigned sizes;
    PinStyle pin = PinStyle::Add;
    bool recommended = false;
};

const std::vector<Definition> &catalog();
const Definition *find(std::string_view type);
const std::vector<std::string> &pageIcons();
bool supports(const Definition &definition, Size size);
base::Size footprint(Size size);

struct Tile {
    int id;
    std::string type;
    Size size;
    base::Point cell;

    base::Rect bounds() const { return {cell, footprint(size)}; }
};

struct Page {
    int id;
    std::string icon;
    std::vector<Tile> tiles;

    const Tile *tile(int id) const;
    int rows() const;
};

class Layout
{
public:
    static constexpr int kColumns = 4;
    static constexpr int kRows = 24;
    static constexpr int kPages = 8;

    explicit Layout(std::string file);
    const std::vector<Page> &pages() const { return m_pages; }
    const Page *page(int id) const;
    int selected() const { return m_selected; }
    void select(int id);
    int addPage();
    bool removePage(int page);
    bool setIcon(int page, const std::string &icon);
    bool add(int page, std::string_view type, base::Point target = {-1, -1});
    bool move(int page, int tile, base::Point target);
    bool resize(int page, int tile, Size size);
    bool remove(int page, int tile);
    Pin pin(std::string_view type) const;
    bool hasPin(std::string_view type) const { return m_settings.contains("Pins/" + std::string(type)); }
    void setPin(std::string_view type, Pin pin);

    // The requested footprint is placed first, then displaced controls are
    // packed row-major. Invalid targets and unplaceable layouts are atomic.
    static bool place(Page &page, Tile tile, base::Point target);

private:
    Page *editablePage(int id);
    void save();

    base::Settings m_settings;
    std::vector<Page> m_pages;
    int m_selected = 0;
    int m_next = 1;
};

} // namespace cutefish::shell::controls
