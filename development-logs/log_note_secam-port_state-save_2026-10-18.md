# Log note — SECAM decoder port: state save (2026-10-18, ~15:35)

User: "save state to dev log and stop". Nothing is committed; this note +
the zips below are the go-back record.

## Working tree (uncommitted, this session's work)

- `src/tbc-chroma-decoder/secamdecoder.cpp` / `.h` — the ported decoder
  (libchromadec v0.1.0 pipeline + tbc-tools adaptations + adaptive
  input band + ident ladder + calibration sanity gates). Built, ctest
  33/33 (incl. the 3 SECAM fixture floors), guardrails pass.
  - **State zip (this version):**
    `development-logs/secamdecoder_post-vsanalogue-port_20261018_153454.zip`
  - **Pre-port zip (the old decoder):**
    `development-logs/secamdecoder_pre-vsanalogue-port_20261018_020621.zip`
    (bytes == commit `4e91a5c`; also recoverable via
    `git checkout 4e91a5c -- src/tbc-chroma-decoder/secamdecoder.*`)
- `development-logs/prompt_readme_secam-vsanalogue-param-port_2026-10-15.md`
  — the full session log (port design, every A/B measurement, the
  combined-input investigation, the adaptive band, timings).
- `development-logs/dev_note_secam-analyse-style-inspection_2026-10-18.md`
  — the inspection method + the vectorscope offset-correction measurement
  + the fitted correction (gain 1.425, phase ~0; Db/Dr ellipse documented
  as capture HF response).
- `development-logs/log_note_secamdecoder_pre-vsanalogue-port_restore-point_2026-10-18.md`
  — the pre-port restore-point note.
- `test-data/secam/secam-etienne-cbars-10f_chroma.tbc.db` (new, untracked)
  — converted from the shared fixture json for chroma-input decodes (the
  etienne fixture set shipped without one; tape275's has one for its ctest).

Not this session's work (user's parallel work, untouched): the tbc-analyse
export-options files (`src/tbc-analyse/*`, `advancedexportoptionsdialog.*`,
`prompt_readme_analyse-export-advanced-options_2026-10-08.md`,
`prompt_cvbs-combined-luma-notch_2026-10-18.md`).

Incident + resolution: during the session the etienne fixture
`secam-etienne-cbars-10f.tbc.json` was re-serialized in place by a metadata
writer (compact one-line JSON with normalized defaults — same data, new
format; `.bup` copies at `*.20261018_01*.bup`). Restored from HEAD
(`git checkout HEAD -- test-data/secam/secam-etienne-cbars-10f.tbc.json`),
fields/counts verified (20 fields, numberOfSequentialFields 20), SECAM
ctest fixtures re-verified 3/3. The stray `.bup` files remain in
test-data/secam/ and can be deleted.

## Verified state (hard data in the prompt readme)

- tape275 MESECAM (split chroma): +105 kHz block offset measured and
  absorbed every field (fob 4.360 / for 4.508 MHz); ident 20/20 fields
  BR.469 four-field-cycle perfect at 0.99-1.00 confidence; grey testcard
  rows read demod +-0.02; V noise std 5154 -> 4742.
- etienne SECAM (split chroma): ident 20/20 cycle-perfect via the BT.470
  field-ident bottles (the capture carries them: rows 6-14, Db bottle
  3.89 MHz); neutral bars 3x closer to zero; offset cast removed
  (vectorscope mean colour-bar displacement -0.024/-0.008 -> +0.000/+0.001).
- Vectorscope-derived correction: chroma gain 1.425, phase ~0 (verified:
  mean per-bar residual 0.178 -> 0.099). Remaining error is the Db/Dr
  ellipse (capture HF channel response, identical in the old decoder).
- Known limitations, documented: combined luma+chroma inputs are a
  per-row coin toss in BOTH decoders (in-band luma); the CURRENT etienne
  combined `.tbc` has its chroma suppressed ~52x by the post-September
  re-decode (the pre-re-decode combined file survives only as
  `Desktop/SECAM/snippets/secam_etienne_colorbars_50f.tbc`); the split
  `_chroma.tbc` is the only clean etienne input.
- One recovery incident (fully recovered + re-verified): a stale backup
  copy of the ported decoder was restored over the adaptive-band version
  mid-session; all edits were re-applied and everything re-verified
  (33/33, identical decode results).

## Pending decisions (user's call, in order)

1. Commit + push the port (then the CI gate: Tests workflow must pass —
   hard rule).
2. Bake `chromaGain: 1.425` into the etienne capture metadata (per-source
   calibration) or leave it as a GUI/CLI setting.
3. Replace the etienne combined fixture `.tbc`/`.tbc.flac` with the
   pre-re-decode version (real chroma level, cut + validated at fields
   1-20) or keep as-is.
4. Delete the stray `.bup` files in test-data/secam/.

## Resume pointers

- Session log with every command/result:
  `development-logs/prompt_readme_secam-vsanalogue-param-port_2026-10-15.md`
- Inspection method + correction numbers:
  `development-logs/dev_note_secam-analyse-style-inspection_2026-10-18.md`
- A/B artifacts in `/tmp/secam-ab/` (old/new YUV decodes, PNGs,
  vectorscopes, diagnostics) — temporary; the durable record is this note +
  the prompt readme.
- Quick re-verify after any resume:
  `nix develop -c ninja -C build && ctest --test-dir build --output-on-failure`
