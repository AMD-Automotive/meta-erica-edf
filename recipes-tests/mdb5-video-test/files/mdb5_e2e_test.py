#!/usr/bin/env python3
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: BW
# Maintainer: BW

"""
MDB5 PCIe video bridge coordinated end-to-end test.

Runs on a dev host and orchestrates BOTH machines over SSH to validate the
full video bridge chain for each format in the test matrix:

    vivid (endpoint) -> ep_proxy capture -> BRAM mailbox -> MSI-X ->
    host mdb5_v4l2 driver -> eDMA -> host /dev/video* frames

Validation uses GOLDEN-CAPTURE COMPARISON: a static, deterministic vivid test
pattern is captured directly on the endpoint (the "golden" frame), then the
frame received on the host is compared against it. A case passes only when the
host-received frame data equals the endpoint's golden frame for every channel.

This script runs from a dev host (not on-target). It needs SSH access to
both machines.

Prerequisites (assumed pre-provisioned; the test SKIPs cleanly otherwise):
  Endpoint: vivid module, /usr/bin/ep_proxy, /dev/mem, CMA, v4l2-ctl
  Host:     mdb5_v4l2 driver available (modinfo), v4l2-ctl (v4l-utils).
            The driver must NOT be auto-loaded at boot: it reads its channel
            layout from BRAM at probe and the orchestrator (re)loads it per
            case, after ep_proxy has stamped the mailbox for that case.
  Dev host: ssh/scp, key-based auth to both machines

Throughput reporting is always on: each case also measures per-channel goodput
(sizeimage x fps) and, when lspci can read the endpoint's link, the % of the
theoretical PCIe ceiling. Numbers print to the CLI; --bw-report writes a
standalone HTML test report with pass/fail summary, failure details, and
throughput data. Throughput numbers are informational and never change PASS/FAIL.

Usage:
    mdb5_e2e_test.py [--matrix PATH] [--filter REGEX] [--timeout SECS]
                     [--endpoint root@IP] [--host amd@IP]
                     [--keep-artifacts] [--frames N]
                     [--bw-frames N] [--bw-report report.html]
"""

import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
import time

# ---------------------------------------------------------------------------
# Defaults (override via CLI flags or MDB5_EP / MDB5_HOST env vars)
# ---------------------------------------------------------------------------
DEFAULT_ENDPOINT = os.environ.get("MDB5_EP", "root@192.168.1.222")
DEFAULT_HOST = os.environ.get("MDB5_HOST", "amd@192.168.1.203")
DEFAULT_MATRIX = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "mdb5_test_matrix.json")
DEFAULT_TIMEOUT = 10        # seconds ep_proxy streams per case
DEFAULT_FRAMES = 5          # host frames captured & compared per channel
DEFAULT_BW_FRAMES = 120     # host frames streamed for the throughput sample
MDB5_PCI_ID = "10ee:9038"   # endpoint device as enumerated on the host RC
EP_PROXY_BIN = "/usr/bin/ep_proxy"
EP_CONFIG = "/tmp/mdb5_e2e_config.json"
# Host driver is an out-of-tree .ko at a custom path (not under /lib/modules),
# so it is loaded by explicit path via insmod, not modprobe-by-name. Loading
# needs root; the host account uses passwordless sudo for insmod/rmmod.
DEFAULT_HOST_KO = "/home/amd/mdb5_v4l2/mdb5_v4l2.ko"
EP_GOLDEN = "/tmp/mdb5_golden_ch{ch}.raw"
HOST_FRAME = "/tmp/mdb5_host_ch{ch}.raw"
HOST_DEV_TIMEOUT = 15       # seconds to wait for host mdb5 nodes to appear

SSH_OPTS = [
    "-o", "BatchMode=yes",
    "-o", "ConnectTimeout=10",
    "-o", "StrictHostKeyChecking=accept-new",
]

# vivid fourcc strings as used by v4l2-ctl pixelformat=
FORMAT_TO_V4L2CTL = {
    "RGB24":  "RGB3", "BGR24":  "BGR3",
    "RGBX32": "XB24", "BGRX32": "RX24",
    "XRGB32": "BX24", "XBGR32": "XR24",
    "YUYV":   "YUYV", "UYVY":   "UYVY", "YVYU": "YVYU",
    "NV12":   "NV12", "NV21":   "NV21",
}

# Bytes-per-pixel of the packed/active row, per format. NV12/NV21 handled
# specially (Y plane width bytes/row * height, then UV width bytes/row * height/2).
FORMAT_BPP = {
    "RGB24": 3, "BGR24": 3,
    "RGBX32": 4, "BGRX32": 4, "XRGB32": 4, "XBGR32": 4,
    "YUYV": 2, "UYVY": 2, "YVYU": 2,
}
SEMIPLANAR = ("NV12", "NV21")

# Effective per-lane throughput (GB/s, decimal) by PCIe link speed, AFTER line
# encoding overhead: Gen1/2 use 8b/10b (20% loss); Gen3+ use 128b/130b (~1.5%).
#   GT/s * (encoded_bits/symbol_bits) / 8  ->  GB/s per lane
PCIE_GTS_TO_GBPS_PER_LANE = {
    2.5:  0.250,   # Gen1  2.5 GT/s * 8/10  / 8
    5.0:  0.500,   # Gen2  5.0 GT/s * 8/10  / 8
    8.0:  0.985,   # Gen3  8.0 GT/s * 128/130 / 8
    16.0: 1.969,   # Gen4 16.0 GT/s * 128/130 / 8
    32.0: 3.938,   # Gen5 32.0 GT/s * 128/130 / 8
    64.0: 7.563,   # Gen6 64.0 GT/s (PAM4, 242B FLIT) ~ 0.945 efficiency
}


def log(msg):
    print(f"[mdb5_e2e] {msg}", flush=True)


# ---------------------------------------------------------------------------
# Remote command helpers
# ---------------------------------------------------------------------------
class Remote:
    """Thin SSH wrapper for one target (user@host)."""

    def __init__(self, target):
        self.target = target

    def run(self, cmd, check=False, timeout=60):
        """Run a shell command remotely. Returns CompletedProcess."""
        full = ["ssh", *SSH_OPTS, self.target, cmd]
        return subprocess.run(
            full, check=check, timeout=timeout,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )

    def ok(self, cmd, timeout=60):
        """Return True if the remote command exits 0."""
        return self.run(cmd, timeout=timeout).returncode == 0

    def fetch(self, remote_path, local_path, timeout=120):
        """scp a file from the target to the dev host. Returns True on success."""
        full = ["scp", *SSH_OPTS, f"{self.target}:{remote_path}", local_path]
        return subprocess.run(
            full, timeout=timeout,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        ).returncode == 0

    def md5(self, remote_path):
        """Return md5 hex of a remote file, or None."""
        r = self.run(f"md5sum {shlex.quote(remote_path)} 2>/dev/null")
        if r.returncode == 0 and r.stdout.strip():
            return r.stdout.split()[0]
        return None


