# ld-lds-converter — 10-second multi-input test log

Date: 2026-06-27

## Inputs (both verified to exist)
- `/media/harry/18TB/raws /Video8_NTSC_Tape_019_NTSC_2025-05-29_17-00-49_01H57M17S.lds` (351,870,648,320 bytes ≈ 352 GB)
- `/media/harry/18TB/raws /Video8_NTSC_Tape_020_NTSC_2025-05-30_07-45-18_02H06M54S.lds` (380,718,284,800 bytes ≈ 381 GB)

Both paths contain a space (`raws /`).

## Output directory (temp, to avoid writing partials to the 18TB source drive)
- `/tmp/lds-10sec-test/`

## Commands run

1. Verify inputs exist:
   ```
   ls -la "/media/harry/18TB/raws /Video8_NTSC_Tape_019_..._.lds" "/media/harry/18TB/raws /Video8_NTSC_Tape_020_..._.lds"
   ```

2. Create temp output dir:
   ```
   mkdir -p /tmp/lds-10sec-test
   ```

3. 10-second bounded CLI run (FLAC, compression 8):
   ```
   timeout --signal=SIGINT 10 /home/harry/tbc-tools/build/bin/ld-lds-converter \
     -u --flac --compression-level 8 -o /tmp/lds-10sec-test \
     -i "/media/harry/18TB/raws /Video8_NTSC_Tape_019_..._.lds" \
     -i "/media/harry/18TB/raws /Video8_NTSC_Tape_020_..._.lds"
   ```
   Exit status: 124 (timeout killed it at 10s — expected).

4. Short debug capture to confirm parsing:
   ```
   timeout --signal=SIGINT 6 /home/harry/tbc-tools/build/bin/ld-lds-converter \
     -u --flac --compression-level 8 -o /tmp/lds-10sec-test --debug \
     -i "...Tape_019...lds" -i "...Tape_020...lds" > /tmp/lds-10sec-test/debug.log 2>&1
   ```
   Exit status: 124.

## Observed results (hard data)

- Partial output produced: `/tmp/lds-10sec-test/Video8_NTSC_Tape_019_NTSC_2025-05-29_17-00-49_01H57M17S.flac` = 87,023,616 bytes (83 MB).
- Debug log confirms correct input parsing:
  `DataConverter::openInputFile(): Input file is "/media/harry/18TB/raws /Video8_NTSC_Tape_019_..._.lds" and is 351870648320 bytes in length`
- Unpacking loop writing ~33,554,432 bytes (32 MiB) of decoded s16 data per ~0.7s.
- Batch-mode output naming (per-input filename inside the `-o` directory) confirms both `-i` values were collected and the multi-input batch branch was taken.

## Limitations / not covered by this test

- CLI batch is sequential: in 10s only input #1 (Tape 019) was reached. Tape 020 is queued but does not start until #1 completes (hours for a 352 GB file).
- This test did NOT exercise the GUI Stop button (requires a real GUI interaction).
- Partial FLAC left in `/tmp/lds-10sec-test/` (ephemeral; cleared on reboot).
