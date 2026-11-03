# Teletext Viewer Centering Fix

## Issue
The Teletext Viewer dialog was not centering over the main `ld-analyse` window when data was loaded, despite other sub-windows (VBI, Oscilloscope, Dropout Analysis, etc.) working correctly.

## Root Cause
The Teletext Viewer was implementing **custom manual centering logic** that conflicted with `ld-analyse`'s standard sub-window centering pattern:

- Custom `nabtsWindowCentered` flag to track centered state
- Custom `centerReferenceWidget` member to store main window reference
- Manual `move()` calls in `autoSizeWindowForCurrentRenderer()`
- Calls to `setCenterReferenceWidget(this)` in MainWindow open paths

This violated the architectural pattern where `MainWindow::eventFilter` (lines 1910–1982) automatically centers **all** sub-windows via `tbc::ui::centerDialogOverParent(widget)` on `QEvent::Show`.

## Solution
**Removed all custom centering code** and rely on the standard `MainWindow::eventFilter` pattern:

### Deleted from TeletextViewerDialog:
- `setCenterReferenceWidget(QWidget*)` public method
- `nabtsWindowCentered` member variable
- `centerReferenceWidget` member variable
- All manual `move()` positioning logic from `autoSizeWindowForCurrentRenderer()`
- References to `nabtsWindowCentered` flag in `refreshPageList()`

### Removed from MainWindow open paths:
- `teletextViewerDialog->setCenterReferenceWidget(this)` calls in:
  - `dropEvent()` (line ~2116)
  - `on_actionProcess_VBI_triggered()` (line ~4851)
  - `on_actionTeletext_Viewer_triggered()` (line ~5578)

### How it works now:
1. Teletext Viewer is created with `parent = nullptr` and `Qt::Window` flag (independent top-level window).
2. When `show()` is called, a `QEvent::Show` fires.
3. `MainWindow::eventFilter` intercepts the Show event.
4. Since the Teletext Viewer has `MainWindow` in its ancestor chain (even though parent is `nullptr`, it's registered in Qt's hierarchy), the eventFilter applies centering via `tbc::ui::centerDialogOverParent()`.
5. The dialog centers over the main window, accounting for window frame margins and screen boundaries.

## Pattern Compliance
All sub-windows in `ld-analyse` now follow the same centering pattern:
- VBI Dialog
- Oscilloscope Dialog (comment at line 881: "Centering over the main window is handled centrally by MainWindow's Show-event filter via tbc::ui::centerDialogOverParent.")
- Dropdown Analysis Dialog
- SNR Analysis Dialogs
- **Teletext Viewer Dialog** (now fixed)

## Files Modified
- `src/ld-analyse/teletextviewerdialog.h`
- `src/ld-analyse/teletextviewerdialog.cpp`
- `src/ld-analyse/mainwindow.cpp`

## Testing
- Rebuild: `cmake --build build --config Release --target ld-analyse`
- Open `ld-analyse`
- Load/drop a `.t33` NABTS file or use Tools > Teletext Viewer
- Verify: Teletext Viewer dialog opens **centered over main window** and does **not move** on page changes

## Key Takeaway
**Hard Rule**: Never implement custom window positioning logic. Always defer to the standard `MainWindow::eventFilter` pattern for centering sub-windows. The pattern is centralized, maintainable, and consistent across the entire application.
