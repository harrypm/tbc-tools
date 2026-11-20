# Prompt Log — tbc-analyse Export tab Main/Proxy readout fix — 2026-11-19

Session: fix the Export tab's status readout (the `statusLabel` above the
process-stats tables showing `Main: …` / `Proxy: …`). User report: it stopped
showing the current info summary (current encode state, duration, completion
estimate) and instead showed "field info and tools info" lines. All work on
tbc-tools `main` @ `ece67be` (segment-editor revert; main keeps live playback,
audio monitor, export speed, completion time and dropout changes).

## Symptom

During an export run, the readout above the Main/Proxy process-stats windows
showed lines from tbc-video-export's log panel (e.g. `[HH:MM:SS]
[tbc-chroma-decoder] Info: Processing field N of M`, tool start-up messages)
or state-block lines (`Total Fields: … / Frame Type: …`) instead of the
encode-state footer (`Size: … Duration: … Bitrate: … Remaining: …
Finishes: …`) it used to show.

## Root cause (verified against code + git history)

Introduced by `54d06e3` ("Faster reads from shared disks, bounded export
output, completion time", 2026-11-17), which added an estimate special-case
to `ExportDialog::processOutputLine` (`src/tbc-analyse/exportdialog.cpp`):

1. tbc-video-export redraws its whole screen every 0.1 s
   (`process/progress_handler.py`: move-home + erase-screen, state block,
   per-process progress lines, message panel, then the
   `Size/Duration/Bitrate/Remaining/Finishes` footer, then the trailing
   `Press Ctrl+C…` fragment with no newline). After ANSI stripping in
   `consumeProcessOutputChunk`, the footer is the last *complete* line of
   each redraw, so it is what `lastStatus` selects — the readout.
2. The estimate branch matched the footer's `Remaining: H:MM:SS Finishes: …`
   text, called `appendStatus("Export: … remaining, finishes at …")`
   (replacing the whole readout, bypassing the per-feed Main/Proxy lines)
   and returned **without setting `*lastStatus`**.
3. At the end of the chunk, `consumeProcessOutputChunk` still called
   `appendFeedStatus(lastStatus)` with the last *other* status-worthy line —
   a message-panel line (field/tool info) — which immediately overwrote the
   estimate text.

So from ~5 s into the ffmpeg stage (when the rate estimate first appears in
the footer) the readout degraded to field/tool info. Before the estimate
kicks in (`Remaining: estimating…`) the regex didn't match and the footer
showed correctly — matching the "worked at first, then wrong" behaviour.

Design confirmation: `isTransientProcessTelemetryLine()` deliberately
recognises this footer (`size:` prefix + `duration:` + `bitrate:`) — set
`lastStatus`, but do not log it to the feed log. The estimate branch broke
that contract by consuming the footer before it could become the status.

## Fix

`src/tbc-analyse/exportdialog.cpp` — `ExportDialog::processOutputLine()`:
removed the estimate special-case branch (10 lines) so the footer line flows
into `*lastStatus` like every other status line. Added a comment documenting
that the readout must go through `lastStatus` (never `appendStatus`, which
bypasses and is then overwritten by the per-feed Main/Proxy status lines).
The readout now shows, per feed:

```
Main: Size: 1.23 GB  Duration: 0:01:23s  Bitrate: 4.56Mb/s  Remaining: 0:12:34  Finishes: 22:45
Proxy: —
```

i.e. the current encode state (size/duration/bitrate) plus the completion
estimate (remaining/finishes), in both split (Main/Proxy) and single-feed
modes. Net diff: 5 insertions, 10 deletions, no new helpers.

## Validation

- `nix develop -c ninja -C build tbc-analyse` → builds and links clean
  (Release).
- `python3 ci/check_ci_contracts.py` → "CI contract checks passed."
- Not yet validated (needs a real run): the readout during an actual export
  (GUI confirmation pending — user to verify on next export), and GitHub
  Actions (repo hard rule: build verification is not complete until Actions
  passes; runs on push).

## Commands run

```
git --no-pager log --oneline -15 -- src/tbc-analyse/exportdialog.{cpp,h,ui}
git --no-pager show 54d06e3 -- src/tbc-analyse/exportdialog.cpp
git --no-pager show 54d06e3^:src/tbc-analyse/exportdialog.cpp   # pre-regression processOutputLine
git --no-pager status --short && git --no-pager diff --stat
nix develop -c ninja -C /home/harry/tbc-tools/build tbc-analyse
python3 /home/harry/tbc-tools/ci/check_ci_contracts.py
```

## Follow-up (same session, ~22:06-22:20): "FFmpeg still encoding far too slow, should be 50fps+, shouldn't be single-core"

User question: was the earlier speed improvement work limited to the
segment-editor branch? **No — verified:** `54d06e3` (chunked 64 MB reads for
shared-disk TBC pairs etc.) is on `main` and in the running binaries (the
live tbc-chroma-decoder / tbc-dropout-correct resolve to
`/home/harry/tbc-tools/build/bin/` = current main). `tbc-seg-edit` has exactly
one commit beyond main (`045e964`, segment framing GUI, editsegmentswidget.cpp
only). The revert `ece67be` explicitly kept the export-speed work on main.

What the live export (VHS PAL S-Video Y+C, Tape_01, NTFS 20TB shared disk,
parallel main FFV1 + proxy H.264) actually showed, measured from /proc:

- **ffmpeg resolution gap**: tbc-video-export finds ffmpeg via a plain PATH
  lookup (`file_helper.py _get_tool_path` → `find_binary`); the nix profile
  ships no ffmpeg, so exports launched from a normal shell use Ubuntu's
  `/usr/bin/ffmpeg` **4.4.8**, not the nix devShell's **7.1.1** (verified via
  /proc/<pid>/exe + `-version`).
- ffmpeg 4.4's filter graphs are **single-threaded** (4.4.8 `scale` filter has
  no `threads` option; 7.1.1 does). The main FFV1 ffmpeg chain
  (`mergeplanes→yuv444p16le→scale=lanczos:interl=1→pad→format=yuv422p`) runs on
  ~1.3 cores (measured 123-130% CPU despite `-threads 18 -slices 4`). This is
  the "single core" the user saw.
- Measured throughput (frame counts in the growing output files + FIFO
  rchar/wchar rates): **main FFV1 ≈ 53.4 fps** (meets the 50fps+ target at
  576p — one core of lanczos is just enough); **proxy H.264 ≈ 41 fps** with
  ~11 cores of x264 `-preset slow -crf 18` on noisy VHS (~11 Mb/s output —
  noise is expensive), plus its own single-threaded 4.4 bwdif+lanczos chain.
- 36 cores, load ~20 — no system saturation; disk reads keeping up.

Fix options (not yet implemented):
1. Make exports use the modern ffmpeg: launch tbc-analyse from `nix develop`
   (PATH puts nix ffmpeg 7.1.1 first), or expose ffmpeg from the tbc-tools nix
   package, or have tbc-analyse/tbc-video-export prefer an ffmpeg adjacent to
   the installation. This removes the single-thread filter ceiling (matters
   for 10-bit / 1080i formats; at 576p it just adds headroom).
2. Proxy preset tradeoff (`slow` → `medium` for a proxy file) — user decision;
   x264-slow on grainy VHS is the proxy's actual limiter, not the filter chain.

Also noted: the running tbc-analyse (started 22:04:42) postdates the readout
fix build (21:50:46), so this export exercised the corrected readout.

## Follow-up 2 (2026-11-20 ~01:35): hard-enforce the self-contained ffmpeg policy

User: PATH-resolving Ubuntu's ffmpeg "violates the project rules of using only
self-contained bundled libraries for deployment control" — make the policy
hard enforced.

Deployment audit (verified in the workflows):
- Linux AppImage (x86 job): already bundles ffmpeg/ffprobe/ffplay from the
  flake's pinned `nix build .#ffmpeg` into `AppDir/usr/bin`.
