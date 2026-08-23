# Prompt README — Graph hover readout + SNR in-bounds scaling

**Date:** 2026-08-23
**Repo:** `C:\Users\Harry\tbc-tools` (branch: `main`)
**Model:** glm 5.2 (Oz)

## User input (request)

> "improve the graphs, so that you can get a dropper/mouse over the peaks to
> see the exact value, also tighten them up so that that line stays inside
> the boundry of the scales for the SNR graphs"

## Clarifying answers (from user)

1. Hover snaps to the **nearest data point** on the line, draws a crosshair
   on it, and shows its exact value (e.g. "Frame 1234: 43.7 dB").
2. "stays inside the boundary" — the current scale goes entirely out of
   bounds of the H/V scales of information (the line draws outside the plot
   rectangle into the axis margins).
3. Hover is a **shared feature** for all analysis plot dialogs that use
   PlotWidget (Black SNR, White SNR, Dropout, Visible Dropout).

## Design summary

- **Hover (shared, PlotWidget):** on mouse-move with no button held, binary-
  search each visible series' sorted data for the nearest point to the cursor
  (Euclidean distance in scene space), snap a dashed crosshair marker to it,
  show a themed label (translucent background) with the value formatted by a
  per-dialog formatter; hide on Leave / cursor outside the plot rect. Emits
  `plotHovered(dataPoint, series)`.
- **In-bounds (two layers):**
  1. `PlotSeries::paint()` now clips to the plot rectangle — the pen can
     never draw into the axis margins. This is the hard guarantee and applies
     to all four dialogs (also tidies the dropout bars).
  2. SNR dialogs compute the Y max from the actual data with headroom (5%,
     min 2 dB) and set the Y min 1 dB below the clamp floor, so peaks and
     floor-clamped points sit visually clear of the border (not jammed on it).
- Dropout dialogs already had 10% Y headroom; scaling unchanged (clip-only).

## Files modified

- `src/ld-analyse/plotwidget.h` — hover API (`setHoverEnabled`,
  `setHoverFormatter`, `plotHovered` signal), `m_hoverMarker`/`m_hoverLabel`/
  `m_hoverFormatter`/`m_hoverEnabled` members, `findNearestDataPoint`/
  `showHoverReadout`/`hideHoverReadout` helpers; `PlotSeries` gains
  `m_plotRect` + `paint()` override; `<QGraphicsTextItem>` + `<functional>`.
- `src/ld-analyse/plotwidget.cpp` — create hover marker/label in `setupView`;
  hover branch in `eventFilter` MouseMove + `Leave` handler; hover helpers
  (nearest-point binary search, crosshair+label positioning with in-view
  flip/shift); `replot()` updates hover marker geometry; `PlotSeries::paint`
  `setClipRect(m_plotRect)`; `updatePath` caches `m_plotRect`.
- `src/ld-analyse/blacksnranalysisdialog.cpp` — hover formatter
  ("Frame N: M.M dB"); Y max = `ceil(actualMax + max(2, actualMax*0.05))`
  with 48 dB floor; Y range `(19.0, maxY)`; marker mid-Y uses 19.0.
- `src/ld-analyse/whitesnranalysisdialog.cpp` — same pattern, floor 14,
  bottom margin 1 (Y range `(13.0, maxY)`), 42 dB floor; marker mid-Y 13.0.
- `src/ld-analyse/dropoutanalysisdialog.cpp` — hover formatter
  ("Frame N: M dots"); scaling unchanged.
- `src/ld-analyse/visibledropoutanalysisdialog.cpp` — hover formatter
  ("Frame N: M dots"); scaling unchanged.

## Plan

- Plan artifact created (`create_plan`): "Graph hover readout + SNR
  in-bounds scaling" — approved (user said "continue").

## Commands run

1. Reads of `plotwidget.{h,cpp}`, `black/whitesnranalysisdialog.{h,cpp}`,
   `dropout/visibledropoutanalysisdialog.cpp`, file_glob for SNR/plot files,
   grep for existing hover/tooltip machinery.
2. `cmake --build C:\Users\Harry\tbc-tools\build --config Release --target ld-analyse`
   → **Success.** Compiled plotwidget.cpp, black/white/dropout/visibledropout
   analysisdialog.cpp + MOC. `build\bin\ld-analyse.exe`. No compile/link
   errors.
3. `python C:\Users\Harry\tbc-tools\ci\check_ci_contracts.py`
   → **"CI contract checks passed."** (no regression)

## Validation status

- [x] Clean compile of all changes (primary correctness gate).
- [x] `ci/check_ci_contracts.py` passes.
- [ ] **Runtime GUI confirmation — PENDING user.** Please open each analysis
      dialog (Analysis menu → Black SNR / White SNR / Dropout analysis /
      Visible Dropout analysis) and confirm:
        - hovering over the line snaps a crosshair to the nearest point and
          shows its exact value ("Frame N: M.M dB" / "Frame N: M dots"),
        - the SNR line no longer touches/exceeds the top or bottom border and
          peaks are fully visible inside the frame,
        - the dropout bars still render correctly (clip didn't break them),
        - zoom (wheel) / pan (right-drag) / click / double-click-reset still
          work and hover doesn't fight them (hover is suppressed while a
          button is held).

## Notes

- No commit made (user has not requested one).
- No restore-point zip (created only when the user states a change is
  "fully fixed/working" — not yet confirmed).
- No child agents: edits tightly coupled across `plotwidget` + 4 dialogs;
  done sequentially.
- The clip is the hard correctness guarantee for "stays inside"; the SNR
  headroom/margin keeps the data visually clear so the clip doesn't merely
  truncate a peak jammed at the edge.
