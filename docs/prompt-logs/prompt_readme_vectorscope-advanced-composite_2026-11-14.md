
## Phase 9 - Playback rate fix (branch fix/tbc-analyse-playback-rate, uncommitted)
- Pacing: playback ticks scheduled against a QElapsedTimer deadline (resync if >2 intervals late) instead of "interval after render finished".
- Removed playbackUseFastMode (halved interval in mono = ~2x speed). SECAM/MESECAM now 25 fps. Field view steps every field (was +2).
- Measured mono: PAL 24.91 fps / 49.99 fields/s, NTSC 29.95 fps / 59.99 fields/s.

## Phase 10 - Colour playback decode-ahead (same branch)
- Profiled: decode 53-66 ms (PAL Transform 2D), 51-61 ms (NTSC 2D comb), 128 ms SECAM; RGB conversion 8-13 ms; field load 0.2 ms.
- TbcSource: new ChromaDecoders struct (pal/secam/secam-predemod/comb/mono + routing) with decode(); TbcSource keeps one set for the displayed frame and, while playing, one per worker (QThreadPool, idealThreadCount-1 capped at 6).
- Decoders configured (FFTW planning) and TBC fields read on the main thread; only decode() runs on workers. prefetchGeneration invalidates results on any setting change (invalidateImageCache except frame moves, configureChromaDecoder, setCombine). NN Transform 3D excluded (shared ORT session, already internally threaded).
- MainWindow::setPlaybackRunning -> tbcSource.setPlaybackPrefetch().
- Verified decode-ahead frames bit-identical to serial decode (PAL 22/22, NTSC 27/27, SECAM 8/8). Throughput 14 ms/frame. Colour playback steady state: PAL ~41 ms/frame (24.4 fps), NTSC ~33.7 ms (29.7 fps), was ~10 fps. Peak RSS 344 MB colour vs 150 MB mono. ctest 37/37.

## Phase 11 - VBI defaults for tape sources (same working tree)
- TbcMetaData::VideoParameters::isTapeSource(): tapeFormat non-empty and not LD/LaserDisc.
- tbc-process-vbi CLI: tape sources default VBI core + NTSC-specific off; new --vbi-core / --ntsc force on; --no-* still wins. Verified on MESECAM VHS test file and NTSC LD-style file.
- tbc-analyse Process VBI dialog: tape sources open with both unticked + note; LD preferences in config not overwritten; ticking passes the force flags.
- tbc-process-vbi GUI: reads metadata when input changes, sets defaults, passes force flags when ticked. resolveDefaultMetadataFilename moved out of main.cpp's anonymous namespace.
- Limitation: older vhs-decode captures with no tapeFormat look like LD.

## Phase 12 - CUDA (nnTransform3D) plugin detection
- Root cause on this machine: ~/.config/tbc-analyse.ini [cudaPlugin] holds the real record in lower-case keys (installedversion/installpath/releasetag) while the camelCase keys the code reads are empty -> Plugin Manager showed "Not installed" although the decoder loads the plugin (verified: tbc-chroma-decoder -f nntransform3d -> ONNX provider CUDA on GTX 1070 Ti).
- Configuration falls back to lower-case keys; CudaPluginManager::detectedInstallDirectory() treats provider lib present in default dir as installed (version "unknown") in the dialog/isInstalled.
- Verified via harness against the real ini (read back 1.0.0 / path / tag; ini untouched).

## Phase 13 - Teletext (started)
- --teletext runs end to end (OpenCL deconvolve, squash, HTML) on ggv-mb-1khz and kagemusha; both produce 0 packets (no teletext on these samples). Needs a teletext-bearing capture for a real inspection.

## Phase 13b - Teletext on Tape 06 (VHS PAL SP, 512,716 fields)
- 1,000-field segment (field 30,000+): full tbc-process-vbi --teletext path, OpenCL via cuda-lite plugin -> 390 HTML pages, 7 magazines (Channel 4 Teletext / Yorkshire, Jan 27 ~2pm). ~11.5 of 16 teletext lines per field yield packets.
- Character errors (TELETE8T, Gre8hound, p for 0): squash runs with --min-duplicates 1 (vhs-teletext default 3), so pages come from one noisy copy. 20 s too short to compare thresholds (d=2: 572 packets, d=3: 187).
- Root cause of the empty full-tape HTML folder (11:10 run): deconvolution was killed after a fixed 180 s (TELETEXT_DECONV_TIMEOUT_SEC default), both GPU attempt and CPU fallback.
- vhs-teletext --start/--limit count useful lines (16 per field), not tbc lines.

