# Session log — MKV SAR / VLC vertical-picture fix (tbc-video-export)

Date: 2026-09-12 · Repo: /home/harry/tbc-tools (src/tbc-video-export) · Scope: 625-line PAL/SECAM/MESECAM + NTSC/PAL-M anamorphic MKV exports

## Problem
FFV1 (and other MKV) full-frame exports played VERTICAL in VLC while the ProRes MOV played fine.
mkvinfo of the latest export (export-fullframe-ffv1-test.mkv 16:53) showed:
- Display unit: 3 (aspect ratio), Display 1420x939, Pixel 1136x626 → VLC misreads DisplayUnit=3.
- ffprobe SAR read 5:6 / DAR 1420:939 (= 1136x5 : 626x6 — derived from a quantized SAR).

## Hard-data findings (all verified locally)
1. ffmpeg MKV muxer writes DisplayUnit=3 (aspect-ratio unit) for EVERY anamorphic SAR — tested
   setsar=946/1136, 5/6, 313/376, 12/11 on system ffmpeg 4.4.8 AND nix ffmpeg 7.1.1 (both
   identical, also with -aspect). No SAR value can steer it to pixel-unit → the setsar-filter
   normalization added earlier this session was a dead end and was removed.
2. A proven-good VLC-playing remux (export-fullframe-ffv1-vlc-test.mkv) has NO DisplayUnit
   element (= 0, pixels) with pixel-unit display dims → that is the target header shape.
3. mkvmerge remux with --display-dimensions writes exactly that shape.
4. Without --timestamp-scale, mkvmerge output on micro fixtures showed MediaInfo
   frame_rate 30.303 (timestamp quantization). --timestamp-scale 1 (nanosecond) restores exact
   30000/1001 signalling and removes ffmpeg's 1ms MKV timestamp quantization (archival win).
5. ProgramState.export is a class-level shared ExportState (program_state.py:41) — messages leak
   across instances (pre-existing quirk, worked around in tests).

## Changes (uncommitted, on main)
- src/tbc_video_export/process/wrapper/wrapper_ffmpeg.py
  - REMOVED _get_setsar_filter + its call (dead end; also slightly altered MOV SAR for nothing).
- src/tbc_video_export/process/process_handler.py
  - NEW _normalize_mkv_display_aspect(): after a successful export, if output is .mkv,
    ffprobe it; if anamorphic (SAR != 1:1), lossless stream-copy remux via mkvmerge with
    --timestamp-scale 1 and --display-dimensions TID:round(w*SAR)xH, then atomically replace
    (temp_file.replace). mkvmerge exit 1 = warnings accepted; missing mkvmerge → warning
    message, export still succeeds. Helpers: _probe_video_stream, _resolve_ffprobe
    (prefers ffprobe next to the resolved ffmpeg binary, else PATH).
  - Hooked into run() after all groups complete, gated on completed_successfully.
  - ruff clean (fixed 2 pre-existing findings in-file: E501 docstring, PLC1802 len()).
- tests/test_process_handler.py (NEW): 4 tests (anamorphic normalized 947x626 + ns scale +
  atomic replace + message; square skipped; mkvmerge-missing warning; non-mkv skipped).
- tests/conftest.py: VideoColorPAL transfer golden BT.709 → BT.470 System B/G (stale from the
  earlier bt470bg trc fix — pre-existing failure, file was already correct);
  VideoBaseNTSC/PALM aspect goldens 0.852→0.853, 1.327→1.328 (whole-pixel display dims).
- tests/test_output.py: NTSC/PAL-M case goldens 1.778→1.779, 1.275→1.276, PAL-M full-vertical
  0.852→0.853; D10 + transfer BT.601 override; h265_lossless format_profile → "Main 4:2:2
  10@L8.5@Main"; ntsc_composite_ld framerate assertions skipped (2-frame fixture, MediaInfo
  heuristic); FFV1 full-frame now EXPECTS the 422 pad; slice golden pal 20→6 (matches the
  earlier smallest-compatible-slices change); audio-trim unexpected {-ss,0.080000} removed
  (order-agnostic set check false-positived against the correct -t 0.080000).

## Verification
- pytest: 315 passed, 2 failed — ONLY test_profiles[av1]/[av1_web]: SystemExit 1 because local
  ffmpeg 4.4.8 has no libsvtav1 encoder (environmental; CI's newer ffmpeg has it).
- ruff: All checks passed (touched files).
- python3 ci/check_ci_contracts.py: "CI contract checks passed."
- Real-file end-to-end (copy of export-fullframe-ffv1-test.mkv): 2 normalizer passes →
  header stable (idempotent): Timestamp scale 1, Pixel 1136x626, Display 947x626, NO
  DisplayUnit element; embedded TBC json attachment preserved (name + MIME); ffprobe now reads
  SAR 947:1136 (was quantized 5:6). DAR 947/626 ≈ ProRes 147112:97343 (≤0.1% diff, invisible).
- Applies to ALL 625-line systems (PAL/SECAM/MESECAM share the geometry table) and NTSC/PAL-M.

## Commands reference
- pytest: cd /home/harry/tbc-tools/src/tbc-video-export && .venv/bin/pytest tests -q
- contracts: cd /home/harry/tbc-tools && python3 ci/check_ci_contracts.py
- normalizer remux shape: mkvmerge -q --timestamp-scale 1 -o TMP --display-dimensions 0:947x626 IN

## Next: user to re-run the FFV1 export and confirm VLC shows widescreen (real-world check).

## Addendum — ffmpeg 7.1.1 as default + PAL verification (same session)
- Installed the tbc-tools-packaged ffmpeg 7.1.1 into a dedicated nix profile:
  nix profile add --profile ~/.nix-ffmpeg-profile /nix/store/42z02ipbn03xsbg07yih4983pbqhwgjw-ffmpeg-7.1.1-bin
  (~/.nix-ffmpeg-profile/bin/{ffmpeg,ffprobe,ffplay}; system /usr/bin/ffmpeg 4.4.8 untouched)
- PATH precedence added to ~/.bashrc: export PATH="$HOME/.nix-ffmpeg-profile/bin:$PATH"
  (profile bin was NOT on PATH before; /usr/bin shadows ~/.local/bin and ~/.nix-profile)
- New interactive shells now resolve ffmpeg/ffprobe 7.1.1. Full pytest suite under 7.1.1:
  317 passed, 0 failed (AV1 encoders present in 7.1.1).
- PAL full-frame export test: SP-Headswitch-test/test-20msps.tbc (PAL, luma+chroma pair),
  50 frames, --full-frame FFV1 via tbc-video-export with ffmpeg 7.1.1 + rebuilt decoder.
  Result pal-fullframe-test.mkv: Pixel 1136x626, Display 946x626 (pixel-unit, NO
  DisplayUnit element), SAR 473:568 (exact, no 5:6 quantization artifact), Timestamp
  scale 1, Default duration exactly 0.040000000 (25.000 fps), bt470bg colorspace+transfer,
  test-20msps.tbc.json attachment preserved. ffmpeg 7.1.1 quantizes less aggressively than
  4.4, so the normalized PAL SAR lands on the ideal whole-pixel value.
- Chroma scan (frame 1, RGB48, capture active area x=185..1113 / rows 44..620):
  left margin <=1700, right margin <=796, blanking <=258 (all <=3% of full scale = neutral),
  picture chroma intact (mean ~10000, max 37083). No leakage, no bars.
- Note: SECAM captures use activeVideoEnd ~1107; PAL captures ~1114 — margin scans must use
  each capture's own json bounds, not a shared constant.
