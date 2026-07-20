## Not yet validated
- Real-world GUI behavior not fully confirmed by the user yet. Pending re-test of: menu labels clean of `[`/`]`, tooltip shows on slider hover, `[`/`]` still set in/out at current frame, Ctrl+I/Ctrl+O unchanged.

## Second follow-up: M and C keys (user feedback)
- User asked to add `M` to make a marker on the timeline. `M` already opened the Add/Edit Marker dialog, so the behavior change was about the `C` key instead.
- First attempt: `M` = silent quick marker (no dialog), `C` = open marker comment dialog. User rejected this — wanted `M` to **open the comment window to make a note**.
- Final decision (confirmed working):
  - `M` (no modifier, or Shift+M) → opens the Add/Edit Marker comment dialog at the current frame (the original behavior is preserved).
  - `C` (no modifier) → opens the Marker Viewer window (`notesViewerDialog`).
  - Tooltip updated to: `M — add/edit a marker comment at the current frame` / `C — open the marker viewer`.
- `Ctrl+C` (copy current display) and typing `m`/`c` in text/spinbox fields remain unaffected.
- Rebuilt: `nix develop -c ninja -C build ld-analyse` → clean (18/18).
- User confirmed: "good, commit and push".

# Prompt log — ld-analyse: Set In/Out at current frame + `[`/`]` shortcuts

Date: 2026-07-20
Repo: /home/harry/tbc-tools
Branch: main (HEAD at start; pulled v3.2.5 tag, already up-to-date)

## User input (request)
1. "I need to fix set in and set out point on the current set frame/marker position ... for ld-analyse"
2. Clarification answers:
   - Set In / Set Out should use the **currently displayed frame** (`currentFrameNumber`), ignoring where on the slider the user right-clicked. (Behavior change, not a bug.)
   - Add keyboard shortcuts for setting in/out when on the main page with the timeline selected.
3. Shortcut conflict resolution:
   - `Ctrl+I` (already bound to VBI dialog) and `Ctrl+O` (already bound to Open TBC/metadata file) were taken.
   - User chose: **use `[` for In and `]` for Out**, leave `Ctrl+I`/`Ctrl+O` as-is.

## Commands run
- `cd ..` / `cd tbc-tools` (navigation)
- `git pull` → fetched tag v3.2.5; "Already up-to-date."
- `nix develop -c ninja -C /home/harry/tbc-tools/build ld-analyse` → built clean (20/20), no errors/warnings from the change.

## Files changed
- `src/ld-analyse/mainwindow.h` — added private method declarations:
  - `void setInPointAtCurrentFrame();`
  - `void setOutPointAtCurrentFrame();`
- `src/ld-analyse/mainwindow.cpp`:
  - Added `MainWindow::setInPointAtCurrentFrame()` / `setOutPointAtCurrentFrame()` — compute the current frame via `frameForSliderPosition(currentFieldNumber|currentFrameNumber)`, call `exportDialog->setInPoint/setOutPoint`, show a status-bar message. Guarded by source-loaded + not metadata-only + exportDialog present.
  - `on_posHorizontalSlider_customContextMenuRequested`: In/Out menu labels now show the **current** frame/timecode (not the click position); the actions call the new helpers. Marker actions still use the click position (`framePoint`) — unchanged.
  - `keyPressEvent`: added `Qt::Key_BracketLeft` (no modifier) → `setInPointAtCurrentFrame()`, `Qt::Key_BracketRight` (no modifier) → `setOutPointAtCurrentFrame()`, using the same typing-context + source-loaded guard as the existing `M` marker shortcut.

## Behavior after change (intended)
- Right-click the timeline slider → "Set In Point (frame | tc)  [" / "Set Out Point (frame | tc)  ]" now act on the frame currently displayed in the viewer, not the pixel you clicked.
- Press `[` → set export In point at the current frame; `]` → set export Out point at the current frame. Active when a source is loaded and focus is not in a text/lineedit/spinbox (so typing `[`/`]` into a timecode/field still inserts the character).
- The in/out spinboxes/timecodes in the Export dialog update, and the green/red markers on the timeline slider refresh (via the existing `userEditRangeSelectionChanged` → `exportRangeSelectionChangedSignalHandler` → `updateTimelineMarkers` path).
- `Ctrl+I` (VBI) and `Ctrl+O` (Open file) are untouched.

## Follow-up change (after user feedback)
- User reported the `[` / `]` suffixes were showing in the right-click menu and asked to **remove them**.
- Removed the `  [` / `  ]` suffixes from the Set In/Out menu labels (they still act on the current displayed frame).
- Added a hover tooltip on the timeline slider: `"Use [ & ] keys to set in & out points at the current frame"` so the shortcut is discoverable without cluttering the menu.
- Rebuilt: `nix develop -c ninja -C build ld-analyse` → clean (18/18).

## Not yet validated
- Real-world GUI behavior not fully confirmed by the user yet. Pending re-test of: menu labels clean of `[`/`]`, tooltip shows on slider hover, `[`/`]` still set in/out at current frame, Ctrl+I/Ctrl+O unchanged.