# ---------------------------------------------------------------------------
# PCIe link state (host root complex view of the endpoint)
# ---------------------------------------------------------------------------
def _parse_speed_width(line):
    """From an 'LnkSta:'/'LnkCap:' line, return (gts: float, width: int)."""
    gts = None
    m = re.search(r"Speed\s+([\d.]+)GT/s", line)
    if m:
        gts = float(m.group(1))
    width = None
    m = re.search(r"Width\s+x(\d+)", line)
    if m:
        width = int(m.group(1))
    return gts, width


def query_link_state(host):
    """Inspect the endpoint's PCIe link from the host RC via lspci.

    Returns a dict with negotiated/capable speed+width and the theoretical
    effective ceiling (GB/s), or {'available': False, ...} if it can't be read.
    Never raises; throughput reporting falls back to goodput-only on failure.
    """
    info = {"available": False, "pci_id": MDB5_PCI_ID}

    bdf = host.run(f"lspci -d {MDB5_PCI_ID} -mm")
    if bdf.returncode != 0 or not bdf.stdout.strip():
        info["reason"] = f"device {MDB5_PCI_ID} not found via lspci"
        return info
    slot = bdf.stdout.split()[0]
    info["bdf"] = slot

    # lspci -vv only exposes the LnkSta/LnkCap capability registers when run as
    # root; unprivileged output omits them entirely. Try plain first (in case we
    # already are root), then fall back to passwordless sudo.
    vv = host.run(f"lspci -vv -s {shlex.quote(slot)}")
    if "LnkSta:" not in vv.stdout:
        vv = host.run(f"sudo -n lspci -vv -s {shlex.quote(slot)}")
    if vv.returncode != 0 or not vv.stdout.strip():
        info["reason"] = "lspci -vv returned no data (needs root)"
        return info

    for line in vv.stdout.splitlines():
        s = line.strip()
        if s.startswith("LnkSta:"):
            info["cur_gts"], info["cur_width"] = _parse_speed_width(s)
        elif s.startswith("LnkCap:"):
            info["max_gts"], info["max_width"] = _parse_speed_width(s)

    cur_gts = info.get("cur_gts")
    cur_width = info.get("cur_width")
    if not (cur_gts and cur_width):
        info["reason"] = "could not parse LnkSta speed/width"
        return info

    per_lane = PCIE_GTS_TO_GBPS_PER_LANE.get(cur_gts)
    if per_lane is None:
        info["reason"] = f"unknown link speed {cur_gts} GT/s"
        return info

    info["per_lane_gbps"] = per_lane
    info["theoretical_bps"] = per_lane * cur_width * 1e9  # bytes/s
    info["degraded"] = bool(
        info.get("max_gts") and info.get("max_width")
        and (cur_gts < info["max_gts"] or cur_width < info["max_width"])
    )
    info["available"] = True
    return info


def format_link_state(link):
    """One-line human summary of the link state for the CLI."""
    if not link.get("available"):
        return f"PCIe link: unavailable ({link.get('reason', 'unknown')})"
    s = (f"PCIe link [{link['bdf']}]: Gen@{link['cur_gts']}GT/s x{link['cur_width']} "
         f"= {link['theoretical_bps'] / 1e6:.0f} MByte/s theoretical")
    if link.get("degraded"):
        s += (f"  [DEGRADED: capable {link.get('max_gts')}GT/s "
              f"x{link.get('max_width')}]")
    return s


# ---------------------------------------------------------------------------
# Preconditions
# ---------------------------------------------------------------------------
def check_preconditions(ep, host, host_ko):
    """Verify both machines are reachable and provisioned. False => SKIP (77)."""
    # Endpoint reachability
    if not ep.ok("true", timeout=15):
        log(f"SKIP: endpoint {ep.target} not reachable over SSH")
        return False
    if not host.ok("true", timeout=15):
        log(f"SKIP: host {host.target} not reachable over SSH")
        return False

    # Endpoint provisioning
    if not ep.ok("command -v v4l2-ctl"):
        log("SKIP: v4l2-ctl not found on endpoint")
        return False
    if not ep.ok(f"test -x {shlex.quote(EP_PROXY_BIN)}"):
        log(f"SKIP: {EP_PROXY_BIN} not found on endpoint")
        return False
    if not ep.ok("test -e /dev/mem"):
        log("SKIP: /dev/mem not available on endpoint")
        return False
    if not ep.ok("modprobe --dry-run vivid"):
        log("SKIP: vivid kernel module not available on endpoint")
        return False
    r = ep.run("awk '/CmaTotal/{print $2}' /proc/meminfo")
    try:
        if int(r.stdout.strip() or "0") <= 0:
            log("SKIP: no CMA configured on endpoint")
            return False
    except ValueError:
        log("SKIP: cannot read CMA info on endpoint")
        return False

    # Host provisioning
    if not host.ok("command -v v4l2-ctl"):
        log("SKIP: v4l2-ctl not found on host")
        return False
    # The host driver latches its channel layout from BRAM at probe time and
    # never re-syncs, so the orchestrator loads it per case (after ep_proxy has
    # stamped the mailbox) and unloads it in teardown. Gate on the .ko file
    # existing (it lives at a custom path, not under /lib/modules) NOT on it
    # being already loaded -- a driver loaded before ep_proxy has no magic to
    # read and would have stale/invalid layout.
    if not host.ok(f"test -f {shlex.quote(host_ko)}"):
        log(f"SKIP: mdb5_v4l2 module not found on host at {host_ko}")
        return False
    # Loading needs root; the host account must allow passwordless sudo for
    # rmmod/insmod. The sudoers rule typically whitelists only those commands
    # (not 'true'), so probe with rmmod itself: when sudo would prompt for a
    # password it prints "password" to stderr -- that's the signal to SKIP.
    # (Exit code can't distinguish a denied prompt from rmmod's no-args usage
    # error, so we key off the message instead.)
    probe = host.run("sudo -n rmmod 2>&1")
    if "password" in probe.stdout.lower():
        log("SKIP: passwordless sudo not available on host for rmmod/insmod "
            "(needed to load mdb5_v4l2)")
        return False

    return True


# ---------------------------------------------------------------------------
# Endpoint-side: vivid setup
# ---------------------------------------------------------------------------
def unload_vivid(ep):
    """Unload vivid, retrying on EAGAIN.

    vivid transiently holds its own refcount while internal streaming/worker
    threads tear down, so a single rmmod can fail with "Resource temporarily
    unavailable" even when no userspace process has a device open. Retry with
    a short backoff until it unloads or the module is already gone.
    """
    for _ in range(10):
        if not ep.ok("lsmod | grep -q '^vivid'"):
            return  # already unloaded
        if ep.ok("rmmod vivid 2>/dev/null"):
            return
        time.sleep(0.5)


