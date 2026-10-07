from __future__ import annotations

import os
from pathlib import Path

if os.name != "posix":
    raise SystemExit("Must be run on Linux")

import dunamai
import PyInstaller.__main__
from vendor_version import write_runtime_hook

# bake the build-time vendor version into the artifact via a runtime hook; the
# frozen binary re-executes consts.py at runtime, where the TBC_VIDEO_EXPORT
# vendor environment variables do not exist, so consts alone resolves to the
# 0.0.0 placeholder.
runtime_hook, vendor_version = write_runtime_hook(Path("build"))

# use .99 as the 4th integer if non-final release
is_final = dunamai.Version.parse(vendor_version).stage == ""
version = (
    f"{dunamai.Version.parse(vendor_version).base}{'' if is_final else '.99'}"
)

print(f"Building Linux binary version {version}")

# Build a single self-contained ELF binary (matches the Windows --onefile flow).
# The Linux release previously shipped a bash wrapper that exec'd a *host* Python
# with host site-packages, which fails on clean machines without scipy/etc. This
# frozen binary bundles the interpreter and all required deps.
PyInstaller.__main__.run(
    [
        "src/tbc_video_export/__main__.py",
        "--clean",
        "--collect-submodules",
        "application",
        "--runtime-hook",
        str(runtime_hook),
        "--onefile",
        "--name",
        "tbc-video-export",
    ]
)
