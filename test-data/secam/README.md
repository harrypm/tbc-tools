# SECAM / ME-SECAM test fixtures

10-frame (20-field) FLAC-Chop snips of two real SECAM-family captures, cut
sample-exactly with FLAC-Chop (`--units samples`) from the full captures on
the capture drive. All TBC snips restore **byte-exact** (MD5-verified against
the source region on 2026-10-15).

## Fixtures

| Fixture | Source | Content |
|---|---|---|
| `secam-etienne-cbars-10f` | Etienne Quadruplex colour bars (SECAM, method 1, MISRC 2.5, 16-bit 40 MSPS RF) | `.tbc` combined luma+chroma; `_chroma.tbc` chroma-only restored FM block |
| `mesecam-tape275-testcard-10f` | Tape 275 track 03 TFB SECAM test card 1987-09-18 (ME-SECAM, 16-bit 17.898 MSPS RF) | `.tbc` luma-only; `_chroma.tbc` chroma-only restored FM block |

Each fixture has: `.tbc` (raw u16 little-endian, 1135x313 per field),
`.tbc.flac` (the FLAC-Chop cut of the same region), `.tbc.json` + `.tbc.db`
(metadata trimmed to fields seqNo 1-20, `numberOfSequentialFields` updated),
and an `-rf.flac` snip of the original RF capture covering the same span.

RF snip spans (one 10-frame span = 20 field-times = 0.40064 s):
- `secam-etienne-cbars-10f-rf.flac`: samples 0-16,025,600 at 40 MSPS
  (header rate 40000 Hz, /1000 RF convention)
- `mesecam-tape275-testcard-10f-rf.flac`: samples 0-7,169,418 at 17.898 MSPS
  (header rate 17898 Hz)

TBC snips are 7,105,100 samples (20 fields x 1135 x 313) = 14,210,200 bytes.
The `_chroma.tbc.db` for the tape275 chroma fixture is converted from the
same shared json (split Y/C sources share one metadata file).

## Restoring `.tbc` from the `.tbc.flac` snips

The TBC is 16-bit **unsigned** little-endian mono; the FLAC round-trips it
losslessly (SoX maps u16 <-> signed FLAC exactly):

```bash
sox -t flac <name>.tbc.flac -t raw -e unsigned -b 16 <name>.tbc
```

Do not pass `-r` on the raw output side: the FLAC header rate is the /1000
RF-convention approximation (17734/17735 Hz), so a rate flag triggers a
1000x resample instead of a plain decode.

## Provenance / cut commands

Cut with FLAC-Chop v1.0.0 CLI (`flac-chop <in> <out> 0 <N> --units samples`).
Raw `.tbc` inputs were presented to FLAC-Chop via `<n>msps`-hint symlinks
(`..._17.734475msps.u16` etienne, `..._17.7345msps.u16` tape275); FLAC-Chop
reads raw u16 as unsigned mono, matching the TBC format exactly.

Original captures (with the FLAC-Chop snips folder `snips-official/`):
`/home/harry/Desktop/SECAM/`