## Revision 2 — Ctrl+C copies current frame from graph windows (2026-08-23)

### User request
> "add CTRL+C to copy current frame as PNG on the graph windows"

### Implementation
- Each of the 4 analysis graph dialogs (Black SNR, White SNR, Dropout,
  Visible Dropout) now binds Ctrl+C (`QKeySequence::Copy`) via a dialog-scoped
  `QShortcut` that emits a new `copyFrameRequested()` signal.
- `MainWindow` gained a `copyCurrentFrameToClipboard()` helper (factored out of
  the existing "Copy current display" menu action): checks a source is loaded,
  gets `renderedCurrentImageForExport()`, and `clipboard->setImage(...)`. The
  menu action's image branch now calls this helper (no behaviour change there).
- In the MainWindow ctor, all 4 dialogs' `copyFrameRequested` are connected to
  `copyCurrentFrameToClipboard`.
- No shortcut conflict: the main-window Ctrl+C action uses `Qt::WindowShortcut`
  scope, so it only fires when the main window has focus; the graph dialogs are
  separate top-level windows whose dialog-scoped shortcut fires when they have
  focus. Pressing Ctrl+C while focused on a graph copies the current video
  frame to the clipboard.

### Files modified (Revision 2)
- `src/ld-analyse/black/white/dropout/visibledropout analysisdialog.h` — add
  `signals: void copyFrameRequested();`.
- `src/ld-analyse/{black,white,dropout,visibledropout}snranalysisdialog.cpp`
  (etc.) — `#include <QShortcut>` + Ctrl+C `QShortcut` → emit
  `copyFrameRequested()`.
- `src/ld-analyse/mainwindow.h` — declare `copyCurrentFrameToClipboard()`.
- `src/ld-analyse/mainwindow.cpp` — implement helper; refactor
  `on_actionCopy_current_display_to_clipboard_triggered` image branch to call
  it; connect the 4 dialog signals in the ctor.

### Revision 2 commands run
- `cmake --build build --config Release --target ld-analyse` → **Success**
  (compile + link clean after the user closed the running ld-analyse.exe that
  had caused an LNK1104 lock). `build\bin\ld-analyse.exe`.
- `python ci\check_ci_contracts.py` → **"CI contract checks passed."**

### Revision 2 validation status
- [x] Clean compile + link.
- [x] CI contracts pass.
- [ ] **Runtime confirmation — PENDING user.** With a source loaded, focus a
      graph window (Black/White SNR / Dropout / Visible Dropout), press Ctrl+C,
      then paste into an image editor and confirm the current video frame
      appears. (The existing main-window Ctrl+C behaviour is unchanged.)

## Revision 3 — Pinch-to-zoom + left-drag pan (2026-08-23)

### User request
> "add pinch and move up/down/left/right in the graphs so the scale can be
> repositioned when zoomed in."

### Clarifying answers
- Pinch = both: touchpad/touchscreen pinch-gesture zoom AND left-drag pan.
- Move = left-drag to pan (common convention); keep right-drag pan too.

### Implementation (all in `src/ld-analyse/plotwidget.cpp`)
- **Pinch-zoom:** `#include <QGestureEvent>`/`<QPinchGesture>`;
  `m_view->viewport()->grabGesture(Qt::PinchGesture)` in `setupView()`; a
  `QEvent::Gesture` branch in `eventFilter` extracts the `QPinchGesture`, maps
  its `centerPoint()` (global) to scene coords, and calls `zoomAt(scenePos,
  1.0/sf)` where `sf` is the gesture's incremental `scaleFactor()`. The invert
  is because pinch sf>1 (fingers apart) = zoom in, while `zoomAt` treats <1 as
  zoom-in. Guards: `m_zoomEnabled`, `sf>0`, `|sf-1|>1e-3`, pinch centre within
  the plot rect. Works alongside the existing mouse-wheel zoom.
- **Left-drag pan:** repurposed the left-button drag (which previously only
  emitted an unused `plotDragged` signal) to pan via `panBySceneDelta`. On
  left-press inside the plot rect: `m_isDragging=true`, record
  `m_lastPanScenePos`, `hideHoverReadout()`, still emit `plotClicked`. On
  left-move: `panBySceneDelta(scenePos - m_lastPanScenePos)`, update anchor. The
  widget's own `mousePressEvent`/`mouseMoveEvent` fallbacks were updated to
  match. Right-drag pan (`m_isPanning`) is unchanged, so both gestures pan.
- Double-click reset and wheel zoom are unchanged. Hover is suppressed during a
  drag so the crosshair doesn't freeze at a stale position.
- `plotDragged` is now an unused signal (kept declared to avoid API churn; no
  consumer connected it for the analysis dialogs — verified the oscilloscope
  uses a *separate* `gui/oscilloscope/plotwidget.{h,cpp}`).

### Revision 3 commands run
- Process check: `ld-analyse` not running (avoided an LNK1104 lock).
- `cmake --build build --config Release --target ld-analyse` → **Success**
  (compiled plotwidget.cpp + MOC; `build\bin\ld-analyse.exe`).
- `python ci\check_ci_contracts.py` → **"CI contract checks passed."**

### Revision 3 validation status
- [x] Clean compile + link.
- [x] CI contracts pass.
- [ ] **Runtime confirmation — PENDING user.** In any analysis graph, confirm:
  wheel-zoom still works; pinch-to-zoom works on a touchpad/touchscreen;
  **left-drag pans** the view up/down/left/right when zoomed in; right-drag
  still pans; double-click resets the zoom; the hover readout resumes after the
  drag ends.

- `CudaPluginManager` / `cudaPlugin` config / plugin catalog: untouched.
