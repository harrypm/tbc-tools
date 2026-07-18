#!/usr/bin/env bash
# scripts/windows-cuda-runtime.sh
#
# Fetch + cache the Windows CUDA 11.8 + cuDNN 8.9 runtime DLLs that the
# ONNX Runtime 1.18.1 CUDA-11.x execution provider (onnxruntime_providers_cuda.dll)
# needs at runtime. ORT's CUDA EP directly imports cudart64_110.dll, cublas64_11.dll,
# cublasLt64_11.dll, cufft64_10.dll and cudnn64_8.dll; cudnn64_8.dll then lazy-loads
# the cudnn_*_infer64_8.dll sub-libraries for the op categories the model uses
# (the chroma_net_v2.onnx 3D-Conv model needs the cnn/ops infer sub-DLLs). Without
# these next to the exe, LoadLibrary("onnxruntime_providers_cuda.dll") fails on a
# clean Windows machine and ORT silently falls back to the CPU EP (no GPU accel).
#
# Source: NVIDIA's official redistributable pip wheels (nvidia-*-cu11, win_amd64),
# which are public on PyPI with NO NVIDIA login (unlike developer.nvidia.com cuDNN).
# This mirrors what ORT's own preload_dlls() documents as the "nvidia site packages"
# source. License files from each wheel are copied alongside for redistribution.
#
# The DLL set is mirrored into the dedicated tbc-tools-ci-cache git repo (chunked
# under 95 MiB so it fits GitHub's 100 MiB/file limit), so CI pulls it from our own
# cache instead of re-downloading ~1.6 GB from PyPI on every Windows x64 build.
#
# Modes:
#   fetch  [--out DIR]   download the 4 wheels via PyPI JSON API, extract the inference
#                        DLL set + licenses into DIR/dlls + DIR/LICENSES, write manifest.txt
#   verify [--out DIR]   check every expected DLL is present and its SHA256 matches manifest.txt
#   push   [--out DIR] [--repo URL] [--yes]  chunk files >95 MiB, commit into tbc-tools-ci-cache, push
#   pull   [--out DIR] [--repo URL]           clone tbc-tools-ci-cache, reassemble chunks, place DLLs
#
# Defaults:
#   --out  ./win-cuda-cache  (resolved to an absolute path)
#   --repo https://github.com/harrypm/tbc-tools-ci-cache.git
#
# Requires: bash, curl, unzip, python3 (JSON parsing), coreutils (sha256sum, split, find, awk)
# Runs on Linux (dev/CI) and on the Windows runner via git-bash (coreutils present).
set -euo pipefail

REPO_DEFAULT="https://github.com/harrypm/tbc-tools-ci-cache.git"
OUT_DEFAULT="${PWD}/win-cuda-cache"
CHUNK_BYTES=$((95 * 1024 * 1024))   # 95 MiB: safely under GitHub's 100 MiB file limit
SUBDIR="windows/cuda-runtime-x64"
SYSTEM="win_amd64"

# Pinned NVIDIA redistributable wheel versions (win_amd64).
# cuda-runtime/cublas/cufft ship CUDA 11.8 runtime DLLs; cudnn 8.9.5 is the latest
# 8.x wheel (cuDNN 8.x is required by ORT 1.18.x CUDA-11.x; 9.x is ABI-incompatible).
declare -a PKGS=(
  "nvidia-cuda-runtime-cu11|11.8.89|nvidia/cuda_runtime/bin/cudart64_110.dll"
  "nvidia-cublas-cu11|11.11.3.6|nvidia/cublas/bin/cublas64_11.dll,nvidia/cublas/bin/cublasLt64_11.dll"
  "nvidia-cufft-cu11|10.9.0.58|nvidia/cufft/bin/cufft64_10.dll"
  "nvidia-cudnn-cu11|8.9.5.29|nvidia/cudnn/bin/cudnn64_8.dll,nvidia/cudnn/bin/cudnn_cnn_infer64_8.dll,nvidia/cudnn/bin/cudnn_ops_infer64_8.dll,nvidia/cudnn/bin/cudnn_adv_infer64_8.dll"
)