- Linux arm64 tar: bundles into `release/bin`.
- macOS .app: bundles from `.#ffmpeg^bin` into `dist/bin`.
- Windows: bundles the pinned 8.0.1 full build (ffmpeg/ffprobe/ffplay + DLLs)
  into `release\`.
- **nix profile (`packages.default`): shipped NO ffmpeg** — the gap that let
  tbc-video-export's PATH lookup fall through to `/usr/bin/ffmpeg` 4.4.8.
  (`find_binary` in files.py already prefers the script-adjacent binary before
  PATH; the deployment just never put one there.)

Changes (all uncommitted in the working tree):
1. `flake.nix` — `mkTbcTools` gained a `postInstall` that symlinks the
   flake-pinned `pkgs.ffmpeg.bin` ffmpeg/ffprobe/ffplay into `$out/bin` next
   to the tbc-video-export wrapper, so `nix profile install .#` is
   self-contained. wrapQtAppsHook wraps them as launchers backed by
   `ffmpeg-7.1.1-bin` (verified).
2. `AGENTS.md` — new hard rule: "exports must use only bundled, self-contained
   ffmpeg — never a host/system ffmpeg" (documents per-platform bundling +
   the find_binary script-adjacent-before-PATH resolution order).
3. `ci/check_ci_contracts.py` — new `FFMPEG_SELF_CONTAINED_*` contracts:
   flake postInstall symlink lines, files.py's
   `get_runtime_directory().with_name(name)` (adjacency before PATH), both
   Linux jobs' `nix build .#ffmpeg --out-link ffmpeg-result` (count ≥ 2) +
   AppImage/tar copy lines, macOS `.#ffmpeg^bin` + dist/bin copy lines,
   Windows `FFMPEG_URL:` + the ffmpeg/ffprobe/ffplay Copy-Item lines (note:
   the Windows YAML literally contains doubled backslashes — the snippets
   are raw strings matching that form). AGENTS hard-rule title added to
   `AGENTS_HARD_RULE_REQUIRED_SNIPPETS`.

