#!/bin/bash
#
# package-aaa-appimage.sh — package the self-built VhsDecodeAutoAudioAlign
# .NET assemblies + a bundled Mono runtime into a self-contained AppImage
# that needs no host Mono at runtime (the fix for the "mono not found on
# Ubuntu" failure).
#
# Replicates upstream's ci-build-appimage.sh `build_app_image` function but
# uses the distro Mono runtime (installed by the CI job) instead of building
# Mono from source.
#
# Usage: scripts/package-aaa-appimage.sh --built-dir <path> --output <path>
#
#   --built-dir  directory produced by build-aaa-linux.sh
#                (must contain VhsDecodeAutoAudioAlign.exe, Binah.dll, appimage/)
#   --output     path to write vhs-decode-aaa.AppImage
#
# Requires: mono-core/mono-devel on PATH (provides /usr/bin/mono + its libs),
#           appimagetool (downloaded by this script if not on PATH),
#           fuse + xz (for appimagetool).
#
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Simon Inns

set -euo pipefail

BUILT_DIR=""
OUTPUT=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --built-dir) BUILT_DIR="$2"; shift 2 ;;
        --output)    OUTPUT="$2";    shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

die() { echo "package-aaa-appimage.sh: $*" >&2; exit 1; }

[ -n "$BUILT_DIR" ] || die "--built-dir is required"
[ -n "$OUTPUT" ]    || die "--output is required"
[ -f "$BUILT_DIR/VhsDecodeAutoAudioAlign.exe" ] || die "built exe missing in $BUILT_DIR"
[ -f "$BUILT_DIR/Binah.dll" ]                   || die "Binah.dll missing in $BUILT_DIR"
[ -d "$BUILT_DIR/appimage" ]                    || die "appimage assets missing in $BUILT_DIR"

MONO_BIN="$(command -v mono || true)"
[ -n "$MONO_BIN" ] || die "mono not found on PATH (install mono-core/mono-devel)"
MONO_REAL="$(readlink -f "$MONO_BIN")"
MONO_PREFIX="$(dirname "$(dirname "$MONO_REAL")")"  # /usr
[ -d "$MONO_PREFIX" ] || die "could not determine mono prefix from $MONO_BIN"

APP_NAME="vhs-decode-aaa"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

APPDIR="$WORK_DIR/AppDir"
mkdir -p \
    "$APPDIR/usr/bin" \
    "$APPDIR/usr/lib" \
    "$APPDIR/usr/aaa" \
    "$APPDIR/share/applications" \
    "$APPDIR/share/icons/hicolor/256x256"

# AppRun — matches upstream: bundled mono runs the bundled .exe.
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "${0}")")"
"$HERE/usr/bin/mono" "$HERE/usr/aaa/VhsDecodeAutoAudioAlign.exe" "$@"
EOF
chmod +x "$APPDIR/AppRun"

# Desktop + icon from the vendored appimage/ assets.
cp -f "$BUILT_DIR/appimage/vhs-decode-aaa.desktop" "$APPDIR/${APP_NAME}.desktop"
cp -f "$BUILT_DIR/appimage/vhs-decode-aaa.desktop" "$APPDIR/share/applications/${APP_NAME}.desktop"
cp -f "$BUILT_DIR/appimage/vhs-decode-aaa-256.png"  "$APPDIR/${APP_NAME}.png"
cp -f "$BUILT_DIR/appimage/vhs-decode-aaa-256.png"  "$APPDIR/share/icons/hicolor/256x256/${APP_NAME}.png"

# The built .NET assemblies.
cp -f "$BUILT_DIR/VhsDecodeAutoAudioAlign.exe" "$APPDIR/usr/aaa/"
cp -f "$BUILT_DIR/Binah.dll"                   "$APPDIR/usr/aaa/"

# Bundle the distro Mono runtime so no host Mono is needed at runtime.
# Copy the mono binary and its shared libs, then close the lib dependency
# graph transitively (same logic the main tbc-tools AppImage uses).
cp -fL "$MONO_REAL" "$APPDIR/usr/bin/mono"
# Mono's managed assemblies live under <prefix>/lib/mono; copy the tree.
if [ -d "$MONO_PREFIX/lib/mono" ]; then
    mkdir -p "$APPDIR/usr/lib/mono"
    cp -aL "$MONO_PREFIX/lib/mono/." "$APPDIR/usr/lib/mono/"
fi
# Mono's native shared libs (libmono-*.so, libMonoPosixHelper.so, ...).
for lib in \
    libmonosgen-2.0.so.1 \
    libmono-2.0.so.1 \
    libMonoPosixHelper.so \
    libikvm-native.so \
    libmono-native.so.0; do
    for candidate in \
        "$MONO_PREFIX/lib64/$lib" \
        "$MONO_PREFIX/lib/$lib" \
        "$MONO_PREFIX/lib/x86_64-linux-gnu/$lib" \
        "$MONO_PREFIX/lib/aarch64-linux-gnu/$lib"; do
        if [ -f "$candidate" ]; then
            cp -fL "$candidate" "$APPDIR/usr/lib/$lib"
            break
        fi
    done
done

# Transitive dependency closure: copy every NEEDED shared lib the bundled
# mono + mono libs reference, recursing until no new libs appear.
SKIP_LIB_REGEX='^(libc\.so\.6|libm\.so\.6|libpthread\.so\.0|libdl\.so\.2|librt\.so\.1|libresolv\.so\.2|ld-linux-.*\.so\..*|libnss_.*|libutil\.so\..*|libanl\.so\..*|libgcc_s\.so\.1|libstdc\+\+\.so\.6)$'
gather_deps() {
    local pending=1
    while [ "$pending" -eq 1 ]; do
        pending=0
        for elf in "$APPDIR"/usr/bin/* "$APPDIR"/usr/lib/*.so*; do
            [ -f "$elf" ] || continue
            for dep in $(ldd "$elf" 2>/dev/null | awk '/=> \// { print $3 }'); do
                [ -f "$dep" ] || continue
                local base
                base="$(basename "$dep")"
                if echo "$base" | grep -Eq "$SKIP_LIB_REGEX"; then
                    continue
                fi
                local dest="$APPDIR/usr/lib/$base"
                if [ ! -e "$dest" ]; then
                    cp -fL "$dep" "$dest"
                    pending=1
                fi
            done
        done
    done
}
gather_deps

# appimagetool packs AppDir into the AppImage. Download it if absent.
APPIMAGETOOL="${APPIMAGETOOL:-}"
if [ -z "$APPIMAGETOOL" ] || [ ! -x "$APPIMAGETOOL" ]; then
    APPIMAGETOOL="$WORK_DIR/appimagetool"
    PLATFORM="$(uname -m)"
    curl --fail -L --output "$APPIMAGETOOL" \
        "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${PLATFORM}.AppImage"
    chmod +x "$APPIMAGETOOL"
fi
"$APPIMAGETOOL" --version >/dev/null 2>&1 || die "appimagetool does not run"

mkdir -p "$(dirname "$OUTPUT")"
ARCH="$(uname -m)" APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGETOOL" "$APPDIR" "$OUTPUT" \
    || die "appimagetool failed to package $OUTPUT"

# Smoke test: the AppImage must run without host mono (it bundles mono).
APPIMAGE_EXTRACT_AND_RUN=1 "$OUTPUT" show-build-info 2>&1 | grep -q -i "audio" \
    || die "packaged AppImage did not run (show-build-info) — host mono missing?"

echo "Packaged $OUTPUT"
