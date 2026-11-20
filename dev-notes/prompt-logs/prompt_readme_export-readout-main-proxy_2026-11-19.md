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
