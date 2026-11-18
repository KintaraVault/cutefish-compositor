// SPDX-License-Identifier: BSD-3-Clause
#include "shell/controlcenter/control_center_view.h"

#include <cmath>
#include <ranges>

#include "base/i18n.h"
#include "shell/controlcenter/control_paint.h"
#include "shell/controlcenter/level_slider.h"
#include "shell/controlcenter/panel_style.h"
#include "shell/controlcenter/tiles.h"
#include "ui/animation.h"
#include "ui/geometry_conversions.h"
#include "ui/menu.h"
#include "ui/search_field.h"
#include "ui/keys.h"

namespace cutefish::shell {
namespace {
using namespace control_paint;
using controls::Size;
using controls::Tile;
using i18n::tr;
using namespace std::chrono_literals;

constexpr double kInset = 6;
constexpr double kTop = 12;
constexpr double kCatalogGap = -1;
constexpr double kFooter = 57;
constexpr double kEditorHeader = 68;
constexpr ui::Color kGreen {42, 157, 75, 255};
constexpr auto kCanvasInk = ui::Color::rgb(0xffffff);
constexpr auto kCatalogInk = ui::Color::rgb(0x171622);

double radius(const ui::RectF &r)
{
    if (r.width > 3 * r.height)
        return r.height * .32;
    return r.height < 105 ? r.height / 2 : r.height * .24;
}

std::string folded(const std::string &text)
{
    std::string result = text;
    for (char &ch : result)
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
    return result;
}

ui::RectF centered(ui::RectF rect, double size)
{
    return {rect.x + (rect.width - size) / 2, rect.y + (rect.height - size) / 2, size, size};
}

std::string artworkFor(std::string_view type, bool white)
{
    if (type == "wifi")
        return {};
    if (type == "bluetooth" || type == "dark" || type == "focus" || type == "camera"
        || type == "microphone" || type == "media" || type == "sound" || type == "display") {
        return panel::artwork(controls::find(type)->icon, white);
    }
    return {};
}

void glyph(ui::Painter &painter, const controls::Definition &def, ui::RectF rect, ui::Color ink)
{
    if (std::string file = artworkFor(def.id, false); !file.empty()) {
        painter.drawSvg(file, rect, ink);
    } else if (std::string file = ui::themeIconPath(def.icon, 24, painter.scale()); file.ends_with(".svg")) {
        painter.drawSvg(file, rect, ink);
    } else {
        // Some icon themes ship only raster artwork or omit optional glyphs.
        const std::string title = tr(def.title);
        size_t end = 1;
        while (end < title.size() && (static_cast<unsigned char>(title[end]) & 0xc0) == 0x80)
            ++end;
        text(painter, rect, title.substr(0, end), 13, ink, true, true);
    }
}

void preview(ui::Painter &painter, const controls::Definition &def, Size size, const ui::RectF &r,
             bool dragging = false)
{
    const auto ink = kCanvasInk;
    const auto fill = dragging ? ui::Color {79, 79, 79, 240} : ui::Color {0, 0, 0, 57};
    if (dragging)
        painter.drawShadow(r, radius(r), 12, ui::Color {0, 0, 0, 100}, {0, 6});
    painter.fillRoundedRect(r, radius(r), fill);
    painter.strokeRoundedRect(inset(r, .5), radius(r) - .5, kCanvasInk.withAlpha(.29));
    if (def.id == "media" && size == Size::Large) {
        painter.fillRoundedRect({r.x + 12, r.y + 12, 36, 36}, 8, panel::textColor(.13));
        text(painter, {r.x + 13, r.bottom() - 64, r.width - 26, 20}, tr("Not Playing"), 9, ink, true);
        text(painter, {r.x, r.bottom() - 39, r.width, 27}, "⏮    ▶    ⏭", 13, ink, true, true);
    } else if (size == Size::Small) {
        glyph(painter, def, centered(r, 25), ink);
    } else if (size == Size::Wide) {
        text(painter, {r.x + 15, r.y + 8, r.width - 30, 24}, tr(def.title), 10, ink, true);
        const ui::RectF track {r.x + 14, r.y + r.height / 2, r.width - 28, 18};
        painter.fillRoundedRect(track, 9, panel::textColor(0.12));
        painter.fillRoundedRect({track.x, track.y, track.width * .53, track.height}, 9, panel::textColor(.65));
    } else {
        const bool large = size == Size::Large;
        const ui::RectF plate {r.x + 17, large ? r.y + 17 : center(r).y - 23, 46, 46};
        const bool network = std::string_view(def.category) == "Connectivity";
        painter.fillRoundedRect(plate, 23, network ? ui::Color::rgb(0x087cf4) : ink.withAlpha(.13));
        glyph(painter, def, centered(plate, 25), ink);
        const double x = large ? r.x + 17 : r.x + 73;
        text(painter, {x, large ? r.bottom() - 44 : r.y, r.right() - x - 9, large ? 34.0 : r.height},
             tr(def.title), 10, ink, true);
    }
}

void pageGlyph(ui::Painter &painter, int icon, ui::RectF rect, ui::Color ink)
{
    const auto c = center(rect);
    if (icon == 0) {
        for (int x : {-4, 4})
            for (int y : {-4, 4})
                painter.fillRoundedRect({c.x + x - 2.5, c.y + y - 2.5, 5, 5}, 2.5, ink);
    } else {
        static const char *symbols[] = {"", "♫", "⌂", "★", "◎"};
        text(painter, rect, symbols[icon], 15, ink, true, true);
    }
}
} // namespace

class ControlCenterView::Chrome final : public ui::View
{
public:
    explicit Chrome(ControlCenterView &owner) : v(owner)
    {
        setTracksHover(true);
        search = addChild<ui::SearchField>(tr("Search Controls"));
        search->setFill(ui::ColorRecipe(kCanvasInk));
        search->setGlyphColor(ui::ColorRecipe(kCatalogInk));
        search->setTextColor(ui::ColorRecipe(kCatalogInk));
        search->setPlaceholderColor(ui::ColorRecipe(kCatalogInk.withAlpha(.74)));
        search->setFontSize(15);
        search->setName("control-search");
        search->setVisible(false);
        search->onTextChanged = [this] { galleryScroll = 0; focusedPreview = 0; requestPaint(); };
    }

