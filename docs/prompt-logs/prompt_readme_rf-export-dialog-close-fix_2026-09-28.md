# Prompt Readme — RF export UI debugging + save-all dialog close fix
Date: 2026-09-28
Worktree: /tmp/tbc-wt-rf-export (branch feature/rf-segment-export)
Repo pair: tbc-tools (this worktree) + FLAC-Chop /home/harry/FLAC-Chop (branch feature/cli-gui-loading, child agent "flac-chop-cli" run 01a0e28c-f205-7c7b-9b07-2f8de91fe136)

## User input
- "Save all view modes window can't be closed and there is no RF export option"
- Clarification answers (real-world confirmation):
  - Launched tbc-analyse from the desktop menu/launcher
  - "Also export source RF segment (FLAC-Chop)" checkbox NOT visible in the save-all dialog
  - "Export source RF segment for frame..." NOT present in the File menu
  - X (window close) button on the dialog does nothing

## Diagnosis commands + hard data
1. Launcher: `~/.nix-profile/share/applications/tbc-analyse.desktop` → `Exec=tbc-analyse` → resolves to `~/.nix-profile/bin/tbc-analyse` (nix wrapper, 15920 B) → execs real binary via readlink -f.
2. Feature markers in profile binary (`strings ~/.nix-profile/bin/.tbc-analyse-wrapped | grep -c ...`):
   - "Export source RF segment for frame" → 2 (present)
   - "Also export source RF segment" → 1 (present)
