#!/usr/bin/env bash
# scripts/cuda-plugin-package.sh
#
# Produce the opt-in CUDA runtime plugin packages (Linux + Windows) for
# nnTransform3D GPU acceleration via the ONNX Runtime CUDA execution provider.
#
# Each platform package contains:
#   - The ORT CUDA EP provider library (libonnxruntime_providers_cuda.so / .dll)
#   - The CUDA 11.8 runtime libs (cudart, cublas, cublasLt, cufft, curand)
#   - The cuDNN 8.9 inference libs (cudnn, cudnn_cnn_infer, cudnn_ops_infer)
#     -- cudnn_adv_infer is DROPPED (unused by the conv-only chroma_net_v2.onnx model)
#   - NVIDIA License.txt files
#
# A manifest JSON (tbc-cuda-plugin-<platform>-<arch>-manifest.json) is produced
# alongside each package, declaring plugin_version + per-file SHA-256 digests.
#
# The packages + manifests are published as GitHub Release assets on
# harrypm/tbc-tools-ci-cache under a cuda-plugin-vX tag (see the publish CI job).
# The GUI plugin manager (cudapluginmanager.cpp) downloads + SHA-256-verifies them.
#
# Sources:
#   - NVIDIA redistributable pip wheels (nvidia-*-cu11) — public on PyPI, no login
#   - ONNX Runtime GPU (immutable nuget.org packages) — for the provider lib
#
# Modes:
#   build-linux   [--out DIR] [--version VER] [--deps-dir DIR]
#       produce the Linux x86_64 tar.gz + manifest. --deps-dir is the output of
#       `nix build .#cuda-plugin-linux-deps` (CUDA 11.8 runtime + cuDNN 8.9 .so
#       files staged from the Nix store). Required for build-linux.
#   build-windows [--out DIR] [--version VER]  produce the Windows x86_64 zip + manifest
#   build-all     [--out DIR] [--version VER] [--deps-dir DIR]  produce both
#   build-cuda-lite-linux   [--out DIR] [--version VER]
#       produce the Linux x86_64 teletext GPU (lite) plugin tar.gz + manifest:
#       pyopencl wheels (bundled ICD loader) for system Pythons cp310-cp313 +
#       the small pure-Python deps. numpy is NOT bundled: tbc-process-vbi's
#       teletext dependency probe already requires it. The OpenCL ICD
#       (/etc/OpenCL/vendors) comes from the user's GPU driver.
#   build-cuda-lite-windows [--out DIR] [--version VER]
#       produce the Windows x86_64 teletext GPU (lite) plugin zip + manifest:
#       Python embeddable distribution (._pth removed so PYTHONPATH works) +
#       the pyopencl win_amd64 wheel closure (numpy included).
#   build-cuda-lite-all     [--out DIR] [--version VER]  produce both lite packages
#
# Defaults:
#   --out     ./cuda-plugin-out
#   --version 1.0.0  (the plugin_version recorded in the manifest)
#
# Requires: bash, curl, unzip, python3 (JSON + sha256), tar, zip
set -euo pipefail

OUT_DEFAULT="${PWD}/cuda-plugin-out"
VERSION_DEFAULT="1.0.0"
ORT_VERSION="1.18.1"
# pyopencl ships a bundled OpenCL ICD loader in its manylinux/win wheels
# (.libs/libOpenCL-*.so), so the plugin needs no system libOpenCL and no
# LD_LIBRARY_PATH wiring -- only an ICD from the user's GPU driver.
PYOPENCL_VERSION="2026.1.4"
# Python minor versions covered by the Linux site-packages wheel set (the
# pyopencl manylinux wheels are interpreter-version specific; the C-side ABI
# tagged .so files of all versions coexist in one site-packages tree).
LITE_PYTHONS=("3.10" "3.11" "3.12" "3.13")
# Python version of the Windows embeddable distribution.
LITE_WIN_PYTHON="3.13"
LITE_WIN_PYTHON_BUILD="3.13.9"
# Dependency wheel pins for the lite packages. Every download uses --no-deps
# with these exact versions so the published package's SHA-256 manifest is
# deterministic and matches plugins/catalog.json regardless of when/where the
# package is built (PyPI wheels are immutable).
LITE_PYTOOLS_VERSION="2026.1.1"
LITE_PLATFORMDIRS_VERSION="4.12.2"
LITE_TYPING_EXTENSIONS_VERSION="4.16.0"
# Compiled pytools dependency (siphash24 is imported as the recommended hash
# with a graceful hashlib fallback; declared as a hard dep in pytools METADATA)
# -- interpreter-version specific wheels, fetched per Python version below.
LITE_SIPHASH24_VERSION="1.9"
# Windows embeddable runtime has no site packages of its own, so numpy is
# bundled into the Windows package (the Linux package relies on the system
# numpy required by tbc-process-vbi's teletext dependency probe).
LITE_NUMPY_VERSION="2.5.3"