    bool editing = false;
    ui::SearchField *search;
    std::unique_ptr<ui::Menu> menu;
    std::optional<Tile> held;
    bool dragging = false;
    double galleryScroll = 0;
    double sidebarScroll = 0;
    double galleryHeight = 0;
    double sidebarHeight = 0;
    size_t focusedPreview = 0;
    std::string category;

    void cancelDrag()
    {
        held.reset();
        dragging = false;
        target.reset();
        v.requestLayout();
        requestPaint();
    }

protected:
    static bool contains(const ui::RectF &r, ui::PointF p) { return control_paint::contains(r, p); }
    bool acceptsPointer() const override { return true; }
    bool acceptsFocus() const override { return editing; }
    void layout() override
    {
        const auto r = v.catalogRect();
        search->setVisible(editing);
        if (editing) {
            search->setGeometry({r.x + 26, r.y + 26, sidebarWidth() - 52, 37});
            search->setOpacity(v.m_catalogReveal);
        }
    }

    void paint(ui::Painter &painter) override
    {
        const auto ink = kCanvasInk;
        const auto footer = editing ? v.editRect() : localRect();
        if (!editing) {
            if (contains(footer, hover))
                painter.fillRoundedRect(footer, footer.height / 2, ink.withAlpha(.10));
            text(painter, footer, tr("Edit Controls"), 11, ink, false, true);
            return;
        }

        painter.save();
        painter.clip(v.gridClip());
        for (int y = 0; y < v.rows(); ++y) {
            for (int x = 0; x < controls::Layout::kColumns; ++x) {
                if (std::ranges::any_of(v.page().tiles, [&](const Tile &tile) {
                        return tile.bounds().contains(base::Point(x, y));
                    }))
                    continue;
                const auto rect = v.tileRect(Size::Small, {x, y});
                painter.fillRoundedRect(rect, rect.height / 2, ink.withAlpha(.22));
            }
        }
        for (const Tile &tile : v.page().tiles) {
            if (dragging && held && held->id == tile.id)
                continue;
            const auto minus = minusRect(tile);
            sign(painter, minus, false, contains(minus, hover) ? ui::Color::rgb(0xd8dcef) : ui::Color::rgb(0xbac0d8), kCatalogInk);
        }
        if (target && held) {
            const auto r = v.tileRect(held->size, *target);
            const auto accent = ui::Theme::current().highlight;
            painter.fillRoundedRect(r, radius(r), accent.withAlpha(.22));
            painter.strokeRoundedRect(inset(r, 1), radius(r), accent, 2);
        }
        painter.restore();

        text(painter, {v.canvasX() + 24, kTop, 62, 52}, tr("Icon:"), 11, ink.withAlpha(.54));
        for (size_t i = 0; i < controls::pageIcons().size(); ++i) {
            const auto r = iconRect(i);
            const bool checked = v.page().icon == controls::pageIcons()[i];
            if (checked || contains(r, hover))
                painter.fillRoundedRect(r, 22, ink.withAlpha(checked ? .36 : .12));
            pageGlyph(painter, int(i), r, ink.withAlpha(checked ? .95 : .62));
        }
        if (v.m_model.pages().size() > 1) {
            for (size_t i = 0; i < v.m_model.pages().size(); ++i) {
                const auto r = pageRect(i);
                const bool selected = v.m_model.pages()[i].id == v.m_page;
                painter.fillRoundedRect(r, 11, ink.withAlpha(selected ? .23 : .08));
                text(painter, r, std::to_string(i + 1), 8, ink, selected, true);
            }
        }
        text(painter, footer, tr("Done"), 11, ink, true, true);
        paintGallery(painter);

        if (dragging && held) {
            const auto r = v.tileRect(held->size, {});
            preview(painter, *controls::find(held->type), held->size,
                    {hover.x - anchor.x, hover.y - anchor.y, r.width, r.height}, true);
        }
    }

    void hoverMove(const ui::PointerEvent &event) override { hover = event.position; requestPaint(); }
    void pointerLeave() override
    {
        if (!dragging) {
            hover = {-1, -1};
            requestPaint();
        }
    }

    void pointerPress(const ui::PointerEvent &event) override
    {
        hover = origin = event.position;
        if (!editing)
            return;
        if (event.button == ui::MouseButton::Right) {
            if (const Tile *tile = tileAt(hover)) {
                showMenu(*tile);
            } else if (v.m_page && contains({v.canvasX(), 0, v.canvasWidth(), kEditorHeader}, hover)) {
                menu = std::make_unique<ui::Menu>();
                menu->addItem(tr("Remove Page"), [this] {
                    v.m_model.removePage(v.m_page);
                    v.selectPage(0);
                });
                menu->popupAt(window(), mapToWindow(hover));
            }
            return;
        }
        if (event.button != ui::MouseButton::Left)
            return;
        setFocus();
        if (const Tile *tile = tileAt(hover)) {
            held = *tile;
            const auto rect = v.tileRect(tile->size, tile->cell);
            anchor = {hover.x - rect.x, hover.y - rect.y};
        } else if (const auto *item = previewAt(hover)) {
            held = Tile {0, item->def->id, item->size, {}};
            const auto rect = v.tileRect(held->size, {});
            anchor = {rect.width / 2, rect.height / 2};
        }
    }

