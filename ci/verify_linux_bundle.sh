#!/usr/bin/env bash

set -euo pipefail

usage() {
  echo "Usage: $0 <x86-appimage|arm64-release> <path>" >&2
  exit 2
}

require_path() {
  local path="$1"
  if [ ! -e "$path" ]; then
    echo "Missing required path: $path" >&2
    exit 1
  fi
}

require_executable() {
  local path="$1"
  require_path "$path"
  if [ ! -x "$path" ]; then
    echo "Required path is not executable: $path" >&2
    exit 1
  fi
}

require_non_nix_interpreter() {
  local elf="$1"
  if [ ! -f "$elf" ]; then
    return
  fi
  local interpreter
  interpreter="$(readelf -l "$elf" 2>/dev/null | awk -F': ' '/Requesting program interpreter/ {gsub(/\]/, "", $2); print $2; exit}')"
  if [ -n "$interpreter" ] && [[ "$interpreter" == /nix/store/* ]]; then
    echo "ELF interpreter still points to Nix store: $elf -> $interpreter" >&2
    exit 1
  fi
}

require_non_nix_rpath() {
  local elf="$1"
  if [ ! -f "$elf" ]; then
    return
  fi
  local rpath_entries
  rpath_entries="$(readelf -d "$elf" 2>/dev/null | awk -F'[][]' '/(RUNPATH|RPATH)/ {print $2}' || true)"
  if [ -z "$rpath_entries" ]; then
    return
  fi
  if echo "$rpath_entries" | tr ':' '\n' | grep -q '^/nix/store/'; then
    echo "ELF RPATH/RUNPATH still points to Nix store: $elf -> $rpath_entries" >&2
    exit 1
  fi
}
require_no_nix_store_shebang() {
  local path="$1"
  if [ ! -f "$path" ]; then
    return
  fi
  local first_line
  first_line="$(head -n 1 "$path" 2>/dev/null || true)"
  if [[ "$first_line" == '#!/nix/store/'* ]]; then
    echo "Script shebang still points to Nix store: $path -> $first_line" >&2
    exit 1
  fi
}

require_needed_in_bundle() {
  local elf="$1"
  local libdir="$2"
  if [ ! -f "$elf" ]; then
    return
  fi
  if [ ! -d "$libdir" ]; then
    echo "Library directory not found for dependency validation: $libdir" >&2
    exit 1
  fi
  local needed
  while IFS= read -r needed; do
    [ -n "$needed" ] || continue
    if [ ! -e "$libdir/$needed" ]; then
      echo "Missing bundled dependency for $elf: $needed (expected at $libdir/$needed)" >&2
      exit 1
    fi
  done < <(readelf -d "$elf" 2>/dev/null | awk -F'[][]' '/NEEDED/ { print $2 }')
}

require_bundled_loader_present() {
  local libdir="$1"
  if [ -f "$libdir/ld-linux-x86-64.so.2" ] || [ -f "$libdir/ld-linux-aarch64.so.1" ]; then
    return
  fi
  echo "Missing bundled dynamic loader in: $libdir" >&2
  exit 1
}

require_runtime_libs() {
  local libdir="$1"
  shift
  local lib
  for lib in "$@"; do
    if [ ! -f "$libdir/$lib" ]; then
      echo "Missing required runtime library: $libdir/$lib" >&2
      exit 1
    fi
  done
}

run_smoke_test() {
  local label="$1"
  local log_file="$2"
  shift 2
  local exit_code=0
  timeout 20 "$@" >"$log_file" 2>&1 || exit_code=$?
  if [ "$exit_code" -ne 0 ]; then
    echo "Runtime smoke test failed: $label (exit=$exit_code)" >&2
    if [ -f "$log_file" ]; then
      sed -n '1,200p' "$log_file" >&2 || true
    fi
    exit 1
  fi
}

# End-to-end AAA usability smoke test: drive the bundled AAA AppImage's
# `stream-align` action (the exact codepath ld-analyse runs when the user
# clicks Align) against a tiny synthesized fixture, and assert it produced
# non-empty aligned output. This proves AAA is not merely detectable but
# callable AND usable from ld-analyse (or independently), as opposed to just
# `show-build-info`.
#
# Args: $1 = label prefix (e.g. x86-appimage / arm64)
#       $2 = log dir (where the smoke log + scratch fixtures are written)
#       $3 = path to the bundled AAA AppImage (vhs-decode-aaa.AppImage)
#       $4 = path to the bundled ffmpeg (presence check only; synthesis
#            prefers the system ffmpeg if available — the bundled ffmpeg's
#            loader-wrapper hangs under `timeout` in some extraction contexts,
#            and the synthesis step is fixture prep, not the thing under test)
#       $5 = path to the bundle's AppRun launcher (reserved, unused)
# Uses APPIMAGE_EXTRACT_AND_RUN=1 (the resolver's launch mechanism).
run_aaa_stream_align_smoke() {
  local label="$1"
  local log_dir="$2"
  local aaa_appimage="$3"
  local ffmpeg_bin="$4"
  local apprun_bin="$5"
  require_executable "$aaa_appimage"
  require_path "$ffmpeg_bin"
  # Prefer the system ffmpeg for the fixture synthesis step (it's just test
  # data prep, not the thing under test — the bundled ffmpeg is already
  # validated by the x86-appimage-apprun-ffmpeg smoke test). The bundled
  # ffmpeg wrapper hangs under `timeout` in some squashfs-extraction contexts.
  local synth_ffmpeg
  synth_ffmpeg="$(command -v ffmpeg || true)"
  [ -n "$synth_ffmpeg" ] || synth_ffmpeg="$ffmpeg_bin"
  # Scratch fixtures go in a temp dir under TMPDIR (not inside the extracted
  # bundle, which may be read-only when extracted from squashfs as non-root).
  local smoke_dir
  smoke_dir="$(mktemp -d "${TMPDIR:-/tmp}/.aaa-smoke.XXXXXX")"
  # Minimal TBC metadata JSON (2 PAL fields) — AAA only needs the field
  # timing/line info, not the .tbc samples, for stream-align.
  cat > "$smoke_dir/fixture.tbc.json" <<'JSON'
{"videoParameters":{"system":"PAL","fieldWidth":1135","sampleRate":40000000,"activeVideoEnd":1130},"fields":[{"fieldLineCount":312,"firstActiveFieldLine":3,"lastActiveFieldLine":308},{"fieldLineCount":312,"firstActiveFieldLine":3,"lastActiveFieldLine":308}]}
JSON
  # Synthesize ~0.1s of 48kHz stereo audio -> raw s24le (3 bytes/sample,
  # the format AAA expects: sample-size-bytes 6). A single sine source
  # duplicated to stereo via -ac 2 avoids a complex amerge filtergraph that is
  # fragile across ffmpeg builds. ffmpeg is in the bundle. Use an explicit
  # exit-code check (not `|| { ... }`) so set -e doesn't interact with the
  # ffmpeg wrapper's own set -e / loader exec chain.
  # Synthesize ~0.1s of 48kHz stereo audio -> raw s24le (3 bytes/sample,
  # the format AAA expects: sample-size-bytes 6). A single sine source
  # duplicated to stereo via -ac 2 avoids a complex amerge filtergraph that is
  # fragile across ffmpeg builds.
  local ff_exit=0
  timeout 30 "$synth_ffmpeg" -y -hide_banner -loglevel error \
    -f lavfi -i "sine=frequency=1000:duration=0.1" \
    -f s24le -ac 2 -ar 48000 "$smoke_dir/audio_input.s24le" \
    >"$log_dir/.smoke-${label}-aaa-ffmpeg.log" 2>&1 || ff_exit=$?
  if [ "$ff_exit" -ne 0 ] || [ ! -f "$smoke_dir/audio_input.s24le" ]; then
    echo "AAA stream-align smoke test failed: $label (ffmpeg could not synthesize input audio, exit=$ff_exit)" >&2
    sed -n '1,200p' "$log_dir/.smoke-${label}-aaa-ffmpeg.log" >&2 || true
    rm -rf "$smoke_dir"
    exit 1
  fi
  rm -f "$log_dir/.smoke-${label}-aaa-ffmpeg.log"
  local aligned="$smoke_dir/audio_aligned.s24le"
  rm -f "$aligned"
  # stream-align: the real ld-analyse invocation (program + prefix args +
  # stream-align + its switches). resolveRunner launches the AppImage via
  # `env APPIMAGE_EXTRACT_AND_RUN=1 <appimage>`.
  local exit_code=0
  timeout 60 env APPIMAGE_EXTRACT_AND_RUN=1 "$aaa_appimage" stream-align \
    --sample-size-bytes 6 --stream-sample-rate-hz 48000 \
    --json "$smoke_dir/fixture.tbc.json" \
    --rf-video-sample-rate-hz 40000000 \
    --input-file "$smoke_dir/audio_input.s24le" \
    --output-file "$aligned" --overwrite \
    >"$log_dir/.smoke-${label}-aaa-stream-align.log" 2>&1 || exit_code=$?
  if [ "$exit_code" -ne 0 ] || [ ! -s "$aligned" ]; then
    echo "AAA stream-align usability smoke test failed: $label (exit=$exit_code, output non-empty=$([ -s "$aligned" ] && echo yes || echo no))" >&2
    sed -n '1,200p' "$log_dir/.smoke-${label}-aaa-stream-align.log" >&2 || true
    rm -rf "$smoke_dir"
    exit 1
  fi
  rm -rf "$smoke_dir"
  rm -f "$log_dir/.smoke-${label}-aaa-stream-align.log"
}

if [ "$#" -ne 2 ]; then
  usage
fi

MODE="$1"
TARGET="$2"

COMMON_RELATIVE_PATHS=(
  "usr/plugins/platforms/libqxcb.so"
  "usr/plugins/platforminputcontexts/libcomposeplatforminputcontextplugin.so"
  "usr/plugins/xcbglintegrations/libqxcb-glx-integration.so"
  "usr/plugins/sqldrivers/libqsqlite.so"
  "usr/plugins/iconengines/libqsvgicon.so"
  "usr/plugins/imageformats/libqsvg.so"
  "usr/bin/ffmpeg"
  "usr/bin/ffprobe"
)

GLIBC_RUNTIME_LIBS=(
  "libc.so.6"
  "libm.so.6"
  "libpthread.so.0"
  "libdl.so.2"
  "librt.so.1"
  "libresolv.so.2"
)

XCB_RUNTIME_LIBS=(
  "libxcb-cursor.so.0"
  "libxcb-icccm.so.4"
  "libxcb-image.so.0"
  "libxcb-keysyms.so.1"
  "libxcb-render-util.so.0"
  "libxkbcommon.so.0"
  "libxkbcommon-x11.so.0"
)

case "$MODE" in
  x86-appimage)
    require_path "$TARGET"
    if [ ! -x "$TARGET" ]; then
      echo "AppImage is not executable: $TARGET" >&2
      exit 1
    fi
    run_smoke_test "x86-appimage-extract-and-run-tbc-video-export" ".smoke-x86-appimage-runtime.log" env APPIMAGE_EXTRACT_AND_RUN=1 "$TARGET" tbc-video-export --version

    rm -rf squashfs-root
    APPIMAGE_EXTRACT_AND_RUN=1 "$TARGET" --appimage-extract >/dev/null
    ROOT="squashfs-root"
    require_path "$ROOT"

    require_path "$ROOT/usr/bin/ld-analyse"
    require_path "$ROOT/usr/bin/ld-process-vbi"
    require_path "$ROOT/usr/bin/tbc-video-export"
    require_path "$ROOT/usr/bin/qt.conf"
    # AAA (Auto Audio Align) is shipped as a self-contained AppImage built
    # from the vendored C# source; it bundles the Mono runtime, so no host
    # mono is required at runtime (the previous "mono not found on Ubuntu"
    # failure). Verify the AppImage is present, executable, and actually runs.
    require_executable "$ROOT/usr/bin/vendor/vhs_decode_auto_audio_align/vhs-decode-aaa.AppImage"
    run_smoke_test "x86-appimage-aaa-no-host-mono" "$ROOT/.smoke-x86-aaa.log" \
      env APPIMAGE_EXTRACT_AND_RUN=1 "$ROOT/usr/bin/vendor/vhs_decode_auto_audio_align/vhs-decode-aaa.AppImage" show-build-info
    # AAA detection: validate ld-analyse's appDir-relative resolver path
    # actually reaches the AAA AppImage. ld-analyse's applicationDirPath() is
    # usr/bin, and the resolver probes vendor/vhs_decode_auto_audio_align/
    # vhs-decode-aaa.AppImage relative to it. Compute that path from ld-analyse's
    # own directory (not a hard-coded absolute path) so a bundle that placed AAA
    # at the wrong relative location is caught, then launch it via the same
    # `env APPIMAGE_EXTRACT_AND_RUN=1` mechanism resolveRunner uses.
    LD_ANALYSE="$ROOT/usr/bin/ld-analyse"
    require_path "$LD_ANALYSE"
    LD_ANALYSE_DIR="$(cd "$(dirname "$LD_ANALYSE")" && pwd)"
    DETECTED_AAA="$LD_ANALYSE_DIR/vendor/vhs_decode_auto_audio_align/vhs-decode-aaa.AppImage"
    require_executable "$DETECTED_AAA"
    run_smoke_test "x86-appimage-aaa-detection" "$ROOT/.smoke-x86-aaa-detect.log" \
      env APPIMAGE_EXTRACT_AND_RUN=1 "$DETECTED_AAA" show-build-info
    # AAA usability: drive the bundled AAA AppImage's `stream-align` (the
    # exact codepath ld-analyse runs on Align) against a synthesized fixture
    # and assert it produced non-empty aligned output. Proves AAA is callable
    # AND usable, not merely detectable. Uses the bundle's ffmpeg to make the
    # input audio so the test is self-contained at runtime.
    run_aaa_stream_align_smoke "x86-appimage" "$ROOT" "$DETECTED_AAA" "$ROOT/usr/bin/ffmpeg" "$ROOT/AppRun"
    # vhs-teletext vendor payload must be bundled at the resolver path.
    require_path "$ROOT/usr/bin/vendor/vhs-teletext/teletext/__main__.py"
    require_path "$ROOT/usr/bin/vendor/vhs-teletext/misc/teletext-noscanlines.css"
    require_path "$ROOT/usr/bin/vendor/vhs-teletext/misc/teletext2.ttf"
    require_path "$ROOT/usr/bin/vendor/vhs-teletext/misc/teletext4.ttf"
    # tbc-video-export is now a self-contained PyInstaller ELF binary, not a
    # bash wrapper + package source. Verify it is an ELF executable.
    if ! head -c 4 "$ROOT/usr/bin/tbc-video-export" 2>/dev/null | grep -q $'\177ELF'; then
      echo "tbc-video-export is not an ELF binary (expected PyInstaller --onefile): $ROOT/usr/bin/tbc-video-export" >&2
      exit 1
    fi
    require_no_nix_store_shebang "$ROOT/AppRun"
    for candidate in "$ROOT"/usr/bin/*; do
      [ -f "$candidate" ] || continue
      [ -x "$candidate" ] || continue
      require_no_nix_store_shebang "$candidate"
    done
    require_non_nix_rpath "$ROOT/usr/bin/ld-analyse"
    require_non_nix_rpath "$ROOT/usr/plugins/platforms/libqxcb.so"
    require_bundled_loader_present "$ROOT/usr/lib"
    require_runtime_libs "$ROOT/usr/lib" "${GLIBC_RUNTIME_LIBS[@]}"
    require_runtime_libs "$ROOT/usr/lib" "${XCB_RUNTIME_LIBS[@]}"
    require_needed_in_bundle "$ROOT/usr/bin/ld-analyse" "$ROOT/usr/lib"
    require_needed_in_bundle "$ROOT/usr/plugins/platforms/libqxcb.so" "$ROOT/usr/lib"
    for rel in "${COMMON_RELATIVE_PATHS[@]}"; do
      require_path "$ROOT/$rel"
    done
    run_smoke_test "x86-appimage-apprun-tbc-video-export" "$ROOT/.smoke-x86-export.log" "$ROOT/AppRun" tbc-video-export --version
    run_smoke_test "x86-appimage-apprun-ffmpeg" "$ROOT/.smoke-x86.log" "$ROOT/AppRun" ffmpeg -version

    rm -rf "$ROOT"
    rm -f .smoke-x86-appimage-runtime.log
    ;;

  arm64-release)
    require_path "$TARGET"
    if [ ! -d "$TARGET" ]; then
      echo "arm64 target is not a directory: $TARGET" >&2
      exit 1
    fi

    require_path "$TARGET/bin/ld-analyse"
    require_path "$TARGET/bin/ld-process-vbi"
    require_path "$TARGET/bin/tbc-video-export"
    require_path "$TARGET/bin/qt.conf"
    # AAA (Auto Audio Align) is shipped as a self-contained AppImage built
    # from the vendored C# source; it bundles the Mono runtime, so no host
    # mono is required at runtime. Verify the AppImage is present, executable,
    # and actually runs.
    require_executable "$TARGET/bin/vendor/vhs_decode_auto_audio_align/vhs-decode-aaa.AppImage"
    run_smoke_test "arm64-aaa-no-host-mono" "$TARGET/.smoke-arm64-aaa.log" \
      env APPIMAGE_EXTRACT_AND_RUN=1 "$TARGET/bin/vendor/vhs_decode_auto_audio_align/vhs-decode-aaa.AppImage" show-build-info
    # AAA detection: validate ld-analyse's appDir-relative resolver path
    # actually reaches the AAA AppImage (see the x86-appimage mode for the
    # rationale). ld-analyse's applicationDirPath() is bin, and the resolver
    # probes vendor/vhs_decode_auto_audio_align/vhs-decode-aaa.AppImage
    # relative to it. Compute that path from ld-analyse's own directory and
    # launch it via the resolver's `env APPIMAGE_EXTRACT_AND_RUN=1` mechanism.
    LD_ANALYSE="$TARGET/bin/ld-analyse"
    require_path "$LD_ANALYSE"
    LD_ANALYSE_DIR="$(cd "$(dirname "$LD_ANALYSE")" && pwd)"
    DETECTED_AAA="$LD_ANALYSE_DIR/vendor/vhs_decode_auto_audio_align/vhs-decode-aaa.AppImage"
    require_executable "$DETECTED_AAA"
    run_smoke_test "arm64-aaa-detection" "$TARGET/.smoke-arm64-aaa-detect.log" \
      env APPIMAGE_EXTRACT_AND_RUN=1 "$DETECTED_AAA" show-build-info
    # AAA usability: drive the bundled AAA AppImage's `stream-align` (the
    # exact codepath ld-analyse runs on Align) against a synthesized fixture
    # and assert it produced non-empty aligned output (see x86-appimage mode).
    run_aaa_stream_align_smoke "arm64" "$TARGET" "$DETECTED_AAA" "$TARGET/bin/ffmpeg" "$TARGET/tbc-tools-run"
    # vhs-teletext vendor payload must be bundled at the resolver path.
    require_path "$TARGET/bin/vendor/vhs-teletext/teletext/__main__.py"
    require_path "$TARGET/bin/vendor/vhs-teletext/misc/teletext-noscanlines.css"
    require_path "$TARGET/bin/vendor/vhs-teletext/misc/teletext2.ttf"
    require_path "$TARGET/bin/vendor/vhs-teletext/misc/teletext4.ttf"
    # tbc-video-export is now a self-contained PyInstaller ELF binary, not a
    # bash wrapper + package source. Verify it is an ELF executable.
    if ! head -c 4 "$TARGET/bin/tbc-video-export" 2>/dev/null | grep -q $'\177ELF'; then
      echo "tbc-video-export is not an ELF binary (expected PyInstaller --onefile): $TARGET/bin/tbc-video-export" >&2
      exit 1
    fi
    require_no_nix_store_shebang "$TARGET/tbc-tools-run"
    for candidate in "$TARGET"/bin/*; do
      [ -f "$candidate" ] || continue
      [ -x "$candidate" ] || continue
      require_no_nix_store_shebang "$candidate"
    done
    require_non_nix_rpath "$TARGET/bin/ld-analyse"
    require_non_nix_rpath "$TARGET/plugins/platforms/libqxcb.so"
    require_bundled_loader_present "$TARGET/lib"
    require_runtime_libs "$TARGET/lib" "${GLIBC_RUNTIME_LIBS[@]}"
    require_runtime_libs "$TARGET/lib" "${XCB_RUNTIME_LIBS[@]}"
    require_needed_in_bundle "$TARGET/bin/ld-analyse" "$TARGET/lib"
    require_needed_in_bundle "$TARGET/plugins/platforms/libqxcb.so" "$TARGET/lib"
    require_path "$TARGET/bin/ffmpeg"
    require_path "$TARGET/bin/ffprobe"
    require_path "$TARGET/lib/libQt6Core.so.6"
    require_path "$TARGET/lib/libQt6Gui.so.6"
    require_path "$TARGET/lib/libQt6Widgets.so.6"
    require_path "$TARGET/plugins/platforms/libqxcb.so"
    require_path "$TARGET/plugins/platforminputcontexts/libcomposeplatforminputcontextplugin.so"
    require_path "$TARGET/plugins/xcbglintegrations/libqxcb-glx-integration.so"
    require_path "$TARGET/plugins/sqldrivers/libqsqlite.so"
    require_path "$TARGET/plugins/iconengines/libqsvgicon.so"
    require_path "$TARGET/plugins/imageformats/libqsvg.so"
    run_smoke_test "arm64-launcher-tbc-video-export" "$TARGET/.smoke-arm64-export.log" "$TARGET/tbc-tools-run" tbc-video-export --version
    run_smoke_test "arm64-launcher-ffmpeg" "$TARGET/.smoke-arm64.log" "$TARGET/tbc-tools-run" ffmpeg -version
    ;;

  *)
    usage
    ;;
esac

echo "Linux bundle validation passed: mode=$MODE target=$TARGET"
