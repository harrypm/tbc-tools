# Restore point — SECAM decoder tab cleanup

- Date (UTC): 2026-08-01
- Commit: a942a92  ("SECAM Window cleanup")
- Branch: main (pushed to origin/main; 022c94b..a942a92)
- Status: User-confirmed working ("this is good") after running nix develop -c build/bin/ld-analyse.

## What changed
The SECAM chroma decoder option was a radio button inside the PAL tab of the
Video Decoder Configuration dialog. It is now in its own dedicated SECAM tab.

### src/ld-analyse/chromadecoderconfigdialog.ui
- Removed the palFilterSecamRadioButton <item> from palTab.
- Added a new secamTab (title "SECAM") to standardTabs containing:
  - secamFilterLabel ("Chroma filter:")
  - palFilterSecamRadioButton ("SECAM") — still in palFilterButtonGroup so it
    stays mutually exclusive with the PAL Mono / PalColour 2D / Transform 2D /
    Transform 3D choices (all set palConfiguration.chromaFilter).
  - trailing vertical spacer (verticalSpacer_12).
- Fixed the indentation of the following verticalSpacer_7 item in palTab.

### src/ld-analyse/chromadecoderconfigdialog.cpp
- setConfiguration() tab auto-selection now routes to ui->secamTab when
  system is PAL/PAL_M and palConfiguration.chromaFilter == PalColour::secam
  (NTSC -> ntscTab, else palTab).
- All other SECAM handling unchanged (object name unchanged):
  setEnabled(isSourcePal), setChecked in the chromaFilter switch,
  on_palFilterButtonGroup_buttonClicked -> PalColour::secam, and the chroma
  phase slider being disabled for SECAM.

## Build verification
nix develop -c ninja -C build bin/ld-analyse  ->  [29/29] Linking CXX executable bin/ld-analyse
(uic regenerated ui_chromadecoderconfigdialog.h with secamTab; secamdecoder.cpp
and chromadecoderconfigdialog.cpp both compiled.)

## How to roll back to this point
- git checkout a942a92 -- src/ld-analyse/chromadecoderconfigdialog.ui src/ld-analyse/chromadecoderconfigdialog.cpp
  (then rebuild with: nix develop -c ninja -C build bin/ld-analyse)
- Or unzip development-logs/secam_tab_cleanup_20260801_085924.zip over the repo root.

## Files preserved in the zip
- src/ld-analyse/chromadecoderconfigdialog.ui
- src/ld-analyse/chromadecoderconfigdialog.cpp
