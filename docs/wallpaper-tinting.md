# Wallpaper tinting

The **Allow wallpaper tinting in windows** setting is on by default. Turning it
off, or turning on **Reduce Transparency**, restores the neutral material
colors. Neither action changes document or accent colors.

The compositor builds a small color grid from each output's rendered wallpaper
crop. It averages the area under a window with some padding, weighting each
output by the area of the window on that output. It sends only bounded light and
dark channel adjustments for that surface. The sample never includes the
contents of any window, even if it sits between the wallpaper and the window
being tinted.

FishUI applies the adjustment when painting `Background`, `PanelBackground`
and `AlertBackground`. Use `DocumentBackground` or an app's own content palette
for canvases and documents. For a custom chrome color, use `wallpaperTinted`
as a color recipe rather than resolving it when constructing the view. Layer
opacity doesn't change. Menus and other glass surfaces still use the composited
backdrop; the header's in-window scroll backdrop is also independent.

The behavior follows Apple's [window tint setting][setting],
[windowBackground material][background] and [materials guidance][materials].
The light and dark adjustments are an approximation: Apple does not publish
the formula. They were calibrated against [macOS 27 Golden Gate screenshots][aqua]
and [the light/dark on-off examples][examples].

[setting]: https://support.apple.com/guide/mac-help/mchlp1225/mac
[background]: https://developer.apple.com/documentation/appkit/nsvisualeffectview/material-swift.enum/windowbackground
[materials]: https://developer.apple.com/design/human-interface-guidelines/materials
[aqua]: https://512pixels.net/projects/aqua-screenshot-library/macos-27-golden-gate/
[examples]: https://zenn.dev/usagimaru/articles/82dbc41e80614a?locale=en
