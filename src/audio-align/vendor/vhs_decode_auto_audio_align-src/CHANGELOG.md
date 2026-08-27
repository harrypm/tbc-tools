# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.2] - 2025-10-06

### Added

- Fallback to original project cache for mono-builds to speed up CI/CD in forks of this project
- Add Linux AArch64 AppImage build

### Fixed

- Fix input/output file confusion (thanks to @davidvankemenade for reporting!)
- Fix PAL-M detection (thanks to @andre-antunesdesa for the fix!)
- 3-Clause BSD license now aligned with https://opensource.org/license/bsd-3-clause

## [1.0.1] - 2025-09-22

### Added

- Add Linux x86_64 AppImage build

## [1.0.0] - 2024-04-26

### Added

- Implement a workaround for signed 32Bit overflow in tbc.json produced by `ld-analyze` or other ld-decode-tools
- Proper documentation
- Package CHANGELOG.md into zip
- CSVs have time offset columns that help binjr to better parse them

### Fixed

- Fix links in CHANGELOG.md
- Fix handling of EOF in non-seekable streams (don't try to get the stream position at the end)

## [0.1.0] - 2023-10-15

### Added

- Linear projection with skipping of missing fields works for sync linear audio / PCM1802 recordings from clock generator
- Nearest neighbor interpolation of an input to an output stream 
- CSV creation from TBC JSON to view drift and compensation over a tape

[unreleased]: https://gitlab.com/wolfre/vhs-decode-auto-audio-align/-/compare/v1.0.2...main
[1.0.2]: https://gitlab.com/wolfre/vhs-decode-auto-audio-align/-/compare/v1.0.1...v1.0.2
[1.0.1]: https://gitlab.com/wolfre/vhs-decode-auto-audio-align/-/compare/v1.0.0...v1.0.1
[1.0.0]: https://gitlab.com/wolfre/vhs-decode-auto-audio-align/-/compare/v0.1.0...v1.0.0
[0.1.0]: https://gitlab.com/wolfre/vhs-decode-auto-audio-align/-/tree/v0.1.0
