#!/usr/bin/env python3
"""End-to-end export tests for tbc-tools.

Three suites, all driven against real binaries and real TBC fixtures:

  contract  Every tbc-video-export option the tbc-analyse Export tab probes or
            passes exists in `tbc-video-export --help` (a renamed/removed
            option otherwise silently degrades the GUI export).
  cli       tbc-video-export run directly: every profile in its default config
            plus an option matrix (framing, chroma decoders, dropout, field
            order, audio, metadata, two-step, ...) across PAL/NTSC/PAL-M/SECAM
            fixtures; each output is verified with ffprobe.
  gui       tbc-analyse driven headlessly (QT_QPA_PLATFORM=offscreen) through
            its hidden --e2e-export driver: the real Export tab loads a source,
            has its widgets set as a user would, and exports. The matrix is
            discovered from the GUI itself (profiles -> variants/containers/bit
            depths, framing -> output resolution modes, audio profiles, dropout
            modes, proxy codecs, advanced options, in/out points, custom JSON
            profile sets). Any "not supported by the detected tbc-video-export"
            fallback, failure log line, warning dialog or bad output fails.

Every run starts from a hostile working directory holding decoy
`tbc-video-export`/`ffmpeg`/`ffprobe` *directories* (regression: an AppImage
export resolved ~/tbc-video-export, a directory, and could not start).
With --strict-bundled (release packages) PATH is also prefixed with decoy
executables that fail loudly, proving the package only uses its own tools.

Exit codes: 0 pass, 1 failures, 77 skipped (missing binaries).
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

SKIP_EXIT_CODE = 77
EXE = ".exe" if os.name == "nt" else ""

# key: (directory relative to the source root, base name, video system)
FIXTURES = {
    "pal_yc": ("src/tbc-video-export/tests/files", "pal_svideo", "PAL"),
    "pal_yc5": ("src/tbc-video-export/tests/files", "pal_svideo_5", "PAL"),
    "pal_cvbs": ("src/tbc-video-export/tests/files", "pal_composite", "PAL"),
    "pal_ld": ("src/tbc-video-export/tests/files", "pal_composite_ld", "PAL"),
    "ntsc_yc": ("src/tbc-video-export/tests/files", "ntsc_svideo", "NTSC"),
    "ntsc_cvbs": ("src/tbc-video-export/tests/files", "ntsc_composite", "NTSC"),
    "ntsc_ld": ("src/tbc-video-export/tests/files", "ntsc_composite_ld", "NTSC"),
    "palm_yc": ("src/tbc-video-export/tests/files", "palm_svideo", "PAL-M"),
    "secam_yc": ("test-data/secam", "secam-etienne-cbars-10f", "SECAM"),
    # Broadcast-level composite SECAM, built at test time (build_secam_composite)
    # from the Etienne pair: the exporter takes the composite path (SECAM FM
    # decoder separating the block from the CVBS signal)
    "secam_cvbs": ("test-data/secam", "secam-etienne-cbars-10f", "SECAM", (".tbc.json", ".tbc.db")),
    "mesecam_yc": ("test-data/secam", "mesecam-tape275-testcard-10f", "MESECAM"),
    # pal_yc with MONO chosen in its metadata (as the chroma decoder dialog saves it)
    "pal_yc_mono": ("src/tbc-video-export/tests/files", "pal_svideo", "PAL"),
}


def build_secam_composite(rel_dir: Path, base: str, dest: Path):
    """CVBS = Y + C at BT.470 level, from the Etienne capture's pair.

    The committed combined .tbc is a luma-path decode output: its SECAM FM
    block sits 45-50 dB below the restored _chroma.tbc, near the in-band
    noise, so it is not a usable composite (the decoder calibrates the rest
    carriers ~100 kHz low on it and the colours come out wrong). A real
    composite carries the undeviated subcarrier at ~23% of the luminance
    amplitude peak-to-peak (BT.470), so the restored block is scaled to that
    rest level, measured on the white bar (where SECAM sends the bare rest
    carrier), and added to the luma. Deterministic; numpy-free so it runs on
    any packaged-test runner.
    """
    width, height = 1135, 313
    params = json.loads((rel_dir / f"{base}.tbc.json").read_text())["videoParameters"]
    luma_range = params["white16bIre"] - params["black16bIre"]
    try:  # fast path; the pure-Python one below gives the same result
        import numpy as np

        luma_np = np.fromfile(rel_dir / f"{base}.tbc", dtype="<u2").astype(np.float64)
        chroma_np = np.fromfile(rel_dir / f"{base}_chroma.tbc", dtype="<u2").astype(np.float64)
        bar = np.sort(chroma_np.reshape(-1, height, width)[:, 40:280, 190:250].ravel())
        scale = 0.23 * luma_range / (bar[len(bar) * 99 // 100] - bar[len(bar) // 100])
        composite = np.clip(np.round(luma_np + scale * (chroma_np - 32768)), 0, 65535)
        composite.astype("<u2").tofile(dest / f"{base}.tbc")
        return
    except ImportError:
        pass
    from array import array
    luma, chroma = array("H"), array("H")
    luma.frombytes((rel_dir / f"{base}.tbc").read_bytes())
    chroma.frombytes((rel_dir / f"{base}_chroma.tbc").read_bytes())
    if sys.byteorder != "little":
        luma.byteswap()
        chroma.byteswap()
    white_bar = sorted(chroma[f * height * width + row * width + x]
                       for f in range(len(chroma) // (height * width))
                       for row in range(40, 280) for x in range(190, 250))
    rest_pp = white_bar[len(white_bar) * 99 // 100] - white_bar[len(white_bar) // 100]
    scale = 0.23 * luma_range / rest_pp
    out = array("H", (min(65535, max(0, round(y + scale * (c - 32768)))) for y, c in zip(luma, chroma)))
    if sys.byteorder != "little":
        out.byteswap()
    (dest / f"{base}.tbc").write_bytes(out.tobytes())


FIXTURE_BUILDERS = {"secam_cvbs": build_secam_composite}

# videoParameters overrides written into a fixture's copied .tbc.json
FIXTURE_JSON_OVERRIDES = {"pal_yc_mono": {"chromaDecoder": "mono"}}
FIXTURE_SUFFIXES = (".tbc", "_chroma.tbc", ".tbc.json", ".tbc.db", "_chroma.tbc.db", "_chroma.tbc.json")
AUDIO_TRACK = "src/tbc-video-export/tests/files/audio_48_16.wav"
# D1 frame height: 625-line 720x576, 525-line 720x486 (SMPTE 259M)
FRAME_HEIGHT = {"PAL": 576, "SECAM": 576, "MESECAM": 576, "NTSC": 486, "PAL-M": 486}
# ffmpeg flags interlaced TFF as "tb" (BFF "bt") for every codec but MJPEG
TFF_ORDERS = ("tt", "tb")
BFF_ORDERS = ("bb", "bt")

# ffmpeg encoder -> ffprobe codec_name
ENCODER_CODEC = {
    "ffv1": "ffv1", "prores_ks": "prores", "v210": "v210", "v410": "v410",
    "jpeg2000": "jpeg2000", "libopenjpeg": "jpeg2000", "mpeg2video": "mpeg2video",
    "libx264": "h264", "libx265": "hevc", "libsvtav1": "av1", "libaom-av1": "av1",
    "librav1e": "av1", "flac": "flac", "pcm_s16le": "pcm_s16le", "pcm_s24le": "pcm_s24le",
    "aac": "aac", "libopus": "opus", "opus": "opus",
}
# tbc-analyse profileComboBox data -> ffprobe codec_name
GUI_PROFILE_CODEC = {
    "ffv1": "ffv1", "prores": "prores", "d10": "mpeg2video", "v210": "v210",
    "j2k": "jpeg2000", "avc": "h264", "hevc": "hevc",
}
WEB_CODEC = {"h264": "h264", "hevc": "hevc", "av1": "av1"}
VIDEO_EXTS = (".mkv", ".mov", ".mxf", ".mp4")

# Colour-bar references in the SECAM fixtures, sampled on the active picture
# scaled to 720x576: bar centre x positions (white, yellow, cyan, green,
# magenta, red, blue) and the y span to average over.
#   etienne: full-field EBU bars (Etienne Quadruplex, SECAM)
#   tfb:     bar band of the 1987 TFB test card (tape 275, ME-SECAM)
# The last value is the minimum saturation (max-min RGB spread) a coloured
# bar must keep: the Quadruplex bars are clean, the VHS card has reduced
# chroma bandwidth (its cyan measures ~75).
BAR_LAYOUTS = {
    "etienne": ((45, 135, 222, 311, 400, 488, 578), 100, 400, 80),
    "tfb": ((162, 217, 272, 327, 382, 437, 492), 115, 195, 45),
}
BAR_PATTERNS = (("white", (1, 1, 1)), ("yellow", (1, 1, 0)), ("cyan", (0, 1, 1)), ("green", (0, 1, 0)),
                ("magenta", (1, 0, 1)), ("red", (1, 0, 0)), ("blue", (0, 0, 1)))


def bar_problems(env, path: Path, layout: str):
    """Check decoded colour bars: each bar must show its primary pattern
    (channel on >= 55% of the bar's brightest channel), coloured bars must
    be saturated and white neutral. Catches lost chroma, swapped D'R/D'B
    line identity and hue errors that a valid-file check cannot see."""
    code, raw = run_bytes(env.ffmpeg + ["-v", "error", "-i", str(path), "-frames:v", "1",
                                        "-vf", "scale=720:576,format=rgb24", "-f", "rawvideo", "-"])
    if code != 0 or len(raw) != 720 * 576 * 3:
        return [f"could not extract a frame for the colour-bar check (ffmpeg exit {code})"]
    centres, y0, y1, min_spread = BAR_LAYOUTS[layout]
    problems = []
    for (name, pattern), cx in zip(BAR_PATTERNS, centres):
        total, count = [0, 0, 0], 0
        for y in range(y0, y1, 2):
            for x in range(cx - 12, cx + 12, 2):
                i = (y * 720 + x) * 3
                total = [t + raw[i + c] for c, t in enumerate(total)]
                count += 1
        rgb = [round(t / count) for t in total]
        brightest, spread = max(rgb), max(rgb) - min(rgb)
        measured = tuple(int(v >= 0.55 * brightest) for v in rgb)
        if brightest < 60 or measured != pattern:
            problems.append(f"{name} bar decoded as RGB{tuple(rgb)}")
        elif name == "white" and spread > 50:
            problems.append(f"white bar has a colour cast RGB{tuple(rgb)}")
        elif name != "white" and spread < min_spread:
            problems.append(f"{name} bar is desaturated RGB{tuple(rgb)}")
    return problems


# Real, diagnosed product issues awaiting a decision. These cases still run:
# a failure is reported as XFAIL (not failing the suite), a pass as XPASS so
# the entry gets removed. Never park a new failure here without a diagnosis.
KNOWN_ISSUES = {
}
SUB_PROFILE_COMBOS = ("proresVariantComboBox", "avcRangeComboBox", "hevcRangeComboBox", "webCodecComboBox")


@dataclass
class Case:
    suite: str
    name: str
    fixture: str
    widgets: list = field(default_factory=list)  # gui: [(objectName, value)]
    args: list = field(default_factory=list)  # cli: extra tbc-video-export args
    advanced: dict = field(default_factory=dict)
    profile_config: str = ""
    audio: int = 0  # number of audio tracks attached
    expect: dict = field(default_factory=dict)
    allow_log: list = field(default_factory=list)
    expect_log: list = field(default_factory=list)  # gui: text the export log must contain
    expect_log_regex: list = field(default_factory=list)  # gui: regexes the export log must match


@dataclass
class Outcome:
    case: Case
    ok: bool
    seconds: float
    detail: str = ""
    known: str = ""  # KNOWN_ISSUES reason when the case is a tracked issue

    @property
    def mark(self):
        if self.known:
            return "XPASS" if self.ok else "XFAIL"
        return "PASS" if self.ok else "FAIL"

    @property
    def counts_as_failure(self):
        return not self.ok and not self.known


class Env:
    def __init__(self, args: argparse.Namespace):
        self.source = Path(args.source).resolve()
        self.work = Path(args.work_dir).resolve() if args.work_dir else Path(
            tempfile.mkdtemp(prefix="tbc-export-e2e-"))
        self.work.mkdir(parents=True, exist_ok=True)
        build_bin = Path(args.build).resolve() / "bin" if args.build else None
        # Each tool is a command prefix: [exe] or [AppImage, subcommand].
        if args.appimage:
            appimage = str(Path(args.appimage).resolve())
            self.analyse = [appimage, "tbc-analyse"]
            self.video_export = [appimage, "tbc-video-export"]
            self.ffprobe = [appimage, "ffprobe"]
        else:
            self.analyse = self._pick(args.analyse, build_bin, "tbc-analyse")
            self.video_export = self._pick(args.video_export, build_bin, "tbc-video-export")
            ffprobe = args.ffprobe or self._adjacent("ffprobe") or shutil.which("ffprobe")
            self.ffprobe = [ffprobe] if ffprobe else None
        if args.ffprobe:
            self.ffprobe = [args.ffprobe]
        # ffmpeg for frame sampling: the ffprobe's sibling ([AppRun, "ffmpeg"]
        # for an AppImage)
        self.ffmpeg = None
        if self.ffprobe:
            if len(self.ffprobe) > 1:
                self.ffmpeg = self.ffprobe[:-1] + ["ffmpeg"]
            else:
                probe_path = Path(self.ffprobe[0])
                sibling = probe_path.with_name(probe_path.name.replace("ffprobe", "ffmpeg"))
                self.ffmpeg = [str(sibling)] if sibling.is_file() else ([shutil.which("ffmpeg")]
                                                                       if shutil.which("ffmpeg") else None)
        self.qpa = args.qpa if args.qpa is not None else ("offscreen" if sys.platform.startswith("linux") else "")
        self.strict = args.strict_bundled
        self.case_timeout = args.case_timeout
        self.hostile = self.work / "hostile-cwd"
        self.decoy_bin = self.work / "decoy-bin"
        self.home = self.work / "home"
        self.fixtures = self.work / "fixtures"

    @staticmethod
    def _pick(explicit, build_bin, name):
        if explicit:
            return [str(Path(explicit).resolve())]
        if build_bin:
            for candidate in (build_bin / (name + EXE), build_bin / name):
                if candidate.is_file():
                    return [str(candidate)]
        return None

    def _adjacent(self, name):
        if not self.video_export:
            return None
        candidate = Path(self.video_export[0]).parent / (name + EXE)
        return str(candidate) if candidate.is_file() else None

    def prepare(self):
        # Decoy *directories* in the working directory: must never be resolved.
        for name in ("tbc-video-export", "ffmpeg", "ffprobe", "tbc-chroma-decoder"):
            (self.hostile / (name + EXE)).mkdir(parents=True, exist_ok=True)
        if self.strict:
            self.decoy_bin.mkdir(parents=True, exist_ok=True)
            for name in ("tbc-video-export", "ffmpeg", "ffprobe", "tbc-chroma-decoder",
                         "tbc-dropout-correct", "tbc-export-metadata", "tbc-process-vbi"):
                if os.name == "nt":
                    (self.decoy_bin / f"{name}.bat").write_text(
                        f"@echo DECOY {name} resolved from PATH 1>&2\r\n@exit /b 97\r\n")
                else:
                    decoy = self.decoy_bin / name
                    decoy.write_text(f"#!/bin/sh\necho 'DECOY {name} resolved from PATH' >&2\nexit 97\n")
                    decoy.chmod(decoy.stat().st_mode | stat.S_IEXEC)
        missing = []
        for key, (rel_dir, base, _system, *only) in FIXTURES.items():
            # Y/C fixtures need their chroma TBC; all need the TBC + metadata.
            required = (".tbc", ".tbc.json") + (("_chroma.tbc",) if key.endswith("_yc") or key in FIXTURE_BUILDERS else ())
            missing += [str(Path(rel_dir) / (base + suffix)) for suffix in required
                        if not (self.source / rel_dir / (base + suffix)).is_file()]
            dest = self.fixtures / key
            dest.mkdir(parents=True, exist_ok=True)
            for suffix in (only[0] if only else FIXTURE_SUFFIXES):
                src = self.source / rel_dir / (base + suffix)
                if src.is_file() and not (dest / src.name).exists():
                    shutil.copy2(src, dest / src.name)
            if key in FIXTURE_BUILDERS and not (dest / (base + ".tbc")).exists():
                FIXTURE_BUILDERS[key](self.source / rel_dir, base, dest)
            if key in FIXTURE_JSON_OVERRIDES:
                json_path = dest / (base + ".tbc.json")
                data = json.loads(json_path.read_text())
                data.setdefault("videoParameters", {}).update(FIXTURE_JSON_OVERRIDES[key])
                json_path.write_text(json.dumps(data))
        return missing

    def fixture_tbc(self, key):
        return self.fixtures / key / (FIXTURES[key][1] + ".tbc")

    def environ(self, case_home: Path):
        env = dict(os.environ)
        case_home.mkdir(parents=True, exist_ok=True)
        env.update({
            "HOME": str(case_home), "USERPROFILE": str(case_home),
            "XDG_CONFIG_HOME": str(case_home / ".config"),
            "XDG_DATA_HOME": str(case_home / ".local" / "share"),
            "XDG_CACHE_HOME": str(case_home / ".cache"),
            "APPDATA": str(case_home / "AppData" / "Roaming"),
            "LOCALAPPDATA": str(case_home / "AppData" / "Local"),
            "APPIMAGE_EXTRACT_AND_RUN": "1",  # CI runners have no FUSE
            "TBC_ANALYSE_NO_UPDATE_CHECK": "1",  # no network / update dialog mid-export
        })
        if self.qpa:
            env["QT_QPA_PLATFORM"] = self.qpa
        if self.strict:
            env["PATH"] = str(self.decoy_bin) + os.pathsep + env.get("PATH", "")
        return env


def run(cmd, env, cwd, timeout):
    try:
        proc = subprocess.run(cmd, env=env, cwd=cwd, capture_output=True, text=True,
                              timeout=timeout, errors="replace")
        return proc.returncode, proc.stdout, proc.stderr
    except subprocess.TimeoutExpired as error:
        def text(data):
            return data.decode(errors="replace") if isinstance(data, bytes) else (data or "")
        return -1, text(error.stdout), text(error.stderr) + f"\ntimed out after {timeout}s"


def run_bytes(cmd):
    proc = subprocess.run(cmd, capture_output=True, timeout=300)
    return proc.returncode, proc.stdout


def probe(env: Env, path: Path):
    cmd = env.ffprobe + ["-v", "error", "-count_frames", "-show_entries",
           "format=format_name:stream=codec_type,codec_name,width,height,pix_fmt,field_order,display_aspect_ratio,"
           "nb_read_frames,sample_rate,channels", "-of", "json", str(path)]
    code, out, err = run(cmd, os.environ, None, 300)
    if code != 0:
        raise AssertionError(f"ffprobe failed on {path.name}: {err.strip()}")
    return json.loads(out)


# ffprobe field_order -> which field is displayed first (FFmpeg's labels name
# the coded/stored field first: "tb" = top stored, bottom displayed first)
DISPLAYED_FIRST = {"tt": "tff", "bt": "tff", "bb": "bff", "tb": "bff"}


def verify_output(env: Env, case: Case, out_base: Path, export_output: str = ""):
    """Check the produced files against case.expect; returns a list of problems."""
    problems = []
    # the final output is <base>.<ext>; two-step keeps <base>.luma.<ext> and
    # proxies sit beside it, so never pick those
    outputs = sorted(p for p in out_base.parent.glob(out_base.name + ".*")
                     if p.suffix in VIDEO_EXTS and p.stem == out_base.name)
    expect = case.expect
    if not outputs:
        return [f"no video output for base {out_base.name} (dir has: "
                f"{sorted(p.name for p in out_base.parent.iterdir())})"]
    main = outputs[0]
    if main.stat().st_size == 0:
        problems.append(f"{main.name} is empty")
    if expect.get("ext") and main.suffix != "." + expect["ext"]:
        problems.append(f"container {main.suffix}, expected .{expect['ext']}")
    data = probe(env, main)
    videos = [s for s in data.get("streams", []) if s.get("codec_type") == "video"]
    audios = [s for s in data.get("streams", []) if s.get("codec_type") == "audio"]
    if not videos:
        return problems + [f"{main.name} has no video stream"]
    video = videos[0]
    if expect.get("codec") and video.get("codec_name") != expect["codec"]:
        problems.append(f"video codec {video.get('codec_name')}, expected {expect['codec']}")
    width, height = int(video.get("width") or 0), int(video.get("height") or 0)
    if width <= 0 or height <= 0:
        problems.append(f"bad dimensions {width}x{height}")
    if expect.get("width") and width != expect["width"]:
        problems.append(f"width {width}, expected {expect['width']}")
    if expect.get("height") and height != expect["height"]:
        problems.append(f"height {height}, expected {expect['height']}")
    if expect.get("min_height") and height < expect["min_height"]:
        problems.append(f"height {height}, expected >= {expect['min_height']}")
    if expect.get("dar") and video.get("display_aspect_ratio") != expect["dar"]:
        problems.append(f"display aspect {video.get('display_aspect_ratio')}, expected {expect['dar']}")
    pix_fmt = video.get("pix_fmt", "")
    if expect.get("pix_fmt_has") and expect["pix_fmt_has"] not in pix_fmt:
        problems.append(f"pix_fmt {pix_fmt} lacks '{expect['pix_fmt_has']}'")
    if expect.get("pix_fmt_not") and expect["pix_fmt_not"] in pix_fmt:
        problems.append(f"pix_fmt {pix_fmt} should not contain '{expect['pix_fmt_not']}'")
    if expect.get("gray") and not pix_fmt.startswith("gray"):
        problems.append(f"pix_fmt {pix_fmt}, expected grayscale")
    # Interlaced exports must carry their field order (a remux once dropped
    # it from every MKV); deinterlaced web outputs must be progressive.
    order = video.get("field_order", "unknown")
    wanted_orders = expect.get("field_order", TFF_ORDERS + BFF_ORDERS)
    wanted_orders = (wanted_orders,) if isinstance(wanted_orders, str) else wanted_orders
    if order not in wanted_orders:
        problems.append(f"field_order {order}, expected one of {', '.join(wanted_orders)}")
    if expect.get("field_order_matches_resolved"):
        # the file must display the field order the exporter resolved
        resolved = re.search(r"Frame Type:\s+\S+ -> (tff|bff)", export_output)
        if not resolved:
            problems.append("exporter did not report its resolved field order")
        elif DISPLAYED_FIRST.get(order) != resolved.group(1):
            problems.append(f"file displays {DISPLAYED_FIRST.get(order, order)} first (field_order {order}), "
                            f"exporter resolved {resolved.group(1)}")
    if expect.get("frames") is not None:
        frames = int(video.get("nb_read_frames") or 0)
        if frames != expect["frames"]:
            problems.append(f"{frames} frames, expected {expect['frames']}")
    if case.audio and len(audios) != case.audio:
        problems.append(f"{len(audios)} audio stream(s), expected {case.audio}")
    if expect.get("audio_codec") and audios and audios[0].get("codec_name") != expect["audio_codec"]:
        problems.append(f"audio codec {audios[0].get('codec_name')}, expected {expect['audio_codec']}")
    if expect.get("proxy_codec"):
        proxies = [p for p in out_base.parent.glob(out_base.name + "*")
                   if p.suffix in VIDEO_EXTS and p != main]
        if not proxies:
            problems.append("proxy output missing")
        else:
            proxy_video = [s for s in probe(env, proxies[0]).get("streams", [])
                           if s.get("codec_type") == "video"]
            if not proxy_video or proxy_video[0].get("codec_name") != expect["proxy_codec"]:
                problems.append(f"proxy codec {proxy_video[0].get('codec_name') if proxy_video else None},"
                                f" expected {expect['proxy_codec']}")
    if expect.get("bars"):
        bars = bar_problems(env, main, expect["bars"])
        if expect.get("bars_must_fail"):
            if not bars:
                problems.append("colour-bar check passed on a chroma-less export: the check is not sensitive")
        else:
            problems += bars
    for sidecar in expect.get("sidecars", []):
        if not (out_base.parent / (out_base.name + sidecar)).is_file():
            problems.append(f"missing sidecar {out_base.name}{sidecar}")
    return problems


# --------------------------------------------------------------------- contract

def contract_suite(env: Env):
    started = time.monotonic()
    case = Case("contract", "gui-options-exist-in-cli-help", "-")
    source = (env.source / "src" / "tbc-analyse" / "exportdialog.cpp").read_text(errors="replace")
    probed = set(re.findall(r'executableSupportsOption\([^;]*?QStringLiteral\("(--[a-z0-9-]+)"\)', source))
    passed = set(re.findall(r'(?:args|exportArgs|proxyArgs)\s*<<\s*QStringLiteral\("(--[a-z0-9-]+)"\)', source))
    code, out, err = run(env.video_export + ["--help"], env.environ(env.home / "contract"), env.hostile, 120)
    if code != 0:
        return [Outcome(case, False, time.monotonic() - started, f"--help failed: {err.strip()}")]
    help_options = set(re.findall(r"(--[a-z0-9][a-z0-9-]*)", out))
    # Probed for older exporters and tolerated missing; accepted but hidden
    # from --help (argparse.SUPPRESS).
    legacy_optional = {"--intra", "--innerfield", "--interfield"}
    hidden_accepted = {"--audio-track-advanced"}
    help_options |= hidden_accepted
    missing = sorted((probed | passed) - help_options - legacy_optional)
    detail = (f"GUI uses {len(probed | passed)} options; missing from --help: {', '.join(missing)}"
              if missing else f"{len(probed | passed)} GUI options all present")
    return [Outcome(case, not missing, time.monotonic() - started, detail)]


# -------------------------------------------------------------------------- cli

def cli_cases(env: Env, level: str):
    dump_dir = env.work / "cli-default-config"
    dump_dir.mkdir(parents=True, exist_ok=True)
    code, _out, err = run(env.video_export + ["--dump-default-config"], env.environ(env.home / "dump"), dump_dir, 120)
    if code != 0:
        raise RuntimeError(f"--dump-default-config failed: {err.strip()}")
    config = json.loads((dump_dir / "tbc-video-export.json").read_text())
    video_profiles = {v["name"]: v for v in config["video_profiles"]}
    audio_profiles = {a["name"]: a for a in config["audio_profiles"]}

    cases = []
    profile_fixtures = ["pal_cvbs", "ntsc_cvbs"] if level == "full" else ["pal_cvbs"]
    for profile in config["profiles"]:
        names = profile["video_profile"] if isinstance(profile["video_profile"], list) else [profile["video_profile"]]
        for fixture in profile_fixtures:
            system = FIXTURES[fixture][2].lower().replace("-", "")
            # The exporter picks the software, system-matching variant by default.
            software = [video_profiles[n] for n in names if not video_profiles[n].get("hardware_accel")]
            matching = [v for v in software if v.get("video_system") in (None, system)
                        and not any(v["name"].endswith("_" + s) and s != system
                                    for s in ("pal", "ntsc", "palm", "secam"))]
            chosen = (matching or software or [None])[0]
            if chosen is None:
                continue
            audio = audio_profiles.get(profile.get("audio_profile"), {})
            web = bool(profile.get("filter_profiles"))
            # D10 (IMX50) has a fixed raster: active picture padded under the VBI
            d10 = ({"width": 720, "height": 608 if FRAME_HEIGHT[FIXTURES[fixture][2]] == 576 else 512,
                    "dar": "4:3"} if chosen.get("output_format") == "mxf_d10" else {})
            cases.append(Case(
                "cli", f"profile-{profile['name']}-{fixture}", fixture,
                args=["--profile", profile["name"], "--audio-track", str(env.source / AUDIO_TRACK)], audio=1,
                expect={"codec": ENCODER_CODEC.get(chosen["codec"], chosen["codec"]),
                        "ext": chosen.get("container"),
                        "audio_codec": ENCODER_CODEC.get(audio.get("codec"), audio.get("codec")),
                        **({"field_order": "progressive"} if web else {}), **d10}))
        if level != "full":
            break  # smoke: default profile only

    h = FRAME_HEIGHT
    option_cases = [
        ("d1-pal", "pal_yc", ["--d1"], {"width": 720, "height": h["PAL"]}),
        ("d1-ntsc", "ntsc_yc", ["--d1"], {"width": 720, "height": h["NTSC"]}),
        ("d1-secam", "secam_yc", ["--d1", "--length", "2"], {"width": 720, "height": h["SECAM"]}),
        # --d1 must not resize D10's own raster away
        ("d10-d1-pal", "pal_cvbs", ["--profile", "d10", "--d1"],
         {"codec": "mpeg2video", "ext": "mxf", "width": 720, "height": 608, "dar": "4:3"}),
        ("d10-d1-ntsc", "ntsc_cvbs", ["--profile", "d10", "--d1"],
         {"codec": "mpeg2video", "ext": "mxf", "width": 720, "height": 512, "dar": "4:3"}),
        ("vbi", "pal_cvbs", ["--vbi"], {"min_height": h["PAL"]}),
        ("full-frame", "pal_cvbs", ["--full-frame"], {"min_height": 600}),
        ("luma-only", "pal_cvbs", ["--luma-only"], {"gray": True}),
        ("luma-4fsc", "pal_cvbs", ["--luma-4fsc"], {"gray": True}),
        ("letterbox", "pal_cvbs", ["--letterbox"], {}),
        ("full-vertical", "pal_cvbs", ["--full-vertical"], {}),
        ("no-dropout-correct", "pal_cvbs", ["--no-dropout-correct"], {}),
        ("overcorrect", "pal_cvbs", ["--overcorrect"], {}),
        # reversing field order needs >1 frame (the first field is dropped)
        ("reverse", "pal_yc5", ["--reverse", "--no-dropout-correct", "--length", "2"], {}),
        ("field-order-tff", "pal_cvbs", ["--field-order", "tff"], {"field_order": TFF_ORDERS}),
        ("field-order-bff", "pal_cvbs", ["--field-order", "bff"], {"field_order": BFF_ORDERS}),
        ("field-order-prog", "pal_cvbs", ["--field-order", "prog"], {"field_order": "progressive"}),
        ("two-step", "pal_yc", ["--two-step"], {}),
        ("export-metadata", "pal_cvbs", ["--export-metadata"], {}),
        # CC subtitles: mov_text in mp4/mov, left out of mxf (which cannot carry them)
        ("metadata-h264-mp4", "pal_ld", ["--export-metadata", "--profile", "h264", "--profile-container", "mp4"],
         {"codec": "h264", "ext": "mp4"}),
        ("metadata-h264-mov", "pal_ld", ["--export-metadata", "--profile", "h264", "--profile-container", "mov"],
         {"codec": "h264", "ext": "mov"}),
        ("metadata-h264-mxf", "pal_ld", ["--export-metadata", "--profile", "h264", "--profile-container", "mxf"],
         {"codec": "h264", "ext": "mxf"}),
        # two-step merge adds the luma file as input 0: audio/CC map indexes shift
        ("two-step-metadata-audio", "pal_yc", ["--two-step", "--export-metadata",
                                               "--audio-track", str(env.source / AUDIO_TRACK)], {"_audio": 1}),
        ("process-vbi", "pal_ld", ["--process-vbi"], {}),
        ("checksum", "pal_cvbs", ["--checksum"], {}),
        ("no-attach-json", "pal_cvbs", ["--no-attach-json"], {}),
        ("force-anamorphic", "pal_cvbs", ["--force-anamorphic"], {}),
        ("yuv420", "pal_cvbs", ["--profile", "h264", "--yuv420", "--8bit"], {"pix_fmt_has": "420"}),
        ("append-vf", "pal_cvbs", ["--append-video-filter", "scale=360:288"], {"width": 360, "height": 288}),
        ("metadata", "pal_cvbs", ["--metadata", "Title", "e2e"], {}),
        ("two-audio-tracks", "pal_cvbs", ["--audio-track", str(env.source / AUDIO_TRACK),
                                          "--audio-track", str(env.source / AUDIO_TRACK)], {"_audio": 2}),
        ("output-padding", "pal_cvbs", ["--output-padding", "16"], {}),
        ("chroma-gain-phase", "pal_cvbs", ["--chroma-gain", "1.2", "--chroma-phase", "5"], {}),
        ("ld-ntsc", "ntsc_ld", [], {"height": None}),
        ("palm", "palm_yc", [], {}),
        ("secam", "secam_yc", ["--length", "2"], {}),
        ("mesecam", "mesecam_yc", ["--length", "2"], {}),
    ]
    # SECAM / ME-SECAM colour accuracy on real bars (see BAR_LAYOUTS)
    option_cases += [
        ("secam-bars", "secam_yc", ["--length", "2"], {"bars": "etienne"}),
        ("secam-bars-decoder", "secam_yc", ["--length", "2", "--chroma-decoder", "SECAM"], {"bars": "etienne"}),
        ("secam-bars-d1", "secam_yc", ["--length", "2", "--d1"],
         {"bars": "etienne", "width": 720, "height": h["SECAM"]}),
        ("secam-bars-web", "secam_yc", ["--length", "2", "--profile", "h264_web"],
         {"bars": "etienne", "codec": "h264", "field_order": "progressive"}),
        ("secam-bars-prores", "secam_yc", ["--length", "2", "--profile", "prores_hq"],
         {"bars": "etienne", "codec": "prores"}),
        ("secam-bars-two-step", "secam_yc", ["--length", "2", "--two-step"], {"bars": "etienne"}),
        ("secam-bars-cvbs", "secam_cvbs", ["--length", "2"], {"bars": "etienne"}),
        ("mesecam-bars", "mesecam_yc", ["--length", "2"], {"bars": "tfb"}),
        ("mesecam-bars-web", "mesecam_yc", ["--length", "2", "--profile", "h264_web"],
         {"bars": "tfb", "codec": "h264", "field_order": "progressive"}),
        # negative control: grey output must fail the bar check
        ("secam-bars-check-rejects-luma-only", "secam_yc", ["--length", "2", "--luma-only"],
         {"bars": "etienne", "bars_must_fail": True}),
    ]
    # MONO on a Y/C source exports the Y TBC only (luma mode) instead of erroring
    for fixture in ("pal_yc", "ntsc_yc", "secam_yc"):
        extra = ["--length", "2"] if fixture.startswith("secam") else []
        option_cases.append((f"mono-yc-luma-only-{fixture}", fixture,
                             ["--chroma-decoder", "MONO"] + extra, {"gray": True}))
    # MXF interlace metadata (common/mxf_interlace.py): FFmpeg describes
    # ProRes/J2K as progressive and flags TFF as bottom-first; the exporter
    # re-flags it. Both field orders (SECAM/MESECAM fixtures resolve BFF on
    # some profiles), every MXF-capable codec, D10 incl. SECAM (PAL variant).
    for codec, profile, mxf_fixtures in (
        ("ffv1", "ffv1", ("pal_cvbs", "ntsc_cvbs", "palm_yc", "secam_yc")),
        ("prores", "prores_hq", ("pal_cvbs", "ntsc_cvbs", "secam_yc", "mesecam_yc")),
        ("jpeg2000", "j2k", ("pal_cvbs", "ntsc_cvbs", "mesecam_yc")),
        ("h264", "h264", ("pal_cvbs", "secam_yc")),
        ("mpeg2video", "d10", ("pal_cvbs", "ntsc_cvbs", "palm_yc", "secam_yc", "mesecam_yc")),
    ):
        for fixture in mxf_fixtures:
            extra = ["--length", "2"] if "secam" in fixture else []
            option_cases.append((f"mxf-interlace-{profile}-{fixture}", fixture,
                                 ["--profile", profile, "--profile-container", "mxf"] + extra,
                                 {"codec": codec, "ext": "mxf", "field_order_matches_resolved": True}))
    option_cases.append(("mxf-web-stays-progressive", "secam_yc",
                         ["--profile", "h264_web", "--profile-container", "mxf", "--length", "2"],
                         {"codec": "h264", "ext": "mxf", "field_order": "progressive"}))
    decoders = [("PAL2D", "pal_yc"), ("TRANSFORM2D", "pal_cvbs"), ("TRANSFORM3D", "pal_cvbs"),
                ("NTSC1D", "ntsc_cvbs"), ("NTSC2D", "ntsc_cvbs"), ("NTSC3D", "ntsc_ld"),
                ("NTSC3DNOADAPT", "ntsc_ld"), ("MONO", "pal_cvbs"), ("SECAM", "secam_yc")]
    for decoder, fixture in decoders:
        extra = ["--length", "2"] if fixture.startswith("secam") else []
        option_cases.append((f"decoder-{decoder.lower()}", fixture, ["--chroma-decoder", decoder] + extra, {}))
    for mode in ("NONE", "COMBINED", "LUMA", "CHROMA"):
        option_cases.append((f"interfield-{mode.lower()}", "pal_yc",
                             ["--dropout-interfield-correction", mode], {}))
    if level != "full":
        keep = {"mxf-interlace-prores_hq-pal_cvbs", "mxf-interlace-ffv1-secam_yc", "mxf-interlace-j2k-ntsc_cvbs",
                "mxf-interlace-d10-secam_yc", "mxf-web-stays-progressive", "d10-d1-pal", "d10-d1-ntsc", "mono-yc-luma-only-pal_yc", "mono-yc-luma-only-secam_yc", "secam-bars", "secam-bars-web", "secam-bars-cvbs", "mesecam-bars",
                "secam-bars-check-rejects-luma-only", "d1-pal", "d1-secam", "vbi", "luma-only", "field-order-tff", "two-step", "process-vbi",
                "decoder-transform3d", "decoder-ntsc2d", "secam", "interfield-combined"}
        option_cases = [c for c in option_cases if c[0] in keep]
    for name, fixture, args, expect in option_cases:
        audio = expect.pop("_audio", 0)
        cases.append(Case("cli", f"option-{name}", fixture, args=args, audio=audio,
                          expect={k: v for k, v in expect.items() if v is not None}))
    return cases


def run_cli_case(env: Env, case: Case) -> Outcome:
    started = time.monotonic()
    case_dir = env.work / "cli" / case.name
    shutil.rmtree(case_dir, ignore_errors=True)
    case_dir.mkdir(parents=True)
    out_base = case_dir / "out"
    cmd = env.video_export + ["--overwrite", "--no-progress"]
    if "--length" not in case.args:
        cmd += ["--length", "1"]
    cmd += case.args + [str(env.fixture_tbc(case.fixture)), str(out_base)]
    code, out, err = run(cmd, env.environ(env.home / "cli" / case.name), env.hostile, env.case_timeout)
    (case_dir / "command.txt").write_text(" ".join(cmd) + f"\nexit={code}\n--- stdout\n{out}\n--- stderr\n{err}\n")
    if code != 0:
        tail = "\n".join((err or out).strip().splitlines()[-15:])
        return Outcome(case, False, time.monotonic() - started, f"exit {code}: {tail}")
    if "DECOY" in out + err:
        return Outcome(case, False, time.monotonic() - started, "a decoy PATH tool was invoked")
    try:
        problems = verify_output(env, case, out_base, out + err)
    except AssertionError as error:
        problems = [str(error)]
    return Outcome(case, not problems, time.monotonic() - started, "; ".join(problems))


# -------------------------------------------------------------------------- gui

def gui_run(env: Env, name: str, fixture: str, spec: dict, timeout=900):
    case_dir = env.work / "gui" / name
    shutil.rmtree(case_dir, ignore_errors=True)
    (case_dir / "out").mkdir(parents=True)
    spec = dict(spec, result=str(case_dir / "result.json"), timeout_seconds=timeout - 30)
    (case_dir / "spec.json").write_text(json.dumps(spec, indent=2))
    cmd = env.analyse + ["--e2e-export", str(case_dir / "spec.json"), str(env.fixture_tbc(fixture))]
    code, out, err = run(cmd, env.environ(env.home / "gui" / name), env.hostile, timeout)
    (case_dir / "process.txt").write_text(f"{' '.join(cmd)}\nexit={code}\n--- stdout\n{out}\n--- stderr\n{err}\n")
    result = {}
    if (case_dir / "result.json").is_file():
        result = json.loads((case_dir / "result.json").read_text())
    return code, result, err, case_dir


def gui_controls(env: Env, fixture: str, widgets, tag: str):
    code, result, err, _ = gui_run(env, f"discover-{fixture}-{tag}", fixture, {
        "list_only": True, "widgets": [{"name": n, "value": v} for n, v in widgets]}, timeout=180)
    if code != 0 or "controls" not in result:
        raise RuntimeError(f"GUI option discovery failed ({fixture}, {tag}): "
                           f"{result.get('reason') or err.strip()[-800:]}")
    return result["controls"]


def items(controls, name):
    control = controls.get(name, {})
    # only choices a user can actually pick (disabled combo/items excluded)
    return ([i["data"] or i["text"] for i in control.get("items", []) if i.get("enabled", True)]
            if control.get("enabled") else [])


def gui_cases(env: Env, level: str):
    cases = []
    base = gui_controls(env, "pal_yc", [], "base")
    system_height = FRAME_HEIGHT

    def ffv1_case(name, widgets, fixture="pal_yc", expect=None, **extra):
        widgets = [("profileComboBox", "ffv1")] + widgets
        cases.append(Case("gui", name, fixture, widgets=widgets,
                          expect={"codec": "ffv1", **(expect or {})}, **extra))

    # Regression for the reported failure: heavy dropout + interfield + D1.
    ffv1_case("regression-heavy-interfield-d1",
              [("dropoutModeComboBox", "heavy"), ("dropoutFieldModeComboBox", "innerfield"),
               ("resolutionModeComboBox", "active_area"), ("outputResolutionModeComboBox", "default_safe")],
              expect={"width": 720, "height": 576},
              expect_log=["--overcorrect", "--dropout-interfield-correction", "--d1"])

    def profile_expect(profile, variant):
        # web profiles and the avc/hevc "web" range are deinterlaced
        if profile == "web":
            return {"codec": WEB_CODEC.get(variant, variant), "field_order": "progressive"}
        expect = {"codec": GUI_PROFILE_CODEC.get(profile)}
        if variant == "web":
            expect["field_order"] = "progressive"
        return expect

    # Every profile, with each variant / container / bit depth it offers.
    for profile in items(base, "profileComboBox"):
        controls = gui_controls(env, "pal_yc", [("profileComboBox", profile)], f"profile-{profile}")
        variant_combo = next((c for c in SUB_PROFILE_COMBOS if items(controls, c)), None)
        variants = items(controls, variant_combo) if variant_combo else [None]
        default_variant = controls.get(variant_combo, {}).get("current") if variant_combo else None
        for variant in variants:
            widgets = [("profileComboBox", profile)] + ([(variant_combo, variant)] if variant else [])
            expect = profile_expect(profile, variant)
            cases.append(Case("gui", f"profile-{profile}" + (f"-{variant}" if variant else ""), "pal_yc",
                              widgets=widgets, expect=expect))
            if level != "full":
                break
        for container in items(controls, "mainContainerComboBox"):
            cases.append(Case("gui", f"profile-{profile}-container-{container}", "pal_yc",
                              widgets=[("profileComboBox", profile), ("mainContainerComboBox", container)],
                              expect={**profile_expect(profile, default_variant), "ext": container}))
        for depth in items(controls, "mainBitDepthComboBox"):
            cases.append(Case("gui", f"profile-{profile}-{depth}bit", "pal_yc",
                              widgets=[("profileComboBox", profile), ("mainBitDepthComboBox", depth)],
                              expect={**profile_expect(profile, default_variant),
                                      **({"pix_fmt_has": "10"} if depth == "10" else {"pix_fmt_not": "10"})}))

    # Framing x output resolution.
    for framing in items(base, "resolutionModeComboBox"):
        if framing == "user_defined":
            continue
        controls = gui_controls(env, "pal_yc", [("resolutionModeComboBox", framing)], f"framing-{framing}")
        modes = items(controls, "outputResolutionModeComboBox") or [None]
        for mode in modes:
            widgets = [("resolutionModeComboBox", framing)] + ([("outputResolutionModeComboBox", mode)] if mode else [])
            expect = {}
            if framing == "active_area" and mode == "default_safe":
                expect = {"width": 720, "height": system_height["PAL"]}
            ffv1_case(f"framing-{framing}" + (f"-{mode}" if mode else ""), widgets, expect=expect)

    # Dropout mode x field mode.
    for mode in items(base, "dropoutModeComboBox"):
        fields = items(base, "dropoutFieldModeComboBox") if mode != "disabled" else [None]
        for field_mode in fields:
            widgets = [("dropoutModeComboBox", mode)] + ([("dropoutFieldModeComboBox", field_mode)] if field_mode else [])
            ffv1_case(f"dropout-{mode}" + (f"-{field_mode}" if field_mode else ""), widgets)

    # Audio profiles with a real track; two tracks.
    for audio in items(base, "audioProfileComboBox"):
        ffv1_case(f"audio-{audio}", [("audioProfileComboBox", audio),
                                     ("audio1LineEdit", str(env.source / AUDIO_TRACK))], audio=1)
    ffv1_case("audio-two-tracks", [("audio1LineEdit", str(env.source / AUDIO_TRACK)),
                                   ("audio1NameLineEdit", "Track A"),
                                   ("audio2LineEdit", str(env.source / AUDIO_TRACK)),
                                   ("audio2NameLineEdit", "Track B")], audio=2)

    # Proxy generation for each proxy codec.
    proxy_controls = gui_controls(env, "pal_yc", [("generateProxyCheckBox", True)], "proxy")
    for codec in items(proxy_controls, "proxyCodecComboBox"):
        ffv1_case(f"proxy-{codec}", [("generateProxyCheckBox", True), ("proxyCodecComboBox", codec)],
                  expect={"proxy_codec": WEB_CODEC.get(codec, codec)})

    # In/out points on the 5-frame fixture: frames 2..4 inclusive.
    ffv1_case("in-out-points", [("inPointSpinBox", 2), ("outPointSpinBox", 4)], fixture="pal_yc5",
              expect={"frames": 3})

    # Advanced options (each individually).
    for key, value in (("exportMetadata", False), ("disableMetadataEmbedding", True), ("letterboxCrop", True),
                       ("forceAnamorphic", True), ("twoStep", True), ("outputPadding", 16), ("threads", 2)):
        ffv1_case(f"advanced-{key}", [], advanced={key: value})
    # reversing field order needs >1 frame (the first field is dropped)
    ffv1_case("advanced-reverseFieldOrder", [("inPointSpinBox", 1), ("outPointSpinBox", 3)], fixture="pal_yc5",
              advanced={"reverseFieldOrder": True})
    # two-step with audio + CC subtitles exercises the merge step's input indexes
    ffv1_case("advanced-twoStep-audio", [("audio1LineEdit", str(env.source / AUDIO_TRACK))],
              advanced={"twoStep": True}, audio=1)

    # Custom JSON profile set (the exporter's own default config, loaded via
    # the saved-preference path).
    ffv1_case("custom-profile-config", [], profile_config=str(env.work / "cli-default-config" / "tbc-video-export.json"))

    # Other video systems / source types: default export + web (field order).
    for fixture in ("pal_cvbs", "pal_ld", "ntsc_yc", "ntsc_cvbs", "ntsc_ld", "palm_yc", "secam_yc", "mesecam_yc"):
        system = FIXTURES[fixture][2]
        long_fixture = fixture.startswith(("secam", "mesecam"))
        range_widgets = [("inPointSpinBox", 1), ("outPointSpinBox", 2)] if long_fixture else []
        # SECAM fixtures are colour bars: check decoded colours, not just the file
        bars = {"secam_yc": {"bars": "etienne"}, "mesecam_yc": {"bars": "tfb"}}.get(fixture, {})
        ffv1_case(f"system-{fixture}-ffv1", range_widgets, fixture=fixture,
                  expect={"width": 720, "height": system_height[system], **bars})
        cases.append(Case("gui", f"system-{fixture}-web-h264", fixture,
                          widgets=[("profileComboBox", "web"), ("webCodecComboBox", "h264")] + range_widgets,
                          expect={"codec": "h264", "field_order": "progressive", **bars}))

    # D10 (IMX50) on 525-line sources: mxf_d10 needs exact 208541-byte frames
    for fixture in ("ntsc_cvbs", "pal_cvbs"):
        cases.append(Case("gui", f"system-{fixture}-d10", fixture, widgets=[("profileComboBox", "d10")],
                          expect={"codec": "mpeg2video", "ext": "mxf", "width": 720,
                                  "height": 512 if fixture.startswith("ntsc") else 608, "dar": "4:3"}))

    # MONO chosen for a Y/C source (chroma decoder dialog -> metadata): the
    # export must be luma-only, not silently colour-decoded or failed
    # the command quoting differs per platform (Linux `script` wrapper vs
    # Windows/macOS), so match the argument pair, not one spelling
    ffv1_case("gui-yc-mono-luma-only", [], fixture="pal_yc_mono",
              expect={"gray": True}, expect_log_regex=[r"--chroma-decoder['\"]?\s+['\"]?mono\b"])

    # SECAM through the Export tab: composite path, ProRes, heavy dropout
    # correction, and the 10-bit/8-bit FFV1 choice, all colour-checked.
    secam_range = [("inPointSpinBox", 1), ("outPointSpinBox", 2)]
    ffv1_case("gui-secam-bars-cvbs", secam_range, fixture="secam_cvbs", expect={"bars": "etienne"})
    cases.append(Case("gui", "gui-secam-bars-prores", "secam_yc",
                      widgets=[("profileComboBox", "prores")] + secam_range,
                      expect={"codec": "prores", "bars": "etienne"}))
    ffv1_case("gui-secam-bars-dropout-heavy", [("dropoutModeComboBox", "heavy")] + secam_range,
              fixture="secam_yc", expect={"bars": "etienne"})
    ffv1_case("gui-secam-bars-8bit", [("mainBitDepthComboBox", "8")] + secam_range,
              fixture="secam_yc", expect={"bars": "etienne", "pix_fmt_not": "10"})
    ffv1_case("gui-mesecam-bars-dropout-heavy", [("dropoutModeComboBox", "heavy")] + secam_range,
              fixture="mesecam_yc", expect={"bars": "tfb"})

    if level != "full":
        keep = ("regression-", "profile-ffv1", "profile-prores", "profile-web", "framing-active_vbi",
                "dropout-heavy", "audio-flac", "proxy-h264", "in-out-points", "custom-profile-config",
                "system-", "gui-secam-bars-cvbs", "gui-secam-bars-prores", "gui-yc-mono-luma-only")
        cases = [c for c in cases if c.name.startswith(keep) and "container" not in c.name and "bit" not in c.name]
    return cases


def run_gui_case(env: Env, case: Case) -> Outcome:
    started = time.monotonic()
    out_base = env.work / "gui" / case.name / "out" / "export"
    widgets = [{"name": n, "value": v} for n, v in case.widgets]
    widgets.append({"name": "outputLineEdit", "value": str(out_base)})
    spec = {"widgets": widgets, "allow_log_patterns": case.allow_log, "expect_log_contains": case.expect_log}
    if case.advanced:
        spec["advanced"] = case.advanced
    if case.profile_config:
        spec["profile_config"] = case.profile_config
    code, result, err, _case_dir = gui_run(env, case.name, case.fixture, spec, env.case_timeout)
    if code != 0:
        reason = result.get("reason") or err.strip()[-1500:]
        if "DECOY" in json.dumps(result) + err:
            reason += " (a decoy PATH tool was invoked)"
        return Outcome(case, False, time.monotonic() - started, f"exit {code}: {reason}")
    try:
        problems = verify_output(env, case, out_base)
    except AssertionError as error:
        problems = [str(error)]
    for pattern in case.expect_log_regex:
        if not re.search(pattern, result.get("log", "")):
            problems.append(f"export log does not match {pattern!r}")
    return Outcome(case, not problems, time.monotonic() - started, "; ".join(problems))


# ------------------------------------------------------------------------- main

def write_junit(path: Path, outcomes):
    suites = ET.Element("testsuites")
    by_suite = {}
    for outcome in outcomes:
        by_suite.setdefault(outcome.case.suite, []).append(outcome)
    for suite_name, suite_outcomes in by_suite.items():
        suite = ET.SubElement(suites, "testsuite", name=f"export-e2e-{suite_name}",
                              tests=str(len(suite_outcomes)),
                              failures=str(sum(o.counts_as_failure for o in suite_outcomes)))
        for outcome in suite_outcomes:
            testcase = ET.SubElement(suite, "testcase", classname=f"export-e2e.{suite_name}",
                                     name=outcome.case.name, time=f"{outcome.seconds:.2f}")
            if outcome.counts_as_failure:
                ET.SubElement(testcase, "failure", message=outcome.detail[:200]).text = outcome.detail
            elif outcome.known and not outcome.ok:
                ET.SubElement(testcase, "skipped", message=f"known issue: {outcome.known}"[:200])
    ET.ElementTree(suites).write(path, encoding="utf-8", xml_declaration=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", required=True, help="tbc-tools source tree (fixtures, exportdialog.cpp)")
    parser.add_argument("--build", help="CMake build dir (binaries taken from <build>/bin)")
    parser.add_argument("--analyse", help="tbc-analyse executable (overrides --build)")
    parser.add_argument("--video-export", help="tbc-video-export executable (overrides --build)")
    parser.add_argument("--ffprobe", help="ffprobe used to verify outputs")
    parser.add_argument("--appimage", help="test a Linux AppImage (tools run as '<AppImage> <tool>')")
    parser.add_argument("--qpa", help="QT_QPA_PLATFORM for tbc-analyse (default: offscreen on Linux, native elsewhere)")
    parser.add_argument("--suite", action="append", choices=["contract", "cli", "gui"],
                        help="suites to run (default: all)")
    parser.add_argument("--level", choices=["smoke", "full"], default="full")
    parser.add_argument("--filter", help="only run cases whose name matches this regex")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    parser.add_argument("--case-timeout", type=int, default=900, help="seconds per export case")
    parser.add_argument("--work-dir", help="keep outputs here (default: a temp dir, removed on success)")
    parser.add_argument("--junit", help="write a JUnit XML report")
    parser.add_argument("--strict-bundled", action="store_true",
                        help="prefix PATH with failing decoy tools (release packages must be self-contained)")
    args = parser.parse_args()
    suites = args.suite or ["contract", "cli", "gui"]

    env = Env(args)
    if not env.video_export:
        print("SKIP: tbc-video-export not found")
        return SKIP_EXIT_CODE
    if "gui" in suites and not env.analyse:
        print("SKIP: tbc-analyse not found")
        return SKIP_EXIT_CODE
    if ("cli" in suites or "gui" in suites) and not env.ffprobe:
        print("SKIP: ffprobe not found")
        return SKIP_EXIT_CODE
    missing_fixtures = env.prepare()
    if missing_fixtures:
        print("FAIL: fixtures missing from the source tree (are they tracked in git?):\n  "
              + "\n  ".join(missing_fixtures))
        return 1
    print(f"work dir: {env.work}\ntbc-analyse: {' '.join(env.analyse or [])}\n"
          f"tbc-video-export: {' '.join(env.video_export)}\nffprobe: {' '.join(env.ffprobe or [])}\nlevel: {args.level}  suites: {', '.join(suites)}  jobs: {args.jobs}"
          f"{'  strict-bundled' if env.strict else ''}", flush=True)

    outcomes = []
    if "contract" in suites:
        outcomes += contract_suite(env)
    cases = []
    try:
        if "cli" in suites or "gui" in suites:
            cases += cli_cases(env, args.level) if "cli" in suites else []
            if "gui" in suites:
                if "cli" not in suites:
                    cli_cases(env, "smoke")  # writes the default config used by custom-profile-config
                cases += gui_cases(env, args.level)
    except RuntimeError as error:
        outcomes.append(Outcome(Case("setup", "case-discovery", "-"), False, 0.0, str(error)))
    if args.filter:
        cases = [c for c in cases if re.search(args.filter, c.name)]

    print(f"running {len(cases)} export cases", flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(run_cli_case if c.suite == "cli" else run_gui_case, env, c): c for c in cases}
        for future in concurrent.futures.as_completed(futures):
            try:
                outcome = future.result()
            except Exception as error:  # a crash in the harness is a failure, not a skip
                outcome = Outcome(futures[future], False, 0.0, f"harness error: {error!r}")
            outcome.known = KNOWN_ISSUES.get(outcome.case.name, "")
            outcomes.append(outcome)
            note = outcome.detail if not outcome.ok else ""
            if outcome.known:
                note = f"known issue: {outcome.known}" + (f"\n        {outcome.detail}" if outcome.detail else "")
                if outcome.ok:
                    note = "known issue now passes; remove it from KNOWN_ISSUES"
            print(f"[{outcome.mark}] {outcome.case.suite:8s} {outcome.case.name} ({outcome.seconds:.1f}s)"
                  + (f"\n        {note}" if note else ""), flush=True)

    for outcome in outcomes:
        if outcome.case.suite in ("contract", "setup"):
            print(f"[{'PASS' if outcome.ok else 'FAIL'}] {outcome.case.suite:8s} {outcome.case.name}: {outcome.detail}")
    if args.junit:
        write_junit(Path(args.junit), outcomes)
    failures = [o for o in outcomes if o.counts_as_failure]
    xfails = sum(1 for o in outcomes if o.known and not o.ok)
    print(f"\n{sum(o.ok for o in outcomes)}/{len(outcomes)} passed, {len(failures)} failed, "
          f"{xfails} known issue(s); artifacts in {env.work}")
    if not failures and not args.work_dir:
        shutil.rmtree(env.work, ignore_errors=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