    void pointerMove(const ui::PointerEvent &event) override
    {
        hover = event.position;
        if (held && !dragging && std::hypot(hover.x - origin.x, hover.y - origin.y) >= 5) {
            dragging = true;
            v.requestLayout();
        }
        if (dragging && held) {
            target.reset();
            if (contains(v.gridClip(), hover)) {
                const double step = v.cell() + v.gap();
                const auto size = controls::footprint(held->size);
                const int x = int(std::lround((hover.x - anchor.x - v.canvasX() - kInset) / step));
                const int y = int(std::lround((hover.y - anchor.y - v.gridTop() + v.m_scroll) / step));
                target = base::Point(std::clamp(x, 0, controls::Layout::kColumns - size.width()),
                                     std::clamp(y, 0, controls::Layout::kRows - size.height()));
            }
        }
        requestPaint();
    }

    void pointerRelease(const ui::PointerEvent &event) override
    {
        if (event.button != ui::MouseButton::Left)
            return;
        hover = event.position;
        if (dragging && held) {
            if (target && contains(v.gridClip(), hover)) {
                if (held->id ? v.m_model.move(v.m_page, held->id, *target)
                             : v.m_model.add(v.m_page, held->type, *target)) {
                    v.changed();
                }
            }
            cancelDrag();
            return;
        }
        const auto footer = editing ? v.editRect() : localRect();
        if (contains(footer, hover) && contains(footer, origin)) {
            v.edit(!editing);
        } else if (editing) {
            if (held && held->id && contains(minusRect(*held), hover) && contains(minusRect(*held), origin)) {
                if (v.m_model.remove(v.m_page, held->id))
                    v.changed();
            } else if (held && !held->id) {
                if (const auto *item = previewAt(hover); item && item->def->id == held->type)
                    addPreview(*item);
            } else {
                for (const auto &[name, r] : categories)
                    if (contains(r, hover) && contains(sidebarClip(), hover)) {
                        category = name;
                        search->setText({});
                        galleryScroll = 0;
                        focusedPreview = 0;
                        break;
                    }
                for (size_t i = 0; i < controls::pageIcons().size(); ++i)
                    if (contains(iconRect(i), hover) && v.m_model.setIcon(v.m_page, controls::pageIcons()[i]))
                        v.changed(false);
                if (v.m_model.pages().size() > 1)
                    for (size_t i = 0; i < v.m_model.pages().size(); ++i)
                        if (contains(pageRect(i), hover)) {
                            v.selectPage(v.m_model.pages()[i].id);
                            break;
                        }
            }
        }
        cancelDrag();
    }

    bool scroll(const ui::ScrollEvent &event) override
    {
        if (editing && contains(v.catalogRect(), hover)) {
            if (contains(sidebarClip(), hover)) {
                sidebarScroll = std::clamp(sidebarScroll + event.deltaY, 0.0,
                                           std::max(0.0, sidebarHeight - sidebarClip().height));
            } else {
                galleryScroll = std::clamp(galleryScroll + event.deltaY, 0.0,
                                           std::max(0.0, galleryHeight - galleryClip().height));
            }
            requestPaint();
            return true;
        }
        return v.scroll(event);
    }

    bool keyPress(const ui::KeyEvent &event) override
    {
        if (!editing || previews.empty())
            return false;
        if (event.key == ui::key::Down || event.key == ui::key::Up) {
            const int step = event.key == ui::key::Down ? 1 : -1;
            focusedPreview = size_t(std::clamp(int(focusedPreview) + step, 0, int(previews.size() - 1)));
            const auto r = previews[focusedPreview].rect;
            const auto clip = galleryClip();
            galleryScroll += r.y < clip.y ? r.y - clip.y : std::max(0.0, r.bottom() + 25 - clip.bottom());
            galleryScroll = std::clamp(galleryScroll, 0.0, std::max(0.0, galleryHeight - clip.height));
            requestPaint();
            return true;
        }
        if (event.key == ui::key::Return) {
            addPreview(previews[std::min(focusedPreview, previews.size() - 1)]);
            return true;
        }
        return false;
    }

private:
    struct Preview { const controls::Definition *def; Size size; ui::RectF rect; };

