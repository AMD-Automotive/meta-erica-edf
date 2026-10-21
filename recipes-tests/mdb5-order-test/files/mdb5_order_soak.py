#!/usr/bin/env python3
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: BW
# Maintainer: BW
"""
MDB5 PCIe video bridge frame-order soak / isolation test.

=============================================================================
WHAT WE ARE TRYING TO SOLVE
=============================================================================
Reported symptom: running 5 real cameras at 1920x1536, for about a minute,
2 of the channels appeared to mix up the ordering of their frames -- with
motion on screen, the moving object jumped back and forth in time quite
erratically.  It was seen once, roughly 1 minute out of an hour.  It is
unknown whether the fault is in the PCIe bridge (ep_proxy / mailbox / eDMA /
host driver) or in the cameras themselves.  This test isolates the two and,
if the bridge is at fault, pins down the mechanism.

-----------------------------------------------------------------------------
ROOT-CAUSE HYPOTHESIS (what the test is built to catch)
-----------------------------------------------------------------------------
Walk the per-frame handshake under load.

  Endpoint (ep_proxy, one capture thread per channel):
    DQBUF -> write frame_addr/size/seq into this channel's BRAM mailbox slot
    -> set VALID -> fire MSI-X -> poll host_ack == frame_seq, BUT GIVE UP
    after ack_timeout_ms (default 100) -> clear VALID -> REQUEUE the V4L2
    buffer -> (low_latency) drain loop recycles still more buffers.

  Host (mdb5_v4l2):
    per-channel MSI-X -> primary ISR stashes ep_frame_addr/seq -> threaded
    handler grabs the SINGLE shared eDMA channel (serialized by dma_lock) ->
    DMAs from ep_frame_addr -> labels the buffer sequence = ch->sequence++
    -> writes host_ack.  The host NEVER checks that the bytes it pulled are
    actually frame N -- it trusts the address.

The race, which only bites near bandwidth saturation (5x1920x1536 is right at
the edge; the driver notes ~840 MB/s aggregate on one shared read channel):

  1. Host frame-N DMA is stuck behind other channels holding dma_lock.
  2. Endpoint hits its ACK timeout, clears VALID, REQUEUES buffer N.
  3. Only 4 buffers per channel cycle, and the drain loop recycles them
     aggressively, so the camera overwrites that physaddr with a LATER frame.
  4. Host's DMA finally runs, reads stale/newer pixels, but still labels them
     sequence = N.

Result: the host emits a frame whose METADATA sequence is monotonic but whose
PIXELS are from the wrong point in time -> the moving object jumps. It clusters
on whichever channels lose the dma_lock race during a congestion episode and
self-heals (~1 min) when timing drifts back.  This matches the report.

Corollary that drives the test design: logging the endpoint frame_sequence is
NOT enough -- in this bug the sequence stays in order; only the pixel content
is stale.  So the order check has to be tied to the BYTES that actually
traverse the eDMA, not to the mailbox metadata.

-----------------------------------------------------------------------------
HOW WE DETECT IT: the in-band watermark
-----------------------------------------------------------------------------
ep_proxy stamps {magic, ch_index, frame_seq, capture_ktime} into the first
bytes of every frame buffer (on by default), AFTER DQBUF and BEFORE VALID.
Because it lives in the payload it rides the exact same eDMA path as the
pixels.  If the buffer is reused under the race, the watermark the host reads
is the LATER frame's -- so the decoded frame_seq jumps, and we catch it.

Two independent signals are checked per channel:
  - pixel watermark: decoded from the frame payload by the host order checker
    (ground truth -- reflects what the pixels actually are).
  - mailbox cross-check: the mdb5_v4l2 driver (wm_decode=1) compares the
    watermark frame_seq against the mailbox ep_frame_seq for the same
    delivered frame; a mismatch is DIRECT proof of buffer reuse, in-kernel.
The driver also maps the watermark onto the standard V4L2 vb.sequence /
vb.timestamp, so the checker validates order via the normal ABI too.

An INVERSION (watermark seq <= the previous delivered seq on a channel) is the
reuse signature.  Forward gaps (seq jumped by >1) are dropped frames and are
reported but never fail a case.

-----------------------------------------------------------------------------
ISOLATION LOGIC (bridge vs camera)
-----------------------------------------------------------------------------
The chain is: camera -> endpoint V4L2 capture -> ep_proxy -> [BRAM + eDMA] ->
host driver -> /dev/video.  Ground truth is injected at ep_proxy (the
watermark), so the order check covers ONLY the bridge -- the camera is removed
from the checked path.  Run with both sources:

  - real cameras  : realistic load/timing that triggered the report.
  - vivid         : clean, deterministic baseline.

  * inversions present (either source)            => BRIDGE bug. Use the knob
                                                     sweep below to localize.
  * watermark clean over a long run, but humans
    still see image jumps with real cameras only  => CAMERA / endpoint capture
                                                     (upstream of ep_proxy's
                                                      stamp).

-----------------------------------------------------------------------------
KNOB SWEEP (Test 2): confirm the mechanism + force fast reproduction
-----------------------------------------------------------------------------
A 1-in-an-hour event is hard to wait out, so we vary one knob at a time and
watch the inversion RATE move.  The knob that drives inversions -> 0 names the
fix lever; the knob that drives them UP confirms the hypothesis and makes the
bug reproducible in minutes:

  - endpoint buffers 4->8->16   : vanish => confirms the buffer-reuse race.
  - endpoint ack_timeout         : raise so the EP doesn't requeue until ACK
                                   => vanish => confirms requeue-on-timeout is
                                   the trigger; lower => reproduce faster.
  - host dma_timeout_ms          : lower => more contention timeouts => worse.
  - channels 5->4->2             : fewer => less dma_lock contention.
  - low_latency on/off           : the drain loop recycles buffers; isolates
                                   its contribution.
  - provoke(ack=5,buf=4)         : load stress -- tiny ACK timeout + few
                                   buffers to FORCE the race.  Note it only
                                   bites if the host actually misses an ACK;
                                   on a fast host (e.g. vivid) it can stay
                                   clean, so a clean provoke is NOT proof the
                                   detector works.
  - selftest(fault=N)            : the real detector self-check.  ep_proxy
                                   deliberately stamps stale watermarks
                                   (-F N), so the host MUST log inversions.
                                   Leads every full sweep; run alone with
                                   --selftest.  If it shows BLIND, fix the
                                   harness before trusting any clean cell.

Likely fix once confirmed: more endpoint buffers, and/or do not requeue the
V4L2 buffer until the host ACKs (decouple the EP timeout from buffer reuse),
and/or have the host validate the watermark before delivering the frame.

=============================================================================
Runs from a dev host; orchestrates the endpoint (ep_proxy + source) and the
x86 root complex (mdb5_v4l2) over SSH.  Mirrors mdb5_e2e_test.py provisioning.

Usage:
    mdb5_order_soak.py [--source real|vivid|both] [--channels N]
        [--width W --height H --format FMT] [--duration SECS]
        [--sweep full|none] [--endpoint root@IP] [--host amd@IP]
        [--host-ko PATH] [--ep-cameras CSV] [--report report.html]
"""

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import time

