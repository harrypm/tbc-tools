# Prompt readme — RF export source-selection fix (issue #29) (2026-09-30)

## Prompt
"https://github.com/harrypm/tbc-tools/issues/29 it seems like there is only one minor bug with output naming and source ID targeting but otherwise this is good" → user approved "Option C" (both fixes).

## Issue report (taymur0804, 2026-09-30 19:37Z)
- Confirmed the sample-rate scaling fix (d9a6a77) works perfectly on the same 20 Msps capture (12 ms padding → correct tiny segment, decodes).
- Noted "6 ms seems to be the new default" but 6 ms was too short to decode on that tape (12 ms works). Verified: the stock default in configuration.cpp is still 500 ms (read/setDefault both 500); 6 ms was the reporter's own persisted value from earlier interactive testing — settings persistence working as designed, per-tape values are remembered.
- Remaining bug: after exporting one RF snippet, the next export's auto-discovery picked the previously exported `...__frame_XXXXXX_rf.flac` in the same folder as the RF source instead of the original capture. Deleting/moving the snippet restored correct selection.
- Root cause (code walk): discovery iterates progressively shorter basename prefixes and breaks on the first prefix with a hit — the snippet (which carries the TBC's full basename, e.g. `capture_luma_12-bit_20msps_vhsv__frame_000123_rf.flac`) matches the longest prefix while the original `capture_luma_12-bit_20msps.flac` only matches a shorter one, so the snippet outranks it.

## Fix — "Option C": both reporter-suggested mitigations
1. **Own-output exclusion**: `runRfSegmentExport()` builds `rfSnippetPattern` = `__frame_\d+_rf\.(?:flac|ldf)$` (case-insensitive) and an `isRfSnippetName` lambda; snippet-named files are skipped as source candidates everywhere (exact-name candidates, directory scan) and never accepted as a remembered source.
2. **Remembered source**: new `rfExport/sourcePath` configuration key (additive; read falls back to empty on older INIs). After a successful probe the used RF source is persisted (`setRfExportSourcePath` + write, alongside the existing FLAC-Chop path persistence). On later exports the remembered source is preferred while it (a) still exists, (b) is not a snippet name, (c) sits in the same folder as the loaded TBC — so switching capture folders re-runs discovery. Picker results in interactive mode also get persisted on probe success.
- ci/check_ci_contracts.py: TBC_ANALYSE_RF_EXPORT_REQUIRED_SNIPPETS extended with `isRfSnippetName` + `getRfExportSourcePath`; comment documents the source-selection contract.

## Commands run
```
gh issue view 29 --repo harrypm/tbc-tools --json comments   # read the new report
nix develop -c ninja -C /home/harry/tbc-tools/build          # rebuild OK (26/26)
python3 ci/check_ci_contracts.py                             # "CI contract checks passed."
nix develop -c ctest --test-dir /home/harry/tbc-tools/build --output-on-failure   # 100% passed, 30/30
git commit + push (this commit)
```

## Files changed
- src/tbc-analyse/mainwindow.cpp (snippet exclusion + remembered-source preference + persistence)
- src/tbc-analyse/configuration.h / configuration.cpp (rfExport.sourcePath key + accessors)
- ci/check_ci_contracts.py (contract snippets)

## Status
- Local: build clean, contracts pass, ctest 30/30.
- CI (full release-pipeline Tests workflow) monitored by agent; verdict pending.
- Reporter re-test needed (user rule: real-world confirmation) — new test build via the CI run artifacts.
