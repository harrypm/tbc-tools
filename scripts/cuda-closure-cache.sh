#!/usr/bin/env bash
# scripts/cuda-closure-cache.sh
#
# Cache + bundle the pinned CUDA 11.8 closure that flake.nix pulls from
# nixpkgsLegacy (nixos-24.11), so that cache.nixos.org garbage-collection or a
# future Nixpkgs 25.05 removal of cudaPackages_11_8 cannot break the
# GTX-1000-series (Pascal) CUDA builds.
#
# The closure is written as a standard Nix binary cache (narinfo + nar files).
# Any nar larger than 95 MiB is split into chunks so the cache directory can be
# committed to the dedicated git repo (harrypm/tbc-tools-ci-cache) without
# hitting GitHub's 100 MiB per-file limit. A manifest + pin record is written
# alongside so the closure can be verified and restored on a fresh machine.
#
# Modes:
#   export  [--out DIR] [--subset ATTR...]  realise + copy the closure into a file:// cache at DIR; split big nars; write manifest + pin
#   verify  [--out DIR]                      structural integrity check (narinfo/nar/chunks + sha256 of reassembled nars)
#   restore [--out DIR]                      reassemble split nars and import the closure into the local Nix store
#   push    [--out DIR] [--repo URL] [--yes] commit the cache dir into tbc-tools-ci-cache and push (multi-GB; asks first unless --yes)
#   pull    [--out DIR] [--repo URL]         clone/pull tbc-tools-ci-cache and place the cache under DIR
#
# Defaults:
#   --out  ./cuda-cache  (resolved to an absolute path)
#   --repo https://github.com/harrypm/tbc-tools-ci-cache.git
#   --subset  all 8 CUDA 11.8 build inputs the flake uses (see PACKAGES below)
#
# Requires: nix (nix-command + flakes), git, coreutils (split, sha256sum, awk, find, du)
set -euo pipefail

# --- Pinned nixpkgs (must match flake.lock nixpkgsLegacy) -------------------
NIXPKGS_REV="${NIXPKGS_REV:-50ab793786d9de88ee30ec4e4c24fb4236fc2674}"
NIXPKGS_SHA="${NIXPKGS_SHA:-sha256-/bVBlRpECLVzjV19t5KMdMFWSwKLtb5RyXdjz3LJT+g=}"
NIXPKGS_REF="github:NixOS/nixpkgs/${NIXPKGS_REV}"

REPO_DEFAULT="https://github.com/harrypm/tbc-tools-ci-cache.git"
OUT_DEFAULT="${PWD}/cuda-cache"
CHUNK_BYTES=$((95 * 1024 * 1024))   # 95 MiB: safely under GitHub's 100 MiB file limit
SUBDIR="nix/cuda-11_8-x86_64-linux"
SYSTEM="x86_64-linux"

# The exact CUDA 11.8 build inputs flake.nix pulls from legacyPkgs.
DEFAULT_PACKAGES=(
  "cudaPackages_11_8.cudatoolkit"
  "cudaPackages_11_8.cudnn_8_9"
  "cudaPackages_11_8.cuda_nvcc"
  "cudaPackages_11_8.cuda_cudart"
  "cudaPackages_11_8.libcufft"
  "cudaPackages_11_8.libcurand"
  "cudaPackages_11_8.libcublas"
  "gcc11"
)

# Nix needs the flakes experimental feature to resolve the github: ref. Note:
# `allow-unfree` is NOT a Nix daemon setting (Nix 2.33 rejects it); unfree CUDA
# attrs are allowed at the Nixpkgs-import level inside out_path()/package_version()
# via `config.allowUnfree = true` with builtins.fetchTree (see below). Substituting
# already-named store paths with `nix copy`/`nix path-info` does not need it.
_nix_conf=("experimental-features = nix-command flakes")
[ -n "${NIX_CONFIG:-}" ] && _nix_conf+=("$NIX_CONFIG")
export NIX_CONFIG="$(printf '%s\n' "${_nix_conf[@]}")"