DEFAULT_ENDPOINT = os.environ.get("MDB5_EP", "root@192.168.1.40")
DEFAULT_HOST = os.environ.get("MDB5_HOST", "amd@192.168.1.203")
DEFAULT_HOST_KO = "/home/amd/mdb5_v4l2/mdb5_v4l2.ko"
DEFAULT_CAMERAS = "/dev/video0,/dev/video2,/dev/video4,/dev/video6,/dev/video8"

MDB5_PCI_ID = "10ee:9038"
EP_PROXY_BIN = "/usr/bin/ep_proxy"
EP_CONFIG = "/tmp/mdb5_order_config.json"
HOST_CHECK_SRC = "/usr/share/mdb5-order-test/mdb5_order_check.c"
HOST_CHECK_HDR = "/usr/share/mdb5-order-test/mdb5_frame_wm.h"
HOST_CHECK_BIN = "/tmp/mdb5_order_check"
HOST_DEV_TIMEOUT = 15
HOST_VB2_BUFS = 4   # host-side vb2 buffers the checker requests

# Teardown hardening.  ep_proxy is the producer (fires MSI-X / drives DMA into
# the host driver); it MUST be fully stopped and its capture devices released
# before the next cell opens them or the host driver is removed -- otherwise
# the next ep_proxy hits "Device or resource busy" and rmmod can wedge against
# in-flight DMA.
EP_STOP_TRIES     = 6    # pkill rounds, escalating to SIGKILL, before giving up
HOST_RMMOD_TRIES  = 5    # rmmod attempts (clean first, -f only as last resort)
HOST_RMMOD_TIMEOUT = 25  # per-rmmod ssh timeout (s); a wedged module blocks here
SETTLE_S          = 1.0  # let the kernel release V4L2 devices after a process dies

# Detector self-test: ep_proxy -F N rewinds the watermark seq every Nth
# frame.  At ~30 fps/channel this fires roughly every 4 s, giving plenty
# of inversions even on a short run.
SELFTEST_FAULT = 120

FORMAT_TO_V4L2CTL = {
    "RGB24": "RGB3", "BGR24": "BGR3", "RGBX32": "XB24", "BGRX32": "RX24",
    "YUYV": "YUYV", "UYVY": "UYVY", "NV12": "NV12", "NV21": "NV21",
}

HOST_DRIVER_DEPS = ["videodev", "videobuf2-common", "videobuf2-v4l2",
                    "videobuf2-dma-sg", "dw-edma"]

SSH_OPTS = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=10",
            "-o", "StrictHostKeyChecking=accept-new"]


_LOGFILE = None    # path of the run logfile under /tmp
_LOGFH = None      # open file handle, or None


def init_logfile():
    """Open a /tmp logfile for this run. Returns the path (or None)."""
    global _LOGFILE, _LOGFH
    _LOGFILE = f"/tmp/mdb5_order_soak_{time.strftime('%Y%m%d_%H%M%S')}.log"
    try:
        _LOGFH = open(_LOGFILE, "a", buffering=1)  # line-buffered
    except OSError as e:
        print(f"[mdb5_soak] WARN: cannot open logfile {_LOGFILE}: {e}",
              flush=True)
        _LOGFILE = None
        _LOGFH = None
    return _LOGFILE


def log(msg):
    line = f"[mdb5_soak] {msg}"
    print(line, flush=True)
    if _LOGFH:
        try:
            _LOGFH.write(line + "\n")
        except OSError:
            pass


def finish(code):
    """Print the logfile location and exit."""
    if _LOGFILE:
        log(f"full log saved to {_LOGFILE}")
    sys.exit(code)


