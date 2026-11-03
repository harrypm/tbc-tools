# Prompt readme — RF export sample-rate scaling fix (issue #29) (2026-09-30)

## Prompt
"https://github.com/harrypm/tbc-tools/issues/29 user noted some quirks and issues"

## Issue report (user taymur0804, verbatim data)
- 40 Msps "Munday" demo tape: RF export works. ~12 ms padding before/after is about the minimum that still yields a decodable frame.
- 20 Msps MISRC VHS capture: RF export landed at ~2x the expected position (selected decoded frame ~3:57, export cut ~8 min; correct RF position ~4:02 due to ~5 s unusable lead-in signal).
- Frame's metadata fileLoc = 9694156800; dividing by 2 → 4847078400, a manual FFmpeg cut at that position in the 20 Msps RF put the intended frame in the middle.
- Diagnosis by reporter: fileLoc uses the internally resampled 40 Msps coordinate system; the RF export applied it directly against the 20 Msps source.
- Suggestion: END could use the next frame's corrected fileLoc (already implemented) rather than a fixed 800,000-sample PAL frame assumption.

## Hard-data verification (decoder source)
- /home/harry/vhs-decode/vhsdecode/main.py:732-734 and cvbsdecode/main.py:170: "we pass 40 as sample frequency, as any other will be resampled by the loader function" — decode chain is always 40 Msps internally.
- /home/harry/vhs-decode/lddecode/core.py:4518: `"fileLoc": int(np.floor(f.readloc))` — readloc is a block-aligned offset in the demodulated (40 Msps) data; blockcut = 1024 (core.py:320).
- Math check: fileLoc 9694156800 in the 40 Msps domain = 242.35 s; x (20e6/40e6) → 4847078400 RF samples = the reporter's verified position. Munday (40 Msps RF) scale = 1.0, which is why it worked.

## Fix (branch feature/rf-segment-export)
- src/tbc-analyse/mainwindow.cpp runRfSegmentExport(): new decode-domain → RF scaling after the frame-range resolution:
  `decodeDomainRateHz = 40000000.0; fileLocToRfScale = probe.realRateHz / decodeDomainRateHz; startRfLoc/endRfLoc = qRound64(fileLoc * scale)`.
  All downstream uses (range preview, ms padding conversion stays in RF domain, end clamp, FLAC-Chop GUI hand-off IN/OUT, background chop start/length) now use the scaled positions.
- Interactive padding dialog shows the scaling explicitly: "Metadata positions are scaled from the decoder's 40 Msps internal rate to the capture rate (20000000 Hz, x0.5000)." so a wrong assumption is visible to the user.
- Doc comments updated: tbcsource.cpp getFieldFileLoc + tbcsource.h + mainwindow.h RF-export comment now state fileLoc is in the decode chain's fixed 40 Msps internal domain, not the capture rate.
- ci/check_ci_contracts.py: TBC_ANALYSE_RF_EXPORT_REQUIRED_SNIPPETS comment corrected (the old comment claimed --units samples is "the same units as vhs-decode metadata fileLoc" — the exact wrong assumption behind this bug) and new required snippet `decodeDomainRateHz` pins the scaling against regression.

## Commands run
```
gh issue view 29 --repo harrypm/tbc-tools --json title,body,comments
nix develop -c ninja -C /home/harry/tbc-tools/build          # rebuild OK
python3 ci/check_ci_contracts.py                             # "CI contract checks passed."
nix develop -c ctest --test-dir /home/harry/tbc-tools/build --output-on-failure   # 100% passed, 30/30
git commit + push (this commit)
```

## Status
- Local: build clean, contracts pass, ctest 30/30.
- Pushed to feature/rf-segment-export; CI (full release-pipeline Tests workflow) monitored by agent; verdict pending.
- Real-world re-test by the issue reporter on their 20 Msps capture still required (user rule: GUI/real-data confirmation).

## Not changed (observations only)
- Default padding remains 500 ms before/after (reporter used 12 ms as their minimum; defaults are user-adjustable and persisted).
- END already uses the next frame's first-field fileLoc (reporter's suggestion), with last-frame extrapolation + next-field fallbacks.