3. Two tbc-tools-3.2.9 store paths found:
   - `3j0mcpb12ngy33crd65gpg3l7dp7rjmh-tbc-tools-3.2.9` — markers present (feature build; profile pointed here before today's rebuild)
   - `pdkxi9pwlcdhaa7wvhz75fpxbx67p69s-tbc-tools-3.2.9` — markers 0 (stale pre-feature build)
4. No other installs: /usr/local/bin, /usr/bin, /opt, /snap all empty of tbc-analyse. `nix profile list` → 3j0mcp… (pre-rebuild).
- Conclusion: the reported "missing RF export option" session was running an older binary (launched before the profile swap / old generation). Current profile binary has the features. Relaunch needed.

## Code defect found (real, pre-existing)
- `src/tbc-analyse/mainwindow.cpp` `on_actionSave_all_modes_as_PNGs_triggered()`: exportModeDialog had only AcceptRole/ActionRole buttons (Everything/Image/Scopes/SNR Graphs) — no RejectRole. Qt's QMessageBox X-close/Escape click the escape (reject) button; with none set the close event is ignored → "X does nothing".
- RF export handler (`runRfSegmentExport`, probeRfSource) uses only standard warning boxes/QFileDialog — closes fine, no change needed.
- CI contracts (ci/check_ci_contracts.py): no references to this dialog — safe to edit.

## Fix applied (this worktree, uncommitted)
- Added `QAbstractButton *cancelButton = exportModeDialog.addButton(tr("Cancel"), QMessageBox::RejectRole);` after the SNR Graphs button.
- Early return on cancel: `if (!selectedExportModeButton || selectedExportModeButton == cancelButton) return;`
- Existing Escape QShortcut left in place (reject → null clickedButton → same early return).

## Build + tests
- `nix develop -c sh -c 'cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build'` → OK, `[25/25] Linking CXX executable bin/tbc-analyse`
- `nix develop -c ctest --test-dir build --output-on-failure` → 100% pass, 30/30 tests, 58.01 s
- Profile: `nix profile install .#` deduped ("already added") → had to `nix profile remove tbc-wt-rf-export` then install again.
- Profile now → `/nix/store/gq4mysk9fs6c7akcm4mp7ikxqb6lbhhr-tbc-tools-3.2.9` (dialog-fixed build), markers verified (2).

## FLAC-Chop child agent status (parallel)
- GUI pre-load (`--gui file --in/--out`) interactively confirmed by user earlier.
- New fix on feature/cli-gui-loading (gui/mainwindow.cpp only): Apply Template button now enabled for ANY loaded FLAC (was gated on is_rf), non-RF gets 5 blank ingest rows, status messages instead of silent no-ops. Rebuilt; GUI relaunched (PID 3063197) awaiting user confirmation; commit/push held until then.

## Pending
- User: fully close any running tbc-analyse, relaunch from desktop menu, confirm (a) File menu entry present, (b) checkbox present, (c) X/Cancel closes the dialog.
- Commit/push of the dialog fix after user confirmation (GH Actions must pass per project rules).

## UPDATE 2026-09-28 (later): consolidation into main checkout
- User reported /home/harry/tbc-tools/build/bin not updated — hard data: main checkout was on main @2041e55, its build/bin/tbc-analyse had 0 feature markers.
- Committed dialog fix + this log on feature/rf-segment-export → da242f5; pushed 71b1fad..da242f5 to origin.
- Removed /tmp/tbc-wt-rf-export worktree; checked out feature/rf-segment-export in /home/harry/tbc-tools.
- Rebuilt /home/harry/tbc-tools/build via nix develop (cmake+ninja) → [32/32] OK; ctest → 100% pass 30/30 (58.59 s).
- build/bin/tbc-analyse markers → 2 (feature build with dialog fix).
- Nix profile re-pointed: removed 'tbc-wt-rf-export' (dead /tmp ref) → installed /home/harry/tbc-tools# (element 'tbc-tools', locked rev da242f5, store 9ha7ks5hi81msffb9g2clq00j2mh4gpb); profile markers → 2.
- Note: stale store path pdkxi9… (0 markers) remains in /nix/store unreferenced; GC will collect it eventually.

## UPDATE 2026-09-28 (final): CI verdict
- GitHub Actions run 36404929277 (Tests workflow, commit da242f5) → SUCCESS (~86 min): CI guardrails, Linux build/test parity, and release-style builds all passed (Windows x86_64/arm64, Linux x86_64/arm64, macOS arm64/x86_64, universal DMG; publish correctly skipped, create_release: false).
- Remaining: user GUI confirmation (File menu entry, save-all checkbox, X/Cancel close).

## UPDATE 2026-09-28: FLAC-Chop locate-and-persist prompt (commit 8cbc23c)
- User request: "Assume binary installed in path or installed in same folder self-contained if no flac-chop found prompt to set location and save that persistently."
- Resolution order (unchanged): persisted path → beside-app/relative roots/cwd (resolveExternalExecutable) → PATH.
- NEW: interactive mode, when unresolved → QFileDialog loop: pick → isRunnableExecutableFile check → --version probe (QProcess, must exit 0 with 'FLAC-Chop' banner; hung binary killed) → persist via setRfExportFlacChopPath + writeConfiguration → continue export. Cancel aborts; wrong pick warns and re-prompts.
- Non-interactive (save-all RF) skip message now points at the interactive prompt; no prompt possible mid-background-loop.
- FLAC-Chop --version contract verified in gui/main.cpp:202-205 (prints 'FLAC-Chop <ver>', exit 0).
- Rebuild [32/32] OK; ctest 30/30 pass (60.67 s). Pushed da242f5..8cbc23c; CI run 36419253914 watched by monitor agent.
- Test recipe (user): the prompt triggers only when flac-chop is NOT resolvable and no valid path is persisted. Recipe: quit tbc-analyse, launch from folder with no flac-chop nearby with persisted setting cleared (Settings rfExport group), File → Export source RF segment for frame... → picker appears → pick /home/harry/FLAC-Chop/build/gui/flac-chop → status 'FLAC-Chop location saved' → export proceeds.
- CI: run 36419253914 (commit 8cbc23c) → SUCCESS; run 36424406302 (commit 4e2fec6, RF auto-discovery + GUI hand-off) → SUCCESS (monitor-verified). All feature-branch commits now CI-green.

## FLAC-Chop 12-bit RF auto-load verification (child agent, parallel)
- Load paths (--gui pre-load, File Open, drag-drop): ALL verified working for true 12-bit MISRC RF FLACs — identical loadFile→startProbe→onProbeFinished chain, no bit-depth gating (code-reviewed + synthetic enc12 fixture: 40M samples @ 20 MSPS, RF tags 1:1). No GUI load changes needed.
- BUG #1 FIXED (core/src/probe.rs): Sanity-2 payload cross-check falsely rescaled RF_TOTAL_SAMPLES ×1000 for low-compression (verbatim-ish) 12-bit content (0.25% framing overhead tripped the old zero-tolerance check). Now rescales only when the unscaled estimate is ≥100× under the payload; regression test added.
- BUG #2 FIXED (core/src/chop.rs): SIGPIPE/EPIPE process death on partial cuts (SoX closes the stdin feed after the trim window; feeder wrote megabytes past it) — Rust binaries failed, C++ GUI binary died with exit 141 mid-cut. SIGPIPE→SIG_IGN (libc, unix-scoped) + BrokenPipe = benign end-of-feed; regression test added (100 KiB past-window write).
- Suite green: 113 core + 6 ffi_plan + 10 roundtrip12. Build relaunched for user confirmation (PID 3811637, git-dirty dev build v1.0.0-2-g842384b-dirty); commit/push held for confirmation.
- BUG #3 RESOLVED — user chose 1:1; implemented in core/src/tags.rs (rewrite_cut_tags): RF_TOTAL_SAMPLES = true on-disk count (×1000 gone), DURATION_SECONDS/LENGTH derived from the REAL rate. New regression rf_cut_output_reprobes_with_correct_total_and_recuts (reload + cut-of-cuts inside probed total — failed under ×1000). Suite: 113 core + 6 ffi_plan + 11 roundtrip12 green. E2E hard data: cut src12rf 0.5→1.0 s → tags RF_TOTAL_SAMPLES=20000000 / DURATION_SECONDS=1.000000, re-probe clean. Note: cmake did not re-run cargo on tags.rs edits (stale staticlib) — explicit cargo build --release + relink required after core edits.
- SHIPPED: user confirmed the held batch; FLAC-Chop master @ 48bdc41 ("12-bit RF fixes: probe tag rescale, stdin-feed EPIPE, .8u extensions, 1:1 cut tags") pushed, tagged v1.0.4, release published + verified (changelog Added/Fixed/Testing). All held work committed and released.

## UPDATE 2026-09-29: log commit + Win arm64 CI snag
- Commit 8aa1fda (docs-only session log) pushed; CI run 36542024745 → FAILURE in 'Build tbc-tools (arm64)' / 'Run CMake': "Generator NMake Makefiles does not support platform specification, but platform arm64" + CMAKE_C/CXX_COMPILER not set. Not caused by the diff (docs-only; identical workflow passed on 4e2fec6 at run 36424406302) — points at runner-image/cmake 3.31.6 arm64 drift on the windows-arm64 runner. Rerun of failed jobs requested; monitor agent watching; if it reproduces, fix the workflow's arm64 CMake generator/platform handling on this branch.
- Rerun FAILED the same way → deterministic. Root cause (hard data): the windows-11-arm label now maps to the windows-11-vs2026-arm64 image (VS2026 only); cmake 3.31.6 cannot select VS2026 (needs CMake 4.x; the 3.x pin exists for fftw3) → fell back to NMake → rejected the preset's -A arm64. FIX commit 25596b4: arm64 matrix entry moved to windows-2022 (VS2022, -A arm64 cross-compile). That run then failed find_package(PkgConfig) — the vcpkg toolchain's CMAKE_PROGRAM_PATH only exposes the TARGET tools dir (arm64 pkgconf.exe not runnable on the x64 host); FIX commit 9d9155f: -DVCPKG_HOST_TRIPLET=x64-windows so the host tools dir is used (no-op for x64 job). Run 36602827779 monitoring.

## UPDATE 2026-09-29: ld- → tbc- internal naming audit (user report: 'tbc-video-export not updated internally, tbc-dropout-correct ext bugged')
- AUDIT RESULT: no functional old-name references remain. Hard data:
  - Repo source + INSTALLED staged payload identical; installed enums.py:137 emits 'tbc-dropout-correct' (also tbc-chroma-decoder / tbc-process-vbi / tbc-export-metadata). The only ld- hits are docstrings/comments ('ld-process-efm' in parser_ld_process_efm.py / wrapper_ld_process_efm.py / wrapper_ffmpeg.py:262 / opts_ldtools.py:422 comment) — cosmetic; module filenames are internal.
  - Intentional ld- binaries kept (LaserDisc-specific): ld-discmap, ld-disc-stacker, ld-lds-converter.
  - End-to-end: scripts/test-video-export-runtime.py runs tbc-video-export WITH dropout correction enabled (no --no-dropout-correct) → forces find_binary('tbc-dropout-correct') resolution + execution → PASSES (local 30/30 ctest + CI). tbc-analyse prepends its app dir to the child PATH (exportdialog.cpp defaultExecutableSearchDirs + prependUniquePathEntries) where tbc-dropout-correct is staged.
- USER CONFIRMED after retest: 'export is fixed in analyse now' — the reported breakage was the stale pre-rebuild install, not a naming bug. Nothing else broke: full ctest 30/30 green and CI success on every code commit.
