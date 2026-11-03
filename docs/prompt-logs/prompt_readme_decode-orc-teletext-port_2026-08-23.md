# Prompt README — Port decode-orc teletext stack into tbc-tools

Date: 2026-08-23
Branch: main
Base commit: e852a12e (origin/main)

## Request (input)
> Read CEA-516_S-2013 spec; pull in the fonts/graphics libraries decode-orc
> added so teletext renders properly for the new (NABTS/NAPLPS) formats.
> Then: pull in ALL upgrades decode-orc made on top of vhs-teletext's
> foundation. Unified port (WST upgrade + NABTS/NAPLPS), viewer-first
> (GUI dialog first). vhs-teletext Python kept as fallback; native C++ WST
> decoder becomes primary.

## Spec read
`C:\Users\Harry\Desktop\11\CEA-516_S-2013\CEA-516_S-2013.md` — NABTS
(ITU-R BT.653 System C). §6.1: presentation records use NAPLPS (ANSI
X3.110-1983 / CSA T500 / ITU-T T.101 Data Syntax III) — a vector drawing
language, NOT the WST character grid. NABTS vs WST told apart by framing
code 0xE7 vs 0xE4 (same clock/VBI lines).

## decode-orc source audited (GPLv3, license-compatible)
Reference clone: `C:\Users\Harry\decode-orc-ref` (tag v2.7.2, fef0115a).
- `orc/plugins/stages/nabts_sink` (21 files): NABTS transport + NAPLPS
  graphics core (naplps_render_grid/state/code_env/interpreter/pdi/raster/
  lint/lint_repair/font) + viewer.
- `orc/plugins/stages/teletext_sink`: native C++ WST upgrade over vendored
  vhs-teletext Python (teletext_page_decoder/row_squasher/recovery_stats/
  slicer + block scanner + frame slicer + catalogue + UTF-8 SRT export).
- `orc/plugins/stages/common/vbi-services`: shared model (nabts_page,
  teletext_page_decoder, vbi_analysis_results).
- SDK coupling to sever: orc::CatalogueDataset/DisplayList/ViewToggle +
  plugin SDK hosting.

## Test data (`C:\Users\Harry\Desktop\Teletext-Works`)
- 6 NABTS `.t33` (CBS ExtraVision, verified 33-byte framing) + Record-*.png
  reference renders (pixel ground truth, keyed channel+addr+version).
- 5 WST `.t34` (TBS Electra, verified 34-byte) + 13 `.t42`. No `.t35`.
- Raw `.tbc` NABTS promised for the VBI-scan phase.

## Plan
Created plan a421e8aa (unified, viewer-first). 6 phases:
1. Foundation + fonts (me) — shared snapshot model + font tables.
2. Viewer GUI first (me) — WST viewer upgrade + NABTS viewer shell.
3. Backend decoders (3 remote children, parallel) — wst-decoder /
   nabts-graphics / nabts-transport.
4. Integrate + validate vs reference PNGs (me).
5. Build + CI + PR (me).
6. VBI scanning layer (deferred until raw .tbc arrives).

## Progress this session — PHASE 1 (foundation + fonts): DONE

Created `src/library/tbc/vbi/` (new shared VBI services seam) — 7 files:

NABTS/NAPLPS graphics foundation (the fonts/graphics libraries decode-orc added):
- `nabts_page.h` — ported NAPLPS page/display-list model
  (NabtsPageSnapshot, NabtsPrimitive, NabtsColour, colour modes, repertoires,
  DRCS, texture masks, diagnostics). orc:: -> tbc::vbi::.
- `nabts_page.cpp` — ported default colour map (X3.110 §5.3.2.5.2 hue
  algorithm, verified vs T.101 Table II-3) + primary/supplementary/mosaic
  repertoire mappings + UTF-8 + page-text extraction.
- `naplps_render_grid.h` — ported NaplpsRenderMode (256x200/512x400/768x600/
  vector) + NaplpsRenderGrid + mode name/from-name helpers.
- `naplps_font.h` — ported NaplpsFontFace + face-count/face/pattern lookups
  + naplps_font_face_for_field() inline selector.
- `naplps_font.cpp` — VENDORED generated table (1627 lines) from
  decode-orc; public-domain X11 misc-fixed BDF (6x10/9x15/10x20). Re-namespaced
  orc:: -> tbc::vbi::; table byte-identical to upstream (regenerate from same
  BDF release to verify). Only residual "orc" string is the provenance note.

WST Level 1 snapshot model (the decode/render seam the WST viewer/decoder
share; decoder class itself stays with the wst-decoder track):
- `teletext_page.h` — extracted TeletextPageSnapshot (25x40), TeletextPageCell
  (per-cell mosaic/held/double-height/flash/conceal/boxed/parity, per-cell G0
  with ESC toggling + g0_set_uncertain), TeletextColour, TeletextG0Set
  (Latin + 3 Cyrillic), TeletextNationalOption, TeletextG0Designation,
  TeletextSubtitleCue + free-function decls (parity, g0->unicode/utf8).
- `teletext_page.cpp` — self-contained G0 tables (7 national-option sub-sets,
  3 Cyrillic 96-glyph tables) + the 6 model free functions. No decoder-only
  constants (MRAG/extension/spacing-attribute codes stay with the decoder).

## Commands run (this session)
- git clone --depth 1 decode-orc -> C:\Users\Harry\decode-orc-ref (reference, tag v2.7.2)
- read_files: decode-orc nabts_page.h/.cpp, naplps_render_grid.h, naplps_font.h,
  teletext_page_decoder.h/.cpp (full, from disk)
- create_file x6 (nabts_page.{h,cpp}, naplps_render_grid.h, naplps_font.h,
  teletext_page.{h,cpp})
- Copy-Item + .Replace() (vendor naplps_font.cpp: namespace + provenance)
- edit_files: src/library/CMakeLists.txt (add 3 sources + tbc/vbi include dir)
- cmake --build build --config Release --target tbc-library  -> CLEAN BUILD
  (nabts_page.cpp + naplps_font.cpp + teletext_page.cpp compiled;
  tbc-library.lib linked; built twice — once after NABTS files, once after WST)
- python ci/check_ci_contracts.py  -> "CI contract checks passed."

## Verified state
- tbc-library builds clean under MSVC (VS 17 2022, vcpkg, Qt6).
- CI contracts pass.
- No orc SDK types in the new files (pure data + standalone functions).
- Namespace is tbc::vbi:: throughout; include guards renamed TBC_VBI_*.

## Status
Phase 1 complete and build-verified (7 files in src/library/tbc/vbi/:
NABTS/NAPLPS model + fonts, WST snapshot model + G0 tables).
NOT yet committed (will bundle with later phases before commit/push).
Remaining: phase 2 viewer GUI -> phase 3 backend children (wst-decoder /
nabts-graphics / nabts-transport) -> phase 4 validate vs reference PNGs ->
phase 5 PR. Phase 6 (VBI scan) deferred until raw .tbc arrives.

NOTE on orchestration: the plan designed 3 remote child agents for the
backend decoder ports, but the decode-orc source lives in a LOCAL reference
clone (C:\Users\Harry\decode-orc-ref) that remote children cannot see. To
use remote children, the decode-orc source must first be vendored into the
tbc-tools tree (a gitignored vendor/ dir) so children can read it; otherwise
the port continues locally.

## Restore point
Changes are git-tracked + untracked new files. Not yet committed; git is the
rollback. The decode-orc reference clone at C:\Users\Harry\decode-orc-ref is
the source of truth for the port.