def list_video_devices(rem):
    r = rem.run("ls -1 /dev/video* 2>/dev/null")
    return set(r.stdout.split()) if r.returncode == 0 else set()


def load_vivid(ep, n_devs, multiplanar):
    mp = ",".join(["1" if multiplanar else "0"] * n_devs)
    alloc = ",".join(["1"] * n_devs)      # dma-contig
    nodes = ",".join(["0x1"] * n_devs)    # capture-only
    cmd = (f"modprobe vivid n_devs={n_devs} node_types={nodes} "
           f"multiplanar={mp} allocators={alloc}")
    log(f"endpoint: {cmd}")
    r = ep.run(cmd)
    if r.returncode != 0:
        log(f"  failed to load vivid: {r.stderr.strip()}")
        return False
    time.sleep(1)  # udev settle
    return True


def find_vivid_capture_devs(ep, new_devs):
    """Filter new /dev/video* to vivid-backed capture nodes."""
    devs = []
    for dev in sorted(new_devs):
        d = ep.run(f"v4l2-ctl -d {dev} -D")
        if d.returncode == 0 and "vivid" in d.stdout.lower():
            a = ep.run(f"v4l2-ctl -d {dev} --all")
            if a.returncode == 0 and "Video Capture" in a.stdout:
                devs.append(dev)
    return devs


def make_vivid_static(ep, dev, fps=30):
    """Disable motion/OSD/noise so vivid emits an identical static frame.

    Control names vary by kernel; discover what's present and set only those.
    Also sets the capture frame rate to `fps` (vivid defaults to 5 fps via
    the NTSC standard timeperframe; the test matrix expects 30).
    """
    r = ep.run(f"v4l2-ctl -d {dev} --list-ctrls")
    ctrls = r.stdout if r.returncode == 0 else ""

    def have(name):
        return re.search(rf"^\s*{re.escape(name)}\b", ctrls, re.M) is not None

    settings = []
    # Fixed pattern (color bars = index 0 on vivid).
    if have("test_pattern"):
        settings.append("test_pattern=0")
    # No movement. On vivid the *_movement menus are:
    #   0=Move Left/Up Fast ... 3=No Movement ... 6=Move Right/Down Fast.
    # Index 3 (NOT 0) is the static setting; 0 scrolls the pattern fastest,
    # which makes golden vs. host captures catch different scroll phases.
    for name in ("horizontal_movement", "vertical_movement"):
        if have(name):
            settings.append(f"{name}=3")
    # No on-screen text/counter overlay (changes per frame otherwise).
    # osd_text_mode menu: 0=All, 1=Counters Only, 2=None. Index 2 (NOT 0)
    # disables the overlay; 0 shows it, leaving a per-frame-varying text box.
    if have("osd_text_mode"):
        settings.append("osd_text_mode=2")
    # Kill anything that introduces per-frame variation.
    for name in ("sense_of_movement", "show_border", "show_square",
                 "noise", "noise_level"):
        if have(name):
            settings.append(f"{name}=0")

    for s in settings:
        ep.run(f"v4l2-ctl -d {dev} --set-ctrl {s}")
    # Set frame rate. vivid defaults to 5 fps (NTSC timeperframe); the test
    # matrix specifies the application target rate (typically 30 fps).
    ep.run(f"v4l2-ctl -d {dev} --set-parm {fps}")
    return True


def set_format(rem, dev, width, height, fmt_str):
    v4l2_fmt = FORMAT_TO_V4L2CTL.get(fmt_str)
    if not v4l2_fmt:
        log(f"  unknown format mapping for '{fmt_str}'")
        return False
    cmd = (f"v4l2-ctl -d {dev} --set-fmt-video="
           f"width={width},height={height},pixelformat={v4l2_fmt}")
    r = rem.run(cmd)
    if r.returncode != 0:
        log(f"  set-fmt failed on {dev}: {r.stderr.strip()}")
        return False
    return True


def query_format(rem, dev):
    """Return dict(width,height,pixelformat,bytesperline,sizeimage) or {}."""
    r = rem.run(f"v4l2-ctl -d {dev} --get-fmt-video")
    info = {}
    for line in r.stdout.splitlines():
        line = line.strip()
        if "Width/Height" in line:
            m = re.search(r"(\d+)/(\d+)", line)
            if m:
                info["width"], info["height"] = int(m.group(1)), int(m.group(2))
        elif "Pixel Format" in line:
            m = re.search(r"'(\w+)'", line)
            if m:
                info["pixelformat"] = m.group(1)
        elif "Bytes per Line" in line:
            m = re.search(r"(\d+)", line)
            if m:
                info["bytesperline"] = int(m.group(1))
        elif "Size Image" in line:
            m = re.search(r"(\d+)", line)
            if m:
                info["sizeimage"] = int(m.group(1))
    return info


# ---------------------------------------------------------------------------
# Capture helpers
# ---------------------------------------------------------------------------
# v4l2-ctl streaming flag per capture mode. dmabuf exercises the driver's
# V4L2_MEMORY_DMABUF path (buffers self-exported via VIDIOC_EXPBUF) — the same
# import path mdb5_v4l2 uses for zerocopy to amdgpu.
CAPTURE_MODE_FLAG = {
    "mmap":   "--stream-mmap",
    "dmabuf": "--stream-dmabuf",
}


def capture(rem, dev, count, out_path, mode="mmap"):
    """Capture `count` frames into out_path on the remote. True on success."""
    flag = CAPTURE_MODE_FLAG.get(mode)
    if not flag:
        log(f"  unknown capture mode '{mode}'")
        return False
    cmd = (f"v4l2-ctl -d {dev} {flag} --stream-count={count} "
           f"--stream-to={shlex.quote(out_path)}")
    r = rem.run(cmd, timeout=60)
    if r.returncode != 0:
        log(f"  {mode} capture failed on {dev}: {r.stderr.strip()}")
        return False
    return rem.ok(f"test -s {shlex.quote(out_path)}")


def _parse_fps(text):
    """Extract the last fps figure v4l2-ctl printed, or None.

    v4l2-ctl emits periodic lines like '<<< 30.01 fps' while streaming.
    The last one is the steadiest, so prefer it.
    """
    matches = re.findall(r"([\d.]+)\s*fps", text)
    if not matches:
        return None
    try:
        return float(matches[-1])
    except ValueError:
        return None


