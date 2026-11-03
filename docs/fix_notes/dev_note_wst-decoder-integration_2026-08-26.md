# WST Native Teletext Decoder Integration

## Issue
The Teletext Viewer's `.t34`/`.t42` (WST) path decoded on-device via a hand-rolled, simplistic fallback that ignored the Phase 1 `TeletextPageSnapshot` model — it kept only the lowest subpage per page, did its own Hamming decode, and fell back to the vendored `vhs-teletext` Python (`Service.from_packets` → `to_html`) as the primary engine. The NABTS `.t33` path was already native and working; WST was the remaining decoding gap.

## Solution
Added a stateful `tbc::vbi::TeletextDecoder` that turns a T42 packet stream into `TeletextPageSnapshot` values, and wired it in as the **primary** WST path (Python is now the fallback only).

### New files
- `src/library/tbc/vbi/teletext_decoder.h` / `.cpp` — `TeletextDecoder` class:
  - MRAG routing (magazine 0 → displayed as 8), row 0 header / rows 1-24 display / rows 25-31 control.
  - Page number, 13-bit subcode, and control bits C4-C14 (ETSI EN 300 706 §9.3.1).
  - Row squashing across repeated transmissions with `row_copies` confidence tracking.
  - Full Level 1 spacing-attribute resolution (§12.2 Table 26): foreground/background colour, mosaic/contiguous/separated, double height/width, flash, conceal, boxed (double-code 0/A-0/B semantics), hold/release mosaic, ESC G0-set toggle.
  - Reuses Phase 1 `teletext_hamming84_decode` and `teletext_g0_to_unicode` — no duplicated coding/charset logic.
  - Matches the vendored `vhs-teletext/parser.py` semantics exactly (mosaic PUA 0xEE00/0xEDE0, set-after/set-at timing).

### Modified files
- `src/library/CMakeLists.txt` — added `tbc/vbi/teletext_decoder.cpp` to `tbc-library`.
- `src/ld-analyse/teletextviewerdialog.cpp`:
  - `writeTeletextHtmlFromT42Native()` now drives `TeletextDecoder` and renders `TeletextPageSnapshot` → HTML via new snapshot converters (`teletextSnapshotCellText`, `teletextSnapshotCellCssClass`, `teletextSnapshotRowToHtml`, `teletextSnapshotToHtml`).
  - `convertTeletextStreamToHtmlDirectory()` WST branch: native decode first, `vhs-teletext` Python fallback only if native fails.
  - Removed ~305 lines of dead old-WST helpers (`bitCount`, `decodeHamming8Nibble`, `decodeHamming16Value`, `teletextDefaultG0Char`, `TeletextRowParseState`, `teletextCharacterForCode`, `makeTeletextCell`, `parseTeletextDisplayRow`, `teletextCellCssClass`, `teletextRowToHtml`, `rowContainsDoubleHeightControl`, `DecodedTeletextSubpage`, `buildTeletextHtmlFromSubpage`). Kept `htmlEscapeTeletextCharacter` and `teletextPageKey` (still used by the snapshot converters).

## Two Real-Data Bugs Found and Fixed (validated against TBS Electra `.t34`)

### 1. Hamming-false-header flood
Uncorrectable MRAG bytes returned `0` from the fallback, so 87% of packets were misread as row-0 headers (1,711,767 bogus pages on one file). Fixed: `hamming84()` now returns `0xF` on failure (matching `vhs-teletext`'s `hamming8_decode`), so noise routes to a control row (≥25) and is dropped. Headers dropped 1,711,767 → 39,447.

### 2. Sparse pages from per-header snapshots
A carousel transmits one row per field, so each header-cycle snapshot carried only 1-2 rows. Fixed: group recurring snapshots by `(magazine, page, subcode)` and merge rows by highest `row_copies` — reassembling complete carousel pages the way `vhs-teletext`'s `Service` accumulates a page over many fields. One page per `(magazine, page)` at the lowest subcode is rendered; pages with fewer than 2 display rows are dropped as noise.

## Validation
Standalone Qt-free harness (built with `cl /utf-8`, run against `TBS_1989-01-09_Electra.t34`):
- 216 complete pages reassembled (18-24 display rows each), 10 noise pages filtered.
- Content matches TBS Electra exactly: P100-P152 headers, P200-P299 stock tables ("SYM PRC VOL DELAYED 20I"), P600s weather ("TEMPO MIN 1/2/8"), P400s "CHARACTER TEST", P800s "BLACK A B C D E F" colour test.
- `cmake --build build --config Release --target ld-analyse` clean (no errors/warnings).
- `python ci/check_ci_contracts.py` passes.

## Key Takeaway
The `.t33` (NABTS) and `.t34`/`.t42` (WST) paths are now both native on-device decoders producing the Phase 1 snapshot models. The `vhs-teletext` Python runtime remains only as a fallback for WST material the native Level 1 decoder does not yet cover, and as the source of the CSS/font assets.