# Pinned NVIDIA redistributable wheel versions (same as windows-cuda-runtime.sh).
# cuDNN 8.9.5 is the latest 8.x (required by ORT 1.18.x CUDA-11.x; 9.x is ABI-incompatible).
declare -a PKGS=(
  "nvidia-cuda-runtime-cu11|11.8.89"
  "nvidia-cublas-cu11|11.11.3.6"
  "nvidia-cufft-cu11|10.9.0.58"
  "nvidia-curand-cu11|10.3.0.86"
  "nvidia-cudnn-cu11|8.9.5.29"
)

die() { printf 'cuda-plugin-package: error: %s\n' "$*" >&2; exit 1; }
say() { printf 'cuda-plugin-package: %s\n' "$*"; }
abs() { case "$1" in /*) printf '%s' "$1" ;; *) printf '%s/%s' "$PWD" "$1" ;; esac; }

# Resolve a wheel download URL + filename for a pinned package+version via the
# PyPI JSON API, optionally filtering by platform tag. Prints "<url> <filename>".
wheel_url() {
  local pkg="$1" ver="$2" platform_filter="$3"
  local json
  json="$(curl -fsSL "https://pypi.org/pypi/${pkg}/${ver}/json" || true)"
  [ -n "$json" ] || die "could not fetch PyPI JSON for ${pkg}==${ver}"
  printf '%s' "$json" | python3 -c "
import sys, json
d = json.load(sys.stdin)
hit = None
for f in d.get('urls', []):
    fn = f['filename']
    if not (fn.endswith('.whl') and 'py3-none' in fn):
        continue
    if '${platform_filter}' and '${platform_filter}' not in fn:
        continue
    hit = f
    break
if hit is None:
    raise SystemExit('no ${platform_filter} wheel for ${pkg}==${ver}')
print(hit['url'], hit['filename'])
"
}

# Download + extract a specific file from a wheel into a target directory.
extract_from_wheel() {
  local pkg="$1" ver="$2" platform_filter="$3" src_path="$4" dest_dir="$5" tmp="$6"
  local url fn wheel
  read -r url fn <<< "$(wheel_url "$pkg" "$ver" "$platform_filter")"
  wheel="$tmp/$fn"
  say "  downloading $fn"
  curl -fsSL -o "$wheel" "$url"
  local base="${src_path##*/}"
  unzip -p "$wheel" "$src_path" > "$dest_dir/$base"
  printf '%s' "$base"
}

# Compute SHA-256 of a file.
sha256_of() { sha256sum "$1" | awk '{print $1}'; }

# Find the License.txt dist-info path in a wheel and copy it out.
copy_license() {
  local wheel="$1" dest="$2" licname="$3"
  local licpath
  licpath="$(unzip -l "$wheel" 2>/dev/null | awk '{print $4}' | grep -E '\.dist-info/License\.txt$' | head -1)"
  if [ -n "$licpath" ]; then
    unzip -p "$wheel" "$licpath" > "$dest/$licname"
  fi
}

# Append a file entry to the manifest JSON array (streaming, no jq dependency).
manifest_add_file() {
  local manifest="$1" name="$2" sha="$3" size="$4" first="$5"
  if [ "$first" -eq 1 ]; then
    printf '  "files": [\n' >> "$manifest"
  else
    printf ',\n' >> "$manifest"
  fi
  printf '    {"name": "%s", "sha256": "%s", "size": %s}' "$name" "$sha" "$size" >> "$manifest"
}