class Remote:
    def __init__(self, target):
        self.target = target

    def run(self, cmd, timeout=120):
        try:
            return subprocess.run(["ssh", *SSH_OPTS, self.target, cmd],
                                  timeout=timeout, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True)
        except subprocess.TimeoutExpired:
            # Treat an ssh/command hang as a failed command rather than letting
            # it abort the whole run (a kernel-wedged rmmod can block forever).
            return subprocess.CompletedProcess(
                cmd, 124, stdout="", stderr=f"timeout after {timeout}s")

    def ok(self, cmd, timeout=60):
        return self.run(cmd, timeout=timeout).returncode == 0

    def put(self, local, remote, timeout=60):
        return subprocess.run(["scp", *SSH_OPTS, local, f"{self.target}:{remote}"],
                              timeout=timeout, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True).returncode == 0


# ---------------------------------------------------------------------------
# Preconditions
# ---------------------------------------------------------------------------
def check_preconditions(ep, host, host_ko, ep_proxy_bin):
    if not ep.ok("true", timeout=15):
        log(f"SKIP: endpoint {ep.target} unreachable")
        return False
    if not host.ok("true", timeout=15):
        log(f"SKIP: host {host.target} unreachable")
        return False
    if not ep.ok(f"test -x {shlex.quote(ep_proxy_bin)}"):
        log(f"SKIP: {ep_proxy_bin} missing on endpoint")
        return False
    # The watermark + knob support is required; the old shipped ep_proxy only
    # understands -j/-h.  Probe the help text so a stale binary SKIPs loudly
    # instead of silently producing all-corrupt (unstamped) frames.  -F is
    # the fault-injection self-test, present in the same build as -n.
    h = ep.run(f"{shlex.quote(ep_proxy_bin)} -h 2>&1")
    if "-n" not in h.stdout:
        log(f"SKIP: {ep_proxy_bin} is too old (no watermark support); "
            f"rebuild/reinstall ep_proxy or pass --ep-proxy-bin")
        return False
    if "-F" not in h.stdout:
        log(f"SKIP: {ep_proxy_bin} predates the detector self-test (-F); "
            f"rebuild/reinstall ep_proxy or pass --ep-proxy-bin")
        return False
    if not ep.ok("test -e /dev/mem"):
        log("SKIP: /dev/mem missing on endpoint")
        return False
    if not host.ok("command -v gcc"):
        log("SKIP: gcc missing on host (needed to build the order checker)")
        return False
    if not host.ok(f"test -f {shlex.quote(host_ko)}"):
        log(f"SKIP: mdb5_v4l2.ko missing on host at {host_ko}")
        return False
    probe = host.run("sudo -n rmmod 2>&1")
    if "password" in probe.stdout.lower():
        log("SKIP: passwordless sudo not available on host for insmod/rmmod")
        return False
    return True


# ---------------------------------------------------------------------------
# Endpoint source setup
# ---------------------------------------------------------------------------
def list_video_devices(rem):
    r = rem.run("ls -1 /dev/video* 2>/dev/null")
    return set(r.stdout.split()) if r.returncode == 0 else set()


def unload_vivid(ep):
    for _ in range(10):
        if not ep.ok("lsmod | grep -q '^vivid'"):
            return
        if ep.ok("rmmod vivid 2>/dev/null"):
            return
        time.sleep(0.5)


def load_vivid(ep, n):
    nodes = ",".join(["0x1"] * n)        # capture-only
    alloc = ",".join(["1"] * n)          # dma-contig
    mp = ",".join(["0"] * n)             # single-plane (host driver is sp)
    cmd = (f"modprobe vivid n_devs={n} node_types={nodes} "
           f"multiplanar={mp} allocators={alloc}")
    log(f"endpoint: {cmd}")
    if ep.run(cmd).returncode != 0:
        return False
    time.sleep(1)
    return True


def find_vivid_capture_devs(ep, new_devs, n):
    devs = []
    for dev in sorted(new_devs):
        d = ep.run(f"v4l2-ctl -d {dev} -D")
        if d.returncode == 0 and "vivid" in d.stdout.lower():
            a = ep.run(f"v4l2-ctl -d {dev} --all")
            if a.returncode == 0 and "Video Capture" in a.stdout:
                devs.append(dev)
    return devs[:n]


def set_format(ep, dev, w, h, fmt, fps=30):
    v4l2 = FORMAT_TO_V4L2CTL.get(fmt)
    if not v4l2:
        return False
    ok = ep.run(f"v4l2-ctl -d {dev} --set-fmt-video="
                f"width={w},height={h},pixelformat={v4l2}").returncode == 0
    ep.run(f"v4l2-ctl -d {dev} --set-parm {fps}")
    return ok


def setup_source(ep, source, n, w, h, fmt, cameras):
    """Return the list of endpoint capture device nodes, or None."""
    if source == "vivid":
        unload_vivid(ep)
        pre = list_video_devices(ep)
        if not load_vivid(ep, n):
            log("  vivid load failed")
            return None
        new = list_video_devices(ep) - pre
        devs = find_vivid_capture_devs(ep, new, n)
        if len(devs) < n:
            log(f"  expected {n} vivid devs, found {len(devs)}")
            return None
        for d in devs:
            set_format(ep, d, w, h, fmt)
        return devs

    # real cameras
    devs = cameras[:n]
    for d in devs:
        if not ep.ok(f"test -e {shlex.quote(d)}"):
            log(f"  camera node {d} missing on endpoint")
            return None
        set_format(ep, d, w, h, fmt)
    return devs