def timed_capture(rem, dev, count, out_path, mode="mmap"):
    """Stream `count` frames for a throughput sample.

    Returns (ok, elapsed_s, fps). fps is from v4l2-ctl when available (it
    excludes open/REQBUF startup skew); otherwise None and the caller falls
    back to wall-clock. Discards frame data to /dev/null-style temp; the file
    is only used to confirm bytes actually moved.
    """
    flag = CAPTURE_MODE_FLAG.get(mode)
    if not flag:
        return False, 0.0, None
    cmd = (f"v4l2-ctl -d {dev} {flag} --stream-count={count} "
           f"--stream-to={shlex.quote(out_path)}")
    t0 = time.monotonic()
    r = rem.run(cmd, timeout=120)
    elapsed = time.monotonic() - t0
    if r.returncode != 0:
        log(f"  {mode} bw capture failed on {dev}: {r.stderr.strip()}")
        return False, elapsed, None
    fps = _parse_fps(r.stdout) or _parse_fps(r.stderr)
    return True, elapsed, fps


# ---------------------------------------------------------------------------
# Frame comparison (stride-aware, padding-tolerant)
# ---------------------------------------------------------------------------
def active_bytes(data, fmt, width, height, stride):
    """Extract packed active-region bytes, stripping per-row stride padding."""
    if fmt in SEMIPLANAR:
        row = width                       # Y plane: 1 byte/pixel/row
        y_stride = stride if stride else width
        out = bytearray()
        # Y plane
        for r in range(height):
            off = r * y_stride
            out += data[off:off + row]
        # UV plane follows Y plane in the buffer
        y_size = y_stride * height
        for r in range(height // 2):
            off = y_size + r * y_stride
            out += data[off:off + row]    # interleaved UV, width bytes/row
        return bytes(out)

    bpp = FORMAT_BPP.get(fmt)
    if not bpp:
        return data  # unknown: compare as-is
    row = width * bpp
    s = stride if stride else row
    out = bytearray()
    for r in range(height):
        off = r * s
        out += data[off:off + row]
    return bytes(out)


def split_frames(data, frame_size, count):
    """Split a multi-frame capture into individual frames."""
    if frame_size <= 0 or len(data) < frame_size:
        return [data]
    return [data[i * frame_size:(i + 1) * frame_size]
            for i in range(min(count, len(data) // frame_size))]


def compare_active(golden_path, host_path, fmt, gfmt, hfmt, frames):
    """Compare active pixel regions of every host frame against the golden.

    Returns (passed, detail).
    """
    with open(golden_path, "rb") as f:
        gdata = f.read()
    with open(host_path, "rb") as f:
        hdata = f.read()

    w = gfmt.get("width") or hfmt.get("width")
    h = gfmt.get("height") or hfmt.get("height")
    if not (w and h):
        return False, "missing width/height for compare"

    g_stride = gfmt.get("bytesperline", 0)
    h_stride = hfmt.get("bytesperline", 0)
    g_size = gfmt.get("sizeimage", 0) or (len(gdata))
    h_size = hfmt.get("sizeimage", 0) or (len(hdata) // max(1, frames))

    golden_active = active_bytes(gdata[:g_size], fmt, w, h, g_stride)

    host_frames = split_frames(hdata, h_size, frames)
    if not host_frames:
        return False, "no host frames captured"

    for i, hf in enumerate(host_frames):
        ha = active_bytes(hf, fmt, w, h, h_stride)
        if ha != golden_active:
            # Find first differing byte for a useful detail line.
            n = min(len(ha), len(golden_active))
            diff_at = next((k for k in range(n) if ha[k] != golden_active[k]), n)
            return False, (f"frame {i} mismatch at active byte {diff_at} "
                           f"(host {len(ha)}B vs golden {len(golden_active)}B)")
    return True, f"{len(host_frames)} frame(s) match golden ({len(golden_active)}B active)"


# ---------------------------------------------------------------------------
# ep_proxy lifecycle on the endpoint
# ---------------------------------------------------------------------------
def gen_ep_config(ep, devices, channel_cfgs):
    channels = [{"v4l2_dev": d, "width": c["width"],
                 "height": c["height"], "format": c["format"]}
                for d, c in zip(devices, channel_cfgs)]
    cfg = json.dumps({"low_latency": True, "channels": channels})
    # Write the config on the endpoint via stdin redirection.
    r = ep.run(f"cat > {EP_CONFIG} << 'EOF'\n{cfg}\nEOF")
    return r.returncode == 0


def start_ep_proxy(ep):
    """Start ep_proxy in the background; return the unit name on success."""
    unit = f"mdb5-ep-{int(time.time())}"
    cmd = (f"systemd-run --unit={unit} --collect "
           f"{EP_PROXY_BIN} -j {EP_CONFIG}")
    r = ep.run(cmd)
    if r.returncode != 0:
        log(f"  failed to start ep_proxy: {r.stderr.strip()}")
        return None
    time.sleep(2)  # let it map BRAM, start streaming, raise the magic
    if not ep.ok(f"systemctl is-active --quiet {unit}"):
        j = ep.run(f"journalctl -u {unit} --no-pager | tail -20")
        log(f"  ep_proxy not active. log:\n{j.stdout}")
        return None
    return unit


def stop_ep_proxy(ep, unit):
    if unit:
        ep.run(f"systemctl stop {unit} 2>/dev/null")
        time.sleep(0.5)


# In-tree modules mdb5_v4l2 links against. insmod (unlike modprobe) does not
# resolve dependencies, so these must be resident before the driver loads.
HOST_DRIVER_DEPS = ["videodev", "videobuf2-common", "videobuf2-v4l2",
                    "videobuf2-dma-sg", "dw-edma"]


def load_host_driver(host, ko_path):
    """insmod the host driver so it reads the (now ep_proxy-stamped) BRAM layout.

    Must be called AFTER ep_proxy is streaming: the driver latches channel
    count + per-channel format from BRAM at probe time and never re-syncs.
    The .ko lives at a custom path (not under /lib/modules), so it is loaded by
    explicit path via sudo insmod -- which does NOT resolve deps, so the in-tree
    dependency modules are modprobe'd first (idempotent). Returns True on success.
    """
    for mod in HOST_DRIVER_DEPS:
        host.run(f"sudo -n modprobe {shlex.quote(mod)} 2>/dev/null")
    r = host.run(f"sudo -n insmod {shlex.quote(ko_path)}")
    if r.returncode != 0:
        log(f"  failed to load mdb5_v4l2: {r.stderr.strip()}")
        return False
    return True


def unload_host_driver(host):
    """Force-unload the host driver, retrying if needed.

    mdb5_v4l2 leaves its module refcount at 1 after streaming (its own IRQ/DMA
    teardown doesn't drop the self-reference), so a plain rmmod fails with
    "Module is in use". Force unload (-f, needs CONFIG_MODULE_FORCE_UNLOAD) is
    required -- this matches the manual workflow.
    """
    for _ in range(10):
        if not host.ok("lsmod | grep -q mdb5"):
            return  # already unloaded
        if host.ok("sudo -n rmmod -f mdb5_v4l2 2>/dev/null"):
            return
        time.sleep(0.5)


# ---------------------------------------------------------------------------
# Host-side device discovery
# ---------------------------------------------------------------------------
def wait_for_host_devs(host, n_expected, pre_devs):
    """Poll for new mdb5_v4l2 capture nodes after ep_proxy starts."""
    deadline = time.time() + HOST_DEV_TIMEOUT
    while time.time() < deadline:
        new = list_video_devices(host) - pre_devs
        mdb5 = []
        for dev in sorted(new):
            d = host.run(f"v4l2-ctl -d {dev} -D")
            if d.returncode == 0 and "mdb5" in d.stdout.lower():
                mdb5.append(dev)
        if len(mdb5) >= n_expected:
            return mdb5[:n_expected]
        time.sleep(1)
    # Final attempt: any mdb5 device currently present.
    final = []
    for dev in sorted(list_video_devices(host)):
        d = host.run(f"v4l2-ctl -d {dev} -D")
        if d.returncode == 0 and "mdb5" in d.stdout.lower():
            final.append(dev)
    return final


# ---------------------------------------------------------------------------
# Per-case execution
# ---------------------------------------------------------------------------
def get_channel_configs(tc):
    n = tc["n_channels"]
    if tc.get("mixed") and "channels" in tc:
        return tc["channels"]
    return [{"width": tc["width"], "height": tc["height"],
             "format": tc["format"]} for _ in range(n)]


def run_case(tc, ep, host, args, artifact_dir, link):
    name = tc["name"]
    n = tc["n_channels"]
    multiplanar = tc.get("multiplanar", False)
    cfgs = get_channel_configs(tc)
    log(f"--- {name} ({n} ch, mp={multiplanar}) ---")

    unit = None
    try:
        # 1. endpoint vivid
        unload_vivid(ep)
        ep_pre = list_video_devices(ep)
        if not load_vivid(ep, n, multiplanar):
            return False, "vivid load failed"
        ep_new = list_video_devices(ep) - ep_pre
        vivid_devs = find_vivid_capture_devs(ep, ep_new)
        if len(vivid_devs) < n:
            return False, f"expected {n} vivid devs, found {len(vivid_devs)}"
        vivid_devs = vivid_devs[:n]
        log(f"  vivid devs: {vivid_devs}")

        # 2. static pattern + format + frame rate
        target_fps = tc.get("fps", 30)
        for dev, c in zip(vivid_devs, cfgs):
            make_vivid_static(ep, dev, fps=target_fps)
            if not set_format(ep, dev, c["width"], c["height"], c["format"]):
                return False, f"set format failed on {dev}"

        # 3. golden capture on endpoint
        golden = []
        for i, dev in enumerate(vivid_devs):
            gpath = EP_GOLDEN.format(ch=i)
            if not capture(ep, dev, 1, gpath):
                return False, f"golden capture failed ch{i}"
            golden.append({"path": gpath, "md5": ep.md5(gpath),
                           "fmt": query_format(ep, dev)})

        # 4. start ep_proxy
        host_pre = list_video_devices(host)
        if not gen_ep_config(ep, vivid_devs, cfgs):
            return False, "ep_proxy config write failed"
        unit = start_ep_proxy(ep)
        if not unit:
            return False, "ep_proxy failed to start/stream"

        # 5. load the host driver NOW -- it reads channel count + per-channel
        #    format from BRAM at probe and latches it for the module's life, so
        #    it must (re)load against this case's freshly stamped mailbox.
        unload_host_driver(host)  # clear any stale layout from a prior case
        if not load_host_driver(host, args.host_ko):
            return False, "mdb5_v4l2 load failed"

        # 6. host device discovery
        host_devs = wait_for_host_devs(host, n, host_pre)
        if len(host_devs) < n:
            return False, f"host exposed {len(host_devs)}/{n} mdb5 devices"
        log(f"  host devs: {host_devs}")

        # 7. host capture + compare, per channel (index-ordered mapping),
        #    once per requested capture mode (mmap/dmabuf).
        modes = [m for m in tc.get("capture_modes", ["mmap"])
                 if m in CAPTURE_MODE_FLAG]
        bw_channels = []  # per-channel throughput samples (informational)
        for i, hdev in enumerate(host_devs):
            hfmt = query_format(host, hdev)
            g = golden[i]
            exp = cfgs[i]
            # sanity: host format should match what we configured
            if hfmt.get("width") != exp["width"] or hfmt.get("height") != exp["height"]:
                return False, (f"ch{i} host fmt {hfmt.get('width')}x"
                               f"{hfmt.get('height')} != expected "
                               f"{exp['width']}x{exp['height']}")

            for mode in modes:
                # dmabuf zerocopy to amdgpu requires a 256-byte-aligned stride;
                # the import fails otherwise, so assert it before capturing.
                if mode == "dmabuf":
                    bpl = hfmt.get("bytesperline", 0)
                    if bpl % 256 != 0:
                        return False, (f"ch{i} dmabuf stride {bpl} not "
                                       f"256-byte aligned (amdgpu zerocopy "
                                       f"requires aligned bytesperline)")

                hpath = HOST_FRAME.format(ch=i)
                if not capture(host, hdev, args.frames, hpath, mode=mode):
                    return False, f"host {mode} capture failed ch{i}"

                # fast path: md5 of a single-frame host capture vs golden
                host_md5 = host.md5(hpath) if args.frames == 1 else None
                if host_md5 and g["md5"] and host_md5 == g["md5"]:
                    log(f"  ch{i} [{mode}]: PASS (md5 match)")
                    continue

                # robust path: pull both, stride-aware active-region compare
                lg = os.path.join(artifact_dir, f"{name}_golden_ch{i}.raw")
                lh = os.path.join(artifact_dir,
                                  f"{name}_host_ch{i}_{mode}.raw")
                if not ep.fetch(g["path"], lg) or not host.fetch(hpath, lh):
                    return False, f"ch{i} could not fetch frames for compare"
                ok, detail = compare_active(lg, lh, exp["format"],
                                            g["fmt"], hfmt, args.frames)
                if not ok:
                    return False, (f"ch{i} [{mode}] data mismatch: {detail} "
                                   f"(artifacts kept)")
                log(f"  ch{i} [{mode}]: PASS ({detail})")
                if not args.keep_artifacts:
                    for p in (lg, lh):
                        try:
                            os.unlink(p)
                        except OSError:
                            pass

            # throughput (always on, informational — never fails a case).
            # Report both the TARGET goodput (matrix fps) and the MEASURED
            # goodput (actual streaming fps through the bridge).
            bw_mode = modes[0] if modes else "mmap"
            sizeimage = hfmt.get("sizeimage", 0)
            target_fps = tc.get("fps", 30)
            bwpath = HOST_FRAME.format(ch=i)
            ok, elapsed, fps = timed_capture(host, hdev, args.bw_frames,
                                             bwpath, mode=bw_mode)
            ch_bw = {"channel": i, "mode": bw_mode, "sizeimage": sizeimage,
                     "target_fps": target_fps, "frames": args.bw_frames,
                     "elapsed_s": round(elapsed, 3),
                     "measured_fps": fps, "target_bps": 0.0,
                     "measured_bps": 0.0}
            if sizeimage > 0:
                ch_bw["target_bps"] = sizeimage * target_fps
                if ok:
                    eff_fps = fps if fps else (args.bw_frames / elapsed
                                               if elapsed > 0 else 0.0)
                    ch_bw["measured_fps"] = round(eff_fps, 2)
                    ch_bw["measured_bps"] = sizeimage * eff_fps
                if link.get("available") and link.get("theoretical_bps"):
                    ch_bw["target_util_pct"] = round(
                        100.0 * ch_bw["target_bps"] / link["theoretical_bps"], 2)
                    if ch_bw["measured_bps"]:
                        ch_bw["measured_util_pct"] = round(
                            100.0 * ch_bw["measured_bps"] / link["theoretical_bps"], 2)
                t_mbps = ch_bw["target_bps"] / 1e6
                m_mbps = ch_bw["measured_bps"] / 1e6
                m_fps = ch_bw.get("measured_fps") or 0.0
                log(f"  ch{i} bw: target {t_mbps:.1f} MByte/s "
                    f"@ {target_fps} fps, "
                    f"measured {m_mbps:.1f} MByte/s @ {m_fps:.1f} fps")
            bw_channels.append(ch_bw)

        case_bw = {
            "name": name,
            "channels": bw_channels,
            "target_bps": sum(c["target_bps"] for c in bw_channels),
            "measured_bps": sum(c["measured_bps"] for c in bw_channels),
        }
        if link.get("available") and link.get("theoretical_bps"):
            tbps = link["theoretical_bps"]
            case_bw["target_util_pct"] = round(
                100.0 * case_bw["target_bps"] / tbps, 2)
            case_bw["measured_util_pct"] = round(
                100.0 * case_bw["measured_bps"] / tbps, 2)

        return True, (f"{n}/{n} channels match golden frame data "
                      f"(modes: {','.join(modes)})"), case_bw

    finally:
        # Unload the host driver before tearing down ep_proxy: the next case
        # restamps BRAM and reloads the driver against its own layout.
        unload_host_driver(host)
        stop_ep_proxy(ep, unit)
        unload_vivid(ep)
        if not args.keep_artifacts:
            ep.run("rm -f /tmp/mdb5_golden_ch*.raw " + EP_CONFIG)
            host.run("rm -f /tmp/mdb5_host_ch*.raw")


# ---------------------------------------------------------------------------
# Throughput reporting (CLI table + HTML)
# ---------------------------------------------------------------------------
BW_CAVEAT = ("Target = sizeimage x matrix fps; measured = actual streaming fps "
             "through the bridge. Per-case aggregate is the SUM of per-channel "
             "goodput (sequential capture, not concurrently saturated).")


def print_bw_summary(link, bw_results):
    """Compact per-case throughput table on the CLI, before the TOTAL line."""
    log("=== PCIe throughput summary ===")
    log(format_link_state(link))
    if not bw_results:
        log("(no throughput samples collected)")
        return
    has_link = link.get("available") and link.get("theoretical_bps")
    header = (f"{'case':<34} {'ch':>3} "
              f"{'target MByte/s':>15} {'measured MByte/s':>17}")
    if has_link:
        header += f" {'%link':>7}"
    log(header)
    for case in bw_results:
        t_agg = case["target_bps"] / 1e6
        m_agg = case["measured_bps"] / 1e6
        line = (f"{case['name']:<34} {len(case['channels']):>3} "
                f"{t_agg:>15.1f} {m_agg:>17.1f}")
        if has_link:
            line += f" {case.get('target_util_pct', 0.0):>6.1f}%"
        log(line)
    log(f"note: {BW_CAVEAT}")


def _html_escape(s):
    return (str(s).replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;"))


def write_report_html(path, link, bw_results, results, elapsed, args):
    """Write a standalone HTML test report with pass/fail summary,
    failure details, and per-channel throughput data."""
    has_link = link.get("available") and link.get("theoretical_bps")
    total = len(results)
    passed = sum(1 for r in results if r["passed"])
    failed = total - passed
    mins, secs = divmod(int(elapsed), 60)

    # -- PCIe link section --
    if has_link:
        link_html = (
            f"<p><strong>PCIe link</strong> [{_html_escape(link['bdf'])}]: "
            f"{link['cur_gts']} GT/s &times; x{link['cur_width']} = "
            f"<strong>{link['theoretical_bps'] / 1e6:.0f} MByte/s</strong> "
            f"theoretical ceiling.</p>")
        if link.get("degraded"):
            link_html += (
                f"<p class='warn'>DEGRADED: link is capable of "
                f"{link.get('max_gts')} GT/s &times; x{link.get('max_width')} "
                f"but only negotiated the above.</p>")
    else:
        link_html = (f"<p class='warn'>PCIe link state unavailable: "
                     f"{_html_escape(link.get('reason', 'unknown'))}. "
                     f"Link-utilization columns omitted.</p>")

    # -- Test results summary table --
    result_rows = []
    for r in results:
        css = "pass" if r["passed"] else "fail"
        tag = "PASS" if r["passed"] else "FAIL"
        result_rows.append(
            f"<tr class='{css}'>"
            f"<td class='case'>{_html_escape(r['name'])}</td>"
            f"<td class='tag'>{tag}</td>"
            f"<td>{_html_escape(r['reason'])}</td></tr>")
    result_table = (
        "<table id='results'><thead><tr>"
        "<th>case</th><th>result</th><th>detail</th>"
        "</tr></thead><tbody>" + "".join(result_rows) + "</tbody></table>")

    # -- Throughput table --
    bw_rows = []
    for case in bw_results:
        n = len(case["channels"])
        t_agg = case["target_bps"] / 1e6
        m_agg = case["measured_bps"] / 1e6
        t_util = (f"{case.get('target_util_pct', 0.0):.1f}%"
                  if has_link else "&mdash;")
        for j, ch in enumerate(case["channels"]):
            t_mbps = ch["target_bps"] / 1e6
            m_mbps = ch["measured_bps"] / 1e6
            t_fps = ch.get("target_fps", 0)
            m_fps = ch.get("measured_fps") or 0.0
            t_util_ch = (f"{ch.get('target_util_pct', 0.0):.1f}%"
                         if has_link else "&mdash;")
            case_cells = ""
            if j == 0:
                case_cells = (
                    f"<td rowspan='{n}' class='case'>"
                    f"{_html_escape(case['name'])}</td>")
                agg_cells = (
                    f"<td rowspan='{n}' class='num agg'>{t_agg:.1f}</td>"
                    f"<td rowspan='{n}' class='num agg'>{m_agg:.1f}</td>"
                    f"<td rowspan='{n}' class='num agg'>{t_util}</td>")
            else:
                agg_cells = ""
            bw_rows.append(
                f"<tr>{case_cells}"
                f"<td class='num'>{ch['channel']}</td>"
                f"<td class='num'>{ch.get('sizeimage', 0):,}</td>"
                f"<td class='num'>{t_fps}</td>"
                f"<td class='num'>{t_mbps:.1f}</td>"
                f"<td class='num'>{m_fps:.1f}</td>"
                f"<td class='num'>{m_mbps:.1f}</td>"
                f"<td class='num'>{t_util_ch}</td>"
                f"{agg_cells}</tr>")

    bw_table = (
        "<table id='throughput'><thead><tr>"
        "<th>case</th><th>ch</th><th>bytes/frame</th>"
        "<th>target fps</th><th>target MByte/s</th>"
        "<th>measured fps</th><th>measured MByte/s</th>"
        "<th>% link</th>"
        "<th>case target MByte/s</th><th>case measured MByte/s</th><th>case % link</th>"
        "</tr></thead><tbody>" + "".join(bw_rows) + "</tbody></table>")

    status_class = "pass" if failed == 0 else "fail"
    status_text = "ALL PASSED" if failed == 0 else f"{failed} FAILED"

    html = f"""<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<title>MDB5 E2E test report</title>
<style>
 body {{ font-family: system-ui, sans-serif; margin: 2rem; color: #222; }}
 h1 {{ font-size: 1.4rem; }}
 h2 {{ font-size: 1.15rem; margin-top: 2rem; border-bottom: 1px solid #ddd;
       padding-bottom: 0.3rem; }}
 table {{ border-collapse: collapse; margin-top: 0.5rem; font-size: 0.9rem;
         width: auto; }}
 th, td {{ border: 1px solid #ccc; padding: 4px 8px; text-align: left; }}
 th {{ background: #f0f0f0; }}
 td.num {{ text-align: right; font-variant-numeric: tabular-nums; }}
 td.mono, td.num {{ font-family: ui-monospace, monospace; }}
 td.case {{ font-weight: 600; vertical-align: top; background: #fafafa; }}
 td.agg {{ vertical-align: top; background: #f5faff; font-weight: 600; }}
 td.tag {{ font-weight: 700; text-align: center; }}
 tr.pass td.tag {{ color: #1a7f37; }}
 tr.fail td.tag {{ color: #cf222e; }}
 tr.fail {{ background: #fff5f5; }}
 .status {{ display: inline-block; padding: 4px 14px; border-radius: 4px;
            font-weight: 700; font-size: 1rem; }}
 .status.pass {{ background: #dafbe1; color: #1a7f37; }}
 .status.fail {{ background: #ffe0e0; color: #cf222e; }}
 .warn {{ color: #b00; font-weight: 600; }}
 .caveat {{ margin-top: 1rem; max-width: 60rem; color: #555;
            font-size: 0.85rem; }}
 .meta {{ color: #555; font-size: 0.85rem; }}
</style></head><body>
<h1>MDB5 PCIe video bridge &mdash; end-to-end test report</h1>
<p class="meta">endpoint <code>{_html_escape(args.endpoint)}</code>,
 host <code>{_html_escape(args.host)}</code>,
 driver <code>{_html_escape(args.host_ko)}</code>,
 bw-frames {args.bw_frames},
 elapsed {mins}m{secs:02d}s</p>

<h2>Summary</h2>
<p><span class="status {status_class}">{status_text}</span>
 &nbsp; {passed} passed, {failed} failed, {total} total</p>
{link_html}

<h2>Test results</h2>
{result_table}

<h2>PCIe throughput</h2>
{bw_table if bw_rows else "<p>No throughput data collected.</p>"}
<p class="caveat"><strong>Note:</strong> {_html_escape(BW_CAVEAT)}</p>
</body></html>
"""
    with open(path, "w") as f:
        f.write(html)


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main():
    epilog = """\
PROVISIONING GUIDE
==================

This script runs on a development machine and orchestrates two targets
over SSH: a Versal endpoint (running ep_proxy + vivid) and an x86 host
(running the mdb5_v4l2 PCIe video bridge driver). Both targets must be
on the same network as the dev machine.

Step 1 -- Copy the script to the dev machine
---------------------------------------------
The script and its test matrix are installed on the endpoint image at:

    /usr/share/mdb5-video-test/mdb5_e2e_test.py
    /usr/share/mdb5-video-test/mdb5_test_matrix.json

Copy both to a working directory on your dev machine:

    scp root@<ENDPOINT_IP>:/usr/share/mdb5-video-test/mdb5_e2e_test.py .
    scp root@<ENDPOINT_IP>:/usr/share/mdb5-video-test/mdb5_test_matrix.json .

Step 2 -- Set up SSH key-based auth
------------------------------------
The script uses BatchMode SSH (no interactive password prompts), so you
need passwordless key-based auth from the dev machine to both targets:

    ssh-copy-id root@<ENDPOINT_IP>     # Versal (root, no password by default)
    ssh-copy-id amd@<HOST_IP>          # x86 host

Verify:  ssh root@<ENDPOINT_IP> 'echo ok'
         ssh amd@<HOST_IP> 'echo ok'

Step 3 -- Provision the Versal endpoint
----------------------------------------
The ERICA appdev image (erica-appdev-image, 2025.2+) includes everything
the endpoint needs out of the box:

    - vivid kernel module (CONFIG_VIDEO_VIVID=m)
    - /usr/bin/ep_proxy
    - /dev/mem access
    - CMA configured (5GB reserved at 0x800000000)
    - v4l2-ctl (v4l-utils)

No additional endpoint setup is required. The vivid kernel module must
include the non-coherent CMA patch (applied automatically in images
built from the meta-erica-edf layer).

Step 4 -- Provision the x86 host
----------------------------------
4a. Install required packages:

    sudo apt update
    sudo apt install v4l-utils pciutils

4b. Build the mdb5_v4l2 driver:

    Clone the driver source from AMD GitHub:

    git clone git@github.amd.com:Automotive/ERICA-S_meta-amdx86-welcome.git
    cd ERICA-S_meta-amdx86-welcome
    git checkout 404a13ab9e960ed3bf3b68ad7bdaf42587bb3fb2

    The driver source is at:
    recipes-modules/mdb5-v4l2/files/

    Direct link (requires AMD GitHub access):
    https://github.amd.com/Automotive/ERICA-S_meta-amdx86-welcome/tree/404a13ab9e960ed3bf3b68ad7bdaf42587bb3fb2/recipes-modules/mdb5-v4l2/files

    Build the out-of-tree mdb5_v4l2.ko module against your host kernel
    and place it at a known path (default: /home/amd/mdb5_v4l2/mdb5_v4l2.ko).
    Override with --host-ko if placed elsewhere.

    The driver must NOT be auto-loaded at boot. It reads its channel
    layout from endpoint BRAM at probe time and never re-syncs, so the
    orchestrator loads it per test case after ep_proxy has stamped the
    mailbox with that case's configuration.

4c. Add passwordless sudo rules:

    The script uses sudo on the host for module loading (insmod/rmmod/
    modprobe) and for reading PCIe link state (lspci -vv). Create a
    sudoers drop-in file:

    echo 'amd ALL=(root) NOPASSWD: /usr/sbin/insmod, /usr/sbin/rmmod, /usr/sbin/modprobe, /usr/sbin/lspci' \\
        | sudo tee /etc/sudoers.d/mdb5-modules
    sudo chmod 0440 /etc/sudoers.d/mdb5-modules

    Adjust paths if your distro puts these binaries in /usr/bin instead:

    which insmod rmmod modprobe lspci    # check actual paths

4d. Verify the PCIe endpoint is enumerated:

    lspci -d 10ee:9038
    # Should show: XX:00.0 Serial controller: Xilinx Corporation Device 9038

Step 5 -- Run the test
-----------------------
Basic run (all 30 matrix cases):

    python3 mdb5_e2e_test.py \\
        --endpoint root@<ENDPOINT_IP> \\
        --host amd@<HOST_IP>

Run a single case to verify setup:

    python3 mdb5_e2e_test.py \\
        --endpoint root@<ENDPOINT_IP> \\
        --host amd@<HOST_IP> \\
        --filter '1ch_uyvy_vga_sp'

The IP addresses can also be set via environment variables:

    export MDB5_EP=root@192.168.1.222
    export MDB5_HOST=amd@192.168.1.203
    python3 mdb5_e2e_test.py

Step 6 -- Collect results
--------------------------
The script prints PASS/FAIL per case and a PCIe throughput summary to
stdout. Capture the full output:

    python3 mdb5_e2e_test.py 2>&1 | tee e2e_results.log

Generate an HTML test report (pass/fail + throughput):

    python3 mdb5_e2e_test.py --bw-report report.html

Keep frame artifacts on failure for offline analysis:

    python3 mdb5_e2e_test.py --keep-artifacts

Exit codes:  0 = all cases passed
             1 = one or more cases failed
             77 = preconditions not met (SKIP)

EXAMPLES
========
Smoke test one case, keep artifacts, generate HTML report:

    python3 mdb5_e2e_test.py --filter '1ch_uyvy_vga_sp' \\
        --keep-artifacts --bw-report smoke.html

Run only 4-channel cases:

    python3 mdb5_e2e_test.py --filter '^4ch_'

Use a custom driver path:

    python3 mdb5_e2e_test.py --host-ko /opt/mdb5/mdb5_v4l2.ko
"""
    ap = argparse.ArgumentParser(
        description="MDB5 PCIe video bridge coordinated end-to-end test",
        epilog=epilog,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--matrix", default=DEFAULT_MATRIX,
                    help="path to test matrix JSON (default: adjacent file)")
    ap.add_argument("--filter", default=None, metavar="REGEX",
                    help="regex filter on test case names")
    ap.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT,
                    help="(reserved) ep_proxy stream window hint "
                         f"(default: {DEFAULT_TIMEOUT}s)")
    ap.add_argument("--frames", type=int, default=DEFAULT_FRAMES,
                    help="host frames captured and compared per channel "
                         f"(default: {DEFAULT_FRAMES})")
    ap.add_argument("--bw-frames", type=int, default=DEFAULT_BW_FRAMES,
                    help="host frames streamed for the throughput sample; "
                         f"longer = steadier fps (default: {DEFAULT_BW_FRAMES})")
    ap.add_argument("--bw-report", default=None, metavar="PATH",
                    help="write a self-contained HTML test report (pass/fail "
                         "summary, failure details, and throughput data)")
    ap.add_argument("--endpoint", default=DEFAULT_ENDPOINT,
                    help=f"endpoint SSH target (default: {DEFAULT_ENDPOINT})")
    ap.add_argument("--host", default=DEFAULT_HOST,
                    help=f"host SSH target (default: {DEFAULT_HOST})")
    ap.add_argument("--host-ko", default=DEFAULT_HOST_KO, metavar="PATH",
                    help="path to mdb5_v4l2.ko on the host; insmod'd per "
                         f"case (default: {DEFAULT_HOST_KO})")
    ap.add_argument("--keep-artifacts", action="store_true",
                    help="keep golden and host frame files after each case")
    args = ap.parse_args()

    ep = Remote(args.endpoint)
    host = Remote(args.host)

    log(f"endpoint={args.endpoint}  host={args.host}")
    if not check_preconditions(ep, host, args.host_ko):
        sys.exit(77)

    link = query_link_state(host)
    log(format_link_state(link))

    try:
        with open(args.matrix) as f:
            matrix = json.load(f)
    except (IOError, json.JSONDecodeError) as e:
        log(f"failed to load matrix: {e}")
        sys.exit(1)

    cases = matrix.get("test_cases", [])
    if args.filter:
        pat = re.compile(args.filter)
        cases = [c for c in cases if pat.search(c["name"])]
    if not cases:
        log("no matching test cases")
        sys.exit(1)

    artifact_dir = tempfile.mkdtemp(prefix="mdb5_e2e_")
    log(f"running {len(cases)} case(s); artifacts in {artifact_dir}")

    results = []
    bw_results = []
    t_start = time.time()
    for tc in cases:
        case_bw = None
        try:
            ret = run_case(tc, ep, host, args, artifact_dir, link)
            # run_case returns (passed, reason) on early exit, or
            # (passed, reason, case_bw) once it reaches the throughput stage.
            if len(ret) == 3:
                passed, reason, case_bw = ret
            else:
                passed, reason = ret
        except Exception as e:  # noqa: BLE001 - one bad case must not abort the run
            passed, reason = False, f"exception: {e}"
        tag = "PASS" if passed else "FAIL"
        print(f"{tag}: mdb5_e2e_{tc['name']}")
        log(f"  -> {tag}: {reason}")
        results.append({"name": tc["name"], "passed": passed,
                        "reason": reason})
        if case_bw:
            bw_results.append(case_bw)

    elapsed = time.time() - t_start

    print_bw_summary(link, bw_results)
    if args.bw_report:
        try:
            write_report_html(args.bw_report, link, bw_results, results,
                              elapsed, args)
            log(f"report: {args.bw_report}")
        except OSError as e:
            log(f"failed to write report: {e}")

    total = len(results)
    failed = sum(1 for r in results if not r["passed"])
    mins, secs = divmod(int(elapsed), 60)
    log(f"TOTAL: {total} FAIL: {failed} ({mins}m{secs:02d}s elapsed)")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