# Build the Linux x86_64 plugin package.
# $1=out dir, $2=version, $3=deps-dir (Nix store .so staging from .#cuda-plugin-linux-deps)
build_linux() {
  local out version deps_dir
  out="$(abs "$1")"; version="$2"; deps_dir="$3"
  [ -n "$deps_dir" ] || die "build-linux requires --deps-dir (the output of `nix build .#cuda-plugin-linux-deps`)"
  [ -d "$deps_dir" ] || die "deps-dir not found: $deps_dir"

  local pkgdir="$out/linux-x86_64" manifest="$out/tbc-cuda-plugin-linux-x86_64-manifest.json"
  local tmp; tmp="$(mktemp -d)"
  mkdir -p "$pkgdir"

  say "building Linux x86_64 plugin package (version $version) from deps: $deps_dir"
  : > "$manifest"
  printf '{\n  "plugin_id": "tbc-tools.cuda-runtime",\n  "plugin_version": "%s",\n  "platform": "linux",\n  "arch": "x86_64",\n  "ort_version": "%s",\n  "cuda_version": "11.8",\n  "cudnn_version": "8.9",\n' "$version" "$ORT_VERSION" >> "$manifest"

  local first=1
  # Copy the CUDA runtime + cuDNN .so files from the Nix-store deps dir.
  local so_files=("libcudart.so.11.0" "libcublas.so.11" "libcublasLt.so.11" "libcufft.so.10"
                  "libcurand.so.10"
                  "libcudnn.so.8" "libcudnn_cnn_infer.so.8" "libcudnn_ops_infer.so.8")
  for so in "${so_files[@]}"; do
    [ -f "$deps_dir/$so" ] || die "missing .so in deps-dir: $so"
    cp -aL "$deps_dir/$so" "$pkgdir/$so"
    local sha size; sha="$(sha256_of "$pkgdir/$so")"; size="$(stat -c '%s' "$pkgdir/$so")"
    manifest_add_file "$manifest" "$so" "$sha" "$size" "$first"; first=0
    say "  staged $so ($size bytes)"
  done

  # ORT CUDA EP provider .so from the immutable nuget.org package (the
  # onnxruntime GitHub releases were purged by Microsoft on 2026-11-20).
  say "  fetching ONNX Runtime GPU nuget package (v$ORT_VERSION) for libonnxruntime_providers_cuda.so"
  local ort_nupkg="$tmp/ort-gpu-linux.nupkg.zip"
  curl -fsSL -o "$ort_nupkg" "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.Gpu.Linux/$ORT_VERSION"
  unzip -j "$ort_nupkg" "runtimes/linux-x64/native/libonnxruntime_providers_cuda.so" -d "$pkgdir/"

  # Remove BIND_NOW from the provider .so. The provider .so references
  # Provider_GetHost, a hidden symbol in the main libonnxruntime.so that
  # can only be resolved through ORT's internal provider bridge (not
  # standard dynamic linking). With BIND_NOW, dlopen(RTLD_NOW) forces
  # immediate resolution of .rela.plt entries (including Provider_GetHost)
  # and fails. Removing BIND_NOW allows .rela.plt entries to be deferred
  # (even with RTLD_NOW), so ORT's bridge resolves them when the provider
  # is actually initialized. patchelf doesn't support --remove-bindnow in
  # all versions, so patch the ELF DT_FLAGS directly with Python.
  python3 -c '
import struct, sys
path = sys.argv[1]
with open(path, "rb") as f: data = bytearray(f.read())
e_phoff = struct.unpack_from("<Q", data, 0x20)[0]
e_phentsize = struct.unpack_from("<H", data, 0x36)[0]
e_phnum = struct.unpack_from("<H", data, 0x38)[0]
DT_FLAGS = 30; DT_FLAGS_1 = 0x6ffffffb; DF_BIND_NOW = 0x8; DF_1_NOW = 0x1
for i in range(e_phnum):
    ph_off = e_phoff + i * e_phentsize
    if struct.unpack_from("<I", data, ph_off)[0] != 2: continue
    p_off = struct.unpack_from("<Q", data, ph_off + 8)[0]
    p_fs = struct.unpack_from("<Q", data, ph_off + 0x20)[0]
    for j in range(0, p_fs, 16):
        tag = struct.unpack_from("<q", data, p_off + j)[0]
        val_off = p_off + j + 8
        if tag == DT_FLAGS:
            v = struct.unpack_from("<Q", data, val_off)[0]
            if v & DF_BIND_NOW: struct.pack_into("<Q", data, val_off, v & ~DF_BIND_NOW)
        elif tag == DT_FLAGS_1:
            v = struct.unpack_from("<Q", data, val_off)[0]
            if v & DF_1_NOW: struct.pack_into("<Q", data, val_off, v & ~DF_1_NOW)
        elif tag == 0: break
with open(path, "wb") as f: f.write(data)
' "$pkgdir/libonnxruntime_providers_cuda.so"
  say "  patched BIND_NOW from libonnxruntime_providers_cuda.so"

  local psha psize; psha="$(sha256_of "$pkgdir/libonnxruntime_providers_cuda.so")"; psize="$(stat -c '%s' "$pkgdir/libonnxruntime_providers_cuda.so")"
  manifest_add_file "$manifest" "libonnxruntime_providers_cuda.so" "$psha" "$psize" "$first"; first=0

  printf '\n  ]\n}\n' >> "$manifest"

  # Create the tar.gz package. Archive the files flat (no linux-x86_64/
  # prefix) so the manager's `tar -xf -C <installDir` lands them directly in
  # the install dir root, matching the manifest's flat filenames.
  local archive="$out/tbc-tools-cuda-plugin-linux-x86_64.tar.gz"
  tar -czf "$archive" -C "$pkgdir" .
  say "Linux package: $archive ($(du -h "$archive" | awk '{print $1}'))"
  say "Linux manifest: $manifest"
  rm -rf "$tmp"
}

