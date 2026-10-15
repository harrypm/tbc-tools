# Prompt readme — SECAM chroma decoder: copy vapoursynth-analog parameters (2026-10-15)

Prompt: "copy the params from vsanalouge for the SEACAM chroma-decoder to make it
more accurate"

## Goal

Make `tbc-chroma-decoder`'s FM SECAM decoder (`src/tbc-chroma-decoder/secamdecoder.cpp`)
match the accuracy of the SECAM decoder used by vapoursynth-analog, which is
libchromadec v0.1.0 (`chd::decoders::secam::SecamDecoder`,
https://github.com/JustinTArthur/libchromadec.git, local clone at
/home/harry/libchromadec). Per
`development-logs/dev_note_secam-decode-tuning_2026-09-13.md` this is the
"FFT bell-network (cloche) + baseband-discrimination rewrite from libchromadec
— noted but not started" item.

## Inputs / sources consulted

- /home/harry/tbc-tools/src/tbc-chroma-decoder/secamdecoder.cpp / .h (current decoder)
- /home/harry/tbc-tools/src/tbc-chroma-decoder/secampredemoddecoder.cpp / .h (untouched)
- /home/harry/libchromadec/src/decoders/secam/secam_decoder.cpp / .h (reference, v0.1.0)
- /home/harry/vapoursynth-analog/src/analog4fsc.cpp / .h (how vsanalog maps opts to
  CHD_DEC_SECAM; secam.py is lattice-fill only, not the FM demod)
- /home/harry/vapoursynth-analog/subprojects/chromadec.wrap (pins libchromadec v0.1.0)
- decoderpool.cpp / decoder.h (threading: shared decoder, concurrent decodeFrames)
- tbc-analyse/tbcsource.cpp + chromadecoderconfigdialog.cpp (GUI-facing SecamConfiguration)
- Test data (from dev note): /home/harry/Desktop/SECAM/ (etienne combined SECAM,
  mesecam_tape275_testcard_50f split MESECAM snippets)

## Parameter delta to port (tbc-tools -> libchromadec v0.1.0 values)

| Item | tbc-tools now | libchromadec (vsanalogue) |
|---|---|---|
| Demod band | ±450 kHz lowpass (3.878–4.778 MHz), 41-tap Hamming sinc | raised cosine: stop 2.9 M / flat 3.5–5.1 M / stop 5.7 M (JVC BPF-1), follows measured carrier offset |
| Bell (cloche) network | none | A=(1+j16F)/(1+j1.26F), F=f/f0−f0/f, f0=4.286 MHz (+ measured offset), frequency domain |
| Discriminator | 1-sample phase diff of filtered signal | mix to baseband, 9-tap maximally-flat central difference (4/5, −1/5, 4/105, −1/280), Im(conj(a)a′)/|a|² |
| Carrier calibration | per-row rest carrier w/in 60 kHz of nominal, else per-identity median (clamped 200 kHz) | per-field cluster medians, ≥8 valid rows, separation gate 90–230 kHz, mask recentre if block centre moves >10 kHz, last-good reuse |
| Rest window | colourBurstStart .. activeVideoStart−10 (+fallbacks) | last 60 samples before activeVideoStart−5, fallback 8.5–15.5% of width |
| De-emphasis | bilinear IIR, f1=85 kHz | exact frequency-domain (1+jf/3f1)/(1+jf/f1), f1=85 kHz |
| Click concealment | dip 12−6L dB, overshoot max(2.6−1.6L, 1.15) | same + noise-adaptive term 6·noiseMadHz/506000 |
| Deviation scaling | −280k×1.902 / +230k×1.505 combined | 230 kHz per D'B, 280 kHz per D'R, kDbScale 1.505, kDrScale −1.902 (equivalent) |
| Deviation rail | ±350/506 kHz (Hz units) | same, D'-units |

## Commands run (research)

- (semantic search) SECAM decoder files in tbc-tools + vapoursynth-analog
- read secamdecoder.cpp/.h, secampredemoddecoder.cpp/.h (tbc-tools)
- read secam.py, analog4fsc.cpp/.h (vapoursynth-analog); chromadec.wrap (pin v0.1.0)
- read secam_decoder.cpp/.h (libchromadec v0.1.0 local clone)
- `git -C /home/harry/tbc-tools log -- src/tbc-chroma-decoder/secamdecoder.cpp` (last: 2ae99e8 rename)
- grep FFTW: linked in src/tbc-chroma-decoder/CMakeLists.txt (PkgConfig::FFTW)
- read decoderpool.cpp / decoder.h (concurrent shared decodeFrames → calibration state
  must be per-call FieldWork, FFTW via new-array execute on shared plans)
- read tbc-analyse tbcsource.cpp SECAM wiring (chromaGain, chromaPhase,
  nominalFirstActiveFieldLine set from GUI; clickNrLevel CLI-only)
- `ls test-data` (ntsc/pal only, no SECAM); grep ci/check_ci_contracts.py (no SECAM refs)
- read dev_note_secam-decode-tuning_2026-09-13.md (context + test data paths)
- ls /home/harry/Desktop/SECAM/snippets (etienne combined + mesecam split snippets present)

## Decisions

- Port the full libchromadec demodulation pipeline (FFT analytic + band×bell masks,
  baseband differentiating-FIR discriminator, per-field calibration + mask recentring,
  porch tail window, noise-adaptive click concealment, exact FFT de-emphasis).
- Keep tbc-tools integration: SecamConfiguration API unchanged (chromaGain,
  chromaPhase, clickNrLevel, nominalFirstActiveFieldLine), ComponentFrame U/V
  pre-scaling for OutputWriter, one-line hold fill, colour killer + V-interval
  zeroing (full-frame/hybrid preview), hue rotation, diagnostics (updated).