    double sidebarWidth() const { return std::clamp(v.catalogWidth() / 3, 118.0, 321.0); }
    ui::RectF sidebarClip() const
    {
        const auto r = v.catalogRect();
        return {r.x + 23, r.y + 76, sidebarWidth() - 46, std::max(0.0, r.height - 82)};
    }
    ui::RectF galleryClip() const
    {
        const auto r = v.catalogRect();
        return {r.x + sidebarWidth() + 1, r.y + 12, r.width - sidebarWidth() - 9, r.height - 24};
    }
    ui::RectF iconRect(size_t i) const
    {
        const double width = (v.canvasWidth() - kInset * 2 - 80) / 5;
        return {v.canvasX() + kInset + 80 + width * i + 3, kTop + 5, width - 8, 42};
    }
    ui::RectF pageRect(size_t i) const
    {
        return {v.canvasX() + 9 + 27 * i, v.editRect().y + 5, 23, 23};
    }
    ui::RectF minusRect(const Tile &tile) const
    {
        const auto it = v.m_tiles.find(tile.id);
        if (it == v.m_tiles.end())
            return {};
        auto r = v.tileRect(tile.size, tile.cell);
        const auto pos = it->second->mapToWindow({0, 0});
        const auto top = mapToWindow({0, 0});
        return {pos.x - top.x + (r.width == r.height ? 5 : 0), pos.y - top.y + 1, 22, 22};
    }
    const Tile *tileAt(ui::PointF where) const
    {
        if (!contains(v.gridClip(), where))
            return nullptr;
        for (const auto &tile : v.page().tiles)
            if (contains(v.tileRect(tile.size, tile.cell), where) || contains(minusRect(tile), where))
                return &tile;
        return nullptr;
    }
    const Preview *previewAt(ui::PointF where) const
    {
        if (!contains(galleryClip(), where))
            return nullptr;
        for (const auto &preview : previews) {
            auto r = preview.rect;
            r.height += 22;
            if (contains(r, where))
                return &preview;
        }
        return nullptr;
    }

    void addPreview(const Preview &item)
    {
        if (v.m_model.add(v.m_page, item.def->id))
            v.changed();
    }

    void showMenu(const Tile &tile)
    {
        const auto *def = controls::find(tile.type);
        const int id = tile.id;
        menu = std::make_unique<ui::Menu>();
        menu->setMinimumWidth(204);
        menu->setItemEnabled(menu->addItem(tr("Size"), {}), false);
        for (const auto &[label, size] : {std::pair {"Small", Size::Small}, {"Medium", Size::Medium},
                                         {"Large", Size::Large}}) {
            const auto index = menu->addItem(tr(label), [this, id, size] {
                if (v.m_model.resize(v.m_page, id, size))
                    v.changed();
            });
            menu->setItemEnabled(index, controls::supports(*def, size));
            menu->setItemChecked(index, tile.size == size || (size == Size::Large && tile.size == Size::Wide));
        }
        menu->addSeparator();
        auto pin = v.m_model.pin(def->id);
        const auto setPin = [this, def](controls::Pin mode) {
            v.m_model.setPin(def->id, mode);
            v.changed(false);
        };
        if (def->pin == controls::PinStyle::Visibility) {
            menu->setItemEnabled(menu->addItem(tr("Show in Menu Bar"), {}), false);
            for (const auto &[label, mode] : {std::pair {"Always", controls::Pin::Always},
                                             {"When Active", controls::Pin::Active}, {"Never", controls::Pin::Never}})
                menu->setItemChecked(menu->addItem(tr(label), [setPin, mode] { setPin(mode); }), pin == mode);
        } else {
            const char *label = pin != controls::Pin::Never ? "Remove from Menu Bar"
                    : def->pin == controls::PinStyle::Copy ? "Copy to Menu Bar" : "Add to Menu Bar";
            menu->addItem(tr(label), [setPin, pin] {
                setPin(pin == controls::Pin::Never ? controls::Pin::Always : controls::Pin::Never);
            });
        }
        menu->addSeparator();
        menu->addItem(tr("Remove Control"), [this, id] {
            if (v.m_model.remove(v.m_page, id))
                v.changed();
        });
        menu->popupAt(window(), mapToWindow(hover));
    }