# ---------------------------------------------------------------------------
# ep_proxy lifecycle
# ---------------------------------------------------------------------------
def gen_ep_config(ep, devices, w, h, fmt, low_latency, num_buffers, ack_ms,
                  fault_inject=0):
    channels = [{"v4l2_dev": d, "width": w, "height": h, "format": fmt}
                for d in devices]
    cfg = json.dumps({
        "low_latency": low_latency,
        "stamp_seq": True,
        "num_buffers": num_buffers,
        "ack_timeout_ms": ack_ms,
        "fault_inject": fault_inject,
        "channels": channels,
    })
    return ep.run(f"cat > {EP_CONFIG} << 'EOF'\n{cfg}\nEOF").returncode == 0


def start_ep_proxy(ep, ep_proxy_bin):
    unit = f"mdb5-soak-{int(time.time())}"
    cmd = (f"systemd-run --unit={unit} --collect "
           f"{shlex.quote(ep_proxy_bin)} -j {EP_CONFIG}")
    if ep.run(cmd).returncode != 0:
        return None
    time.sleep(2)
    if not ep.ok(f"systemctl is-active --quiet {unit}"):
        j = ep.run(f"journalctl -u {unit} --no-pager | tail -20")
        log(f"  ep_proxy not active:\n{j.stdout}")
        return None
    return unit


def stop_ep_proxy(ep, unit):
    """Stop ep_proxy and CONFIRM the process is gone, so it releases the
    capture devices before the next cell opens them.  Also reaps any stray
    ep_proxy (e.g. from a previous cell that failed teardown).  Returns True
    if no ep_proxy remains."""
    if unit:
        ep.run(f"systemctl stop {unit} 2>/dev/null", timeout=30)
        ep.run(f"systemctl reset-failed {unit} 2>/dev/null", timeout=15)
    for i in range(EP_STOP_TRIES):
        # pgrep returns 0 while an ep_proxy is still alive.
        if not ep.ok("pgrep -x ep_proxy >/dev/null 2>&1", timeout=15):
            time.sleep(SETTLE_S)   # let the kernel release the V4L2 nodes
            return True
        sig = "-9" if i >= 2 else ""   # SIGTERM first, escalate to SIGKILL
        ep.run(f"pkill {sig} -x ep_proxy 2>/dev/null", timeout=15)
        time.sleep(0.5)
    still = ep.ok("pgrep -x ep_proxy >/dev/null 2>&1", timeout=15)
    if still:
        log("  WARNING: ep_proxy will not die; capture devices may stay busy")
    return not still


# ---------------------------------------------------------------------------
# Host driver lifecycle
# ---------------------------------------------------------------------------
def unload_host_driver(host):
    """Remove mdb5_v4l2, preferring a CLEAN rmmod (the producer must already be
    stopped so no DMA is in flight).  -f is used only as a last resort.  Returns
    True if the module is gone, False if it is wedged in the kernel -- which
    typically means a host reboot is required, so the caller should stop."""
    if not host.ok("lsmod | grep -q '^mdb5_v4l2'", timeout=15):
        return True
    for i in range(HOST_RMMOD_TRIES):
        forced = i >= HOST_RMMOD_TRIES - 2
        cmd = "sudo -n rmmod {}mdb5_v4l2".format("-f " if forced else "")
        host.run(f"{cmd} 2>/dev/null", timeout=HOST_RMMOD_TIMEOUT)
        if not host.ok("lsmod | grep -q '^mdb5_v4l2'", timeout=15):
            return True
        time.sleep(1)
    log("  WARNING: mdb5_v4l2 will not unload (wedged in kernel); "
        "a host reboot is likely required")
    return False


def load_host_driver(host, ko, dma_timeout_ms):
    for mod in HOST_DRIVER_DEPS:
        host.run(f"sudo -n modprobe {shlex.quote(mod)} 2>/dev/null")
    r = host.run(f"sudo -n insmod {shlex.quote(ko)} "
                 f"wm_decode=1 dma_timeout_ms={dma_timeout_ms}")
    if r.returncode != 0:
        log(f"  insmod mdb5_v4l2 failed: {r.stderr.strip()}")
        return False
    return True


def wait_for_host_devs(host, n, pre):
    deadline = time.time() + HOST_DEV_TIMEOUT
    while time.time() < deadline:
        new = list_video_devices(host) - pre
        mdb5 = []
        for dev in sorted(new):
            d = host.run(f"v4l2-ctl -d {dev} -D")
            if d.returncode == 0 and "mdb5" in d.stdout.lower():
                mdb5.append(dev)
        if len(mdb5) >= n:
            return mdb5[:n]
        time.sleep(1)
    return []


def build_checker(host):
    """Compile the order checker on the host. Returns True on success."""
    if not host.ok(f"test -f {shlex.quote(HOST_CHECK_SRC)}"):
        # Fall back: the dev host pushes the sources next to this script.
        here = os.path.dirname(os.path.abspath(__file__))
        src = os.path.join(here, "mdb5_order_check.c")
        hdr = os.path.join(here, "mdb5_frame_wm.h")
        if not (os.path.exists(src) and os.path.exists(hdr)):
            log("  order checker sources not found locally or on host")
            return False
        if not host.put(src, "/tmp/mdb5_order_check.c"):
            return False
        if not host.put(hdr, "/tmp/mdb5_frame_wm.h"):
            return False
        srcpath, incdir = "/tmp/mdb5_order_check.c", "/tmp"
    else:
        srcpath, incdir = HOST_CHECK_SRC, os.path.dirname(HOST_CHECK_HDR)
    r = host.run(f"gcc -O2 -Wall -I{shlex.quote(incdir)} "
                 f"-o {HOST_CHECK_BIN} {shlex.quote(srcpath)}")
    if r.returncode != 0:
        log(f"  checker build failed: {r.stderr.strip()}")
        return False
    return True