Validation:
- `python3 ci/check_ci_contracts.py` → passes (after fixing the Windows
  snippet escaping — `\b`/`\$` in normal Python strings mangled them; raw
  strings + the literal double-backslash form of the YAML fixed it).
- `nix build .#` → `bin/` now contains ffmpeg/ffprobe/ffplay launchers +
  `.ffmpeg-wrapped → ffmpeg-7.1.1-bin`.
- Resolution proof: `env -i PATH=/usr/bin:/bin` + the deployed wrapper's
  runtime file → `find_binary("ffmpeg")` resolved the bundled
  `<out>/bin/ffmpeg` and it reports **7.1.1** — the system 4.4.8 on PATH
  loses even when it is the only PATH entry.
- Pending: GitHub Actions (repo hard rule — verification is not complete
  until Actions passes; runs on push) and a real export run from the
  updated profile (`nix profile install .#` needs re-running to replace the
  currently-installed profile, which predates the bundling).

## Follow-up 3 (2026-11-20 ~03:30-03:40): export failed to start — stale build tree + shadow copies scrubbed

User's 03:30 export attempt from tbc-analyse produced:
"--overcorrect is not supported by the detected tbc-video-export" (plus
interfield/resolution-mode fallbacks) and "Failed to start export config
generation."

Root causes found (both verified):
1. **The entire `build/` tree was configured 2026-09-22.** Ninja only rebuilt
   what source-file changes trigger, so `build/bin/tbc-analyse` got the 21:50
   readout-fix rebuild, but `build/bin/tbc-video-export` (configure-time
   wrapper) and `build/share/tbc-video-export/src` (configure-time copy of the
   vendored Python the wrapper imports) were two months stale — APP_VERSION
   baked as 3.2.9, missing all November source (completion estimate etc.).
   A full `ninja -C build` was a no-op because nothing it tracked had changed.