    void paintGallery(ui::Painter &painter)
    {
        const auto r = v.catalogRect();
        const auto ink = kCatalogInk;
        const auto soft = ink.withAlpha(.80);
        painter.save();
        painter.multiplyOpacity(v.m_catalogReveal);
        painter.clipRoundedRect(r, 31);
        painter.fillRect({r.x, r.y, sidebarWidth(), r.height}, kCanvasInk.withAlpha(.045));
        painter.fillRect({r.x + sidebarWidth(), r.y, 1, r.height}, ink.withAlpha(.16));

        categories.clear();
        previews.clear();
        const auto side = sidebarClip();
        painter.save();
        painter.clip(side);
        double y = side.y - sidebarScroll;
        const auto categoryRow = [&](const std::string &name, size_t index) {
            const ui::RectF row {side.x + 3, y, side.width - 6, 60};
            categories.emplace_back(name, row);
            const bool selected = search->text().empty() && name == category;
            if (selected || (contains(row, hover) && contains(side, hover)))
                painter.fillRoundedRect(row, 12, ink.withAlpha(selected ? .19 : .08));
            static const int colors[] = {0x1685F7, 0x7657ED, 0xE75362, 0xF49E22, 0x57AF70, 0x6473ED};
            const ui::RectF badge {row.x + 13, row.y + 13, 34, 34};
            if (name.empty())
                pageGlyph(painter, 0, badge, ink);
            else if (const auto it = std::ranges::find_if(controls::catalog(), [&](const auto &def) {
                         return name == def.category;
                     }); it != controls::catalog().end()) {
                painter.fillRoundedRect(badge, 8, ui::Color::rgb(colors[index % 6]));
                glyph(painter, *it, centered(badge, 21), kCanvasInk);
            }
            text(painter, {row.x + 62, row.y, row.width - 66, row.height}, name.empty() ? tr("All Controls") : tr(name.c_str()),
                 12, ink);
            y += 60;
        };
        categoryRow({}, 0);
        std::vector<std::string> groups;
        for (const auto &def : controls::catalog()) {
            if (std::ranges::find(groups, def.category) == groups.end()) {
                groups.emplace_back(def.category);
                categoryRow(groups.back(), groups.size());
            }
        }
        sidebarHeight = y - side.y + sidebarScroll;
        painter.restore();

        const auto clip = galleryClip();
        painter.save();
        painter.clip(clip);
        const double left = clip.x + 19;
        const double width = std::max(52.0, clip.width - 36);
        const double gap = 16;
        const int columns = width >= 510 ? 6 : width >= 232 ? 4 : 2;
        const double unit = std::min(83.0, (width - gap * (columns - 1)) / columns);
        y = clip.y + 14 - galleryScroll;
        const auto title = [&](std::string name, double size) {
            text(painter, {left, y, width, 34}, std::move(name), size, ink, true);
            y += 36;
        };
        const auto addPreviewAt = [&](const controls::Definition *def, ui::RectF rect) {
            previews.push_back({def, def->initial, rect});
            if (rect.bottom() + 25 < clip.y || rect.y >= clip.bottom())
                return;
            preview(painter, *def, def->initial, rect);
            text(painter, {rect.x - 4, rect.bottom() + 3, rect.width + 8, 22}, tr(def->title), 10, ink, false, true);
            const bool selected = previews.size() - 1 == focusedPreview && hasFocus();
            if (selected || (contains(rect, hover) && contains(clip, hover)))
                sign(painter, {rect.x - 4, rect.y - 5, 25, 25}, true, kGreen, kCanvasInk);
        };
        const auto section = [&](const char *heading, const std::vector<const controls::Definition *> &items) {
            if (items.empty())
                return;
            painter.fillRect({left, y, width, 1}, ink.withAlpha(.16));
            title(tr(heading), 11);
            double x = left;
            double height = 0;
            for (const auto *def : items) {
                const auto footprint = controls::footprint(def->initial);
                const double w = std::min(width, unit * footprint.width() + gap * (footprint.width() - 1));
                const double h = unit * footprint.height() + gap * (footprint.height() - 1);
                if (x > left && x + w > left + width) {
                    y += height + 33;
                    x = left;
                    height = 0;
                }
                addPreviewAt(def, {x, y, w, h});
                height = std::max(height, h);
                x += w + gap;
            }
            y += height + 45;
        };

        const auto query = folded(search->text());
        const auto matching = [&](auto predicate) {
            std::vector<const controls::Definition *> result;
            for (const auto &def : controls::catalog())
                if (predicate(def))
                    result.push_back(&def);
            return result;
        };
        if (!query.empty()) {
            const auto results = matching([&](const auto &def) {
                return folded(tr(def.title)).find(query) != std::string::npos
                        || folded(tr(def.category)).find(query) != std::string::npos;
            });
            if (results.empty())
                title(tr("No Controls Found"), 14);
            else
                section("Search Results", results);
        } else if (!category.empty()) {
            section(category.c_str(), matching([&](const auto &def) { return category == def.category; }));
        } else {
            const bool artwork = width >= 370;
            const double height = artwork ? 221 : 99;
            const double copyWidth = artwork ? width * .52 - 32 : width - 32;
            painter.fillRoundedRect({left, y, width, height}, 8, ink.withAlpha(.09));
            text(painter, {left + 26, y + (artwork ? 36 : 10), copyWidth, 30}, tr("What’s New"), 15, ink, true);
            const double introY = y + (artwork ? 67 : 42);
            text(painter, {left + 26, introY, copyWidth, 20}, tr("Make Control Center your own."), 11, ink);
            text(painter, {left + 26, introY + 22, copyWidth, 20}, tr("Resize, move, and pin controls."), 11, soft);
            if (artwork) {
                const ui::RectF screen {left + width * .52, y + 17, width * .48 - 16, height - 34};
                painter.fillRoundedRect(screen, 5, ui::LinearGradient::vertical(screen,
                        ui::Color::rgb(0x558f9b), ui::Color::rgb(0x995c82)));
                painter.fillRoundedRect({screen.x + 3, screen.y + 3, screen.width - 6, 8}, 3, panel::textColor(.17));
                const double tile = std::min((screen.width - 22) / 11, 13.0);
                const double x = screen.right() - tile * 4 - 12;
                const double top = screen.y + 16;
                const ui::RectF gallery {screen.x + 10, top + 14, x - screen.x - 16, screen.height - 41};
                painter.fillRoundedRect(gallery, 5, panel::textColor(.17));
                painter.fillRect({gallery.x + gallery.width * .29, gallery.y, 1, gallery.height}, panel::textColor(.20));
                for (int row = 0; row < 6; ++row) {
                    painter.fillRoundedRect({gallery.x + 4, gallery.y + 6 + row * 9., gallery.width * .20, 3}, 1.5,
                                            panel::textColor(.24));
                    for (int col = 0; col < 4; ++col) {
                        const double unit = gallery.width * .15;
                        painter.fillRoundedRect({gallery.x + gallery.width * .34 + col * unit,
                                                 gallery.y + 6 + row * 9., unit - 3, 6}, 3, panel::textColor(.26));
                    }
                }
                for (int row = 0; row < 7; ++row) {
                    const int wide = row == 3 || row == 4 ? 4 : row < 2 ? 2 : 1;
                    for (int col = 0; col < 4; col += wide)
                        painter.fillRoundedRect({x + col * tile, top + row * 14., wide * tile - 3, 11}, 5, panel::textColor(.29));
                }
            }
            y += height + 13;
            if (columns == 6) {
                title(tr("Recommendations"), 11);
                const double top = y + 14;
                const double step = unit + gap;
                const auto recommendation = [&](const char *id, int column, double dy) {
                    const auto *def = controls::find(id);
                    const auto span = controls::footprint(def->initial);
                    addPreviewAt(def, {left + 8 + step * column, top + dy,
                                      step * span.width() - gap, step * span.height() - gap});
                };
                recommendation("home", 0, 0);
                recommendation("keyboard", 2, 0);
                recommendation("clock", 3, 0);
                recommendation("wifi", 4, 0);
                recommendation("shortcut", 2, 144);
                recommendation("media", 4, 157);
                recommendation("calculator", 0, 270);
                recommendation("note", 1, 270);
                recommendation("recognize", 2, 270);
                y = top + 427;
            } else {
                section("Recommendations", matching([](const auto &def) { return def.recommended; }));
            }
            for (const auto &group : groups)
                section(group.c_str(), matching([&](const auto &def) { return group == def.category; }));
        }
        galleryHeight = y - clip.y + galleryScroll;
        painter.restore();
        painter.restore();
    }