die() { printf 'windows-cuda-runtime: error: %s\n' "$*" >&2; exit 1; }
say() { printf 'windows-cuda-runtime: %s\n' "$*"; }
abs() { case "$1" in /*) printf '%s' "$1" ;; *) printf '%s/%s' "$PWD" "$1" ;; esac; }

# Resolve the win_amd64 wheel download URL + filename for a pinned package+version
# via the PyPI JSON API. Prints "<url> <filename>".
wheel_url() {
  local pkg="$1" ver="$2"
  local json
  json="$(curl -fsSL "https://pypi.org/pypi/${pkg}/${ver}/json" || true)"
  [ -n "$json" ] || die "could not fetch PyPI JSON for ${pkg}==${ver}"
  printf '%s' "$json" | python3 -c "
import sys, json
d = json.load(sys.stdin)
hit = None
for f in d.get('urls', []):
    fn = f['filename']
    if fn.endswith('.whl') and 'win_amd64' in fn and 'py3-none' in fn:
        hit = f
        break
if hit is None:
    raise SystemExit('no win_amd64 py3-none wheel for ${pkg}==${ver}')
print(hit['url'], hit['filename'])
"
}

# Resolve the wheel's License.txt dist-info path (handles <pkg>-<ver>.dist-info/License.txt).
license_path_in_wheel() {
  local wheel="$1"
  unzip -l "$wheel" 2>/dev/null | awk '{print $4}' | grep -E '\.dist-info/License\.txt$' | head -1
}

cmd_fetch() {
  local out="$OUT_DEFAULT"
  while [ "$#" -gt 0 ]; do case "$1" in --out) out="$2"; shift 2 ;; *) die "fetch: unknown arg: $1" ;; esac; done
  out="$(abs "$out")"; local dlls="$out/dlls" lic="$out/LICENSES" tmp
  mkdir -p "$dlls" "$lic"
  tmp="$(mktemp -d)"
  say "fetching + extracting CUDA 11.8 + cuDNN 8.9 runtime DLLs -> $dlls"
  : > "$out/manifest.txt"
  printf '# windows-cuda-runtime manifest (system=%s, generated_utc=%s)\n' "$SYSTEM" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$out/manifest.txt"
  printf '# <dll> <sha256> <size> <source_wheel> <wheel_version>\n' >> "$out/manifest.txt"
  for entry in "${PKGS[@]}"; do
    IFS='|' read -r pkg ver dllcsv <<< "$entry"
    say "  resolving ${pkg}==${ver} wheel via PyPI"
    local url fn wheel
    read -r url fn <<< "$(wheel_url "$pkg" "$ver")"
    wheel="$tmp/$fn"
    say "  downloading $fn"
    curl -fsSL -o "$wheel" "$url"
    # Extract each selected DLL into $dlls (flatten, basename only).
    local dllcsv_wo
    IFS=',' read -ra dlls_arr <<< "$dllcsv"
    for src in "${dlls_arr[@]}"; do
      local base="${src##*/}"
      unzip -p "$wheel" "$src" > "$dlls/$base"
      local sha size
      sha="$(sha256sum "$dlls/$base" | awk '{print $1}')"
      size="$(stat -c '%s' "$dlls/$base")"
      printf '%s %s %s %s %s\n' "$base" "$sha" "$size" "$fn" "$ver" >> "$out/manifest.txt"
      say "    $base ($((size / 1048576)) MiB) sha256=$sha"
    done
    # License: copy into LICENSES with a package-specific name.
    local licpath
    licpath="$(license_path_in_wheel "$wheel")"
    if [ -n "$licpath" ]; then
      local licname
      case "$pkg" in
        *cudnn*) licname="copyright-cudnn.txt" ;;
        *cublas*|*cufft*|*cuda-runtime*) licname="copyright-cuda-runtime.txt" ;;
        *) licname="copyright-${pkg}.txt" ;;
      esac
      unzip -p "$wheel" "$licpath" > "$lic/$licname"
      say "    license -> LICENSES/$licname"
    fi
  done
  rm -rf "$tmp"
  say "fetch complete: $dlls"
  du -sh "$dlls" 2>/dev/null | sed 's/^/windows-cuda-runtime: dll size: /'
}