def run_checker(host, host_devs, duration):
    devs = " ".join(shlex.quote(d) for d in host_devs)
    cmd = (f"{HOST_CHECK_BIN} --duration {duration} "
           f"--buffers {HOST_VB2_BUFS} {devs}")
    r = host.run(cmd, timeout=duration + 60)
    if r.returncode != 0 and not r.stdout.strip():
        log(f"  checker failed: {r.stderr.strip()}")
        return None
    try:
        return json.loads(r.stdout)
    except json.JSONDecodeError:
        log(f"  could not parse checker output:\n{r.stdout[:500]}")
        return None


# ---------------------------------------------------------------------------
# One cell of the matrix
# ---------------------------------------------------------------------------
def capture_host_dmesg(host, source, label):
    """Save host kernel log to a per-cell file on the dev box.

    Called at the TOP of the cell's finally block -- before stop_ep_proxy /
    rmmod -- because the streamoff + rmmod teardown emits multi-line vb2
    WARNING stack dumps that flood (and evict) the per-frame INVERSION
    forensics from the kernel ring buffer.  The degraded-state evidence we
    care about (INVERSION lines with mailbox_seq vs wm_seq, and the -110 DMA
    timeouts) lives in the run itself, so we must grab it first.

    dmesg is world-readable here (dmesg_restrict=0), so no sudo is needed.
    Best-effort: never raises into the teardown path."""
    safe = re.sub(r"[^A-Za-z0-9._-]", "_", f"{source}_{label}")
    path = f"/tmp/mdb5_dmesg_{safe}.log"
    try:
        r = host.run("dmesg", timeout=30)
        if r.returncode != 0:
            log(f"  dmesg capture failed (rc={r.returncode}): {r.stderr.strip()}")
            return
        lines = r.stdout.splitlines()
        # Scope to this cell's driver session: the last probe is this cell's
        # insmod (the clean-slate step unloaded any prior instance).
        start = 0
        for i, ln in enumerate(lines):
            if "V4L2 Driver" in ln and "probing" in ln:
                start = i
        cell = lines[start:]
        with open(path, "w") as f:
            f.write("\n".join(cell) + "\n")
        hot = [ln for ln in cell
               if re.search(r"INVERSION|REGRESSION|DMA timeout|transfer failed", ln)]
        n_inv = sum("INVERSION" in ln for ln in hot)
        n_to = sum("DMA timeout" in ln for ln in hot)
        log(f"  host dmesg -> {path} ({len(cell)} lines; "
            f"{n_inv} INVERSION, {n_to} DMA-timeout lines)")
        for ln in hot[:6]:
            log(f"    | {ln.strip()}")
    except OSError as e:
        log(f"  dmesg capture error: {e}")


def run_cell(label, knobs, ep, host, args, source):
    """Run a single configuration. Returns a result dict."""
    n = knobs["channels"]
    log(f"=== cell [{source}] {label}: {knobs} ===")
    unit = None
    try:
        # Clean slate: a stray ep_proxy (devices busy) or a leftover/wedged
        # host driver from a previous cell must not cascade into this one.
        stop_ep_proxy(ep, None)
        if not unload_host_driver(host):
            return {"label": label, "source": source, "knobs": knobs,
                    "error": "host driver wedged before cell; reboot host",
                    "fatal": True}

        devs = setup_source(ep, source, n, args.width, args.height,
                            args.format, args.cameras)
        if not devs:
            return {"label": label, "source": source, "knobs": knobs,
                    "error": "source setup failed"}
        log(f"  endpoint devs: {devs}")

        host_pre = list_video_devices(host)
        if not gen_ep_config(ep, devs, args.width, args.height, args.format,
                             knobs["low_latency"], knobs["buffers"],
                             knobs["ack_ms"], knobs.get("fault", 0)):
            return {"label": label, "source": source, "knobs": knobs,
                    "error": "ep config write failed"}
        unit = start_ep_proxy(ep, args.ep_proxy_bin)
        if not unit:
            return {"label": label, "source": source, "knobs": knobs,
                    "error": "ep_proxy failed to start"}

        # Driver was already removed in the clean-slate step above; load the
        # consumer now that the producer is up and writing the BRAM magic.
        if not load_host_driver(host, args.host_ko, knobs["dma_ms"]):
            return {"label": label, "source": source, "knobs": knobs,
                    "error": "host driver load failed"}

        host_devs = wait_for_host_devs(host, n, host_pre)
        if len(host_devs) < n:
            return {"label": label, "source": source, "knobs": knobs,
                    "error": f"host exposed {len(host_devs)}/{n} devices"}
        log(f"  host devs: {host_devs}")

        res = run_checker(host, host_devs, args.duration)
        if res is None:
            return {"label": label, "source": source, "knobs": knobs,
                    "error": "checker produced no result"}

        res.update({"label": label, "source": source, "knobs": knobs})
        t = res.get("totals", {})

        if knobs.get("expect_fail"):
            # Detector self-test: ep_proxy deliberately stamped stale
            # watermarks, so the host MUST have logged inversions.  The
            # cell "passes" the suite only if the detector actually fired;
            # a clean result here means the decode path is blind.
            detected = t.get("inversions", 0) > 0
            res["selftest"] = True
            res["selftest_detected"] = detected
            res["pass"] = detected
            log(f"  -> SELFTEST inversions={t.get('inversions')} "
                f"detector={'FIRED (good)' if detected else 'BLIND (bad)'}")
        else:
            log(f"  -> inversions={t.get('inversions')} "
                f"ch_mismatch={t.get('ch_mismatch')} "
                f"wm_corrupt={t.get('wm_corrupt')} pass={res.get('pass')}")
        return res
    finally:
        # Grab the host kernel log BEFORE teardown -- the rmmod/streamoff
        # WARNING flood would otherwise evict the per-frame INVERSION
        # forensics and -110 DMA-timeout lines from the ring buffer.
        capture_host_dmesg(host, source, label)
        # Order matters: stop the PRODUCER first so no MSI-X / DMA is in
        # flight, THEN remove the consumer -- otherwise rmmod races live DMA
        # and wedges (the 60s rmmod hang seen in the field).
        stop_ep_proxy(ep, unit)
        unload_host_driver(host)
        if source == "vivid":
            unload_vivid(ep)
        ep.run(f"rm -f {EP_CONFIG}", timeout=15)


