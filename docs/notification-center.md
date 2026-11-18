# Notification Center and widgets

Click the date and time in the menu bar to open Notification Center. It slides
in as a right-aligned column, without an enclosing sheet. Related notifications
stack; select the top card to expand the group. **Clear All Notifications**
appears after the notification list. The heading is replaced by **No recent
notifications** when history is empty. Notifications keep using the compositor's
existing desktop notification service, history and actions.

**Edit Widgets** opens the same searchable gallery as the desktop context menu
and the Control Center shortcut. Click a preview to add an instance to the
surface that opened the gallery. You can also drag a preview to the desktop or
into the open Notification Center. The two surfaces keep separate instances.
Drag cards to move them while editing, use the minus button to remove them, or
right-click for available sizes and settings. **Done** and Escape close the
gallery. The `desktop-widgets.conf` preferences preserve surface, order, size,
desktop position and settings, and read positions saved by earlier versions.

| Widget | Data |
| --- | --- |
| Clock, World Clock | Local time and fixed IANA time zones, including DST |
| Calendar | Local month and date |
| Batteries | Live UPower system battery when available; empty rings aren't simulated devices |
| Photos | User-selected local image |
| Reminders | A local task with a completion toggle, not a synced account |
| Weather | Current and hourly forecast from Open-Meteo for user-entered coordinates; no network request before configuration |
| System Monitor | Local Linux memory usage |

The notification cards and widget gallery reuse the compositor's native glass
and clip its optics to each surface. The glass does not set a wallpaper hue.
The layout is based on the supplied Tahoe crops. Motion approximates the visible
transitions; the crops don't establish the original timings and easing curves.
The gallery shows the providers available in this fork, not a simulated Apple
catalog.

## Reproduce the captures

Build `controlcenter_test` with `BUILD_TESTING=ON`, then run:

```sh
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 \
CUTEFISH_TAHOE_PREVIEW=/tmp/tahoe \
  build/tests/shell/controlcenter_test --gtest_filter=ControlCenterTest.OffscreenReferenceFrames
```

The test writes `-notification-centre-start.png`, `-notification-centre.png`
and `-widget-gallery.png`, along with the Control Center captures. Set
`CUTEFISH_TAHOE_WALLPAPER` to a local image to compare the same materials against
another backdrop.