- All new mutable state per-call in FieldWork (thread-safety under DecoderPool).

## Follow-up prompt (same day): SECAM test-data snips via FLAC-Chop

Prompt: "/home/harry/Desktop/SECAM - make some flac chop snips for offical tests
for my smaple repo if need be"

User decisions (asked via question panel): RF snips of the two RF .flac captures
+ 10 frames per snip + both .flac snips and restored .tbc committed + place data
and add ctest fixtures with measured expectations.

### What was done

Sources verified with hard data first:
- ffprobe on the two big .flac files: they are the RF captures
  (etienne 40 MSPS header 40000, tape275 17.898 MSPS header 17898, /1000
  convention, mono 16-bit)
- spectrum check (numpy FFT, picture line 30, fields 1-2) of all four .tbc
  files: etienne main .tbc = combined luma+chroma; etienne _chroma.tbc =
  chroma-only FM block; tape275 main .tbc = luma-only; tape275 _chroma.tbc =
  chroma-only FM block (all four first-field md5s distinct - not duplicates)
- test-data conventions read: fixtures are `<name>.tbc` + `.tbc.json` +
  `.tbc.db` tracked in-repo (max existing fixture 33 MB); official tests are
  the `test-decode-pretbc` ctest entries (CMakeLists.txt)

Snips made (FLAC-Chop v1.0.0 CLI, `--units samples`, all cuts from sample 0):
- RF snips (native FLAC in, lossless trim): etienne 16,025,600 samples
  (20 field-times @ 40 MSPS); tape275 7,169,418 samples (@ 17.898 MSPS)
- TBC snips (raw u16 via `<n>msps`-hint symlinks, FLAC-Chop reads raw u16 as
  unsigned mono = exactly the TBC format): 7,105,100 samples = 20 fields
  (fields 1-20 = frames 1-10) per TBC, all four TBCs (etienne combined,
  etienne chroma, tape275 luma, tape275 chroma)
- Restore + verification: `sox -t flac X.flac -t raw -e unsigned -b 16 X.tbc`
  (no -r on the raw side - a rate flag there triggers a 1000x resample;
  first attempt made 14 GB files, deleted and redone) - all four restores
  MD5-identical to the source region, exactly 14,210,200 bytes
- Metadata: .tbc.json trimmed to fields seqNo 1-20 with
  numberOfSequentialFields updated (8393/992 -> 20; converter validates it)

Placed in test-data/secam/ (12 files, ~101 MB) + README.md with provenance
and restore commands:
- secam-etienne-cbars-10f.{tbc,tbc.flac} + _chroma.tbc{,.flac} +
  .tbc.json + .tbc.db + -rf.flac
- mesecam-tape275-testcard-10f.{tbc,tbc.flac} + _chroma.tbc{,.flac} +
  .tbc.json + .tbc.db + _chroma.tbc.db + -rf.flac
- No .gitignore change needed (no *.flac rules exist; .tbc negations
  already cover test-data)

### Bug found + fixed while wiring the fixtures

`tbc-metadata-converter` json->sqlite dropped legitimate 0-valued field
metadata: jsonconverter.cpp guarded diskLoc/fileLoc/audioSamples/
decodeFaults/efmTValues with `> 0`, but the metadata convention uses -1 as
the unset sentinel and 0 is real data (etienne field 1 has diskLoc 0.0 /
fileLoc 0 at the capture start). Symptom: db NULL -> process-vbi writes -1
-> tbc-export-metadata omits the keys -> test-decode-pretbc's export-json
check KeyErrors on 'diskLoc'. Fixed the guards to `!= -1` (sentinel only);
reconverted dbs verified (field 0 disk_loc 0.0 / file_loc 0 preserved,
unset stays NULL).

### Measured expectations (hard data) + ctest wiring

- etienne combined, decoders mono + secam: 10 RGB frames (928x576), VITS
  20 rows, bPSNR median 47.3 dB (min 45.9), VBI 0 rows (no decodable VBI)
- tape275 luma, decoder mono: 9 RGB frames, bPSNR median 32.15 dB (min 30.3)
- tape275 chroma, decoder secam: 9 RGB frames, chain passes end-to-end
  (tbc-process-vbi/exporter/dropout-correct/chroma-decoder all fine on a
  chroma-only TBC)

Added 3 add_test entries (decode-pretbc-secam-etienne-cbars,
decode-pretbc-mesecam-tape275, decode-pretbc-mesecam-tape275-chroma) with
--pal --no-efm, unique build/testout output dirs, and the measured floors
(bpsnr 45 / 30, frames 10 / 9 / 9).

## Validation

- ctest: 33/33 passed (30 existing + 3 new SECAM fixtures)
- ci/check_ci_contracts.py: passed (exit 0)
- FLAC snip restores: MD5 byte-exact vs source regions (all four TBCs)
- ffprobe on every snip: sample counts confirmed
  (16,025,600 / 7,169,418 RF; 7,105,100 x4 TBC)
- jsonconverter.cpp fix verified via DB values + full test suite

## Not done / notes

- Nothing committed to git (not requested). Working tree has: test-data/secam/
  (12 files + README.md), CMakeLists.txt (3 add_test), jsonconverter.cpp
  (sentinel fix), test-data .tbc.db fixtures.
- The vsanalogue SECAM decoder parameter port (bell network + baseband
  discriminator rewrite - the original prompt) was NOT started; the plan
  draft was cancelled. These fixtures are the official regression data for
  that work when it proceeds.
- snips-official/ on the Desktop holds the intermediates (symlinks, FLAC
  cuts, restored .tbc, trimmed jsons).
