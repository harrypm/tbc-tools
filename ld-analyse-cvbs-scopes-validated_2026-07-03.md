# ld-analyse CVBS scopes — validated restore point (2026-07-03)

## Scope of work
Ported decode-orc's waveform monitor + vectorscope "Multi-colour" colourise and
the 10-bit CVBS_U10_4FSC measurement code into tbc-tools/ld-analyse. This is a
local-repo feature port; decode-orc is only read from, never modified.

Plan document: `d03c04ff-c6f6-4399-8389-5f6bdc6c82b0`.

## Files changed (all under `src/ld-analyse/`)
New:
- `cvbs_signal_constants.h` — normative PAL/NTSC/PAL_M 10-bit CVBS levels + 16-bit TBC constants.
- `amplitude_conversion.h` — `tbc::amp` samples10_to_mv/ire, tick/format helpers, AmplitudeDisplayUnit.
- `tbc_cvbs_conversion.{h,cpp}` — `tbc_to_cvbs()` 16-bit TBC -> 10-bit CVBS per-sample map + `resolve_blanking_16b()` (NTSC 7.5 IRE setup derivation).
- `waveformmonitorwidget.{h,cpp}` — multi-line waveform raster (count buffer, phosphor/gain/Y-only, Blanking/Black/White markers, mV/IRE/10-bit axes).
- `waveformmonitordialog.{h,cpp}` — channel (Y+C/Y-only), range (active/whole), phosphor, intensity; 4-tap FIR `extractYFromComposite`; VBI slicing; QSettings geometry.

Modified:
- `vectorscopedialog.{h,cpp,ui}` — added `multiColourCheckBox`; BT.601 inverse-matrix colourise in `getTraceImage()`.
- `CMakeLists.txt`, `mainwindow.{h,cpp,ui}`, `configuration.{h,cpp}` — waveform monitor wiring, menu action, geometry save/restore.

## E2/E3 reconciliation (this session)
Verified the scopes route through the 10-bit CVBS domain:
- Waveform monitor: `tbc_to_cvbs()` then `tbc::amp::samples10_to_mv/ire` + normative constants. NTSC black marker correctly lands at 282 (7.5 IRE setup) via derived blanking.
- Vectorscope colourise (E3 FIX): the previous lambda divided recovered U/V by
  `ireRange` AND by the 100%-bar magnitudes (0.436010 / 0.614975), which
  double-normalised and warped hues (e.g. yellow rendered as yellow-orange).
  Corrected to match decode-orc's `vectorscope_dialog.cpp` Pass-2 exactly:
  `u_n = u_units / ireRange` (= U_bt601), then `b_raw = 0.5 + u_n/0.492111`,
  `r_raw = 0.5 + v_n/0.877283`, `g_raw = (0.5 - 0.299*r - 0.114*b)/0.587`,
  clamp + max-component normalise. `ireRange` (16-bit) is the correct
  amplitude scale here because ld-decode component U/V = U_bt601 * ireRange;
  this is proportional to decode-orc's `/kVectorscopeSignedFullScale`.

## Deferred (follow-up, not in this restore point)
- E1: CVBS_U10_4FSC file-format loading (`.composite`/`.y`/`.c` + `.meta`
  SQLite sidecar) in `TbcSource`. User chose to defer. The `.meta` schema is
  table `cvbs_file` (preset, sample_encoding_preset, signal_state_preset,
  signal_type, number_of_sequential_frames, audio_locked, black_level);
  require `STANDARD_TBC_LOCKED`; normalise 4 encodings
  (U10 identity / U16 /64 / TPG21 /64+508 / S16 /32+blanking).

## Build / validation
- Build: `nix develop -c ninja -C build ld-analyse` — clean, no errors
  (GCC/Linux Mint, Qt6, Nix flake). HEAD before session: tbc-tools main
  eb9d5e7; decode-orc read from `b4d5310`.
- GUI contrast guard: `tbc::ui::enforceInputWidgetContrast(a)` applied
  application-wide in `main.cpp:295` — covers the new dialog (AGENTS.md rule).
- ctest: unit-test binaries not built in this config ("Not Run / unable to find
  executable"); integration tests fail due to uninitialized `testdata/`
  submodule — pre-existing/environmental, not caused by this GUI-only change.
- Live GUI validation: launched `build/bin/ld-analyse` with
  `test-data/pal/kagemusha-leadout-cbar.tbc` (PAL colour bars). User confirmed
  in person: waveform monitor Blanking/Black/White markers line up with the
  composite levels; vectorscope Multi-colour hues land in the correct
  graticule target boxes (B/G/Cy/R/Mg/Yl). Both scopes confirmed correct.

## Restore point
Companion zip: `ld-analyse-cvbs-scopes-validated_2026-07-03.zip` (this
directory) containing the changed `src/ld-analyse/` files listed above.
