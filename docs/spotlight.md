# Koifish Spotlight

Spotlight is drawn inside the compositor. It replaces the fullscreen Launchpad and the separate clipboard picker in the session; it does not replace or capture the desktop background. The backdrop uses the existing FishUI PopupGlass implementation with a neutral light/dark tint.

- **Super+Space** toggles general search. In Cutefish's Command convention use **Command+Space** (the key next to Space).
- The dock's application button (and a tap of Super in the PC convention) opens **Applications** directly. Applications, Files, Actions and Clipboard are also available from the four buttons next to the search field, or by pressing **Super+1…4** while Spotlight is open (**Command+1…4** in the Command convention).
- **Arrow keys** navigate results, **Enter** opens the selection, and **Esc** dismisses the panel. Right-click a result for its context menu. In Files, select a folder and press **Tab** to search within it; file results can also be dragged into other applications.
- Touchpad scrolling follows the gesture and coasts on release; the mouse wheel eases through successive steps (one grid row per detent in Applications). Use a horizontal gesture or **Shift+wheel** to scroll overflowing filter chips. New input stops the coast.
- Applications uses the desktop-entry registry. Its five-column grid, list preference and recently launched apps live in `~/.config/cutefish/spotlight.conf`. Categories can be combined with text search. The file index is in RAM, scans visible entries under the user's home directory in the background, skips symlinks and large dependency subtrees, and is bounded (depth 7, 35,000 visited entries). It matches file names, not contents. Recents in Files are ordered by file modification time.
- Visible Files results load thumbnail previews off the UI thread using the same thumbnail cache as Cutefish Files. An unsupported format, directory, or failed thumbnail keeps its type icon. Non-image formats may require an installed desktop thumbnailer.
- Actions displays built-in Linux actions and installed `.desktop` actions. Quick keys are editable by clicking the chip beside an action. Compose Email and Search the Web use clickable inline parameters. Use **Tab/Shift+Tab** to move among tokens, **↑/↓** and **Enter** to choose a suggestion, and **Ctrl+Enter** to preview the result and then open it. Recipient domain completion is local; mail is opened as a draft, never sent by Spotlight. There is no App Intents, calendar/route provider, system contact autocomplete or iPhone app mirroring.
- Clipboard uses the compositor's existing payload store and pastes back into the previous application. HTTP(S) entries show the host on the secondary line, without contacting the site. The `…` menu can clear history or disable recording (which also clears history). Entries expire after eight hours; large and marked-sensitive selections follow Cutefish's existing exclusions.

The legacy `org.cutefish.Launchpad` and `org.cutefish.ClipboardHistory` session D-Bus entry points remain compatible. `org.cutefish.Spotlight` provides `show`, `hide`, `toggle` on `/Spotlight`.

Build and run the targeted tests from a configured build tree:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target cutefish-compositor spotlight_test accelerator_test
ctest --test-dir build -R '^(spotlight_test|accelerator_test)$' --output-on-failure
```