2. **Shadow copies of the tools existed outside build/bin**: repo-root
   `AppDir/` (400M manual AppImage staging, its bundled tbc-video-export
   verified to LACK `--overcorrect` in `--help` — matches the 03:30 failure
   signature if launched from there), `squashfs-root/` (400M), `release/`
   (222M, stale tbc-tools-x86_64.AppImage), stale `result` nix out-link, and
   `~/.local/bin/.old-tbc-tools-20260324-002304/`.

User directive: "the only local builds should be in
/home/harry/tbc-tools/build/bin — scrub anything else."

Actions:
- Scrubbed: `AppDir/`, `squashfs-root/`, `release/`, `result` (stale 3.2.9
  out-link), `~/.local/bin/.old-tbc-tools-20260324-002304/`. Kept:
  `~/.local/bin` symlinks (they all point AT build/bin — every shell now
  resolves the one true location first), ld-* pipx scripts (vhs-decode,
  unrelated), appimagetool/linuxdeploy tooling, RESTOREPOINT zips, build-aaa/.
- Clean-reconfigured and fully rebuilt `build/` from main @ fabd80d
  (rm -rf build + cmake + ninja in the devShell). Now:
  `build/bin/tbc-video-export --version` → 3.3.1, `--help` lists the vendored
  options (overcorrect x3), `tbc-analyse --version` → 3.3.1 (readout fix
  included), and `build/share` carries the current source (Remaining:/
  Finishes: present in progress_handler.py).
- Path check after scrub: `which -a tbc-video-export` → ~/.local/bin
  (→build/bin) first, nix profile second. The nix profile still holds the
  old 3.2.9 (its vendored source predates --overcorrect; refresh with
  `nix profile install .#` to get 3.3.1 + the bundled ffmpeg 7.1.1).

CI for the pushed commits (run 44667963387): CI guardrails (contract
checker) PASSED; Linux/Windows build jobs failed with the same
job pattern as the previous main run (44211916999), whose root cause was
the pre-existing ONNX Runtime GPU 1.18.1 tarball 404 breaking
`onnxruntime-gpu-prebuilt` (main was already red before these commits).
Failed-job logs were not yet retrievable while macOS jobs were still
running; full diagnosis pending run completion.

## Follow-up 4 (2026-11-20 ~05:04-05:20): purge legacy ld-* naming from tbc-video-export

User report: 3.3.1 exports "trying to call old ld-xxx tools". Root cause
(verified): `ProcessName.__str__` in the vendored exporter mapped five members
explicitly to tbc-* binaries but `LD_PROCESS_EFM` fell through to
`str(self.name).lower()` = "ld-process-efm", so `--process-efm` resolved the
legacy tool from PATH on any machine with the old ld-decode/pipx tools
installed. The remaining LD_* member names / wrapper_ld_* / parser_ld_*
modules / WrapperLD* / ParserLD* classes were leftovers of the vendored fork
that the binary rename never touched (user: "why is there any LD naming in
here still?" / "everything should be moved hard to tbc-xxxx now").

Changes (all in the vendored tree):
- `enums.py`: `LD_PROCESS_EFM` member removed; `__str__` now maps EVERY member
  explicitly (tbc-export-metadata / tbc-dropout-correct / tbc-chroma-decoder /
  tbc-process-vbi / ffmpeg / none) and raises on unmapped members — a future
  member can never silently derive an ld-* name again.
- The experimental `--process-efm` path is removed entirely (opts group,
  opts fields, tool resolution, process wiring, WrapperLDProcessEFM,
  ParserLDProcessEFM): it could only ever run by accident against a foreign
  legacy binary — tbc-tools EFM handling lives in the efm-decoder-* suite /
  tbc-efm-handler. tbc-analyse never passes --process-efm (verified), so only
  direct CLI use was affected; argparse now rejects the flag.
- Full internal rename: 8 modules git-mv'd (wrapper_ld_* → wrapper_tbc_*,
  parser_ld_* → parser_tbc_*), classes WrapperLD*/ParserLD* → WrapperTBC*/
  ParserTBC*, enum members LD_* → TBC_*, docstrings referencing legacy binary
  names updated. LaserDisc domain terms (CHROMA_COMBINED_LD, is_combined_ld)
  and mentions of the upstream ld-decode software intentionally unaffected
  (word-bounded sed — `FFMPEG_USE_OLD_MERGEPLANES` contains the substring
  `LD_` and must not be touched).
