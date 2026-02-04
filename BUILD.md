# Build (Nix)

This project uses a Nix dev shell to provide a consistent build environment.

## Enter the dev shell

```bash
nix develop
```

This exposes all build dependencies (CMake, Ninja, Qt6, FFmpeg, FFTW, SQLite, OpenGL, etc.).

## Configure and build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
```

Artifacts will be under `build/`.

## Build without entering the shell (one-off)

```bash
nix develop -c cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
nix develop -c ninja -C build
```

## Optional: clean build

```bash
rm -rf build
```

## Notes

- The flake sets `-DEZPWD_DIR`, `-DAPP_BRANCH`, and `-DAPP_COMMIT` automatically for package builds.
- The dev shell exports `EZPWD_DIR`, so manual builds via `nix develop` pick it up automatically.
