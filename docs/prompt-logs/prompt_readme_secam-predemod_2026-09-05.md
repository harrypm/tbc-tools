# Prompt README — SECAM pre-demod decoder + secamFirstLineIsRed control

- Date (UTC): 2026-09-05
- Repo: /home/harry/tbc-tools (branch: main)
- Source fork: https://github.com/hugoatease/tbc-tools (commits 69932ad, de2d760)

## Goal
Port the fork's missing SECAM decoder/control features into harrypm/tbc-tools:
1. Add the fork's pre-demodulated Dr/Db SECAM decoder as a NEW selectable decoder
   (`secam-predemod`), keeping the existing FM-demod `secam` decoder intact.
2. Add the fork's per-field `secamFirstLineIsRed` metadata field (JSON + SQLite).
3. Expose `secamFirstLineIsRed` in the Metadata Editor (per-field) plus a
   decode-time override (CLI `--secam-first-line-is-red` + GUI control).
4. Support both SECAM source types (vhs-decode method-1/MESECAM FM block, and
   vhs-decode native secam.py pre-demod Dr/Db).

## Inputs / context
- Existing repo already has advanced FM-demod SECAM decoder
  (src/ld-chroma-decoder/secamdecoder.cpp/.h, 443 lines) + full GUI/CLI/Python
  wiring for `secam`.
- Fork's decoder (211 lines) expects pre-demod Dr/Db and reads
  `sf.field.secamFirstLineIsRed` for line identity.
- test-artifacts/secam-crash.log shows "This decoder is for PAL-line
  (SECAM/MESECAM) video only" rejections.

## Commands run / outputs
- Research: gh api compare commits 69932ad, de2d760; fetched fork secamdecoder.{h,cpp}.
- CI contracts: python3 ci/check_ci_contracts.py -> "CI contract checks passed." (exit 0)
- Configure: nix develop -c cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -> exit 0
- Build: nix develop -c ninja -C build ld-chroma-decoder ld-analyse testmetadata -> 61/61, exit 0
- C++ tests: ctest --test-dir build -R testmetadata|testlinenumber|testvbidecoder|testvitcdecoder -> 5/5 passed, exit 0
- Python tests: PYTHONPATH=src pytest test_tbc_json.py test_wrappers_ldtools.py -> 83 passed, exit 0
  (Full suite: 132 passed; remaining errors are pre-existing infra gaps: pymediainfo, ffmpeg binary, pytest-mock mocker fixture — none SECAM-related)
- CLI verify: build/bin/ld-chroma-decoder --help shows "secam-predemod" + "--secam-first-line-is-red"

## Files changed
### New
- src/ld-chroma-decoder/secampredemoddecoder.h / .cpp — pre-demod Dr/Db SECAM decoder (SecamPredemodDecoder/Thread)
- src/tbc-video-export/tests/files/secam_predemod_composite.tbc.json — test fixture
- prompt_readme_secam-predemod_2026-09-05.md — this log

### Modified — C++ metadata layer
- src/library/tbc/tbcmetadata.h / .cpp — secamFirstLineIsRed per-field field (struct, JSON, SQLite read/write) + copyright
- src/library/tbc/sqliteio.cpp / .h — field_record schema v7 column + ensureFieldRecordColumns ALTER migration + conditional readFields SELECT + writeField param + copyright

### Modified — ld-chroma-decoder CLI
- src/ld-chroma-decoder/CMakeLists.txt — added secampredemoddecoder sources
- src/ld-chroma-decoder/main.cpp — -f secam-predemod + validDecoders + config + instantiation + chromaGain + --secam-first-line-is-red tristate
- src/ld-chroma-decoder/palcolour.h — secamPredemod enum value
- src/ld-chroma-decoder/secamdecoder.h / .cpp — Harry Munday copyright (no logic change)

### Modified — ld-analyse GUI
- src/ld-analyse/tbcsource.h / .cpp — SecamPredemodDecoder member/config + setSecamPredemodFirstLineIsRedOverride + setSecamFirstLineIsRed + getSecamFirstLineIsRed + decodeFrame branches + applyChromaSettingsFromMetadata SECAM split + configureChromaDecoder
- src/ld-analyse/chromadecoderconfigdialog.h / .cpp / .ui — secamButtonGroup + secamMonoRadioButton + palFilterSecamPredemodRadioButton + secamFirstLineIsRedComboBox (tri-state) + separated SECAM/PAL radio enablement + handlers
- src/ld-analyse/metadataeditordialog.h / .cpp / .ui — per-field secamFirstLineIsRedCheckBox + applySecamToAllFieldsCheckBox + setSecamFieldContext + signal + split decoder combo by system + secam-predemod auto-select
- src/ld-analyse/mainwindow.cpp — chromaDecoderNameFromConfig SECAM branch + override push + Metadata Editor SECAM context + secamFirstLineIsRedChanged handler
- src/ld-analyse/exportdialog.cpp — isSecamFamilySystem + secamDecoders list (secam removed from palDecoders)

### Modified — tbc-video-export Python
- src/tbc_video_export/common/enums.py — VideoSystem.SECAM/MESECAM + ChromaDecoder.SECAM_PREDEMOD
- src/tbc_video_export/common/video_system.py — video_system_secam VideoSystemData + VideoSystemData.get SECAM/MESECAM case
- src/tbc_video_export/common/tbc_json_helper.py — video_system returns SECAM/MESECAM (not PAL)
- src/tbc_video_export/opts/opt_validators.py — SECAM/MESECAM validation case
- src/tbc_video_export/opts/opt_types.py — TypeChromaDecoder hyphen→underscore lookup fix
- src/tbc_video_export/opts/opts.py — secam_first_line_is_red Opts field
- src/tbc_video_export/opts/opts_ldtools.py — SECAM_PREDEMOD help + --secam-first-line-is-red option
- src/tbc_video_export/process/wrapper/wrapper_ld_chroma_decoder.py — SECAM valid set {MONO,SECAM,SECAM_PREDEMOD} + secam removed from PAL set + --secam-first-line-is-red pass-through

### Modified — tests
- src/tbc-video-export/tests/test_tbc_json.py — SECAM/MESECAM → own VideoSystem (not PAL)
- src/tbc-video-export/tests/test_wrappers_ldtools.py — PAL+secam now exception; added secam-predemod + override + PAL-on-SECAM exception cases
- src/tbc-video-export/tests/files/secam_composite.tbc.json / mesecam_composite.tbc.json — added secamFirstLineIsRed per-field

## Status
Implementation complete. Build + tests + CI contracts pass. Awaiting user real-data verification before creating the development-logs restore point.