- Hard enforcement: new `TBC_TOOL_NAMING_*` contracts in
  `ci/check_ci_contracts.py` — required enums.py mappings + raise message, the
  8 renamed modules as required files, and forbidden snippets (LD_* member
  names, wrapper_ld_/parser_ld_, WrapperLD/ParserLD, quoted "ld-*" exe names)
  scanned across every .py in the vendored tree. New AGENTS.md hard rule.

Verification: contract checker passes; `python3 -m compileall` clean;
imports of WrapperGroup/ParserFactory/process_handler OK; every ProcessName
str asserted tbc-*/ffmpeg; build/share rebuilt (cmake+ninja);
`build/bin/tbc-video-export --version` → 3.3.1, `--help` has no process-efm,
`--process-efm` rejected by argparse; dry-run export on
test-data/pal/kagemusha-leadout-cbar invokes only tbc-dropout-correct /
tbc-chroma-decoder / ffmpeg — zero `ld-` tool names (word-boundary checked).

Remaining ld-named items flagged for a decision (NOT changed here):
- The still-ld-named tbc-tools binaries: `ld-discmap`, `ld-disc-stacker`,
  `ld-lds-converter` (tbc-analyse invokes ld-lds-converter at mainwindow.cpp
  ~5555). Renaming these is a cross-cutting change (binaries, CI, docs, user
  scripts).
- tbcmetadata.cpp writes "ld-decode" as the capture decoder name (existing
  TODO to make it configurable).

## Follow-up 5 (2026-11-20 ~05:51): EFM tools SIGFPE on 0-byte input — guarded

While proving the EFM suite still worked after the ld-naming purge,
efm-decoder-d24 crashed with SIGFPE on a 0-byte input. Root cause: the four
section readers measure one serialized section (`F2Section`/`Data24Section`
dummy) and compute `totalSize / sectionSize`; an empty file leaves
sectionSize = 0 → 0/0 integer division → SIGFPE.

Fix: each of the four readers (efm-decoder-d24 reader_f2section,
efm-decoder-audio reader_data24section, efm-decoder-data
reader_data24section, efm-stacker-f2 reader_f2section) now fails cleanly
when sectionSize <= 0 — qCritical "Input file ... is empty or contains no
section data", stream deleted, file closed, return false. efm-decoder-f2's
raw byte reader has no such division and is unaffected.

Verified: d24/audio/data exit 1 with the guard message on 0-byte inputs;
stacker-f2 exits 255 with the guard message (two distinct empty files — its
CLI rejects duplicate input names); non-empty garbage does not trigger the
guard.

A/B notes (pre-existing, NOT changed here):
- efm-decoder-d24 Aborts (SIGABRT, exit 134) on non-empty garbage input —
  the old nix-profile 3.2.9 binary aborts identically, so this predates the
  rebuild; a separate data-validation robustness issue.
- efm-decoder-f2 exits 0 even when decoding fails ("Decoding FAILED" on the
  repo EFM fixtures, which fail F2 lead-in detection in both 3.2.9 and
  3.3.1 — fixture-data characteristic, not tool breakage).

## Follow-up 6 (2026-11-20 ~06:00-06:20): Microsoft purged the onnxruntime GitHub releases — ORT moved to nuget.org

Monitoring run 44684351107 per user request. Verified root causes of the 5
failed jobs (the macOS jobs were still running):
- Linux parity + Linux x64: the flake's `onnxruntime-linux-x64-gpu-1.18.1.tgz`
  fetch 404s — Microsoft deleted the ENTIRE onnxruntime GitHub Releases
  section (`releases/tags/v1.18.1` AND `releases/tags/v1.23.2` both 404; even
  the releases list endpoint 404s). The macOS flake branch (osx 1.23.2 tgz)
  and both Windows ORT zip downloads were equally dead.
