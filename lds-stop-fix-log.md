# ld-lds-converter — Stop button close/crash fix

Date: 2026-06-27 (root cause confirmed via gdb backtrace; real-data validated 2026-06-28)

## User report
"pressing stop still closes the gui" — window vanishes **instantly** on Stop click, any config.

## Definitive root cause (hard data: gdb backtrace)
```
free(): double free detected in tcache 2
#7  fclose@@GLIBC_2.2.5
#8  DataConverter::closeOutputFile()
#9  DataConverter::process()
#10 ...ConverterDialog::on_convertButton_clicked()::{lambda()#1}  (worker thread)
```
`FLAC__stream_encoder_init_FILE()` **transfers ownership of the FILE
to the FLAC encoder**. `FLAC__stream_encoder_delete()` closes (frees)
that FILE\ itself. `closeOutputFile()` then called `fflush`/`fclose`
on the same handle *after* `delete` → double-close → glibc double-free
→ SIGABRT, taking the whole process (and the GUI) down. This fired on
**any** FLAC conversion reaching close — Stop AND normal completion.
The earlier 10s SIGINT CLI test never reached close, which is why it
missed it.

The previous attempt (worker-thread unification, removing activeConverter)
was real but addressed a separate defect (frozen UI / undeliverable Stop
in sequential mode) and did NOT touch the double-free. This run is the
actual crash fix.

## Fix
`src/ld-lds-converter/dataconverter.cpp` `closeOutputFile()`: removed
the post-`delete` `fflush`/`fclose` of `flacOutputFileHandle`. The FLAC
encoder owns and closes the FILE\; we only null our pointer afterward.
The init-failure path (failed `init_FILE` does NOT transfer ownership)
still correctly `fclose`s. Kept the worker-thread + close-event guard
work from the previous attempt.

## Real-data validation (hard data)
- Fetched the real `ve-snw-cut.lds` LFS blob (50,032,640 bytes; the
  local copy was a 133-byte LFS pointer — fetched via nix-shell git-lfs,
  non-invasive). NOTE: every .lds/.ldf in
  /home/harry/decode-test-data/ld-decode-testdata is an LFS pointer until
  fetched; git-lfs is NOT installed on the system.
- Full FLAC conversion to completion on real data:
  `ld-lds-converter -u --flac --compression-level 8 -i ve-snw-cut.lds -o /tmp/ve-snw-cut.flac`
  → EXIT 0, 1.96s, output 29,857,862 bytes, header `fLaC`.
- Reference decoder integrity test: `flac -t /tmp/ve-snw-cut.flac` → `ok`.
- This exercises the exact crash path (closeOutputFile→process). Old code
  SIGABRT'd here; new code completes with a decoder-validated FLAC.
- GUI Stop/cancel uses the same closeOutputFile on the worker thread, so
  the double-free is fixed there too.

## Build/test
- `nix develop -c ninja -C /home/harry/tbc-tools/build ld-lds-converter` → clean
- `ctest -R ld-lds-converter` → 3/3 passed (help, invalid-format, invalid-compression-level)

## Files changed
- `src/ld-lds-converter/dataconverter.cpp` (the actual crash fix)
- `src/ld-lds-converter/converterdialog.cpp` / `.h` (prior worker-thread + stop hardening, retained)

## NOT yet verified from user end
- Real GUI Stop click (cannot be done headlessly). Please confirm:
  single-file Convert→Stop, batch Convert→Stop, parallel-batch Convert→Stop.
- If still vanishing, re-run the gdb capture and compare — but the
  closeOutputFile double-free is now structurally eliminated.
