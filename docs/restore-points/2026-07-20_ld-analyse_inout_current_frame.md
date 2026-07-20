# Restore point — ld-analyse In/Out at current frame + `[`/`]` shortcuts

Date: 2026-07-20
Base commit (HEAD at capture): `3315dec` (main)
User status: confirmed working ("This looks good").

## What this restore point captures
A confirmed-working state of the ld-analyse change that:
1. Makes the right-click "Set In Point" / "Set Out Point" actions on the timeline slider use the **currently displayed frame** instead of the pixel the user right-clicked.
2. Adds keyboard shortcuts `[` (In) and `]` (Out) that set the export in/out point at the current displayed frame, active whenever a source is loaded and focus is not in a text widget.
3. `M` (no modifier, or Shift+M) opens the Add/Edit Marker comment dialog at the current frame (original behavior preserved).
4. `C` (no modifier) opens the Marker Viewer window.
5. Adds a multi-line hover tooltip on the timeline slider documenting `[`/`]`, `M`, and `C`.
6. Leaves `Ctrl+I` (VBI), `Ctrl+O` (Open TBC/metadata file), and `Ctrl+C` (copy current display) unchanged.

## Changed files (relative to HEAD `3315dec`)
- `src/ld-analyse/mainwindow.h` — added private methods `setInPointAtCurrentFrame()` / `setOutPointAtCurrentFrame()`.
- `src/ld-analyse/mainwindow.cpp`:
  - New `MainWindow::setInPointAtCurrentFrame()` / `setOutPointAtCurrentFrame()` helpers.
  - `on_posHorizontalSlider_customContextMenuRequested`: In/Out menu labels show the current frame/timecode and the actions call the new helpers. Marker actions still use the click position.
  - `keyPressEvent`: handles `Qt::Key_BracketLeft` / `Qt::Key_BracketRight` (no modifier) → set in/out at current frame, with the same typing-context + source-loaded guard as the `M` marker shortcut.
  - Constructor: sets the slider tooltip.
- `docs/prompt-logs/2026-07-20_ld-analyse_inout_current_frame.md` — full prompt log.
- `docs/restore-points/2026-07-20_ld-analyse_inout_current_frame.zip` — this restore point zip (contains the two source files at their confirmed-working state + this log + the prompt log).

## How to restore from this point
```bash
cd /home/harry/tbc-tools
unzip -o docs/restore-points/2026-07-20_ld-analyse_inout_current_frame.zip -d /tmp/restore-inout
cp /tmp/restore-inout/mainwindow.h      src/ld-analyse/mainwindow.h
cp /tmp/restore-inout/mainwindow.cpp    src/ld-analyse/mainwindow.cpp
nix develop -c ninja -C build ld-analyse
```

## Verification at capture time
- Build: `nix develop -c ninja -C /home/harry/tbc-tools/build ld-analyse` → clean (18/18, no errors/warnings from the change).
- User in-app confirmation across three review rounds:
  1. In/Out at current frame + `[`/`]` + tooltip (reply: "This looks good").
  2. M/C rework round 1 rejected; round 2 (M = comment dialog, C = marker viewer) confirmed.
  3. Final reply: "good, commit and push".