# ---------------------------------------------------------------------------
# Sweep definition
# ---------------------------------------------------------------------------
def build_cells(args):
    """Baseline + one-variable-at-a-time sweep. Production defaults match the
    reported failing condition; each cell changes exactly one knob."""
    base = {"channels": args.channels, "buffers": 4, "ack_ms": 100,
            "dma_ms": args.dma_ms, "low_latency": True}

    def variant(name, **over):
        k = dict(base)
        k.update(over)
        return (name, k)

    # Deterministic detector self-test: ep_proxy is told (-F / fault_inject)
    # to rewind the watermark seq every Nth frame, so the host MUST log
    # inversions.  Unlike the load-based provoke cell, this does not depend
    # on hitting an ACK timeout -- it ALWAYS fires, so a clean result here
    # proves the decode path is blind.  expect_fail flips the pass logic.
    selftest = variant(f"selftest(fault={SELFTEST_FAULT})",
                       fault=SELFTEST_FAULT)
    selftest[1]["expect_fail"] = True

    # --selftest: validate the detector and nothing else (quick).
    if getattr(args, "selftest", False):
        return [selftest]

    cells = [("baseline", dict(base))]
    if args.sweep == "none":
        return cells

    # Full sweep always self-validates the detector first.
    cells = [selftest, ("baseline", dict(base))]
    cells += [
        variant("buffers=8", buffers=8),
        variant("buffers=16", buffers=16),
        variant("ack_ms=20", ack_ms=20),
        variant("ack_ms=1000", ack_ms=1000),
        variant("dma_ms=20", dma_ms=20),
        variant("dma_ms=1000", dma_ms=1000),
        variant("low_latency=off", low_latency=False),
    ]
    if args.channels > 4:
        cells.append(variant("channels=4", channels=4))
    if args.channels > 2:
        cells.append(variant("channels=2", channels=2))
    # Detector self-check: tiny ACK timeout + few buffers should PROVOKE the
    # race -- if this stays clean, the detector (not the bridge) is suspect.
    cells.append(variant("provoke(ack=5,buf=4)", ack_ms=5, buffers=4))
    return cells


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------
def print_summary(results):
    log("=== soak summary ===")
    hdr = (f"{'source':<6} {'cell':<22} {'frames':>9} {'inv':>6} "
           f"{'maxjmp':>7} {'gaps':>7} {'chmm':>5} {'corrupt':>8} {'pass':>5}")
    log(hdr)
    for r in results:
        if "error" in r:
            log(f"{r['source']:<6} {r['label']:<22} ERROR: {r['error']}")
            continue
        t = r.get("totals", {})
        frames = sum(c.get("frames", 0) for c in r.get("channels", []))
        maxjmp = max((c.get("max_backward_jump", 0)
                      for c in r.get("channels", [])), default=0)
        gaps = sum(c.get("gaps", 0) for c in r.get("channels", []))
        if r.get("selftest"):
            verdict = "FIRED" if r.get("pass") else "BLIND"
        else:
            verdict = "YES" if r.get("pass") else "no"
        log(f"{r['source']:<6} {r['label']:<22} {frames:>9} "
            f"{t.get('inversions', 0):>6} {maxjmp:>7} {gaps:>7} "
            f"{t.get('ch_mismatch', 0):>5} {t.get('wm_corrupt', 0):>8} "
            f"{verdict:>5}")


