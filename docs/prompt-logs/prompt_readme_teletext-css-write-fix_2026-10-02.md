# Prompt readme — teletext CSS write-permission fix (2026-10-02)

## User prompt(s)
- "There should be a note to also update the vendored version of teletext decoding"
- (Prior context, from earlier session) "lets fix teletext decoding as the decoding option checkbox under process vbi does not do anything in terms of actually running the teletext data extraction..."

## Context
Teletext export ran fine in the nix develop build environment but failed in the installed nix profile
environment with:
`Teletext export failed (non-fatal, VBI metadata preserved): Could not write teletext CSS after font embedding: <outdir>/teletext-noscanlines.css`

## Root cause (verified with hard data)
- Installed vendor runtime: `/nix/store/6s4ndnpaff85lgxjggw4jmiy1jgyycpn-tbc-tools-3.2.9/bin/vendor/vhs-teletext/misc/`
  → files are mode `-r--r--r--` (root-owned, read-only Nix store).
- `QFile::copy` propagates the source mode to the copy in the user's output dir
  → copied CSS/TTFs landed as `-r--r--r-- harry harry` (confirmed in `/tmp/tt-userenv-installed/tt_html/`).
- `embedTeletextFontsInCss()` then fails to reopen `teletext-noscanlines.css` for writing.
- Build-tree vendor copies are `-rw-rw-r--`, so the build environment never reproduced it
  (embedded CSS there = 277203 bytes vs 1226 source, written successfully).

## Reproduction (installed 3.2.9 binary, NTSC issue176 fixture)
```
rm -rf /tmp/tt-userenv-installed && mkdir -p /tmp/tt-userenv-installed && \
cp test-data/ntsc/issue176.tbc test-data/ntsc/issue176.tbc.db /tmp/tt-userenv-installed/ && \
TELETEXT_FORCE_CPU=1 TELETEXT_DECONV_TIMEOUT_SEC=120 ~/.nix-profile/bin/tbc-process-vbi \
  --nobackup --threads 1 --input-metadata /tmp/tt-userenv-installed/issue176.tbc.db \
  --teletext --teletext-html-dir /tmp/tt-userenv-installed/tt_html \
  --teletext-tape-format vhs --teletext-min-duplicates 1 /tmp/tt-userenv-installed/issue176.tbc
```
Result: exact failure reproduced; output-dir copies were read-only `-r--r--r--`.

## Fix (src/tbc-process-vbi/teletextintegration.cpp)
1. `copyFileReplacing()`: after a successful `QFile::copy`, restore owner write permission
   (`QFile::WriteOwner | QFile::WriteUser`) on the copied file; warn (non-fatal) if it fails.
2. `embedTeletextFontsInCss()`: before reopening the CSS for writing, make it writable if it
   is not (covers stale read-only copies made by older releases).

## Verification (after fix, build binary, vendor sources chmod 444 to simulate the Nix store)
- Same command with `build/bin/tbc-process-vbi` and `chmod 444 build/bin/vendor/vhs-teletext/misc/*`:
  - No CSS error; run completes with `Teletext export finished but generated no HTML pages`
    (expected — issue176 fixture carries no teletext data).
  - `teletext-noscanlines.css` = 277203 bytes (fonts embedded), mode `-rw-rw-r--` (writable).
- Build-tree vendor perms restored to 664 afterwards.
- `nix develop -c python3 ci/check_ci_contracts.py` → "CI contract checks passed."
- `ctest --test-dir build --output-on-failure` → 30/30 passed
  (incl. #21 tbc-process-vbi-teletext-help, #22 tbc-process-vbi-teletext-runtime).

## NOTE (user-requested follow-up): update the vendored teletext decoding version
- Vendored runtime: `src/tbc-process-vbi/vendor/vhs-teletext` (upstream: https://github.com/ali1234/vhs-teletext)
- Pinned upstream commit: `f470629a3d5d3b0a577153832505e187beeb7204` (recorded in `VENDOR_INFO.txt`)
- TODO: refresh the vendored copy to a newer upstream commit, re-record the pin in `VENDOR_INFO.txt`,
  and re-verify help/runtime tests plus an end-to-end HTML export on a teletext-bearing capture.

## Remaining validation
- Real-world confirmation with a teletext-bearing capture in the installed (nix profile) environment
  after the next `nix profile install .#` / release build.
