# Prompt Readme — SECAM export flicker fix (continuation)

Started: 2026-09-13T01:52Z
Repo: /home/harry/tbc-tools (branch main, HEAD 0715160)
Continuing from session: prompt_readme_sar-mkv-vlc-fix_2026-09-12.md + uncommitted
working-tree reverts in development-logs/prompt_readme.md (2026-09-13 entries).

## Carried-over state (from last session, NOT assumed — re-verified below)
- Uncommitted changes (git diff --stat):
  - development-logs/prompt_readme.md (log only)
  - src/ld-chroma-decoder/decoderpool.cpp  (restored applyFullFrameDecodeBounds)
  - src/ld-chroma-decoder/outputwriter.cpp (removed chromaInActiveArea gate)
  - src/ld-chroma-decoder/secamdecoder.cpp (reverted fillChannel V-interval re-zero)
- Diagnosis recorded 2026-09-13 13:51:
  - SECAM flicker is CHROMA-SPECIFIC (Y stable, Cb/Cr sign-mean erratic).
  - Frames 15-18 etienne colorbars: Y mean 32180/32201/32218/32197 (stable);
    Cb sign-mean +14678/-619/+14671/+7047; Cr +14076/+346/+14245/+7311.
  - Pattern erratic (not clean 2-frame parity flip) -> unstable per-field parity
    detection (drOnEven majority vote over rest carriers, secamdecoder.cpp:302-309).
  - Pre-existing in parent e668bec; not caused by bbf977b reverts.
- Test data: /home/harry/Desktop/SECAM/snippets/secam_etienne_colorbars_50f.tbc (+.json)
  and mesecam_tape275_testcard_50f.tbc (+_chroma.tbc +.json).

## Goal
Fix the SECAM chroma flicker (unstable per-field Dr/Db parity / line-red detection)
so SECAM exports are stable. Do NOT regress the full-frame chroma extent fix or
hybrid/export mode. Verify against hard data (ffmpeg stats over decoded frames),
never assume.

## Log
(append per step)
