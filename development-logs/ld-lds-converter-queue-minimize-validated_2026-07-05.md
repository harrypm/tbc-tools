# ld-lds-converter — one-by-one input queue + minimize button (validated 2026-07-05)

## Summary
- GUI now appends `.lds` files to the queue one-by-one (click **Add...**,
  drag/drop, or type a path and press **Enter**) instead of replacing the whole
  queue on each add. Duplicates are skipped, so files can be accumulated from
  several different directories rather than selected all at once from one.
- Added **Remove selected** and **Clear all** queue-management buttons; the
  queue table now supports extended row selection.
- Dialog promoted to a top-level `Qt::Window` so the title bar shows a working
  minimize (and maximize) button on Linux WMs; `exec()` still keeps it modal.

## Files changed
- `src/ld-lds-converter/converterdialog.h`
- `src/ld-lds-converter/converterdialog.cpp`
- `src/ld-lds-converter/converterdialog.ui`
- `src/ld-lds-converter/README.md`

## Build / test
- Built clean inside `nix develop`: `ninja -C build ld-lds-converter` (two
  builds — initial, then after the `Qt::Window` minimize fix).
- `ctest -R ld-lds-converter`: 3/3 passed (help, invalid-format,
  invalid-compression-level).
- Binary: `build/bin/ld-lds-converter`.

## Real-world GUI confirmation (user-verified 2026-07-05)
- Append (Add... / drag-drop / type+Enter) + Remove selected + Clear all:
  confirmed working.
- Minimize button: confirmed visible and working after promoting the dialog to
  `Qt::Window` (the initial `Qt::Dialog` + `Qt::WindowMinimizeButtonHint`
  approach showed no minimize button on this Linux WM).
- Launch command:
  `cd /home/harry/tbc-tools && nix develop -c build/bin/ld-lds-converter`

## Restore point
This `.md` and the accompanying
`ld-lds-converter-queue-minimize-validated_2026-07-05.zip` preserve the working
state of the changed files at the time of validation. To restore, unzip over a
clean checkout and rebuild with `nix develop -c ninja -C build ld-lds-converter`.
