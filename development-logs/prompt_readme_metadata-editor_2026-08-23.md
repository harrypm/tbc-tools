# Prompt README — Metadata Editor dialog + SECAM/MESECAM enum extension

**Date:** 2026-08-23
**Repo:** `C:\Users\Harry\tbc-tools` (branch: `main`)
**Model:** glm 5.2 (Oz)

## User input

> "Add metdata page to ld-analyse"
> "all fields are editable aside from SNR values"
> "when saved it reloads with new saved metadata"
> "SECAM and MESECAM are 2 values that should both be treated as one SECAM by the tools"

## What changed

### 1. VideoSystem enum extended (`src/library/tbc/tbcmetadata.{h,cpp}`)
- Added `SECAM` and `MESECAM` as separate enum values (after `PAL_M`).
- Both share the same 625-line geometry as PAL (same active-line defaults); fSC set to PAL's placeholder.
- `parseVideoSystemName` now resolves "SECAM"→`SECAM` and "MESECAM"→`MESECAM` via the `VIDEO_SYSTEM_DEFAULTS` loop (removed the old alias that mapped both to `PAL`).
- JSON write round-trips the exact name ("SECAM" / "MESECAM") in the `system` field.
- `isSecamFamilyVideoSystemName` unchanged (still recognises both names).

### 2. Decode pipeline updated (`src/ld-analyse/tbcsource.cpp`)
- Every `system == PAL || PAL_M` check (8 locations: configureChromaDecoder, decodeFrame ×2, applyChromaSettingsFromMetadata, loadInputFields, startBackgroundLoad ×2) now includes `SECAM || MESECAM`.
- The chroma decoder selection is still controlled by the `chromaDecoder` string ("secam"/"mono"/"pal2d" etc.), not by the system enum. SECAM-system sources now have `system == SECAM` (not `PAL`) but decode identically via `SecamDecoder`.

### 3. TbcSource PCM audio setters (`src/ld-analyse/tbcsource.{h,cpp}`)
- Added `getPcmAudioParameters()` and `setPcmAudioParameters()` wrappers (the library had them; TbcSource didn't).

### 4. New MetadataEditorDialog (`src/ld-analyse/metadataeditordialog.{h,cpp,ui}`)
- Form-layout dialog editing **all** `VideoParameters` + `PcmAudioParameters` fields except SNR (which is per-field VitsMetrics).
- TV System: QComboBox (PAL, NTSC, PAL-M, SECAM, MESECAM).
- Chroma decoder: QComboBox populated based on system (PAL-family: mono/pal2d/transform2d/transform3d/secam; NTSC: mono/ntsc1d/ntsc2d/ntsc3d/nntransform3d).
- Checkboxes: widescreen, subcarrier locked, mapped, NTSC adaptive, NTSC phase compensation, PCM signed, PCM little-endian.
- Double spinboxes: chroma gain/phase, luma NR, NTSC adapt threshold/weight, PAL transform threshold, sample rate, PCM sample rate.
- Integer spinboxes: colour burst start/end, active video start/end, white/black/blanking 16b IRE, PCM bits.
- Line edits: tape format (free text), git branch, git commit.
- OK / Cancel / Apply buttons.
- Signals: `videoParametersChanged`, `pcmAudioParametersChanged`.

### 5. MainWindow integration (`src/ld-analyse/mainwindow.{h,cpp,ui}`)
- **Tools → Metadata Editor...** menu action added.
- Dialog created in ctor; `videoParametersChanged` → existing `videoParametersChangedSignalHandler` (live apply + mark Save dirty); `pcmAudioParametersChanged` → lambda that calls `tbcSource.setPcmAudioParameters` + enables Save.
- **Reload after save:** `on_finishedSaving(true)` now reloads the source with `preserveStateDuringReload=true` so the GUI reflects the edited metadata (e.g. TV system change, chroma decoder switch).

### 6. Build wiring
- `src/ld-analyse/CMakeLists.txt`: added `metadataeditordialog.cpp/.h/.ui` to `ld-analyse_SOURCES`.

## Build/CI results

- **ld-analyse build:** success — `build\bin\ld-analyse.exe` (compiled metadataeditordialog.cpp + MOC + UIC; all system== checks updated).
- **CI contracts:** `python ci\check_ci_contracts.py` → "CI contract checks passed."

## Validation status

- [x] Clean compile of ld-analyse (primary correctness gate).
- [x] CI contracts pass.
- [ ] **Runtime GUI confirmation — PENDING user.** Please:
  1. Load a TBC source.
  2. Tools → Metadata Editor...
  3. Change TV System to SECAM (or MESECAM).
  4. Click OK.
  5. Verify File → Save Metadata is now enabled.
  6. Save.
  7. Verify the source reloads and the chroma decoder switches to SECAM.
  8. Verify other fields (chroma gain, IRE levels, tape format, PCM audio) edit correctly.

## Notes

- No commit made (user has not requested one).
- SECAM and MESECAM are two enum values but decode identically (both → SecamDecoder). The distinction round-trips in the `system` JSON field and can be recorded in `tapeFormat`.
- The Metadata Editor overlaps with VideoParametersDialog on active-video/line fields — both can edit them; the slider dialog remains the interactive editor.
- `CudaPluginManager` / `cudaPlugin` config / plugin catalog: untouched.