    ControlCenterView &v;
    ui::PointF hover {-1, -1};
    ui::PointF origin;
    ui::PointF anchor;
    std::optional<base::Point> target;
    std::vector<Preview> previews;
    std::vector<std::pair<std::string, ui::RectF>> categories;
};

ControlCenterView::ControlCenterView(ControlCenterServices &services, controls::Layout &model, int pageId,
                                     int maximumWidth, int maximumHeight)
    : m_services(services)
    , m_model(model)
    , m_page(pageId)
    , m_maximumWidth(maximumWidth)
    , m_maximumHeight(maximumHeight)
{
    m_users = addChild<DeviceUsersTile>();
    m_grid = addChild<ui::View>();
    m_grid->setClipsChildren(true);
    m_chrome = addChild<Chrome>(*this);
    rebuild();
}

ControlCenterView::~ControlCenterView() = default;

const controls::Page &ControlCenterView::page() const { return *m_model.page(m_page); }

bool ControlCenterView::editing() const { return m_chrome->editing; }

double ControlCenterView::canvasWidth() const
{
    return std::min({kWidth, double(m_maximumWidth), std::max(208.0, (m_maximumHeight - 60) / 2.0)});
}

double ControlCenterView::catalogWidth() const
{
    return editing() ? std::clamp(m_maximumWidth - canvasWidth() - 24, 0.0, 961.0) : 0;
}

double ControlCenterView::canvasX() const { return editing() ? catalogWidth() + kCatalogGap : 0; }
double ControlCenterView::windowWidth() const { return canvasX() + canvasWidth(); }
double ControlCenterView::gap() const { return std::round(canvasWidth() * .041); }
double ControlCenterView::cell() const { return (canvasWidth() - 2 * kInset - 3 * gap()) / 4; }

double ControlCenterView::gridTop() const
{
    return kTop + (editing() ? kEditorHeader : m_users->isVisible()
                    ? m_users->sizeHint().height + 9 : 0);
}

int ControlCenterView::rows() const { return std::max(editing() ? 8 : 1, page().rows()); }
double ControlCenterView::gridHeight() const { return rows() * (cell() + gap()) - gap(); }
double ControlCenterView::gridViewport() const
{
    return std::max(cell(), std::min(gridHeight(), m_maximumHeight - gridTop() - kFooter));
}

double ControlCenterView::windowHeight(double) const
{
    return std::ceil(editing() ? m_maximumHeight : gridTop() + gridViewport() + kFooter);
}

ui::RectF ControlCenterView::gridClip() const { return {canvasX(), gridTop(), canvasWidth(), gridViewport()}; }

ui::RectF ControlCenterView::tileRect(Size size, base::Point point) const
{
    const auto dim = controls::footprint(size);
    const double step = cell() + gap();
    return {canvasX() + kInset + point.x() * step, gridTop() + point.y() * step - m_scroll,
            dim.width() * step - gap(), dim.height() * step - gap()};
}

ui::RectF ControlCenterView::catalogRect() const { return {0, 23, catalogWidth(), windowHeight(1) - 23}; }

ui::RectF ControlCenterView::editRect() const
{
    const double width = editing() ? 86 : 172;
    return {canvasX() + (editing() ? canvasWidth() - width - kInset : (canvasWidth() - width) / 2),
            gridTop() + gridViewport() + 20, width, 32};
}

void ControlCenterView::rebuild(bool animate)
{
    if (m_transition)
        m_transition->stop();
    m_previous.clear();
    if (animate)
        for (const auto &[id, view] : m_tiles)
            m_previous.emplace(id, view->geometry());

    std::erase_if(m_tiles, [&](const auto &entry) {
        if (page().tile(entry.first))
            return false;
        m_grid->takeChild(entry.second);
        return true;
    });
    for (const auto &tile : page().tiles) {
        if (m_tiles.contains(tile.id))
            continue;
        const std::string &type = tile.type;
        ui::View *view;
        if (type == "media") {
            auto *media = m_grid->addChild<MediaTile>();
            media->setControlStyle(true);
            media->onPrevious = [this] { m_services.mediaPrevious(); };
            media->onPlay = [this] { m_services.mediaPlay(); };
            media->onPause = [this] { m_services.mediaPause(); };
            media->onNext = [this] { m_services.mediaNext(); };
            view = media;
        } else if (type == "display" || type == "sound") {
            const bool display = type == "display";
            auto *tile = m_grid->addChild<TileSlider>(tr(controls::find(type)->title));
            tile->setControlStyle(true);
            tile->setHasDetail(!display);
            tile->onDetail = [this] { if (onSoundDetail) onSoundDetail(); };
            tile->slider()->setRange(display ? 1 : 0, display ? 100 : 1, display ? 1 : .01);
            tile->slider()->onMoved = [this, display](double value) {
                if (display && m_services.brightnessEnabled())
                    m_services.setBrightness(int(std::lround(value)));
                else if (!display && m_services.hasSink()) {
                    m_services.setVolume(value);
                    m_services.setMuted(value == 0);
                }
            };
            view = tile;
        } else {
            const auto *def = controls::find(type);
            auto *card = m_grid->addChild<CardItem>();
            card->setTitle(tr(def->title));
            if (artworkFor(type, false).empty())
                card->setThemeGlyph(def->icon, type == "wifi");
            else
                card->setArtwork(def->icon);
            card->setHasDetail(type == "wifi" || type == "bluetooth" || type == "battery");
            card->onClicked = [this, type] { if (activate) activate(type, false); };
            card->onDetail = [this, type] {
                if (type == "wifi" && onWirelessDetail)
                    onWirelessDetail();
                else if (type == "bluetooth" && onBluetoothDetail)
                    onBluetoothDetail();
                else if (activate)
                    activate(type, true);
            };
            view = card;
        }
        view->setName(type);
        m_tiles.emplace(tile.id, view);
    }

    m_progress = 1;
    if (animate && window()) {
        m_transition = std::make_unique<ui::Animation>(window());
        m_transition->setDuration(240ms);
        m_transition->setEasing(ui::Animation::Easing::OutCubic);
        m_transition->onUpdate = [this](double t) { m_progress = t; requestLayout(); m_chrome->requestPaint(); };
        m_progress = 0;
        m_transition->start();
    }
    refresh();
}

void ControlCenterView::changed(bool animate)
{
    rebuild(animate);
    if (onModelChanged)
        onModelChanged();
    if (onHeightChanged)
        onHeightChanged();
    m_chrome->requestPaint();
}

void ControlCenterView::reveal()
{
    if (!window())
        return;
    m_transition = std::make_unique<ui::Animation>(window());
    m_transition->setDuration(250ms);
    m_transition->setEasing(ui::Animation::Easing::OutCubic);
    m_previous.clear();
    m_progress = 0;
    m_transition->onUpdate = [this](double t) { m_progress = t; requestLayout(); };
    m_transition->start();
}

void ControlCenterView::edit(bool enabled)
{
    if (editing() == enabled || (enabled && m_maximumWidth < 490))
        return;
    m_chrome->cancelDrag();
    m_chrome->editing = enabled;
    m_chrome->galleryScroll = m_chrome->sidebarScroll = 0;
    m_chrome->category.clear();
    m_chrome->search->setText({});
    m_scroll = 0;
    m_catalogReveal = 1;
    if (enabled && window()) {
        m_catalogReveal = 0;
        m_catalogTween = std::make_unique<ui::Animation>(window());
        m_catalogTween->setDuration(255ms);
        m_catalogTween->setEasing(ui::Animation::Easing::OutCubic);
        m_catalogTween->onUpdate = [this](double t) {
            m_catalogReveal = t;
            requestLayout();
            m_chrome->requestLayout();
            m_chrome->requestPaint();
        };
        m_catalogTween->start();
    } else if (m_catalogTween) {
        m_catalogTween->stop();
    }
    changed(false);
}

bool ControlCenterView::escapeEditor()
{
    if (!editing())
        return false;
    if (m_chrome->held)
        m_chrome->cancelDrag();
    else
        edit(false);
    return true;
}

void ControlCenterView::selectPage(int id)
{
    if (!m_model.page(id))
        return;
    m_model.select(id);
    m_page = id;
    m_scroll = 0;
    changed(false);
    reveal();
}

void ControlCenterView::addPage()
{
    const int id = m_model.addPage();
    if (id >= 0)
        selectPage(id);
}

void ControlCenterView::retranslate()
{
    m_chrome->search->setPlaceholder(tr("Search Controls"));
    m_users->retranslate();
    refresh();
}

void ControlCenterView::refresh()
{
    const auto previousHeight = m_users->isVisible() ? m_users->sizeHint().height : 0;
    m_users->setUsers(m_services.deviceUsers());
    m_users->setVisible(!editing() && !m_users->users().empty());
    for (const auto &tile : page().tiles) {
        const auto it = m_tiles.find(tile.id);
        if (it == m_tiles.end())
            continue;
        const auto *def = controls::find(tile.type);
        const State live = state ? state(tile.type) : State {};
        if (auto *card = dynamic_cast<CardItem *>(it->second)) {
            card->setTitle(tr(def->title));
            card->setChecked(live.active);
            card->setStatus(live.status.empty() && !live.available ? tr("Unavailable") : live.status);
            card->setEnabled(live.available);
            card->setControlStyle(tile.size == Size::Small, tile.size == Size::Large);
        } else if (auto *slider = dynamic_cast<TileSlider *>(it->second)) {
            slider->setTitle(live.available ? tr(def->title) : std::string(tr(def->title)) + " · " + tr("Unavailable"));
            slider->slider()->setEnabled(live.available);
            const bool display = tile.type == "display";
            slider->slider()->setValue(display ? double(m_services.brightness()) : live.active ? m_services.volume() : 0);
            slider->slider()->setIcon(panel::artwork(display ? "brightness" : volumeIconName(live.active, m_services.volume()), false));
        } else if (auto *media = dynamic_cast<MediaTile *>(it->second)) {
            media->setState(m_services.media());
        }
    }
    requestLayout();
    if (onHeightChanged && previousHeight != (m_users->isVisible() ? m_users->sizeHint().height : 0))
        onHeightChanged();
}

ui::View *ControlCenterView::control(std::string_view type) const
{
    for (const auto &tile : page().tiles)
        if (tile.type == type)
            return m_tiles.at(tile.id);
    return nullptr;
}

CardItem *ControlCenterView::wirelessCard() const { return static_cast<CardItem *>(control("wifi")); }
CardItem *ControlCenterView::bluetoothCard() const { return static_cast<CardItem *>(control("bluetooth")); }
CardItem *ControlCenterView::darkModeCard() const { return static_cast<CardItem *>(control("dark")); }
CardItem *ControlCenterView::doNotDisturbCard() const { return static_cast<CardItem *>(control("focus")); }
CardItem *ControlCenterView::cameraCard() const { return static_cast<CardItem *>(control("camera")); }
MediaTile *ControlCenterView::media() const { return static_cast<MediaTile *>(control("media")); }
TileSlider *ControlCenterView::brightnessSlider() const { return static_cast<TileSlider *>(control("display")); }
TileSlider *ControlCenterView::soundSlider() const { return static_cast<TileSlider *>(control("sound")); }

void ControlCenterView::layout()
{
    m_scroll = std::clamp(m_scroll, 0.0, std::max(0.0, gridHeight() - gridViewport()));
    const auto clip = gridClip();
    m_grid->setGeometry(clip);
    m_glassTiles.clear();
    for (const auto &tile : page().tiles) {
        auto *view = m_tiles.at(tile.id);
        auto r = translated(tileRect(tile.size, tile.cell), -clip.x, -clip.y);
        if (m_progress < 1) {
            if (const auto it = m_previous.find(tile.id); it != m_previous.end()) {
                const auto &from = it->second;
                r = {std::lerp(from.x, r.x, m_progress), std::lerp(from.y, r.y, m_progress),
                     std::lerp(from.width, r.width, m_progress), std::lerp(from.height, r.height, m_progress)};
            } else {
                r = translated(r, 0, -12 * (1 - m_progress));
            }
        }
        view->setGeometry(r);
        const bool dragging = m_chrome->dragging && m_chrome->held && m_chrome->held->id == tile.id;
        view->setVisible(!dragging);
        view->setOpacity(m_previous.contains(tile.id) ? 1 : .55 + .45 * m_progress);
        if (!dragging)
            m_glassTiles.push_back(translated(r, clip.x, clip.y));
    }
    if (m_users->isVisible()) {
        const auto hint = m_users->sizeHint();
        const double width = std::min(canvasWidth() - 2 * kInset, hint.width);
        m_users->setGeometry({(canvasWidth() - width) / 2, kTop, width, hint.height});
    }
    // In live mode the chrome leaves the native sliders and media interactive.
    m_chrome->setGeometry(editing() ? localRect() : editRect());
    if (onGlassChanged)
        onGlassChanged();
}

ui::Path ControlCenterView::glassShape() const
{
    ui::Path mask;
    for (const auto &r : m_glassTiles)
        mask.addRoundedRect(r, radius(r), radius(r));
    ui::Path clip;
    clip.addRect(gridClip());
    mask = mask.intersected(clip);
    if (editing()) {
        mask.addRoundedRect(catalogRect(), 31, 31);
        mask.addRoundedRect({canvasX() + kInset, kTop, canvasWidth() - 2 * kInset, 52}, 26, 26);
    } else if (m_users->isVisible()) {
        mask.addRoundedRect(m_users->geometry(), 18, 18);
    }
    const auto footer = editRect();
    mask.addRoundedRect(footer, footer.height / 2, footer.height / 2);
    return mask;
}

std::vector<render::GlassLens> ControlCenterView::glassLenses() const
{
    std::vector<render::GlassLens> pieces;
    const auto add = [&](const ui::RectF &r, double corners, float tint = -1, float opacity = 1) {
        pieces.push_back({ui::toBase(r), corners, tint, opacity});
    };
    const auto clip = ui::toBase(gridClip());
    for (const auto &tile : m_glassTiles)
        if (clip.intersects(ui::toBase(tile)))
            add(tile, radius(tile));
    if (editing()) {
        add(catalogRect(), 31, .48f, float(m_catalogReveal));
        add({canvasX() + kInset, kTop, canvasWidth() - 2 * kInset, 52}, 26);
    } else if (m_users->isVisible()) {
        add(m_users->geometry(), 18);
    }
    const auto footer = editRect();
    add(footer, footer.height / 2);
    return pieces;
}

void ControlCenterView::scrollGrid(double delta)
{
    const auto next = std::clamp(m_scroll + delta, 0.0, std::max(0.0, gridHeight() - gridViewport()));
    if (next != m_scroll) {
        m_scroll = next;
        requestLayout();
        m_chrome->requestPaint();
    }
}

bool ControlCenterView::scroll(const ui::ScrollEvent &event)
{
    if (gridHeight() <= gridViewport())
        return false;
    scrollGrid(event.deltaY);
    return true;
}

std::string volumeIconName(bool hasSink, double volume)
{
    if (!hasSink || volume <= 0)
        return "audio-volume-muted-symbolic";
    if (volume <= .25)
        return "audio-volume-low-symbolic";
    if (volume <= .75)
        return "audio-volume-medium-symbolic";
    return "audio-volume-high-symbolic";
}

} // namespace cutefish::shell