def write_html(path, results, args):
    def esc(s):
        return (str(s).replace("&", "&amp;").replace("<", "&lt;")
                .replace(">", "&gt;"))
    rows = []
    for r in results:
        if "error" in r:
            rows.append(f"<tr class='err'><td>{esc(r['source'])}</td>"
                        f"<td>{esc(r['label'])}</td>"
                        f"<td colspan='7'>ERROR: {esc(r['error'])}</td></tr>")
            continue
        t = r.get("totals", {})
        frames = sum(c.get("frames", 0) for c in r.get("channels", []))
        maxjmp = max((c.get("max_backward_jump", 0)
                      for c in r.get("channels", [])), default=0)
        gaps = sum(c.get("gaps", 0) for c in r.get("channels", []))
        css = "pass" if r.get("pass") else "fail"
        if r.get("selftest"):
            verdict = "DETECTOR FIRED" if r.get("pass") else "DETECTOR BLIND"
        else:
            verdict = "PASS" if r.get("pass") else "FAIL"
        rows.append(
            f"<tr class='{css}'><td>{esc(r['source'])}</td>"
            f"<td>{esc(r['label'])}</td><td class='n'>{frames}</td>"
            f"<td class='n'>{t.get('inversions', 0)}</td>"
            f"<td class='n'>{maxjmp}</td><td class='n'>{gaps}</td>"
            f"<td class='n'>{t.get('ch_mismatch', 0)}</td>"
            f"<td class='n'>{t.get('wm_corrupt', 0)}</td>"
            f"<td>{verdict}</td></tr>")
    html = f"""<!DOCTYPE html><html><head><meta charset="utf-8">
<title>MDB5 frame-order soak report</title><style>
body{{font-family:system-ui,sans-serif;margin:2rem;}}
table{{border-collapse:collapse;font-size:.9rem;}}
th,td{{border:1px solid #ccc;padding:4px 8px;}}
td.n{{text-align:right;font-variant-numeric:tabular-nums;}}
tr.fail{{background:#fff5f5;}} tr.fail td:last-child{{color:#cf222e;font-weight:700;}}
tr.pass td:last-child{{color:#1a7f37;font-weight:700;}}
tr.err{{background:#fffbe6;}}
</style></head><body>
<h1>MDB5 PCIe video bridge &mdash; frame-order soak</h1>
<p>endpoint <code>{esc(args.endpoint)}</code>, host <code>{esc(args.host)}</code>,
 {esc(args.width)}x{esc(args.height)} {esc(args.format)},
 {esc(args.duration)}s/cell</p>
<p>An <b>inversion</b> (watermark seq &le; previous on a channel) is the
 frame-reuse signature. <b>gaps</b> are dropped frames (informational).
 <b>chmm</b> = cross-channel watermark; <b>corrupt</b> = undecodable watermark.</p>
<table><thead><tr><th>source</th><th>cell</th><th>frames</th><th>inversions</th>
<th>max back-jump</th><th>gaps</th><th>ch-mismatch</th><th>wm-corrupt</th>
<th>result</th></tr></thead><tbody>{''.join(rows)}</tbody></table>
</body></html>"""
    with open(path, "w") as f:
        f.write(html)


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
EPILOG = r"""
RUNTIME ESTIMATE
================
Total wall clock  ~=  sources x cells x (per-cell duration + ~30s overhead)

  sources : 1 for real|vivid, 2 for both
  cells   : 1 with --selftest or --sweep none; with --sweep full at
            --channels 5 it is 12: a leading selftest(fault=N) detector
            check, then baseline, buffers=8, buffers=16, ack_ms=20,
            ack_ms=1000, dma_ms=20, dma_ms=1000, low_latency=off,
            channels=4, channels=2, provoke(ack=5,buf=4).  Fewer channels
            drop the channels=4/2 cells.
  overhead: vivid load, ep_proxy start, driver reload, host-dev discovery,
            teardown -- roughly 20-40s per cell.

  invocation                                          cells   ~wall clock
  --------------------------------------------------- -----   -----------
  --source vivid --selftest --duration 60                1      ~1.5 min
  --source vivid --sweep none --duration 60  (smoke)     1      ~1.5 min
  --source vivid --sweep none --duration 600             1      ~10 min
  --source vivid --sweep full  --duration 600           12      ~2.1 hr
  --source both  --sweep full  --duration 600 (default) 24      ~4.3 hr
  --source both  --sweep full  --duration 1800          24      ~12.5 hr

NOTE: the baseline cell runs at production settings, where the fault is
~1-in-an-hour, so --duration 600 likely will NOT reproduce it at baseline.
The sweep's provoke / low-timeout / low-buffer cells try to FORCE the race,
but they only fire if the host actually misses an ACK -- on a fast host
(e.g. vivid) they may stay clean.  The leading selftest(fault=N) cell is the
reliable check that the DETECTOR works: ep_proxy stamps deliberately stale
watermarks, so the host MUST log inversions there (result column = FIRED).
If selftest shows BLIND, fix the harness before trusting any clean cell.
To hunt the rare event at production settings, run a long baseline-only soak
(see the last example).

EXAMPLES
========
NOTE on --ep-proxy-bin: the orchestrator launches ep_proxy at this path
(default /usr/bin/ep_proxy) and SKIPs loudly if that binary is too old to
stamp watermarks.  Until the endpoint image is rebuilt with the
watermark-capable ep_proxy, point it at your freshly built binary, e.g.
--ep-proxy-bin /usr/local/bin/ep_proxy (shown in the examples below).  Once
the image ships the new ep_proxy, drop the switch and the default works.

A logfile is always written to /tmp/mdb5_order_soak_<timestamp>.log on the
machine running this script; its path is printed at start and end of the run.

Detector self-test only (validate the harness before trusting a sweep;
PASS = detector FIRED on injected faults); ~1.5 min:
    python3 mdb5_order_soak.py --source vivid --selftest --duration 60 \
        --endpoint root@192.168.1.222 --host amd@192.168.1.203 \
        --ep-proxy-bin /usr/local/bin/ep_proxy

Quick pipeline smoke (self-test + full chain); ~25 min:
    python3 mdb5_order_soak.py --source vivid --sweep full --duration 120 \
        --endpoint root@192.168.1.222 --host amd@192.168.1.203 \
        --ep-proxy-bin /usr/local/bin/ep_proxy

5-channel VIVID-only full sweep (clean, deterministic baseline source):
    python3 mdb5_order_soak.py --source vivid --channels 5 \
        --width 1920 --height 1536 --format NV12 \
        --endpoint root@192.168.1.222 --host amd@192.168.1.203 \
        --ep-proxy-bin /usr/local/bin/ep_proxy \
        --report soak_vivid.html

5-channel CAMERA-only full sweep.  Real cameras have fixed device nodes, so
select them explicitly with --ep-cameras (comma-separated, one per channel,
in channel order).  Here the 5 cameras enumerate as the even nodes 0..8:
    python3 mdb5_order_soak.py --source real --channels 5 \
        --ep-cameras /dev/video0,/dev/video2,/dev/video4,/dev/video6,/dev/video8 \
        --width 1920 --height 1536 --format NV12 \
        --endpoint root@192.168.1.222 --host amd@192.168.1.203 \
        --ep-proxy-bin /usr/local/bin/ep_proxy \
        --report soak_real.html

BOTH sources, full sweep (real for realistic timing, vivid for a clean
baseline -- the default source mode); ~4.3 hr:
    python3 mdb5_order_soak.py --source both --channels 5 \
        --ep-cameras /dev/video0,/dev/video2,/dev/video4,/dev/video6,/dev/video8 \
        --width 1920 --height 1536 --format NV12 \
        --endpoint root@192.168.1.222 --host amd@192.168.1.203 \
        --ep-proxy-bin /usr/local/bin/ep_proxy \
        --report soak_both.html

Long baseline-only soak to catch the rare event at production settings
(one cell, 4 hours, real cameras, no knob sweep):
    python3 mdb5_order_soak.py --source real --sweep none --duration 14400 \
        --channels 5 \
        --ep-cameras /dev/video0,/dev/video2,/dev/video4,/dev/video6,/dev/video8 \
        --endpoint root@192.168.1.222 --host amd@192.168.1.203 \
        --ep-proxy-bin /usr/local/bin/ep_proxy
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__, epilog=EPILOG,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", choices=["real", "vivid", "both"],
                    default="both")
    ap.add_argument("--channels", type=int, default=5)
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1536)
    ap.add_argument("--format", default="NV12")
    ap.add_argument("--dma-ms", dest="dma_ms", type=int, default=100,
                    help="host driver DMA completion timeout (ms) for the "
                         "baseline cell (default 100); raise to test whether "
                         "the first -110 is a transient that terminate_sync "
                         "turns into a permanent wedge")
    ap.add_argument("--duration", type=int, default=600,
                    help="seconds per cell (default 600)")
    ap.add_argument("--sweep", choices=["full", "none"], default="full")
    ap.add_argument("--selftest", action="store_true",
                    help="run ONLY the fault-injection detector self-test: "
                         "ep_proxy deliberately stamps stale watermarks and "
                         "the host MUST log inversions (PASS = detector fired). "
                         "Use this to validate the harness before trusting a "
                         "clean sweep. Quick (one cell).")
    ap.add_argument("--endpoint", default=DEFAULT_ENDPOINT)
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--host-ko", default=DEFAULT_HOST_KO)
    ap.add_argument("--ep-proxy-bin", default=EP_PROXY_BIN,
                    help="path to ep_proxy on the endpoint; override to test a "
                         f"freshly built binary without reflashing "
                         f"(default {EP_PROXY_BIN}). Must support watermark "
                         f"stamping (-n) and fault injection (-F); the "
                         f"orchestrator SKIPs if either is missing.")
    ap.add_argument("--ep-cameras", default=DEFAULT_CAMERAS,
                    help="CSV of endpoint camera nodes for --source real")
    ap.add_argument("--report", default=None, metavar="PATH",
                    help="write a standalone HTML report")
    args = ap.parse_args()
    args.cameras = [c.strip() for c in args.ep_cameras.split(",") if c.strip()]

    path = init_logfile()
    if path:
        log(f"logging to {path}")

    ep = Remote(args.endpoint)
    host = Remote(args.host)
    log(f"endpoint={args.endpoint} host={args.host}")
    if not check_preconditions(ep, host, args.host_ko, args.ep_proxy_bin):
        finish(77)
    if not build_checker(host):
        log("SKIP: could not build the order checker on the host")
        finish(77)

    sources = ["real", "vivid"] if args.source == "both" else [args.source]
    cells = build_cells(args)
    log(f"{len(sources)} source(s) x {len(cells)} cell(s), "
        f"{args.duration}s each")

    results = []
    aborted = False
    for source in sources:
        if aborted:
            break
        for label, knobs in cells:
            try:
                r = run_cell(label, knobs, ep, host, args, source)
            except Exception as e:  # noqa: BLE001 one bad cell mustn't abort
                r = {"label": label, "source": source,
                     "knobs": knobs, "error": f"exception: {e}"}
            results.append(r)
            # A wedged host driver poisons every subsequent cell; stop now with
            # a clear message instead of emitting a wall of cascading errors.
            if r.get("fatal"):
                log("ABORT: host driver is wedged in the kernel. Reboot the "
                    "host (and power-cycle the endpoint if needed), then re-run.")
                aborted = True
                break

    print_summary(results)
    if args.report:
        write_html(args.report, results, args)
        log(f"report: {args.report}")

    # Exit non-zero if any cell showed inversions or cross-channel frames.
    bad = sum(1 for r in results
              if "error" not in r and not r.get("pass", False))
    errs = sum(1 for r in results if "error" in r)
    log(f"TOTAL cells={len(results)} reorder_fail={bad} errors={errs}")
    finish(1 if bad else 0)


if __name__ == "__main__":
    main()