## Phase 14 - VBI processing never stops early
- User: "When a VBI processing option is enabled it should not ever stop till end of file is reached".
- teletextintegration.cpp: deconvolution has no timeout by default; TELETEXT_DECONV_TIMEOUT_SEC opts in to a no-progress limit (restarts whenever the raw .t42 grows). Audited: VBI DecoderPool runs to lastFieldNumber; squash/HTML have no timeout; tbc-analyse runExternalToolWithProgress and tbc-process-vbi GUI runProcessStep only kill on Cancel.
- Teletext tape profile auto-selected from metadata tapeFormat when --teletext-tape-format isn't given (BETA* -> betamax, *2X4* -> grundig_2x4, else vhs).

## Phase 15 - Disk full / Nix GC
- / hit 0 bytes free mid-session (2.6 GB free before; my scratch use was <2 GB and removed). User asked to purge old nix store data: nix-collect-garbage -d -> 1006 GB free.
- GC removed the dev shell closure; rebuilt via nix develop --profile /home/harry/tbc-tools/.nix-devshell-profile (GC root; excluded in .git/info/exclude).

## Phase 16 - Audio Monitor checkboxes (Export tab)
- exportdialog.ui: audio1..4MonitorCheckBox after each Browse button.
- ExportDialog::startAudioMonitor(seconds)/stopAudioMonitor(): ffmpeg decodes + amix the ticked tracks (raw .pcm as s16le 44.1k stereo, like the export trim) -> nut over pipe -> ffplay -nodisp -autoexit. resolveFfmpegPath(toolName) finds ffplay like ffmpeg. Toggling Monitor while playing restarts at the current position.
- MainWindow::setPlaybackRunning starts at (frame-1)/fps or (field-1)/(2*fps), stops on stop/end.
- CI: Linux AppImage + release, Windows, macOS bundle ffplay when the ffmpeg build has it (optional). ci/check_ci_contracts.py passed.
- Verified: harness (start -ss 0.16 at frame 5, untick stops, retick restarts at 0.47 s, stop leaves no processes); pipe plays 1 s of audio in 0.999 s. ctest 37/37.
- Not done: seeking during playback doesn't move the audio.

## Phase 17 - Teletext 5-minute verification + accuracy study (Tape 06)
- 15,000-field segment through tbc-process-vbi --teletext: deconvolution 4m22s (> old 180 s cap), completed, 442 HTML pages. Log confirms tape defaults (VBI core/NTSC off) and vhs profile from metadata.
- Squash threshold: d=1/2/3/5 -> 442/404/337/202 pages; page 100 text identical at d=1 and d=3 -> errors are systematic, not noise.
- Raw bytes: wrong chars all pass odd parity (e.g. '0' 0xB0 decoded as 'p' 0x70 = top two bits swapped); at header pos 15 wrong 'p' outnumbers '0' 24:18, so majority voting picks the wrong value. Deconvolver output has 0.00% parity failures -> it always snaps to a parity-valid byte.
- Profile sweep, 6,000 fields, all magazine headers ("Teletext"/"Jan27" chars): vhs 68.5%, betamax 73.1%, grundig_2x4 73.9%; OpenCL == CPU. Profile choice helps ~5 points only.
- Conclusion: the vhs-teletext pattern profiles don't fit vhs-decode TBC luma well; a real fix likely needs patterns trained on vhs-decode output (open question). min-duplicates default left at 1 (raising it drops pages without fixing these errors).

## Phase 18 - Audio sync + reset on new files
- User: playback rate off for audio sync; must be exact 29.97/25 "in-spec"; new TBC or audio file breaks audio -> must reset internally on every load.
- Rates were already exact (25 / 30000/1001); the drift came from stepping one frame per tick and resetting the schedule after late frames. Playback now follows the wall clock: playbackStartNumber + elapsed/interval; next tick due at (shown - start + 1) * interval; late renders skip frames instead of shifting the timeline (playbackNextDueMs removed).
- Measured (colour on): PAL 25.00000 fps / 50 fields/s, NTSC 29.97003 / 59.94006; last frame 22-28 ms after exact due time (one render, non-accumulating); start-up skips 3-11 frames while decoders initialise.
- ExportDialog::setSource stops/resets the monitor; changing any audio track file (or Monitor tick) while playing restarts at the current position with the new files. on_finishedLoading/loadTbcFile already stop playback.
- Harness: swap audio mid-play -> new file used; load second TBC -> monitor reset, play restarts audio; end of clip stops audio; no leftover ffmpeg/ffplay.