# Build the Windows x86_64 plugin package.
build_windows() {
  local out version
  out="$(abs "$1")"; version="$2"
  local pkgdir="$out/windows-x86_64" manifest="$out/tbc-cuda-plugin-windows-x86_64-manifest.json"
  local tmp; tmp="$(mktemp -d)"
  mkdir -p "$pkgdir"

  say "building Windows x86_64 plugin package (version $version)"
  : > "$manifest"
  printf '{\n  "plugin_id": "tbc-tools.cuda-runtime",\n  "plugin_version": "%s",\n  "platform": "windows",\n  "arch": "x86_64",\n  "ort_version": "%s",\n  "cuda_version": "11.8",\n  "cudnn_version": "8.9",\n' "$version" "$ORT_VERSION" >> "$manifest"

  local first=1
  # Windows DLL paths in the wheels: nvidia/<lib>/bin/<dll>
  declare -A win_libs=(
    ["nvidia-cuda-runtime-cu11"]="nvidia/cuda_runtime/bin/cudart64_110.dll"
    ["nvidia-cublas-cu11"]="nvidia/cublas/bin/cublas64_11.dll"
    ["nvidia-cublas-cu11-Lt"]="nvidia/cublas/bin/cublasLt64_11.dll"
    ["nvidia-cufft-cu11"]="nvidia/cufft/bin/cufft64_10.dll"
    ["nvidia-curand-cu11"]="nvidia/curand/bin/curand64_10.dll"
    ["nvidia-cudnn-cu11"]="nvidia/cudnn/bin/cudnn64_8.dll"
    ["nvidia-cudnn-cu11-cnn"]="nvidia/cudnn/bin/cudnn_cnn_infer64_8.dll"
    ["nvidia-cudnn-cu11-ops"]="nvidia/cudnn/bin/cudnn_ops_infer64_8.dll"
  )

  for entry in "${PKGS[@]}"; do
    IFS='|' read -r pkg ver <<< "$entry"
    local platform_filter="win_amd64"
    local wheel_url_fn wheel_fn wheel
    read -r wheel_url_fn wheel_fn <<< "$(wheel_url "$pkg" "$ver" "$platform_filter")"
    wheel="$tmp/$wheel_fn"
    say "  downloading $wheel_fn"
    curl -fsSL -o "$wheel" "$wheel_url_fn" 2>/dev/null || curl -fsSL -o "$wheel" "$wheel_url_fn"

    case "$pkg" in
      nvidia-cuda-runtime-cu11)
        local src="${win_libs[$pkg]}"; local base="${src##*/}"
        unzip -p "$wheel" "$src" > "$pkgdir/$base" 2>/dev/null || true
        local sha size; sha="$(sha256_of "$pkgdir/$base")"; size="$(stat -c '%s' "$pkgdir/$base")"
        manifest_add_file "$manifest" "$base" "$sha" "$size" "$first"; first=0
        ;;
      nvidia-cublas-cu11)
        for suffix in "" "-Lt"; do
          local key="${pkg}${suffix}"
          local src="${win_libs[$key]}"
          local base="${src##*/}"
          unzip -p "$wheel" "$src" > "$pkgdir/$base" 2>/dev/null || continue
          local sha size; sha="$(sha256_of "$pkgdir/$base")"; size="$(stat -c '%s' "$pkgdir/$base")"
          manifest_add_file "$manifest" "$base" "$sha" "$size" "$first"; first=0
        done
        ;;
      nvidia-cufft-cu11)
        local src="${win_libs[$pkg]}"; local base="${src##*/}"
        unzip -p "$wheel" "$src" > "$pkgdir/$base" 2>/dev/null || true
        local sha size; sha="$(sha256_of "$pkgdir/$base")"; size="$(stat -c '%s' "$pkgdir/$base")"
        manifest_add_file "$manifest" "$base" "$sha" "$size" "$first"; first=0
        ;;
      nvidia-curand-cu11)
        local src="${win_libs[$pkg]}"; local base="${src##*/}"
        unzip -p "$wheel" "$src" > "$pkgdir/$base" 2>/dev/null || true
        local sha size; sha="$(sha256_of "$pkgdir/$base")"; size="$(stat -c '%s' "$pkgdir/$base")"
        manifest_add_file "$manifest" "$base" "$sha" "$size" "$first"; first=0
        ;;
      nvidia-cudnn-cu11)
        for sub in "" "-cnn" "-ops"; do
          local key="${pkg}${sub}"
          local src="${win_libs[$key]}"
          local base="${src##*/}"
          unzip -p "$wheel" "$src" > "$pkgdir/$base" 2>/dev/null || continue
          local sha size; sha="$(sha256_of "$pkgdir/$base")"; size="$(stat -c '%s' "$pkgdir/$base")"
          manifest_add_file "$manifest" "$base" "$sha" "$size" "$first"; first=0
        done
        copy_license "$wheel" "$pkgdir" "copyright-cudnn.txt"
        ;;
    esac
  done

  # ORT CUDA EP provider DLL from the immutable nuget.org package (the
  # onnxruntime GitHub releases were purged by Microsoft on 2026-11-20).
  say "  fetching ONNX Runtime GPU nuget package (v$ORT_VERSION) for onnxruntime_providers_cuda.dll"
  local ort_nupkg="$tmp/ort-gpu-win.nupkg.zip"
  curl -fsSL -o "$ort_nupkg" "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.Gpu.Windows/$ORT_VERSION"
  unzip -j "$ort_nupkg" "runtimes/win-x64/native/onnxruntime_providers_cuda.dll" -d "$pkgdir/"
  unzip -j "$ort_nupkg" "runtimes/win-x64/native/onnxruntime_providers_shared.dll" -d "$pkgdir/" 2>/dev/null || true
  local psha psize
  psha="$(sha256_of "$pkgdir/onnxruntime_providers_cuda.dll")"; psize="$(stat -c '%s' "$pkgdir/onnxruntime_providers_cuda.dll")"
  manifest_add_file "$manifest" "onnxruntime_providers_cuda.dll" "$psha" "$psize" "$first"; first=0
  if [ -f "$pkgdir/onnxruntime_providers_shared.dll" ]; then
    local sh_sha sh_size
    sh_sha="$(sha256_of "$pkgdir/onnxruntime_providers_shared.dll")"; sh_size="$(stat -c '%s' "$pkgdir/onnxruntime_providers_shared.dll")"
    manifest_add_file "$manifest" "onnxruntime_providers_shared.dll" "$sh_sha" "$sh_size" "$first"; first=0
  fi

  printf '\n  ]\n}\n' >> "$manifest"

  # Create the zip package. Archive the files flat (no windows-x86_64/
  # prefix) so the manager's `tar -xf -C <installDir` lands them directly in
  # the install dir root, matching the manifest's flat filenames.
  local archive="$out/tbc-tools-cuda-plugin-windows-x86_64.zip"
  (cd "$pkgdir" && zip -qr "$archive" .)
  say "Windows package: $archive ($(du -h "$archive" | awk '{print $1}'))"
  say "Windows manifest: $manifest"
  rm -rf "$tmp"
}

