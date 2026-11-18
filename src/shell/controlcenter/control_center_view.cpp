// SPDX-License-Identifier: BSD-3-Clause
#include "shell/controlcenter/control_center_view.h"

#include <cmath>
#include <ranges>

#include "base/i18n.h"
#include "shell/controlcenter/control_paint.h"
#include "shell/controlcenter/editor_palette.h"
#include "shell/controlcenter/level_slider.h"
#include "shell/controlcenter/panel_style.h"
#include "shell/controlcenter/tiles.h"
#include "ui/animation.h"
#include "ui/geometry_conversions.h"
#include "ui/menu.h"
#include "ui/search_field.h"
#include "ui/keys.h"
#include <unicode/unistr.h>

namespace cutefish::shell {
namespace {
using namespace control_paint;
using controls::Size;
using controls::Tile;
using i18n::tr;
using namespace std::chrono_literals;

constexpr double kInset = 4;
constexpr double kTop = 12;
constexpr double kCatalogGap = -1;
constexpr double kFooter = 39;
constexpr double kEditorHeader = 68;
constexpr double kEditorHeaderHeight = 44;
constexpr double kCatalogFooter = 54;
constexpr ui::Color kGreen {42, 157, 75, 255};
constexpr auto kCanvasInk = ui::Color::rgb(0xffffff);
constexpr auto kCatalogInk = ui::Color::rgb(0x171622);
ui::Color catalogInk() { return editor_palette::forTheme(panel::dark()).ink; }

double radius(const ui::RectF &r)
{
    if (r.width > 3 * r.height)
        return r.height * .32;
    return r.height < 105 ? r.height / 2 : r.height * .24;
}

icu::UnicodeString folded(const std::string &text)
{
    return icu::UnicodeString::fromUTF8(text).foldCase();
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
             const ControlCenterView::State &state, const MediaState &media)
{
    const auto ink = kCanvasInk;
    const bool solid = state.active && (size == Size::Small || size == Size::Large);
    const auto fg = solid ? kCatalogInk : ink;
    if (solid)
        painter.fillRoundedRect(r, radius(r), ink.withAlpha(.85));
    if (def.id == "media" && size == Size::Large) {
        const ui::RectF cover {r.x + 12, r.y + 12, 40, 40};
        painter.fillRoundedRect(cover, 11, ink.withAlpha(.18));
        painter.drawSvg(panel::artwork("media-playback-start-symbolic", true), centered(cover, 19), ink.withAlpha(.66));
        text(painter, {r.x + 15, r.bottom() - 83, r.width - 30, 25},
             media.hasPlayer && !media.title.empty() ? media.title : tr("Not Playing"), 11, ink, true);
        static constexpr std::array buttons {"media-skip-backward-symbolic", "media-playback-start-symbolic", "media-skip-forward-symbolic"};
        for (size_t i = 0; i < buttons.size(); ++i)
            painter.drawSvg(panel::artwork(i == 1 && media.playing ? "media-playback-pause-symbolic" : buttons[i], true),
                            {r.x + r.width * (i + 1) / 4 - 16, r.bottom() - 51, 32, 32}, ink);
    } else if (size == Size::Small) {
        glyph(painter, def, centered(r, std::min(32., r.height * .39)), fg);
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
        glyph(painter, def, centered(plate, 28), ink);
        const double x = large ? r.x + 17 : r.x + 73;
        const double width = r.right() - x - 11;
        if (large)
            paragraph(painter, {x, r.bottom() - 49, width, 43}, tr(def.title), 12, fg);
        else {
            const bool details = !state.status.empty();
            text(painter, {x, details ? center(r).y - 22 : r.y, width, details ? 23.0 : r.height}, tr(def.title), 11, fg, true);
            if (details)
                text(painter, {x, center(r).y + 1, width, 20}, state.status, 10, fg.withAlpha(.84));
        }
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
    class CatalogSearch final : public ui::SearchField
    {
    public:
        using ui::SearchField::SearchField;
        std::function<bool(const ui::KeyEvent &)> navigate;
    protected:
        bool keyPress(const ui::KeyEvent &event) override
        {
            return (navigate && navigate(event)) || ui::SearchField::keyPress(event);
        }
    };

public:
    explicit Chrome(ControlCenterView &owner) : v(owner)
    {
        setTracksHover(true);
        auto *field = addChild<CatalogSearch>(tr("Search Controls"));
        search = field;
        field->navigate = [this](const auto &event) { return keyPress(event); };
        search->setFontSize(15);
        search->setName("control-search");
        search->setVisible(false);
        search->onTextChanged = [this] { galleryScroll = 0; focusedPreview = 0; v.requestLayout(); requestPaint(); };
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
        if (proposed)
            v.animateTiles();
        held.reset();
        dragging = false;
        menuBarDrop = false;
        target.reset();
        candidate.reset();
        proposed.reset();
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
            const auto palette = editor_palette::forTheme(panel::dark());
            search->setFill(ui::ColorRecipe(palette.surface.withAlpha(.76)));
            search->setGlyphColor(ui::ColorRecipe(palette.muted));
            search->setTextColor(ui::ColorRecipe(catalogInk()));
            search->setPlaceholderColor(ui::ColorRecipe(palette.muted));
            search->setGeometry({r.x + 26, r.y + 26, sidebarWidth() - 52, 37});
            search->setOpacity(v.m_catalogReveal);
        }
    }

    void paint(ui::Painter &painter) override
    {
        const auto ink = kCanvasInk;
        if (!editing) {
            const auto footer = localRect();
            if (contains(footer, hover))
                painter.fillRoundedRect(footer, footer.height / 2, ink.withAlpha(.10));
            text(painter, footer, tr("Edit Controls"), 11, ink, false, true);
            return;
        }

        const double progress = v.editorProgress();
        painter.save();
        painter.clip(v.gridClip());
        for (int y = 0; y < v.rows(); ++y) {
            // Follow the editor tween in both directions, with a short cascade
            // from the first visible row rather than the start of a scrolled page.
            const double row = std::clamp(y - std::floor(v.m_scroll / (v.cell() + v.gap())), 0., 7.);
            const double delay = row * .027;
            const double reveal = std::clamp((progress - delay) / (1 - delay), 0., 1.);
            if (reveal == 0)
                continue;
            for (int x = 0; x < controls::Layout::kColumns; ++x) {
                if (std::ranges::any_of(proposed ? proposed->tiles : v.page().tiles, [&](const Tile &tile) {
                        return tile.bounds().contains(base::Point(x, y));
                    }))
                    continue;
                const auto cell = v.tileRect(Size::Small, {x, y});
                const auto rect = inset(cell, cell.width * .13 * (1 - reveal));
                painter.fillRoundedRect(rect, rect.height / 2, ink.withAlpha(.22 * reveal));
            }
        }
        painter.save();
        painter.multiplyOpacity(progress);
        for (const Tile &tile : v.page().tiles) {
            if (progress <= 0 || (dragging && held && held->id == tile.id))
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
        painter.restore();

        const auto header = v.editorHeaderRect();
        painter.save();
        painter.multiplyOpacity(progress);
        text(painter, {v.canvasX() + 13, header.y, 48, header.height}, tr("Icon:"), 10, ink.withAlpha(.54));
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
                painter.fillRoundedRect(r, r.height / 2, ink.withAlpha(selected ? .26 : .09));
                text(painter, r, std::to_string(i + 1), 8, ink, selected, true);
            }
        }
        painter.restore();
        visitGallery(&painter);
        paintCatalogFooter(painter);

        if (dragging && held) {
            const auto r = dragRect();
            const auto *def = controls::find(held->type);
            painter.drawShadow(r, radius(r), 16, ui::Color{0, 0, 0, 115}, {0, 6});
            preview(painter, *def, held->size, r, v.state ? v.state(def->id) : State {}, v.m_services.media());
        }
        if (v.m_removed && v.m_removal < 1) {
            const auto &tile = v.m_removed->tile;
            const auto r = v.removingRect();
            painter.save();
            painter.clip(v.gridClip());
            painter.clipRoundedRect(r, radius(r));
            painter.multiplyOpacity(1 - v.m_removal);
            const auto *def = controls::find(tile.type);
            if (tile.size == Size::Small)
                glyph(painter, *def, centered(r, r.height * .35), ink);
            else
                text(painter, r, tr(def->title), 10 * (1 - .55 * v.m_removal), ink, true, true);
            painter.restore();
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
        if (!editing || !v.m_editTarget)
            return;
        if (event.button == ui::MouseButton::Right) {
            if (const Tile *tile = tileAt(hover)) {
                showMenu(*tile);
            } else if (v.m_page && v.editorProgress() > 0 && contains(v.editorHeaderRect(), hover)) {
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
            if (contains(minusRect(*tile), hover))
                return;
            const auto rect = v.tileRect(tile->size, tile->cell);
            anchor = {hover.x - rect.x, hover.y - rect.y};
        } else if (const auto *item = previewAt(hover)) {
            held = Tile {0, item->def->id, item->size, {}};
            const auto rect = v.tileRect(held->size, {});
            anchor = {rect.width * std::clamp((hover.x - item->rect.x) / item->rect.width, 0., 1.),
                      rect.height * std::clamp((hover.y - item->rect.y) / item->rect.height, 0., 1.)};
        }
    }

    void pointerMove(const ui::PointerEvent &event) override
    {
        hover = event.position;
        if (held && !dragging && (!held->id || !contains(minusRect(*held), origin))
                && std::hypot(hover.x - origin.x, hover.y - origin.y) >= 5) {
            dragging = true;
            v.requestLayout();
        }
        if (dragging && held) {
            updateDropTarget();
            v.requestLayout();
        }
        requestPaint();
    }

    void pointerRelease(const ui::PointerEvent &event) override
    {
        if (event.button != ui::MouseButton::Left)
            return;
        hover = event.position;
        if (dragging && held) {
            updateDropTarget();
            if (menuBarDrop) {
                v.m_model.setPin(held->type, controls::Pin::Always);
                if (v.onModelChanged)
                    v.onModelChanged();
            } else if (target && contains(v.gridClip(), hover)) {
                if (held->id ? v.m_model.move(v.m_page, held->id, *target)
                             : v.m_model.add(v.m_page, held->type, *target)) {
                    proposed.reset();
                    v.changed();
                }
            }
            cancelDrag();
            return;
        }
        const auto footer = editing ? doneRect() : localRect();
        if (contains(footer, hover) && contains(footer, origin)) {
            v.edit(!editing);
        } else if (editing && v.m_editTarget) {
            if (held && held->id && contains(minusRect(*held), hover) && contains(minusRect(*held), origin)) {
                v.removeTile(held->id);
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
        if (dragging) {
            const bool scrolled = v.scroll(event);
            updateDropTarget();
            return scrolled;
        }
        if (editing && contains(v.catalogRect(), hover)) {
            if (contains(sidebarClip(), hover)) {
                sidebarScroll = std::clamp(sidebarScroll + event.deltaY, 0.0,
                                           std::max(0.0, sidebarHeight - sidebarClip().height));
            } else {
                galleryScroll = std::clamp(galleryScroll + event.deltaY, 0.0,
                                           std::max(0.0, galleryHeight - galleryClip().height));
            }
            v.requestLayout();
            requestPaint();
            return true;
        }
        return v.scroll(event);
    }

    bool keyPress(const ui::KeyEvent &event) override
    {
        if (!editing || !v.m_editTarget || previews.empty())
            return false;
        if (event.key == ui::key::Down || event.key == ui::key::Up) {
            const int step = event.key == ui::key::Down ? 1 : -1;
            focusedPreview = size_t(std::clamp(int(focusedPreview) + step, 0, int(previews.size() - 1)));
            const auto r = previews[focusedPreview].rect;
            const auto clip = galleryClip();
            galleryScroll += r.y < clip.y ? r.y - clip.y : std::max(0.0, r.bottom() + 39 - clip.bottom());
            galleryScroll = std::clamp(galleryScroll, 0.0, std::max(0.0, galleryHeight - clip.height));
            v.requestLayout();
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

    void updateDropTarget()
    {
        menuBarDrop = held && v.isMenuBarDrop && v.isMenuBarDrop(mapToWindow(hover));
        std::optional<base::Point> next;
        if (held && contains(v.gridClip(), hover)) {
            const double step = v.cell() + v.gap();
            const auto span = controls::footprint(held->size);
            const auto first = v.tileRect(Size::Small, {});
            next = base::Point(std::clamp(int(std::lround((hover.x - anchor.x - first.x) / step)),
                                         0, controls::Layout::kColumns - span.width()),
                               std::clamp(int(std::lround((hover.y - anchor.y - first.y) / step)),
                                         0, controls::Layout::kRows - span.height()));
        }
        if (next == candidate)
            return;
        candidate = next;
        if (proposed || next)
            v.animateTiles();
        target.reset();
        proposed.reset();
        if (next) {
            auto page = v.page();
            if (controls::Layout::place(page, *held, *next)) {
                proposed = std::move(page);
                target = next;
            }
        }
    }

    ui::RectF dragRect() const
    {
        auto r = v.tileRect(held->size, {});
        r.x = hover.x - anchor.x;
        r.y = menuBarDrop ? 2 : hover.y - anchor.y;
        return r;
    }

    ui::RectF catalogFooter() const
    {
        const auto r = v.catalogRect();
        const double left = r.x + sidebarWidth() + 21;
        return {left, std::max(r.y, v.windowHeight(1) - kCatalogFooter), std::max(0., r.right() - left - 22), kCatalogFooter};
    }
    ui::RectF doneRect() const
    {
        const auto r = catalogFooter();
        return {r.right() - 70, r.y + 10, 70, 32};
    }
    void paintCatalogFooter(ui::Painter &painter)
    {
        const auto r = catalogFooter();
        const auto palette = editor_palette::forTheme(panel::dark());
        painter.save();
        painter.multiplyOpacity(v.editorProgress());
        painter.clipRoundedRect(v.catalogRect(), 31);
        painter.fillRect({r.x, r.y, r.width, 1}, palette.border.withAlpha(.25));
        paragraph(painter, {r.x, r.y + 11, r.width - 82, 40},
                  menuBarDrop ? tr("Release to add to the menu bar.")
                              : tr("Drag a control to place it in Control Center or in the menu bar."), 11, palette.ink.withAlpha(.86));
        const auto done = doneRect();
        painter.fillRoundedRect(done, 11, palette.accent.withAlpha(contains(done, hover) ? 1 : .91));
        text(painter, done, tr("Done"), 11, palette.accentInk, true, true);
        painter.restore();
    }

    double sidebarWidth() const { return std::clamp(v.catalogWidth() / 3, 118.0, 321.0); }
    ui::RectF sidebarClip() const
    {
        const auto r = v.catalogRect();
        return {r.x + 23, r.y + 76, sidebarWidth() - 46, std::max(0.0, v.windowHeight(1) - r.y - 84)};
    }
    ui::RectF galleryClip() const
    {
        const auto r = v.catalogRect();
        return {r.x + sidebarWidth() + 1, r.y + 12, r.width - sidebarWidth() - 9,
                std::max(0.0, catalogFooter().y - r.y - 12)};
    }
    ui::RectF iconRect(size_t i) const
    {
        const double width = (v.canvasWidth() - kInset * 2 - 61) / 5;
        return {v.canvasX() + kInset + 61 + width * i + 3, v.editorHeaderRect().y + 5, width - 8, 34};
    }
    ui::RectF pageRect(size_t i) const
    {
        const auto header = v.editorHeaderRect();
        const double step = std::min(29., header.width / controls::Layout::kPages);
        return {header.x + (header.width - step * v.m_model.pages().size()) / 2 + step * i,
                header.bottom() + 3, step - 4, 19};
    }
    ui::RectF minusRect(const Tile &tile) const
    {
        const auto it = v.m_tiles.find(tile.id);
        if (it == v.m_tiles.end())
            return {};
        auto r = v.tileRect(tile.size, tile.cell);
        const auto pos = it->second->mapToWindow({0, 0});
        const auto top = mapToWindow({0, 0});
        return inset({pos.x - top.x + (r.width == r.height ? 5 : 0), pos.y - top.y + 1, 22, 22},
                     5.28 * (1 - v.editorProgress()));
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
            v.removeTile(id);
        });
        menu->popupAt(window(), mapToWindow(hover));
    }

    // One geometry pass for labels, hit targets and backdrop lenses. Only
    // layout publishes lens geometry; painting must not schedule another frame.
    void visitGallery(ui::Painter *painter)
    {
        const auto r = v.catalogRect();
        const auto palette = editor_palette::forTheme(panel::dark());
        const auto ink = palette.ink;
        const auto soft = palette.muted;
        if (painter) {
            painter->save();
            painter->multiplyOpacity(v.m_catalogReveal);
            painter->clipRoundedRect(r, 31);
            painter->fillRect({r.x, r.y, sidebarWidth(), r.height}, palette.surface.withAlpha(.14));
            painter->fillRect({r.x + sidebarWidth(), r.y, 1, r.height}, palette.border.withAlpha(.20));
        } else {
            categories.clear();
            previews.clear();
            previewLenses.clear();
        }

        const auto side = sidebarClip();
        if (painter) {
            painter->save();
            painter->clip(side);
        }
        double y = side.y - sidebarScroll;
        const auto categoryRow = [&](const std::string &name) {
            const ui::RectF row {side.x + 3, y, side.width - 6, 47};
            y += 47;
            if (!painter) {
                categories.emplace_back(name, row);
                return;
            }
            if (row.bottom() <= side.y || row.y >= side.bottom())
                return;
            const bool selected = search->text().empty() && name == category;
            if (selected || (contains(row, hover) && contains(side, hover)))
                painter->fillRoundedRect(row, 12, ink.withAlpha(selected ? .19 : .08));
            const ui::RectF badge {row.x + 11, row.y + 10, 27, 27};
            painter->fillRoundedRect(badge, 6, palette.raised.withAlpha(.65));
            if (name.empty())
                pageGlyph(*painter, 0, badge, ink);
            else if (const auto it = std::ranges::find_if(controls::catalog(), [&](const auto &def) {
                         return name == def.category;
                     }); it != controls::catalog().end()) {
                glyph(*painter, *it, centered(badge, 19), ink);
            }
            text(*painter, {row.x + 47, row.y, row.width - 52, row.height}, name.empty() ? tr("All Controls") : tr(name.c_str()),
                 12, ink, selected);
        };
        categoryRow({});
        std::vector<std::string> groups;
        for (const auto &def : controls::catalog()) {
            if (std::ranges::find(groups, def.category) == groups.end()) {
                groups.emplace_back(def.category);
                categoryRow(groups.back());
            }
        }
        const auto clip = galleryClip();
        const auto viewport = ui::toBase(clip);
        if (painter) {
            painter->restore();
            painter->save();
            painter->clip(clip);
        } else {
            sidebarHeight = y - side.y + sidebarScroll;
        }
        const double left = clip.x + 19;
        const double width = std::max(52.0, clip.width - 36);
        const double gap = 16;
        const int columns = width >= 510 ? 6 : width >= 232 ? 4 : 2;
        const double unit = std::min(83.0, (width - gap * (columns - 1)) / columns);
        y = clip.y + 14 - galleryScroll;
        const auto title = [&](std::string name, double size) {
            if (painter)
                text(*painter, {left, y, width, 34}, std::move(name), size, ink, true);
            y += 36;
        };
        size_t previewIndex = 0;
        const auto addPreviewAt = [&](const controls::Definition *def, ui::RectF rect) {
            const auto index = previewIndex++;
            if (!painter) {
                previews.push_back({def, def->initial, rect});
                const auto body = ui::toBase(rect);
                if (body.intersects(viewport))
                    previewLenses.push_back({body, radius(rect), panel::dark() ? .35f : .23f, float(v.m_catalogReveal), viewport});
                return;
            }
            if (rect.bottom() + 39 < clip.y || rect.y >= clip.bottom())
                return;
            preview(*painter, *def, def->initial, rect, v.state ? v.state(def->id) : State {}, v.m_services.media());
            const auto label = tr(def->title);
            if (rect.width < 100 && std::string_view(label).find(' ') != std::string_view::npos)
                paragraph(*painter, {rect.x + 3, rect.bottom() + 5, rect.width - 6, 39}, label, 10, ink);
            else
                text(*painter, {rect.x - 4, rect.bottom() + 3, rect.width + 8, 22}, label, 10, ink, false, true);
            const bool selected = index == focusedPreview && (hasFocus() || search->hasFocus());
            if (selected || (contains(rect, hover) && contains(clip, hover)))
                sign(*painter, {rect.x - 4, rect.y - 5, 25, 25}, true, kGreen, kCanvasInk);
        };
        const auto section = [&](const char *heading, const std::vector<const controls::Definition *> &items) {
            if (items.empty())
                return;
            if (painter)
                painter->fillRect({left, y, width, 1}, ink.withAlpha(.16));
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
        if (!query.isEmpty()) {
            const auto results = matching([&](const auto &def) {
                return folded(tr(def.title)).indexOf(query) >= 0 || folded(tr(def.category)).indexOf(query) >= 0;
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
            if (painter) {
                const double copyWidth = artwork ? width * .52 - 32 : width - 32;
                painter->fillRoundedRect({left, y, width, height}, 8, ink.withAlpha(.09));
                text(*painter, {left + 26, y + (artwork ? 36 : 10), copyWidth, 30}, tr("What’s New"), 15, ink, true);
                const double introY = y + (artwork ? 67 : 42);
                paragraph(*painter, {left + 26, introY, copyWidth, height - (introY - y) - 6},
                          tr("Control Center is more flexible than ever. Resize and move controls to keep the ones that matter within reach. You can also add your favorite controls to the menu bar."), 11, soft);
            }
            if (painter && artwork) {
                const ui::RectF screen {left + width * .52, y + 17, width * .48 - 16, height - 34};
                painter->fillRoundedRect(screen, 8, ui::LinearGradient::vertical(screen,
                        palette.raised.withAlpha(.58), palette.accent.withAlpha(.19)));
                painter->fillRoundedRect({screen.x + 3, screen.y + 3, screen.width - 6, 8}, 3, panel::textColor(.17));
                const double tile = std::min((screen.width - 22) / 11, 13.0);
                const double x = screen.right() - tile * 4 - 12;
                const double top = screen.y + 16;
                const ui::RectF gallery {screen.x + 10, top + 14, x - screen.x - 16, screen.height - 41};
                painter->fillRoundedRect(gallery, 5, panel::textColor(.17));
                painter->fillRect({gallery.x + gallery.width * .29, gallery.y, 1, gallery.height}, panel::textColor(.20));
                for (int row = 0; row < 6; ++row) {
                    painter->fillRoundedRect({gallery.x + 4, gallery.y + 6 + row * 9., gallery.width * .20, 3}, 1.5,
                                            panel::textColor(.24));
                    for (int col = 0; col < 4; ++col) {
                        const double unit = gallery.width * .15;
                        painter->fillRoundedRect({gallery.x + gallery.width * .34 + col * unit,
                                                 gallery.y + 6 + row * 9., unit - 3, 6}, 3, panel::textColor(.26));
                    }
                }
                for (int row = 0; row < 7; ++row) {
                    const int wide = row == 3 || row == 4 ? 4 : row < 2 ? 2 : 1;
                    for (int col = 0; col < 4; col += wide)
                        painter->fillRoundedRect({x + col * tile, top + row * 14., wide * tile - 3, 11}, 5, panel::textColor(.29));
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
                recommendation("overview", 0, 0);
                recommendation("dark", 2, 0);
                recommendation("clock", 3, 0);
                recommendation("wifi", 4, 0);
                recommendation("battery", 2, 144);
                recommendation("media", 4, 157);
                recommendation("calculator", 0, 270);
                recommendation("note", 1, 270);
                recommendation("focus", 2, 270);
                y = top + 427;
            } else {
                section("Recommendations", matching([](const auto &def) { return def.recommended; }));
            }
            for (const auto &group : groups)
                section(group.c_str(), matching([&](const auto &def) { return group == def.category; }));
        }
        if (painter) {
            painter->restore();
            painter->restore();
        } else {
            galleryHeight = y - clip.y + galleryScroll;
        }
    }

    ControlCenterView &v;
    ui::PointF hover {-1, -1};
    ui::PointF origin;
    ui::PointF anchor;
    std::optional<base::Point> target;
    std::optional<base::Point> candidate;
    std::optional<controls::Page> proposed;
    bool menuBarDrop = false;
    std::vector<Preview> previews;
    std::vector<std::pair<std::string, ui::RectF>> categories;
    std::vector<render::GlassLens> previewLenses;
    friend class ControlCenterView;
};

ControlCenterView::ControlCenterView(ControlCenterServices &services, controls::Layout &model, int pageId,
                                     int maximumWidth, int maximumHeight, int editorHeight)
    : m_services(services)
    , m_model(model)
    , m_page(pageId)
    , m_maximumWidth(maximumWidth)
    , m_maximumHeight(maximumHeight)
    , m_editorHeight(editorHeight)
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

double ControlCenterView::liveGridTop() const
{
    return kTop + (m_users->users().empty() ? 0 : m_users->sizeHint().height + 9);
}

double ControlCenterView::gridTop() const
{
    const double pages = m_model.pages().size() > 1 ? 12 : 0;
    return std::lerp(liveGridTop(), kTop + kEditorHeader + pages, editorProgress());
}

int ControlCenterView::rows() const { return std::max(editing() ? 8 : 1, page().rows()); }
double ControlCenterView::gridHeight() const { return rows() * (cell() + gap()) - gap(); }
double ControlCenterView::gridViewport() const
{
    return gridViewport(rows(), gridTop(), editing() ? m_editorHeight : m_maximumHeight);
}

double ControlCenterView::gridViewport(int rows, double top, int maximumHeight) const
{
    return std::max(cell(), std::min(rows * (cell() + gap()) - gap(), maximumHeight - top - kFooter));
}

double ControlCenterView::windowHeight(double) const
{
    return editing() ? m_editorHeight : liveHeight();
}

double ControlCenterView::liveHeight() const
{
    const auto top = liveGridTop();
    return std::ceil(top + gridViewport(std::max(1, page().rows()), top, m_maximumHeight) + kFooter);
}

ui::RectF ControlCenterView::gridClip() const { return {canvasX(), gridTop(), canvasWidth(), gridViewport()}; }

ui::RectF ControlCenterView::tileRect(Size size, base::Point point) const
{
    const auto dim = controls::footprint(size);
    const double step = cell() + gap();
    return {canvasX() + kInset + point.x() * step, gridTop() + point.y() * step - m_scroll,
            dim.width() * step - gap(), dim.height() * step - gap()};
}

ui::RectF ControlCenterView::catalogRect() const
{
    const double height = std::max(1., windowHeight(1) - 23);
    // The sheet rises from the output edge. Its bottom corners sit outside
    // the output, so the catalog never becomes a floating rounded window.
    return {0, 23 + height * (1 - m_catalogReveal), catalogWidth(), height + 32};
}

ui::RectF ControlCenterView::editorHeaderRect() const
{
    return {canvasX() + kInset, kTop - 9 * (1 - editorProgress()), canvasWidth() - 2 * kInset, kEditorHeaderHeight};
}

ui::RectF ControlCenterView::editRect() const
{
    constexpr double width = 125;
    return {(canvasWidth() - width) / 2, gridTop() + gridViewport() + 12, width, 25};
}

void ControlCenterView::animateTiles()
{
    if (m_transition)
        m_transition->stop();
    m_previous.clear();
    m_progress = 1;
    if (!window())
        return;
    for (const auto &[id, view] : m_tiles)
        m_previous.emplace(id, view->geometry());
    m_transition = std::make_unique<ui::Animation>(window());
    m_transition->setDuration(210ms);
    m_transition->setEasing(ui::Animation::Easing::OutCubic);
    m_transition->onUpdate = [this](double t) { m_progress = t; requestLayout(); m_chrome->requestPaint(); };
    m_progress = 0;
    m_transition->start();
}

void ControlCenterView::rebuild(bool animate)
{
    if (animate) {
        animateTiles();
    } else {
        if (m_transition)
            m_transition->stop();
        m_previous.clear();
        m_progress = 1;
    }
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
            tile->onDetail = [this, display] {
                if (display) {
                    if (activate) activate("display", true);
                } else if (onSoundDetail) {
                    onSoundDetail();
                }
            };
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
    if (m_editTarget == enabled || (enabled && m_maximumWidth < 490))
        return;
    m_editTarget = enabled;
    m_chrome->cancelDrag();
    if (enabled && !editing()) {
        m_chrome->editing = true;
        m_chrome->galleryScroll = m_chrome->sidebarScroll = 0;
        m_chrome->category.clear();
        m_chrome->previewLenses.clear();
        m_chrome->search->setText({});
        m_scroll = 0;
        m_catalogReveal = 0;
    }
    const auto settle = [this, enabled] {
        m_catalogReveal = enabled ? 1 : 0;
        m_chrome->editing = enabled;
        if (!enabled)
            m_removed.reset();
        changed(false);
    };
    if (!window()) {
        settle();
        return;
    }
    const double from = m_catalogReveal;
    const double to = enabled ? 1 : 0;
    m_catalogTween = std::make_unique<ui::Animation>(window());
    m_catalogTween->setDuration(enabled ? 340ms : 250ms);
    m_catalogTween->setEasing(enabled ? ui::Animation::Easing::OutCubic : ui::Animation::Easing::InOutCubic);
    m_catalogTween->onUpdate = [this, from, to](double t) {
        m_catalogReveal = from + (to - from) * t;
        requestLayout();
        m_chrome->requestLayout();
        m_chrome->requestPaint();
        if (onHeightChanged)
            onHeightChanged();
    };
    m_catalogTween->onFinished = settle;
    m_catalogTween->start();
    changed(false);
}

ui::RectF ControlCenterView::removingRect() const
{
    const auto r = m_removed->rect;
    return inset(r, std::min(r.width, r.height) * .37 * m_removal);
}

void ControlCenterView::removeTile(int id)
{
    const auto *tile = page().tile(id);
    if (!tile)
        return;
    m_removed = RemovedTile {*tile, tileRect(tile->size, tile->cell)};
    if (!m_model.remove(m_page, id)) {
        m_removed.reset();
        return;
    }
    if (window()) {
        m_removal = 0;
        m_removeTween = std::make_unique<ui::Animation>(window());
        m_removeTween->setDuration(230ms);
        m_removeTween->setEasing(ui::Animation::Easing::OutCubic);
        m_removeTween->onUpdate = [this](double t) {
            m_removal = t;
            requestLayout();
            m_chrome->requestPaint();
        };
        m_removeTween->onFinished = [this] { m_removed.reset(); };
    }
    changed();
    if (m_removeTween)
        m_removeTween->start();
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
        const auto *proposed = m_chrome->proposed ? m_chrome->proposed->tile(tile.id) : nullptr;
        auto r = translated(tileRect(tile.size, proposed ? proposed->cell : tile.cell), -clip.x, -clip.y);
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
    if (editing())
        m_chrome->visitGallery(nullptr);
    if (onGlassChanged)
        onGlassChanged();
}

render::GlassUnderlay ControlCenterView::glassUnderlay() const
{
    // The catalog has its own glass. The veil follows the canvas horizontally
    // but keeps the compact panel's height, not the expanded editor viewport.
    const double x = std::max(0., canvasX() - 2);
    return {{x, 0, canvasWidth() + canvasX() - x, liveHeight()},
            std::lerp(.69f, .86f, float(editorProgress()))};
}

ui::Path ControlCenterView::glassShape() const
{
    ui::Path mask;
    for (const auto &r : m_glassTiles)
        mask.addRoundedRect(r, radius(r), radius(r));
    if (m_removed && m_removal < 1) {
        const auto r = removingRect();
        mask.addRoundedRect(r, radius(r), radius(r));
    }
    ui::Path clip;
    clip.addRect(gridClip());
    mask = mask.intersected(clip);
    if (editing()) {
        ui::Path catalog;
        catalog.addRoundedRect(catalogRect(), 31, 31);
        ui::Path viewport;
        viewport.addRect({0, 0, catalogWidth(), windowHeight(1)});
        mask.addPath(catalog.intersected(viewport));
        if (editorProgress() > 0)
            mask.addRoundedRect(editorHeaderRect(), kEditorHeaderHeight / 2, kEditorHeaderHeight / 2);
    } else if (m_users->isVisible()) {
        mask.addRoundedRect(m_users->geometry(), 18, 18);
    }
    if (!editing()) {
        const auto footer = editRect();
        mask.addRoundedRect(footer, footer.height / 2, footer.height / 2);
    }
    if (m_chrome->dragging && m_chrome->held) {
        const auto ghost = m_chrome->dragRect();
        ui::Path path;
        path.addRoundedRect(ghost, radius(ghost), radius(ghost));
        mask = mask.united(path);
    }
    return mask;
}

std::vector<render::GlassLens> ControlCenterView::glassLenses() const
{
    std::vector<render::GlassLens> pieces;
    const auto add = [&](const ui::RectF &r, double corners, float tint = -1, float opacity = 1) {
        pieces.push_back({ui::toBase(r), corners, tint, opacity});
    };
    const auto clip = ui::toBase(gridClip());
    for (const auto &tile : m_glassTiles) {
        if (!clip.intersects(ui::toBase(tile)))
            continue;
        if (m_chrome->dragging)
            pieces.push_back({ui::toBase(tile), radius(tile), -1, 1, clip});
        else
            add(tile, radius(tile));
    }
    if (m_removed && m_removal < 1) {
        const auto r = removingRect();
        add(r, radius(r), -1, float(1 - m_removal));
    }
    if (editing()) {
        pieces.push_back({ui::toBase(catalogRect()), 31, -1, float(m_catalogReveal), std::nullopt,
                          panel::dark() ? render::GlassFinish::FrostedDark : render::GlassFinish::FrostedLight});
        pieces.insert(pieces.end(), m_chrome->previewLenses.begin(), m_chrome->previewLenses.end());
        if (editorProgress() > 0)
            add(editorHeaderRect(), kEditorHeaderHeight / 2, -1, float(editorProgress()));
    } else if (m_users->isVisible()) {
        add(m_users->geometry(), 18);
    }
    if (!editing()) {
        const auto footer = editRect();
        add(footer, footer.height / 2);
    }
    if (m_chrome->dragging && m_chrome->held) {
        const auto ghost = m_chrome->dragRect();
        pieces.push_back({ui::toBase(ghost), radius(ghost), -1, 1, ui::toBase(localRect())});
    }
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
