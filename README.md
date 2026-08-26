# TBC Tools


This is the complete suite of tools for processing TBC (Time Base Corrected) files from the decode projects, [vhs-decode](https://github.com/oyvindln/vhs-decode/wiki), [cvbs-decode](https://github.com/oyvindln/vhs-decode/wiki/CVBS-Composite-Decode), [ld-decode](https://github.com/happycube/ld-decode). 

This is an cross-platform (experimental) highly subject to change feature addtion focused branch based off the orignal ld-tools, directly after the cut-off for [decode-orc](https://simoninns.github.io/decode-orc-docs/decode-orc/) development.

These tools are for analyzing decoded analog video sources, handling the 4fsc video data, sound, metadata and final tweaks in framing, chroma-decoding and video levels before converting to YUV digital video files.

Please see the [releases page](https://github.com/harrypm/tbc-tools/releases/) for ready to use self-contained binarys for production usage. 


## Images

<img width="1920" height="1080" alt="Screenshot from 2026-04-04 19-16-33" src="https://github.com/user-attachments/assets/9458ffcb-6bc5-4801-8716-476921b8506e" />

<img width="1920" height="1080" alt="Screenshot from 2026-04-04 19-12-14" src="https://github.com/user-attachments/assets/b5d43138-07d3-4eae-9b65-389df2963792" />

<img width="1354" height="975" alt="Screenshot from 2026-05-02 07-02-52" src="https://github.com/user-attachments/assets/e6241648-a911-445b-a777-629a691ce7d6" />

<img width="665" height="345" alt="Screenshot from 2026-05-02 07-03-24" src="https://github.com/user-attachments/assets/de08ed72-00cb-4887-8951-0fc27dea12cf" />

<img width="860" height="762" alt="Screenshot from 2026-05-02 07-01-35" src="https://github.com/user-attachments/assets/d147dd99-6b09-4752-b3b3-c90801b72f21" />


## Tool Categories

### Core Processing Tools

- **ld-analyse**       - GUI tool for TBC file visual analysis & adjustment handler. Supports drag-and-drop loading of `.tbc`, `.ytbc`, `.ctbc`, `.tbcy`, `.tbcc`, `.db`, and `.json` files into the main window.
- **ld-process-vbi**   - Decode Vertical Blanking Interval data (Closed Captions, VITC - Vertical Interval Time Code, XDS Data) 
- **ld-process-vits**  - Process Vertical Interval Test Signals
- **ac3-decoder**      - Decode AC3 data from demodulated AC3-RF QPSK symbols

### EFM Decoder Suite

*Replaces deprecated ld-process-efm with staged decoding and stacking capabilities*

- **efm-handler**        - GUI handling tool for stages automatic use.
- **efm-decoder-f2**     - Convert EFM T-values to F2 sections
- **efm-decoder-d24**    - Convert F2 sections to Data24 format
- **efm-decoder-audio**  - Convert EFM Data24 sections to 16-bit stereo PCM audio
- **efm-decoder-data**   - Convert EFM Data24 sections to ECMA-130 binary data
- **efm-stacker-f2**     - Combine multiple F2 captures for improved quality

### Tools

- **ld-discmap**          - TBC and VBI alignment and correction tool
- **ld-dropout-correct**  - Advanced dropout detection and correction
- **ld-chroma-decoder**   - Color decoder for TBC LaserDisc video to RGB/YUV conversion
- **ld-disc-stacker**     - Combine multiple TBC captures for improved quality

### Export and Conversion Tools

- **tbc-export-metadata**     - Export TBC metadata to external formats
- **ld-lds-converter**        - Convert between 10-bit packed and 16-bit data formats for DomesDay Duplicator captures.
- **tbc-metadata-converter**  - Convert between  JSON & SQLite metadata formats
- **tbc-video-export**        - Direct to video export task handler for .tbc files. 

### Utility Scripts

- **ld-compress**              - Compress TBC files for storage (in scripts/)
- **filtermaker**              - Create custom filtering profiles (in scripts/)
- **tbc-video-export-legacy**  - Legacy TBC to video conversion (archived)

## Getting Started

**Analyse & Adjust**: Use `ld-analyse` to assess capture quality and identify issues such as off-set active picture area or chroma phase/gain & video levels corrections.

**Align**: Using Auto Audio Align, your can align your source capture to your metadata and load in the audio for perfect cut.

**Export**: Convert to final formats using the export page to chroma-decode and encode via FFmpeg profile to a main and or proxy video file. 

## Linux Dependencies and Installation (Multi-Distro)

### Recommended: Nix (reproducible)

Use the Nix-based install/build instructions in `INSTALL.md` and `BUILD.md`:

- Install to profile: `nix profile install .#`
- Build locally: `nix develop -c cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && nix develop -c ninja -C build`

### Native distro packages (manual build)

Before building, ensure submodules are present (required for `ezpwd`):

```bash
git submodule update --init --recursive
```

Install dependencies for your distro family:

Package names can vary slightly by distro release; if one package name differs, install the equivalent Qt6/FFTW/SQLite/FFmpeg development package for your release.

#### Debian/Ubuntu (including Linux Mint)

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config qt6-base-dev qt6-tools-dev qt6-tools-dev-tools libqt6svg6-dev libfftw3-dev libsqlite3-dev ffmpeg libgl1-mesa-dev python3 python3-numpy
```

#### Fedora

```bash
sudo dnf install -y gcc-c++ cmake ninja-build pkgconf-pkg-config qt6-qtbase-devel qt6-qtsvg-devel fftw-devel sqlite-devel ffmpeg ffmpeg-devel mesa-libGL-devel python3 python3-numpy
```

#### Arch Linux / EndeavourOS / Manjaro

```bash
sudo pacman -S --needed base-devel cmake ninja pkgconf qt6-base qt6-svg fftw sqlite ffmpeg mesa python python-numpy
```

#### openSUSE Tumbleweed/Leap

```bash
sudo zypper install -y gcc-c++ cmake ninja pkgconf-pkg-config qt6-base-devel libqt6-qtsvg-devel fftw3-devel sqlite3-devel ffmpeg Mesa-libGL-devel python3 python3-numpy
```

Build and install:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo cmake --install build
```

If you do not want to install system-wide, run tools directly from `build/bin/`.

## Local CI parity checks

To keep local validation aligned with GitHub Actions, run the same guardrails and full build/test flow locally before pushing:

```bash
bash ci/run_local_ci_parity.sh
```

Useful scoped modes:

```bash
bash ci/run_local_ci_parity.sh --guardrails-only
bash ci/run_local_ci_parity.sh --build-test-only
```

## Important Notes

- **Metadata Formats**: SQLite (`.tbc.db`) is the 2026-present metadata format, JSON metadata (2017-2026) being still supported by the tools and used by many older builds for the decode suite. 
- **File Extensions**: TBC files use `.tbc` extension; metadata is commonly `.tbc.json` & `.tbc.db` (SQLite) 
- **Dependencies**: Most tools require FFmpeg and other multimedia libraries.
- **Performance**: Many tools support multi-threading for fast real-time or faster processing.

## Documentation

Each tool directory contains detailed README.md files with:
- Basic usage instructions
- Complete option references
- Usage examples
- Input/output format specifications
- Troubleshooting Refrences

See individual tool directories for specific tool documentation.