# Build the Linux x86_64 teletext GPU (lite) plugin package: pyopencl wheels
# (+ pure-Python deps) extracted into one site-packages tree covering the
# system Pythons listed in LITE_PYTHONS. numpy is intentionally NOT bundled.
build_cuda_lite_linux() {
  local out version
  out="$(abs "$1")"; version="$2"
  local pkgdir="$out/cuda-lite-linux-x86_64" manifest="$out/tbc-cuda-lite-plugin-linux-x86_64-manifest.json"
  local tmp; tmp="$(mktemp -d)"
  mkdir -p "$pkgdir/site-packages"

  say "building Linux x86_64 teletext GPU (lite) plugin package (version $version)"
  : > "$manifest"
  printf '{\n  "plugin_id": "tbc-tools.cuda-lite",\n  "plugin_version": "%s",\n  "platform": "linux",\n  "arch": "x86_64",\n  "pyopencl_version": "%s",\n  "python_versions": "%s",\n' "$version" "$PYOPENCL_VERSION" "$(IFS=,; echo "${LITE_PYTHONS[*]//.}")" >> "$manifest"

  local pyver pyabi
  for pyver in "${LITE_PYTHONS[@]}"; do
    pyabi="cp${pyver//./}"
    say "  downloading pyopencl $PYOPENCL_VERSION + siphash24 $LITE_SIPHASH24_VERSION for python $pyver ($pyabi)"
    pip download --quiet --only-binary=:all: --no-deps \
      --platform manylinux_2_27_x86_64 --platform manylinux_2_28_x86_64 \
      --python-version "$pyver" --implementation cp --abi "$pyabi" \
      -d "$tmp/wheels" "pyopencl==$PYOPENCL_VERSION" "siphash24==$LITE_SIPHASH24_VERSION" \
      || die "pip download failed for python $pyver"
  done
  # Pure-Python deps resolve to py3-none-any wheels (one per package).
  say "  downloading pure-Python deps (pytools platformdirs typing_extensions)"
  pip download --quiet --only-binary=:all: --no-deps \
    -d "$tmp/wheels" \
    "pytools==$LITE_PYTOOLS_VERSION" \
    "platformdirs==$LITE_PLATFORMDIRS_VERSION" \
    "typing_extensions==$LITE_TYPING_EXTENSIONS_VERSION" || die "pip download of deps failed"

  # Merge every wheel into one site-packages tree: ABI-tagged .so files of
  # different Python versions coexist; shared pure files are identical.
  local wheel
  for wheel in "$tmp"/wheels/*.whl; do
    say "  extracting $(basename "$wheel")"
    unzip -q -o "$wheel" -d "$pkgdir/site-packages"
  done

  # Full per-file manifest (the GUI generic installer SHA-256-verifies these).
  local first=1
  local rel sha size
  while IFS= read -r rel; do
    [ -n "$rel" ] || continue
    sha="$(sha256_of "$pkgdir/$rel")"
    size="$(stat -c '%s' "$pkgdir/$rel")"
    manifest_add_file "$manifest" "$rel" "$sha" "$size" "$first"
    first=0
  done < <(cd "$pkgdir" && find site-packages -type f | LC_ALL=C sort)
  printf '\n  ]\n}\n' >> "$manifest"

  local archive="$out/tbc-tools-cuda-lite-plugin-linux-x86_64.tar.gz"
  tar -czf "$archive" -C "$pkgdir" .
  say "Linux lite package: $archive ($(du -h "$archive" | awk '{print $1}'))"
  say "Linux lite manifest: $manifest"
  rm -rf "$tmp"
}

# Build the Windows x86_64 teletext GPU (lite) plugin package: Python
# embeddable distribution + full pyopencl wheel closure (numpy included, the
# embeddable runtime has no site packages of its own).
build_cuda_lite_windows() {
  local out version
  out="$(abs "$1")"; version="$2"
  local pkgdir="$out/cuda-lite-windows-x86_64" manifest="$out/tbc-cuda-lite-plugin-windows-x86_64-manifest.json"
  local tmp; tmp="$(mktemp -d)"
  mkdir -p "$pkgdir/site-packages"

  say "building Windows x86_64 teletext GPU (lite) plugin package (version $version)"
  : > "$manifest"
  printf '{\n  "plugin_id": "tbc-tools.cuda-lite",\n  "plugin_version": "%s",\n  "platform": "windows",\n  "arch": "x86_64",\n  "pyopencl_version": "%s",\n  "python_version": "%s",\n' "$version" "$PYOPENCL_VERSION" "$LITE_WIN_PYTHON" >> "$manifest"

  local first=1
  # Python embeddable distribution.
  say "  downloading Python $LITE_WIN_PYTHON_BUILD embeddable distribution"
  local py_zip="$tmp/python-embed.zip"
  curl -fsSL -o "$py_zip" "https://www.python.org/ftp/python/${LITE_WIN_PYTHON_BUILD}/python-${LITE_WIN_PYTHON_BUILD}-embed-amd64.zip"
  unzip -q -o "$py_zip" -d "$pkgdir/python"
  # The ._pth file enables isolated path mode which IGNORES PYTHONPATH, so
  # remove it: the embedded runtime then behaves like a normal interpreter
  # and honors the PYTHONPATH the teletext integration sets.
  rm -f "$pkgdir/python/python${LITE_WIN_PYTHON//./}._pth"

  local pyabi="cp${LITE_WIN_PYTHON//./}"
  say "  downloading pyopencl $PYOPENCL_VERSION + numpy $LITE_NUMPY_VERSION + siphash24 $LITE_SIPHASH24_VERSION for win_amd64 python $LITE_WIN_PYTHON ($pyabi)"
  pip download --quiet --only-binary=:all: --no-deps \
    --platform win_amd64 --python-version "$LITE_WIN_PYTHON" --implementation cp --abi "$pyabi" \
    -d "$tmp/wheels" \
    "pyopencl==$PYOPENCL_VERSION" \
    "numpy==$LITE_NUMPY_VERSION" \
    "siphash24==$LITE_SIPHASH24_VERSION" || die "pip download failed for win_amd64"
  pip download --quiet --only-binary=:all: --no-deps \
    -d "$tmp/wheels" \
    "pytools==$LITE_PYTOOLS_VERSION" \
    "platformdirs==$LITE_PLATFORMDIRS_VERSION" \
    "typing_extensions==$LITE_TYPING_EXTENSIONS_VERSION" || die "pip download of deps failed"

  local wheel
  for wheel in "$tmp"/wheels/*.whl; do
    say "  extracting $(basename "$wheel")"
    unzip -q -o "$wheel" -d "$pkgdir/site-packages"
  done

  local rel sha size
  while IFS= read -r rel; do
    [ -n "$rel" ] || continue
    sha="$(sha256_of "$pkgdir/$rel")"
    size="$(stat -c '%s' "$pkgdir/$rel")"
    manifest_add_file "$manifest" "$rel" "$sha" "$size" "$first"
    first=0
  done < <(cd "$pkgdir" && find . -type f | sed 's|^\./||' | LC_ALL=C sort)
  printf '\n  ]\n}\n' >> "$manifest"

  local archive="$out/tbc-tools-cuda-lite-plugin-windows-x86_64.zip"
  (cd "$pkgdir" && zip -qr "$archive" .)
  say "Windows lite package: $archive ($(du -h "$archive" | awk '{print $1}'))"
  say "Windows lite manifest: $manifest"
  rm -rf "$tmp"
}

main() {
  [ "$#" -ge 1 ] || { sed -n '2,55p' "$0" >&2; exit 64; }
  local mode="$1"; shift
  local out="$OUT_DEFAULT" version="$VERSION_DEFAULT" deps_dir=""
  while [ "$#" -gt 0 ]; do
    case "$1" in
      --out) out="$2"; shift 2 ;;
      --version) version="$2"; shift 2 ;;
      --deps-dir) deps_dir="$2"; shift 2 ;;
      *) die "unknown arg: $1" ;;
    esac
  done
  out="$(abs "$out")"
  mkdir -p "$out"
  case "$mode" in
    build-linux)             build_linux "$out" "$version" "$deps_dir" ;;
    build-windows)           build_windows "$out" "$version" ;;
    build-all)               build_linux "$out" "$version" "$deps_dir"; build_windows "$out" "$version" ;;
    build-cuda-lite-linux)   build_cuda_lite_linux "$out" "$version" ;;
    build-cuda-lite-windows) build_cuda_lite_windows "$out" "$version" ;;
    build-cuda-lite-all)     build_cuda_lite_linux "$out" "$version"; build_cuda_lite_windows "$out" "$version" ;;
    -h|--help)               sed -n '2,55p' "$0" ;;
    *) die "unknown mode '$mode' (try: build-linux|build-windows|build-all|build-cuda-lite-all)" ;;
  esac
  say "done. packages + manifests in $out"
}

main "$@"
