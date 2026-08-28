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
# Self-contained Mono: the bundled mono relocates its libdir relative to this
# binary, so point it at the bundled dllmap config (MONO_CFG_DIR) and put the
# bundled native libs on the loader path. Without these the AppImage silently
# falls back to the host's /etc/mono + /usr/lib — the "needs host Mono"
# failure this AppImage exists to fix.
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export MONO_CFG_DIR="$HERE/etc"
exec "$HERE/usr/bin/mono" "$HERE/usr/aaa/VhsDecodeAutoAudioAlign.exe" "$@"
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

# Bundle Mono's dllmap config (etc/mono/config) so the bundled mono can
# resolve P/Invokes such as System.Native -> libmono-native.so without a host
# /etc/mono. It lives at /etc/mono on distro Mono installs (and under
# <prefix>/etc/mono on custom-prefix builds); copy whichever exists.
MONO_CFG_SRC=""
for cfg in "$MONO_PREFIX/etc/mono" "/etc/mono"; do
    [ -d "$cfg" ] && MONO_CFG_SRC="$cfg" && break
done
if [ -n "$MONO_CFG_SRC" ]; then
    mkdir -p "$APPDIR/etc"
    cp -aL "$MONO_CFG_SRC" "$APPDIR/etc/mono"
fi

# The Mono dllmap targets the unversioned libmono-native.so, but distros ship
# only the versioned libmono-native.so.0. Create the unversioned symlink inside
# the bundle so the dllmap resolves against the bundled lib (not the host's).
if [ -f "$APPDIR/usr/lib/libmono-native.so.0" ] && [ ! -e "$APPDIR/usr/lib/libmono-native.so" ]; then
    ln -s libmono-native.so.0 "$APPDIR/usr/lib/libmono-native.so"
fi

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

# Harden the bundled Mono runtime: set RPATH on the bundled mono binary +
# native libs so the dynamic loader finds them even if LD_LIBRARY_PATH is
# stripped by a launcher. AppRun's export is the primary mechanism; this is
# belt-and-suspenders so the AppImage stays self-contained. Skipped silently
# where patchelf is unavailable.
PATCH_ELF_BIN="$(command -v patchelf || true)"
if [ -n "$PATCH_ELF_BIN" ]; then
    for elf in "$APPDIR"/usr/bin/mono "$APPDIR"/usr/lib/*.so*; do
        [ -f "$elf" ] || continue
        "$PATCH_ELF_BIN" --set-rpath '$ORIGIN:$ORIGIN/../lib' "$elf" 2>/dev/null || true
    done
fi

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