- Windows x64/arm64: ORT zip download fails, then the header/lib validation
  throws.
- Linux arm64 (tar job): DIFFERENT cause — `cp: 'ffmpeg-result-bin/bin/ffmpeg'
  and 'release/bin/ffmpeg' are the same file` — a collision introduced by the
  ffmpeg self-contained commit (fabd80d): the result/bin copy loops pre-stage
  ffmpeg/ffprobe/ffplay as Nix-store symlinks (from the flake's bundled
  launchers), and the explicit `cp -a` of the same store path onto that
  symlink errors. The x86 AppImage job would hit the same once ORT was fixed.

User direction: these dependency issues should have been pre-fixed by the
cache and self-contained policies — bring ORT under the policy umbrella.

Fix (all under contract):
- flake.nix: both ORT branches now fetch immutable nuget.org packages
  (versions are never deleted, unlike GitHub releases):
  `Microsoft.ML.OnnxRuntime.Gpu.Linux` 1.18.1 (Linux x64 GPU; installPhase
  arranges the flat public headers + native .so set into include/ + lib/ and
  adds the SONAME name libonnxruntime.so.1.18.1 — the nupkg ships only the
  unversioned file) and the fat `Microsoft.ML.OnnxRuntime` 1.23.2 (macOS per-
  arch thin dylibs + headers). nupkgs need `sourceRoot = "."` (multi-dir
  unpack) + unzip in nativeBuildInputs + a .zip `name` for fetchurl.
- build_windows_tools.yml: the ORT step fetches the nupkg and arranges it
  into the include/ + lib/ tree ONNXRUNTIME_ROOT expects — x64 uses
  `Microsoft.ML.OnnxRuntime.Gpu.Windows` 1.18.1 (headers + onnxruntime.lib +
  DLLs + providers), arm64 uses the fat CPU package's runtimes/win-arm64
  natives (arm64 is CPU-only ORT per the CUDA policy).
- cuda-plugin-package.sh: both ORT provider fetches (Linux .so + Windows
  .dll) pull from the Gpu nupkgs instead of the dead release URLs.
- build_linux_tools.yml (BOTH jobs): the explicit ffmpeg staging now uses
  `cp -a --remove-destination` so it replaces the pre-staged Nix-store
  symlinks with the real binaries.
- New hard rule (AGENTS.md) + `ORT_SOURCE_*` contracts: GitHub-release ORT
  URLs are FORBIDDEN across flake/workflows/scripts; the nuget URLs + the
  SONAME symlink + the win-arm64 natives are required snippets. The
  FFMPEG_SELF_CONTAINED_LINUX_* snippets were updated to the
  --remove-destination forms (pins the collision fix).

Verification: contract checker passes; full `nix build .#` with the nuget ORT
succeeds — ORT package has include/ (flat public headers) + lib/ (so set +
SONAME symlink), `ldd` resolves libonnxruntime.so.1.18.1 from the new store
path, tbc-chroma-decoder runs. Windows/macOS paths verified by package
content inspection (the nupkgs carry everything the steps require); CI is the
final proof. Local build/ tree needs a cmake re-run to pick up the new
ONNXRUNTIME_ROOT.

## Key locations (for future reference)

- `ExportDialog::processOutputLine` / `consumeProcessOutputChunk` /
  `appendFeedStatus` / `isTransientProcessTelemetryLine` —
  `src/tbc-analyse/exportdialog.cpp` (~3905-3980, ~6007-6032).
- Footer producer: `_get_output_file_line` + `_get_completion_estimate` —
  `src/tbc-video-export/src/tbc_video_export/process/progress_handler.py`.
- ffmpeg resolution: `src/tbc-video-export/src/tbc_video_export/common/file_helper.py`
  `_get_tool_paths`/`_get_tool_path` (PATH only); tbc-analyse side:
  `ExportDialog::resolveFfmpegPath` / `resolveVideoExportPath`
  (exportdialog.cpp ~4309-4364, also PATH fallback).
- Regression commit: `54d06e3` (2026-11-17). Fix is uncommitted in the
  working tree as of this note.