# Reassemble any chunked files (foo.dll.parts/) back to foo.dll in the cache dir.
reassemble_all() {
  local dir="$1" idx partsdir name size want_sha got_sha
  while IFS= read -r idx; do
    partsdir="$(dirname "$idx")"
    name="$(awk -F= '$1=="original_name"{print $2}' "$idx")"
    size="$(awk -F= '$1=="original_size"{print $2}' "$idx")"
    want_sha="$(awk -F= '$1=="original_sha256"{print $2}' "$idx")"
    [ -n "$name" ] || die "index $idx missing original_name"
    local outp="$partsdir/../$name"
    : > "$outp"
    local c
    for c in "$partsdir"/*.part.*; do cat "$c" >> "$outp"; done
    got_sha="$(sha256sum "$outp" | awk '{print $1}')"
    [ "$got_sha" = "$want_sha" ] || die "reassembled $name sha256 mismatch (want $want_sha, got $got_sha)"
    rm -rf "$partsdir"
    say "  reassembled $name ($((size / 1048576)) MiB) OK"
  done < <(find "$dir" -type f -name index -path '*.parts/index' 2>/dev/null)
}

# Split a single file >95MiB into chunked parts with an index, then remove the original.
split_one() {
  local file="$1"
  local name; name="$(basename "$file")"
  local partsdir="$file.parts"
  mkdir -p "$partsdir"
  local orig_size orig_sha
  orig_size="$(stat -c '%s' "$file")"
  orig_sha="$(sha256sum "$file" | awk '{print $1}')"
  split -b "${CHUNK_BYTES}" -a 4 -d -- "$file" "$partsdir/${name}.part."
  local chunk_names=() f
  for f in "$partsdir"/*.part.*; do chunk_names+=("$(basename "$f")"); done
  {
    printf 'original_name=%s\n' "$name"
    printf 'original_size=%d\n' "$orig_size"
    printf 'original_sha256=%s\n' "$orig_sha"
    printf 'chunk_count=%d\n' "${#chunk_names[@]}"
    printf '# chunk filenames (relative to this .parts dir):\n'
    local c; for c in "${chunk_names[@]}"; do printf '%s\n' "$c"; done
  } > "$partsdir/index"
  rm -f "$file"
  say "  split $name ($((orig_size / 1048576)) MiB) -> ${#chunk_names[@]} chunk(s)"
}

cmd_verify() {
  local out="$OUT_DEFAULT"
  while [ "$#" -gt 0 ]; do case "$1" in --out) out="$2"; shift 2 ;; *) die "verify: unknown arg: $1" ;; esac; done
  out="$(abs "$out")"; local dlls="$out/dlls"
  [ -f "$out/manifest.txt" ] || die "missing $out/manifest.txt (run fetch first)"
  local missing=0 bad=0
  while IFS=' ' read -r dll sha size wheel ver; do
    [ -n "$dll" ] || continue
    case "$dll" in \#*) continue ;; esac
    if [ ! -f "$dlls/$dll" ]; then say "MISSING: $dll"; missing=$((missing+1)); continue; fi
    local got; got="$(sha256sum "$dlls/$dll" | awk '{print $1}')"
    if [ "$got" != "$sha" ]; then say "BAD sha256: $dll (want $sha, got $got)"; bad=$((bad+1)); else say "OK: $dll ($((size / 1048576)) MiB)"; fi
  done < "$out/manifest.txt"
  [ "$missing" -eq 0 ] || die "$missing DLL(s) missing"
  [ "$bad" -eq 0 ] || die "$bad DLL(s) failed sha256"
  say "verify OK"
}

cmd_push() {
  local out="$OUT_DEFAULT" repo="$REPO_DEFAULT" yes=0
  while [ "$#" -gt 0 ]; do
    case "$1" in --out) out="$2"; shift 2 ;; --repo) repo="$2"; shift 2 ;; --yes) yes=1; shift ;; *) die "push: unknown arg: $1" ;; esac
  done
  out="$(abs "$out")"; local dlls="$out/dlls" lic="$out/LICENSES"
  [ -d "$dlls" ] || die "no dlls at $dlls (run fetch first)"
  local size_gb; size_gb="$(du -sb "$dlls" "$lic" 2>/dev/null | awk '{s+=$1} END{printf "%.1f", s/1073741824.0}')"
  say "dll set is ~${size_gb} GB; target repo: $repo"
  if [ "$yes" -eq 0 ]; then
    printf 'windows-cuda-runtime: commits ~%s GB into a PUBLIC git repo and pushes. Continue? [y/N] ' "$size_gb" >&2
    local r; read -r r || r=n
    case "$r" in y|Y|yes) ;; *) say "aborted"; exit 130 ;; esac
  fi
  local tmp; tmp="$(mktemp -d)"
  say "cloning $repo -> $tmp"
  git clone --quiet "$repo" "$tmp"
  local dest="$tmp/$SUBDIR"
  rm -rf "$dest"; mkdir -p "$(dirname "$dest")"; mkdir -p "$dest"
  # Copy DLLs + licenses + manifest into the cache subdir.
  cp -a "$dlls"/. "$dest/"
  mkdir -p "$dest/LICENSES"; cp -a "$lic"/. "$dest/LICENSES/" 2>/dev/null || true
  cp -a "$out/manifest.txt" "$dest/manifest.txt"
  # Chunk any file >95 MiB so the repo fits GitHub's 100 MiB/file limit.
  local f big_count=0
  while IFS= read -r f; do
    [ -f "$f" ] || continue
    split_one "$f"
    big_count=$((big_count + 1))
  done < <(find "$dest" -maxdepth 1 -type f -size "+${CHUNK_BYTES}c" 2>/dev/null)
  say "split $big_count large file(s) into chunked parts"
  git -C "$tmp" add -A
  git -C "$tmp" -c user.name='tbc-tools-ci' -c user.email='ci@tbc-tools' \
    commit -q -m "windows/cuda-runtime-x64: cache CUDA 11.8 + cuDNN 8.9 runtime DLLs for ORT CUDA EP

Bundled from NVIDIA redistributable pip wheels (nvidia-*-cu11, win_amd64) so the
ONNX Runtime 1.18.1 CUDA-11.x execution provider loads on a clean Windows x64
machine without a separate CUDA/cuDNN install. See manifest.txt for wheel
versions + per-DLL sha256.

Co-Authored-By: Oz <oz-agent@warp.dev>" || say "nothing to commit (cache unchanged)"
  git -C "$tmp" push --quiet origin HEAD:main
  say "pushed to $repo (branch main)"
  rm -rf "$tmp"
}

cmd_pull() {
  local out="$OUT_DEFAULT" repo="$REPO_DEFAULT"
  while [ "$#" -gt 0 ]; do case "$1" in --out) out="$2"; shift 2 ;; --repo) repo="$2"; shift 2 ;; *) die "pull: unknown arg: $1" ;; esac; done
  out="$(abs "$out")"; local dlls="$out/dlls" lic="$out/LICENSES"
  local tmp; tmp="$(mktemp -d)"
  say "cloning $repo -> $tmp"
  # Shallow + single-branch: the cache repo also holds the multi-GB Nix CUDA
  # closure, so a full clone is far too slow for CI. We only need the latest tree.
  git clone --depth 1 --single-branch --quiet "$repo" "$tmp"
  [ -d "$tmp/$SUBDIR" ] || die "repo has no $SUBDIR (has it been pushed yet?)"
  mkdir -p "$dlls" "$lic"
  # Reassemble any chunked files in the cache subdir back to full DLLs.
  reassemble_all "$tmp/$SUBDIR"
  # Copy DLLs (top-level files, excluding LICENSES/ + manifest) into dlls/.
  local f
  for f in "$tmp/$SUBDIR"/*.dll; do
    [ -f "$f" ] || continue
    cp -a "$f" "$dlls/$(basename "$f")"
  done
  # Licenses + manifest.
  [ -d "$tmp/$SUBDIR/LICENSES" ] && cp -a "$tmp/$SUBDIR/LICENSES"/. "$lic/" 2>/dev/null || true
  cp -a "$tmp/$SUBDIR/manifest.txt" "$out/manifest.txt" 2>/dev/null || true
  say "pulled CUDA runtime DLLs -> $dlls"
  rm -rf "$tmp"
}

main() {
  [ "$#" -ge 1 ] || { sed -n '2,40p' "$0" >&2; exit 64; }
  local mode="$1"; shift
  case "$mode" in
    fetch)  cmd_fetch  "$@" ;;
    verify) cmd_verify "$@" ;;
    push)   cmd_push   "$@" ;;
    pull)   cmd_pull   "$@" ;;
    -h|--help) sed -n '2,40p' "$0" ;;
    *) die "unknown mode '$mode' (try: fetch|verify|push|pull)" ;;
  esac
}

main "$@"
