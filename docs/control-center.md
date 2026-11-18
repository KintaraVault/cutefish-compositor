# Control Center and desktop widgets

Open Control Center from the menu bar. **Edit Controls** opens a searchable
catalog alongside the four-column grid. Click a preview or drag it into the
grid to add it. Drag an existing control to move it; Escape cancels the drag.
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

The interface uses the compositor's native glass. The wallpaper shows through
the plates; their tints follow the system appearance. Animations approximate
the supplied footage, which does not specify easing curves or durations.
Controls that have no corresponding system service (such as AirDrop and Home)
are available as catalog entries but explicitly marked unavailable in use.