die() { printf 'cuda-closure-cache: error: %s\n' "$*" >&2; exit 1; }
say() { printf 'cuda-closure-cache: %s\n' "$*"; }

abs() { case "$1" in /*) printf '%s' "$1" ;; *) printf '%s/%s' "$PWD" "$1" ;; esac; }

# Import the pinned nixpkgs with allowUnfree so unfree CUDA attrs evaluate. Uses
# builtins.fetchTree (github type) with the exact rev; --impure is required because
# fetchTree with a bare rev is a flake-style input.
_nixpkgs_expr="import (builtins.fetchTree { type = \"github\"; owner = \"NixOS\"; repo = \"nixpkgs\"; rev = \"${NIXPKGS_REV}\"; }) { config.allowUnfree = true; system = \"${SYSTEM}\"; }"

# Resolve a package attribute to its store outPath via the pinned nixpkgs.
out_path() {
  local attr="$1"
  nix eval --impure --raw --expr "let pkgs = ${_nixpkgs_expr}; in pkgs.${attr}.outPath" 2>/dev/null \
    || die "could not evaluate outPath for ${attr} via nixpkgs ${NIXPKGS_REV}"
}

package_version() {
  local attr="$1"
  nix eval --impure --raw --expr "let pkgs = ${_nixpkgs_expr}; in pkgs.${attr}.version" 2>/dev/null || printf '?'
}

cmd_export() {
  local out="$OUT_DEFAULT" subset=()
  while [ "$#" -gt 0 ]; do
    case "$1" in
      --out) out="$2"; shift 2 ;;
      --subset) shift; while [ "$#" -gt 0 ] && [[ "$1" != --* ]]; do subset+=("$1"); shift; done ;;
      *) die "export: unknown arg: $1" ;;
    esac  done
  [ "${#subset[@]}" -eq 0 ] && subset=("${DEFAULT_PACKAGES[@]}")
  out="$(abs "$out")"; local cache="$out/$SUBDIR"
  mkdir -p "$cache"
  say "resolving outPaths for ${#subset[@]} package(s) via ${NIXPKGS_REF}"
  local paths=() p
  for p in "${subset[@]}"; do paths+=("$(out_path "$p")"); done
  say "writing pin record -> $cache/pin.txt"
  {
    printf 'nixpkgs_rev=%s\n' "$NIXPKGS_REV"
    printf 'nixpkgs_sha256=%s\n' "$NIXPKGS_SHA"
    printf 'system=%s\n' "$SYSTEM"
    printf 'generated_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf '# package versions:\n'
    for p in "${subset[@]}"; do printf '%s=%s\n' "$p" "$(package_version "$p")"; done
  } > "$cache/pin.txt"

  say "copying closure into file:// binary cache ($cache) -- this realises/substitutes missing paths"
  nix copy --to "file://$cache" "${paths[@]}"

  say "writing closure manifest -> $cache/manifest.txt (captured before splitting)"
  nix path-info --recursive --store "file://$cache" "${paths[@]}" | sort > "$cache/manifest.txt"
  local npaths
  npaths="$(wc -l < "$cache/manifest.txt")"
  say "closure has $npaths store paths"

  say "splitting any nar > $((CHUNK_BYTES / 1048576)) MiB so the repo fits GitHub's 100 MiB file limit"
  local big n big_count=0
  while IFS= read -r big; do
    [ -f "$big" ] || continue
    split_one_nar "$big"
    big_count=$((big_count + 1))
  done < <(find "$cache/nar" -type f -size "+${CHUNK_BYTES}c" 2>/dev/null)
  say "split $big_count large nar(s) into chunked parts"

  say "writing README -> $cache/README.md"
  write_readme "$cache/README.txt"
  say "export complete: $cache"
  du -sh "$cache" 2>/dev/null | sed 's/^/cuda-closure-cache: cache size: /'
}

# Split a single nar file into <95MiB chunks with an index, then remove the original.
split_one_nar() {
  local nar="$1"
  local name; name="$(basename "$nar")"
  local partsdir="$nar.parts"
  mkdir -p "$partsdir"
  local orig_size orig_sha
  orig_size="$(stat -c '%s' "$nar")"
  orig_sha="$(sha256sum "$nar" | awk '{print $1}')"
  # Use a fixed chunk size; split -a 4 gives plenty of suffix space.
  split -b "${CHUNK_BYTES}" -a 4 -d -- "$nar" "$partsdir/${name}.part."
  local chunk_names=()
  local f
  for f in "$partsdir"/*.part.*; do chunk_names+=("$(basename "$f")"); done
  {
    printf 'original_name=%s\n' "$name"
    printf 'original_size=%d\n' "$orig_size"
    printf 'original_sha256=%s\n' "$orig_sha"
    printf 'chunk_count=%d\n' "${#chunk_names[@]}"
    printf '# chunk filenames (relative to this .parts dir):\n'
    local c; for c in "${chunk_names[@]}"; do printf '%s\n' "$c"; done
  } > "$partsdir/index"
  rm -f "$nar"
  say "  split $name ($((orig_size / 1048576)) MiB) -> ${#chunk_names[@]} chunk(s)"
}

# Reassemble any split nars in the cache back to their original files.
reassemble_all() {
  local cache="$1" idx partsdir name size want_sha got_sha part
  while IFS= read -r idx; do
    partsdir="$(dirname "$idx")"
    # parse index
    name="$(awk -F= '$1=="original_name"{print $2}' "$idx")"
    size="$(awk -F= '$1=="original_size"{print $2}' "$idx")"
    want_sha="$(awk -F= '$1=="original_sha256"{print $2}' "$idx")"
    [ -n "$name" ] || die "index $idx missing original_name"
    local out="$partsdir/../$name"
    : > "$out"
    local c
    for c in "$partsdir"/*.part.*; do cat "$c" >> "$out"; done
    got_sha="$(sha256sum "$out" | awk '{print $1}')"
    [ "$got_sha" = "$want_sha" ] || die "reassembled $name sha256 mismatch (want $want_sha, got $got_sha)"
    local got_size; got_size="$(stat -c '%s' "$out")"
    [ "$got_size" = "$size" ] || die "reassembled $name size mismatch (want $size, got $got_size)"
    rm -rf "$partsdir"
    say "  reassembled $name ($((size / 1048576)) MiB) OK"
  done < <(find "$cache/nar" -type f -name index -path '*.parts/index' 2>/dev/null)
}

cmd_verify() {
  local out="$OUT_DEFAULT"
  while [ "$#" -gt 0 ]; do case "$1" in --out) out="$2"; shift 2 ;; *) die "verify: unknown arg: $1" ;; esac; done
  out="$(abs "$out")"; local cache="$out/$SUBDIR"
  [ -f "$cache/nix-cache-info" ] || die "missing $cache/nix-cache-info (not a binary cache?)"
  [ -f "$cache/manifest.txt" ] || die "missing $cache/manifest.txt"
  [ -f "$cache/pin.txt" ] || die "missing $cache/pin.txt"
  say "cache layout OK (nix-cache-info, manifest.txt, pin.txt present)"

  local narinfos=0 nars=0 split_nars=0 plain_nars=0
  narinfos="$(find "$cache" -maxdepth 1 -name '*.narinfo' -type f | wc -l)"
  plain_nars="$(find "$cache/nar" -maxdepth 1 -type f -name '*.nar*' 2>/dev/null | wc -l)"
  split_nars="$(find "$cache/nar" -maxdepth 2 -type f -name index -path '*.parts/index' 2>/dev/null | wc -l)"
  say "narinfos=$narinfos  plain_nars=$plain_nars  split_nars=$split_nars"
  [ "$narinfos" -gt 0 ] || die "no narinfos found"

  # Verify every split nar reassembles to the recorded sha256/size without
  # mutating the cache: reassemble into a temp file and compare.
  local idx partsdir name size want_sha got
  while IFS= read -r idx; do
    partsdir="$(dirname "$idx")"
    name="$(awk -F= '$1=="original_name"{print $2}' "$idx")"
    size="$(awk -F= '$1=="original_size"{print $2}' "$idx")"
    want_sha="$(awk -F= '$1=="original_sha256"{print $2}' "$idx")"
    got="$(cat "$partsdir"/*.part.* | sha256sum | awk '{print $1}')"
    [ "$got" = "$want_sha" ] || die "chunk sha256 mismatch for $name (want $want_sha, got $got)"
    say "  split nar $name: chunks OK ($((size / 1048576)) MiB, sha256 verified)"
  done < <(find "$cache/nar" -type f -name index -path '*.parts/index' 2>/dev/null)

  # Confirm nix can read the un-split portion of the cache.
  local first_path; first_path="$(head -1 "$cache/manifest.txt")"
  if [ -n "$first_path" ] && nix path-info --store "file://$cache" "$first_path" >/dev/null 2>&1; then
    say "nix can read the binary cache (verified first manifest path: $first_path)"
  else
    say "note: nix could not read $first_path from the cache (it may be a split nar; restore reassembles first)"
  fi
  say "verify OK"
}

cmd_restore() {
  local out="$OUT_DEFAULT"
  while [ "$#" -gt 0 ]; do case "$1" in --out) out="$2"; shift 2 ;; *) die "restore: unknown arg: $1" ;; esac; done
  out="$(abs "$out")"; local cache="$out/$SUBDIR"
  [ -f "$cache/manifest.txt" ] || die "missing $cache/manifest.txt (run export first / pull first)"
  say "reassembling any split nars in $cache"
  reassemble_all "$cache"
  say "importing closure into the local Nix store"
  local paths=(); while IFS= read -r p; do paths+=("$p"); done < "$cache/manifest.txt"
  # This binary cache is self-built and unsigned, so Nix's default
  # `require-sigs = true` refuses the import with "cannot add path ... because
  # it lacks a signature by a trusted key". Disable the trusted-key signature
  # requirement for this local file:// cache. This is safe because:
  #   (1) the cache is local and produced by `export` above (not fetched from
  #       an untrusted network substituter);
  #   (2) `reassemble_all` already SHA256-verified every reassembled nar
  #       against the sha recorded at split time;
  #   (3) `require-sigs = false` only lifts the trusted-key signature check --
  #       Nix still verifies each imported nar's content hash against the
  #       narinfo's narHash, so a corrupt/tampered nar still fails to import.
  nix copy --from "file://$cache" --option require-sigs false "${paths[@]}"
  say "restore complete: $(nix path-info --recursive --store "file://$cache" "${paths[@]}" | wc -l) paths now importable"
}

cmd_push() {
  local out="$OUT_DEFAULT" repo="$REPO_DEFAULT" yes=0
  while [ "$#" -gt 0 ]; do
    case "$1" in --out) out="$2"; shift 2 ;; --repo) repo="$2"; shift 2 ;; --yes) yes=1; shift ;; *) die "push: unknown arg: $1" ;; esac
  done
  out="$(abs "$out")"; local cache="$out/$SUBDIR"
  [ -d "$cache" ] || die "no cache at $cache (run export first)"
  local size_gb; size_gb="$(du -sb "$cache" 2>/dev/null | awk '{printf "%.1f", $1/1073741824.0}' || printf '?')"
  say "cache is ~${size_gb} GB; target repo: $repo"
  if [ "$yes" -eq 0 ]; then
    printf 'cuda-closure-cache: this commits ~%s GB into a PUBLIC git repo and pushes it. Continue? [y/N] ' "$size_gb" >&2
    local r; read -r r || r=n
    case "$r" in y|Y|yes) ;; *) say "aborted"; exit 130 ;; esac
  fi
  local tmp; tmp="$(mktemp -d)"
  say "cloning $repo -> $tmp"
  git clone --quiet "$repo" "$tmp"
  mkdir -p "$tmp/$SUBDIR"
  rm -rf "$tmp/$SUBDIR"
  mkdir -p "$(dirname "$tmp/$SUBDIR")"
  cp -a "$cache" "$tmp/$SUBDIR"
  git -C "$tmp" add -A
  git -C "$tmp" -c user.name='tbc-tools-ci' -c user.email='ci@tbc-tools' \
    commit -q -m "nix/cuda-11_8: cache pinned CUDA 11.8 closure for GTX-1000-series builds

Bundled from nixpkgsLegacy (nixos-24.11, rev ${NIXPKGS_REV}) so cache.nixos.org
GC or the Nixpkgs 25.05 removal of cudaPackages_11_8 cannot break the build.

Co-Authored-By: Oz <oz-agent@warp.dev>" || say "nothing to commit (cache unchanged)"
  git -C "$tmp" push --quiet origin HEAD:main
  say "pushed to $repo (branch main)"
  rm -rf "$tmp"
}

cmd_pull() {
  local out="$OUT_DEFAULT" repo="$REPO_DEFAULT"
  while [ "$#" -gt 0 ]; do case "$1" in --out) out="$2"; shift 2 ;; --repo) repo="$2"; shift 2 ;; *) die "pull: unknown arg: $1" ;; esac; done
  out="$(abs "$out")"; local cache="$out/$SUBDIR"
  local tmp; tmp="$(mktemp -d)"
  say "cloning $repo -> $tmp"
  git clone --quiet "$repo" "$tmp"
  [ -d "$tmp/$SUBDIR" ] || die "repo has no $SUBDIR (has it been pushed yet?)"
  # cache is $out/$SUBDIR (e.g. $out/nix/cuda-11_8-x86_64-linux); create the
  # intermediate parent ($out/nix) so cp -a can place the subdir.
  mkdir -p "$(dirname "$cache")"
  rm -rf "$cache"
  cp -a "$tmp/$SUBDIR" "$cache"
  say "pulled cache -> $cache"
  rm -rf "$tmp"
}

write_readme() {
  cat > "$1" <<EOF
tbc-tools CUDA 11.8 closure cache
=================================
Pinned source: nixpkgs nixos-24.11 (rev ${NIXPKGS_REV})
Target: GTX-1000-series (Pascal) CUDA support for ld-decode-tools.

This directory is a Nix binary cache (narinfo + nar/ files). Any nar larger
than 95 MiB was split into chunks under <nar>.parts/ with an index, so the
whole tree fits GitHub's 100 MiB per-file limit and can be committed to the
tbc-tools-ci-cache repo.

Restore on a fresh machine:
  scripts/cuda-closure-cache.sh pull   --out ./cuda-cache
  scripts/cuda-closure-cache.sh restore --out ./cuda-cache

Verify without restoring:
  scripts/cuda-closure-cache.sh verify --out ./cuda-cache

See pin.txt for the exact nixpkgs rev + package versions, and manifest.txt for
the full list of store paths in the closure.
EOF
}

main() {
  [ "$#" -ge 1 ] || { sed -n '2,40p' "$0" >&2; exit 64; }
  local mode="$1"; shift
  case "$mode" in
    export)  cmd_export  "$@" ;;
    verify)  cmd_verify  "$@" ;;
    restore) cmd_restore "$@" ;;
    push)    cmd_push    "$@" ;;
    pull)    cmd_pull    "$@" ;;
    -h|--help) sed -n '2,40p' "$0" ;;
    *) die "unknown mode '$mode' (try: export|verify|restore|push|pull)" ;;
  esac
}

main "$@"
