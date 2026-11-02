# tbc-analyse

**TBC File Analysis and Visualization GUI**

## Overview

tbc-analyse is a graphical user interface (GUI) tool for analyzing and visualizing Time Base Corrected (TBC) LaserDisc video files produced by ld-decode. It provides comprehensive analysis tools for signal quality assessment, dropout detection, VBI metadata viewing, and real-time video preview.
It can also open metadata directly (SQLite `.db` or JSON `.json`) and export using current in-memory parameter adjustments without requiring a metadata save first.

## Usage

### Command Line
```bash
tbc-analyse [options] <input.tbc>
```

### GUI Operation

1. **Open File**: File → Open TBC file, or specify on command line
2. **Navigate**: Use media controls to move between frames/fields
   - Previous/Next buttons
   - Slider for quick navigation
   - Frame number entry
   - Start/End buttons for chapter-style navigation:
     - Uses LaserDisc VBI chapter map when available
     - Falls back to user-defined timeline markers when no VBI chapters exist
     - If neither chapter type exists, moves by 10 frames backward/forward
     - Press-and-hold repeats 10-frame backward/forward skips continuously
3. **View Options**: Click buttons to toggle
   - Video: Enable/disable video display
   - Aspect: Toggle aspect ratio correction
   - Dropouts: Highlight dropout regions
   - Sources: Compare multiple sources (when available)
   - View: Switch between frame/field/split modes
   - Field Order: Toggle first/second field first
4. **Windows and tool modes** (menus):
   - Window menu: Dropout analysis, Visible Dropout analysis, Black/White SNR analysis, VBI, Teletext Viewer, Closed Captions, Marker Viewer, Video parameters, Video decoder configuration
   - Scopes menu: Vectorscope, Waveform monitor, Line scope, RGB scope, YUV range, Field timing scope
   - Tools menu: Process VBI, Fix JSON SNR, Auto Audio Align, EFM Handler, LDS Converter, Metadata Status/Editor/Conversion/Export
5. **Save/Export**:
   - File → Save Metadata
   - File → Save frame as PNG
   - Export tab for `tbc-video-export`

## Options

#### Common Options
- `-h, --help`: Display help on command-line options
- `-v, --version`: Display version information
- `-d, --debug`: Show debug information
- `-q, --quiet`: Suppress info and warning messages

#### Display Options
- Theme: tbc-analyse follows the OS light/dark colour scheme by default (dark when the OS reports dark or unknown) and switches live when the OS setting changes. The Themes menu offers System / Dark / Light presets plus `Keep theme choice` to persist the selected preset between sessions; Qt style overrides below the presets are session-only
- `--force-dark-theme`: Pin the dark Fusion theme instead of following the system light/dark setting (takes precedence over a persisted choice)
- `--light-theme`: Pin the light Fusion theme instead of following the system light/dark setting (takes precedence over a persisted choice)
- `--metadata-only`: Load metadata (`.db` or `.json`) without TBC image data

#### Arguments
- `input`: Input TBC file or metadata file (`.tbc`, `.db`, `.json`)

## Examples

```bash
# Open TBC file for analysis
tbc-analyse capture.tbc
```

## Input/Output

### Input
- `.tbc` files with associated `.tbc.db` metadata
- Metadata-only input: `.db` (SQLite) or `.json`
- Supports PAL (625-line, 50 Hz), NTSC (525-line, 60 Hz), and PAL-M (525-line PAL variant)

### Output
- PNG images (frame export)
- Analysis reports and statistics

## Metadata Workflow

### Saving metadata
- `File → Save Metadata` writes through a temporary file named by appending `.new` to the metadata filename.
- If an existing metadata file is present, it is renamed to `.bup`, then `.new` is renamed into place.
- This preserves the established backup workflow while keeping writes atomic.

### JSON and SQLite handling
- tbc-analyse supports both SQLite (`.db`) and JSON (`.json`) metadata sources.
- JSON format detection includes temporary/backup naming patterns such as `.json.new` and `.json.bup`, so save/backup operations retain correct JSON handling.

### Export without saving first
- Export from the in-app Export tab uses current in-memory parameter values, including unsaved adjustments.
- tbc-analyse creates a temporary metadata snapshot and passes it to `tbc-video-export`, so exporting does not require running `Save Metadata` first.

### Export tab dropout controls
- The Export tab includes a `Dropout` selector with `Basic`, `Heavy`, and `Disabled`.
  - `Basic` uses normal dropout correction.
  - `Heavy` requests stronger correction mode (`--overcorrect`) when supported by the detected export toolchain.
  - `Disabled` exports with dropout correction disabled.
- The Export tab also includes a `Field` selector with `Intra` and `Innerfield` (default is `Intra`).
  - If an explicit selected mode is not supported by the detected `tbc-video-export`/toolchain version, export falls back to tool defaults and logs a notice in the Export log.

## Troubleshooting

### Performance Issues
- Toggle chroma decoder off during seeking for faster navigation
- Reduce zoom level if display is slow
- Check that Qt6 GPU acceleration is available

### Analysis Issues
- Use the oscilloscope to verify signal levels and timing
- Check the vectorscope to ensure correct color burst phase
- Monitor dropout analysis to identify disc damage patterns
- Compare SNR across the disc to find optimal playback regions
- Use VBI data to verify frame numbers and chapter markers
