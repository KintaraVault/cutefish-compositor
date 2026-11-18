# Control Center and desktop widgets

The tile and widget lenses transmit the blurred scene and reflect a wider patch
around their contours. A bright or colored surface moving behind a tile changes
its rim as well as its body; the material has no wallpaper-specific palette.
The desktop widget gallery rises from the physical bottom edge, above the dock.
Its sidebar filters previews by widget family, and dragging a preview lifts a
separate, input-transparent glass card that follows the pointer onto the desktop.

Open Control Center from the menu bar. **Edit Controls** opens a searchable
catalog alongside the four-column grid. The catalog rises from the physical
bottom edge and covers the dock; the live panel stays within the work area.
Click a preview or drag it into the grid to add it. Drag an existing control to
move it; Escape cancels the drag.
Use its minus button to remove it or its context menu to resize it and manage
its menu-bar presence. Only supported sizes are selectable. The plus in the
menu bar creates a new page. Pick an icon above the grid and open that page
from its own menu-bar icon. Right-click the icon strip of a custom page to
remove the page.

For desktop widgets, select **Edit Widgets** in Notification Center, add the
**Edit Widgets** control, or right-click an existing widget. The gallery offers
small and medium Clock, Calendar and Battery widgets. Click or drag a preview
to add it, then drag widgets on the desktop or use their minus buttons while
editing. **Done** or Escape closes the gallery. The Battery widget reports the
UPower reading when present, not a simulated value.

Pages, control positions and visibility preferences are stored in
`control-center.conf`. Widget types, sizes and positions are stored in
`desktop-widgets.conf`. Both use the compositor's normal per-user preferences
directory.

The interface uses the compositor's native glass. Each tile refracts the same
captured scene independently. A wider, softly feathered blur below the grid
remains visible between tiles without a solid panel or a second outline.
The rim uses a pixel footprint aligned with the curve, with separate reflected
and transmitted highlights to keep a smooth edge at fractional output scales.
Catalog previews have their own lenses, clipped to the scrolling viewport.
The center, catalog and desktop widgets use separate neutral material settings,
so the wallpaper supplies their hue. Animations approximate the supplied footage,
which does not specify easing curves or durations.
Controls that have no corresponding system service (such as AirDrop and Home)
are available as catalog entries but explicitly marked unavailable in use.

## Isolated reference captures

Build the `controlcenter_test` target with `BUILD_TESTING=ON`. The optional EGL
capture test writes 1920×1080 PNGs without connecting to the running desktop:

```sh
env -u DISPLAY -u WAYLAND_DISPLAY CUTEFISH_TAHOE_PREVIEW=/tmp/tahoe \
  dbus-run-session -- build/tests/shell/controlcenter_test \
  --gtest_filter='*OffscreenReferenceFrames'
```

Set `CUTEFISH_TAHOE_WALLPAPER` to a reference backdrop for panel comparisons.
Omit it to render widgets over a neutral test wallpaper without reference UI
baked into the image.
