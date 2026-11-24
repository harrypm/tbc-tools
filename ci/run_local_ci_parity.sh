#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ACTIONLINT_FALLBACK_VERSION="1.7.12"
MODE="${1:---all}"

usage() {
  echo "Usage: $0 [--all|--guardrails-only|--build-test-only|--python-tests-only]" >&2
}

require_cmd() {
  local cmd="$1"
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "Missing required command: $cmd" >&2
    exit 1
  fi
}

resolve_actionlint_platform() {
  local os arch
  os="$(uname -s)"
  arch="$(uname -m)"

  case "$os" in
    Linux)
      os="linux"
      ;;
    Darwin)
      os="darwin"
      ;;
    *)
      echo "Unsupported OS for actionlint bootstrap: $os" >&2
      exit 1
      ;;
  esac

  case "$arch" in
    x86_64|amd64)
      arch="amd64"
      ;;
    aarch64|arm64)
      arch="arm64"
      ;;
    *)
      echo "Unsupported architecture for actionlint bootstrap: $arch" >&2
      exit 1
      ;;
  esac

  printf "%s_%s" "$os" "$arch"
}

resolve_actionlint_version() {
  if [[ -n "${ACTIONLINT_VERSION:-}" ]]; then
    printf "%s" "$ACTIONLINT_VERSION"
    return
  fi

  local latest_json latest_version
  latest_json="$(curl -fsSL --retry 3 --retry-delay 1 --retry-connrefused https://api.github.com/repos/rhysd/actionlint/releases/latest || true)"
  latest_version="$(printf "%s\n" "$latest_json" | sed -nE 's/.*"tag_name"[[:space:]]*:[[:space:]]*"v?([^"]+)".*/\1/p' | head -n 1 || true)"

  if [[ -n "$latest_version" ]]; then
    printf "%s" "$latest_version"
    return
  fi

  printf "%s" "$ACTIONLINT_FALLBACK_VERSION"
}

download_actionlint_binary() {
  local tmp_root bin_dir platform version archive_name archive_path archive_url extract_dir binary_path
  tmp_root="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"
  bin_dir="$tmp_root/actionlint-bin"
  platform="$(resolve_actionlint_platform)"
  version="$(resolve_actionlint_version)"
  archive_name="actionlint_${version}_${platform}.tar.gz"
  archive_path="$bin_dir/$archive_name"
  archive_url="https://github.com/rhysd/actionlint/releases/download/v${version}/${archive_name}"
  binary_path="$bin_dir/actionlint-${version}-${platform}"

  if [[ -x "$binary_path" ]]; then
    printf "%s" "$binary_path"
    return
  fi

  mkdir -p "$bin_dir"
  extract_dir="$(mktemp -d "$bin_dir/.extract.XXXXXX")"

  if ! curl -fsSL --retry 3 --retry-delay 1 --retry-connrefused -o "$archive_path" "$archive_url"; then
    rm -rf "$extract_dir"
    echo "Failed to download actionlint archive: $archive_url" >&2
    exit 1
  fi

  if ! tar -xzf "$archive_path" -C "$extract_dir"; then
    rm -rf "$extract_dir"
    echo "Failed to extract actionlint archive: $archive_path" >&2
    exit 1
  fi

  if [[ ! -x "$extract_dir/actionlint" ]]; then
    rm -rf "$extract_dir"
    echo "Downloaded actionlint archive did not contain an executable binary." >&2
    exit 1
  fi

  if ! mv "$extract_dir/actionlint" "$binary_path" 2>/dev/null && [[ ! -x "$binary_path" ]]; then
    rm -rf "$extract_dir"
    echo "Failed to install downloaded actionlint binary." >&2
    exit 1
  fi
  chmod +x "$binary_path"
  rm -rf "$extract_dir"

  printf "%s" "$binary_path"
}

run_actionlint() {
  local actionlint_args=(-color -shellcheck=)
  local actionlint_cmd

  if [[ -n "${ACTIONLINT_BIN:-}" ]]; then
    actionlint_cmd="$ACTIONLINT_BIN"
    if [[ ! -x "$actionlint_cmd" ]]; then
      echo "ACTIONLINT_BIN is set but not executable: $actionlint_cmd" >&2
      exit 1
    fi
    "$actionlint_cmd" "${actionlint_args[@]}"
    return
  fi

  if command -v actionlint >/dev/null 2>&1; then
    actionlint_cmd="$(command -v actionlint)"
    "$actionlint_cmd" "${actionlint_args[@]}"
    return
  fi

  require_cmd curl
  require_cmd tar
  actionlint_cmd="$(download_actionlint_binary)"
  "$actionlint_cmd" "${actionlint_args[@]}"
}

run_guardrails() {
  cd "$ROOT_DIR"
  require_cmd python3
  run_actionlint
  python3 -m unittest -v ci.tests.test_check_ci_contracts
  python3 ci/check_ci_contracts.py
  bash -n ci/verify_linux_bundle.sh
}

run_full_build_and_test() {
  cd "$ROOT_DIR"
  require_cmd nix
  nix --version
  nix develop -c cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
  nix develop -c ninja -C build
  nix develop -c ctest --test-dir build --output-on-failure
}

run_python_tests() {
  cd "$ROOT_DIR"
  require_cmd nix
  local venv_bin="$ROOT_DIR/src/tbc-video-export/.venv/bin"
  local build_bin="$ROOT_DIR/build/bin"
  local extra_ld_path=""
  if [[ ! -x "$venv_bin/pytest" ]]; then
    # No developer venv (always the case on CI runners): build a throwaway
    # one instead of skipping. A silent skip here hid a fully broken
    # tbc-video-export test suite (stale ld-* imports) from CI for weeks.
    # PYTHONPATH=src stands in for the editable install.
    local ci_venv="$ROOT_DIR/build/pytest-venv"
    if [[ ! -x "$ci_venv/bin/pytest" ]]; then
      # PYTHONPATH unset: the dev shell's PYTHONPATH carries Nix's pytest,
      # which would make pip skip installing it into the venv.
      if ! nix develop -c sh -c "python3 -m venv '$ci_venv' && env -u PYTHONPATH '$ci_venv/bin/pip' install --quiet \
          'pytest>=8.1,<9' 'pytest-mock>=3.14,<4' 'pymediainfo>=6.1,<7' 'rapidfuzz>=3.14,<4' 'typing-extensions>=4.10'"; then
        if [[ -n "${CI:-}" ]]; then
          echo "Failed to create the pytest venv on CI" >&2
          return 1
        fi
        echo "Skipping python tests: could not create $ci_venv (offline?)" >&2
        return 0
      fi
    fi
    venv_bin="$ci_venv/bin"
    # pymediainfo dlopens libmediainfo; take it from the same pinned package
    # set as the dev shell's python (nixpkgsLegacy) so their shared libs
    # (openssl, ...) agree. The dev .venv relies on a host copy.
    local mediainfo_store
    mediainfo_store="$(nix build --inputs-from "$ROOT_DIR" nixpkgsLegacy#libmediainfo --no-link --print-out-paths | tail -n 1)"
    extra_ld_path="$mediainfo_store/lib"
  fi
  # Run inside the root Nix dev shell so the tests use the same bundled/pulled
  # ffmpeg as the release builds (the AV1 profiles need libsvtav1, which the
  # stock distro ffmpeg lacks), with build/bin providing the renamed tools.
  nix develop -c sh -c "export PATH=\"$venv_bin:$build_bin:\$PATH\" PYTHONPATH=\"$ROOT_DIR/src/tbc-video-export/src\" LD_LIBRARY_PATH=\"$extra_ld_path\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}\"; cd src/tbc-video-export && '$venv_bin/python' -m pytest tests -q"
}

if [[ "$#" -gt 1 ]]; then
  usage
  exit 2
fi

case "$MODE" in
  --all)
    run_guardrails
    run_full_build_and_test
    run_python_tests
    ;;
  --guardrails-only)
    run_guardrails
    ;;
  --build-test-only)
    # The tbc-video-export pytest suite is part of build verification
    # (it was previously only run by --all, i.e. never on CI).
    run_full_build_and_test
    run_python_tests
    ;;
  --python-tests-only)
    run_python_tests
    ;;
  *)
    usage
    exit 2
    ;;
esac
