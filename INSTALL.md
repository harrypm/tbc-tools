# Installation (Nix)

This project ships a Nix flake that provides both a build and a development environment. The simplest way to install is with Nix flakes.

## Prerequisites

- Nix with flakes enabled.
  - Ensure `experimental-features = nix-command flakes` is set in your Nix config.

## Install with Nix

### Install into your user profile

```bash
nix profile install .#
```

This installs the tools into your Nix profile (for example, `~/.nix-profile/bin`).

### Build without installing

```bash
nix build .#
```

The build output will be available under `./result`.

### Run from the build output

```bash
./result/bin/ld-analyse
```

Replace `ld-analyse` with any other tool from the suite.

## Notes

- The flake pins all required dependencies, including Qt6, FFmpeg, FFTW, SQLite, and OpenGL.
- Build metadata (branch/commit) is set by the flake during the build.
