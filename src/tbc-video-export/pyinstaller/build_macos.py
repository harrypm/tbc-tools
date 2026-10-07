from __future__ import annotations

import os
import sys

if sys.platform != "darwin":
    raise SystemExit("Must be run on macOS")

import plistlib
from pathlib import Path

import dunamai
import PyInstaller.__main__
import PyInstaller.utils.osx as osxutils
from vendor_version import write_runtime_hook

# bake the build-time vendor version into the artifact via a runtime hook; the
# frozen binary re-executes consts.py at runtime, where the TBC_VIDEO_EXPORT
# vendor environment variables do not exist, so consts alone resolves to the
# 0.0.0 placeholder.
runtime_hook, vendor_version = write_runtime_hook(Path("build"))

version = dunamai.Version.parse(vendor_version).base

print(f"Building macOS binary version {version}")

# Build a thin native Mach-O --onefile binary for the current runner arch.
# The repo CI runs this once per arch (arm64 + x86_64) and unifies the two
# thin binaries via the existing lipo -create merge in build_macos_tools.yml,
# so we must NOT force --target-arch universal2 here (that requires fat wheels
# for every dep and breaks the per-arch PyInstaller analysis). The previous
# "--windowed" produced a .app bundle; the repo bundles the binary itself into
# its own tbc-tools.app, so a plain Mach-O (console) binary is what we need.
PyInstaller.__main__.run(
    [
        "src/tbc_video_export/__main__.py",
        "--clean",
        "--collect-submodules",
        "application",
        "--icon",
        "assets/icon.icns",
        "--runtime-hook",
        str(runtime_hook),
        "--onefile",
        "--name",
        "tbc-video-export",
    ]
)

# --onefile yields dist/tbc-video-export (a thin Mach-O), not a .app.
bin_path = Path("dist/tbc-video-export")
if not bin_path.exists():
    raise SystemExit(f"PyInstaller did not produce {bin_path}")

# set the version string in the binary's embedded plist if present
app_plist = Path("dist/tbc-video-export.app/Contents/Info.plist")
if app_plist.exists():
    with app_plist.open(mode="rb+") as file:
        plist = plistlib.load(file)
        plist["CFBundleShortVersionString"] = version
        file.seek(0)
        file.write(plistlib.dumps(plist))
        file.truncate()
    # re-sign the .app if PyInstaller produced one
    osxutils.sign_binary("dist/tbc-video-export.app", deep=True)
