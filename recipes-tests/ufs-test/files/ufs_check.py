#!/usr/bin/env python3
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

"""UFS storage ptest — read-only health, status and performance checks.

Discovers the UFS controller and LUNs dynamically via sysfs.
All I/O tests are read-only (--readonly flag to fio, direct reads).
Nothing is written to the UFS device.
"""

import glob
import json
import os
import subprocess
import sys
import time

# ---------------------------------------------------------------------------
#  Thresholds  (conservative; safe for any point in device life)
# ---------------------------------------------------------------------------
# EOL info: 0x01 = normal, 0x02 = 10% consumed, 0x03 = critical
MAX_EOL_INFO = 0x02
# Life-time estimates: 0x01 = 0-10%, … 0x0A = 90-100%, 0x0B = exceeded
MAX_LIFETIME = 0x0A
# HS power mode bits [7:4]=Tx gear, [3:0]=Rx gear  — at least gear 1
MIN_POWER_MODE = 0x11
# Sequential read bandwidth (MB/s) — read from 128K blocks
MIN_SEQ_READ_MBS = 500
# Random 4K read IOPS
MIN_RAND_READ_IOPS = 20000
# fio test size (reads only, no data modified)
FIO_SEQ_SIZE = "256m"
FIO_RAND_SIZE = "64m"

# ---------------------------------------------------------------------------
#  Helpers
# ---------------------------------------------------------------------------

def sysfs_read(path):
    """Read a sysfs file, return stripped contents or None."""
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return None


def sysfs_read_int(path):
    """Read a sysfs file and parse as int (auto-detect hex)."""
    val = sysfs_read(path)
    if val is None:
        return None
    try:
        return int(val, 0)
    except ValueError:
        return None


def human_bytes(n):
    """Format byte count as human-readable string."""
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if abs(n) < 1024:
            return f"{n:.1f} {unit}"
        n /= 1024
    return f"{n:.1f} PB"


def find_ufs_controllers():
    """Discover UFS controllers by scanning /sys/class/scsi_host for ufshcd."""
    controllers = []
    for host_dir in sorted(glob.glob("/sys/class/scsi_host/host*")):
        proc = sysfs_read(os.path.join(host_dir, "proc_name"))
        if proc and "ufshcd" in proc:
            host_name = os.path.basename(host_dir)
            # Walk up to platform device for sysfs descriptors
            real = os.path.realpath(host_dir)
            # typical: /sys/devices/platform/<addr>.ufs/host0/scsi_host/host0
            parts = real.split("/")
            platform_dev = None
            for i, p in enumerate(parts):
                if p.endswith(".ufs"):
                    platform_dev = "/".join(parts[: i + 1])
                    break
            controllers.append((host_name, platform_dev))
    return controllers


def find_ufs_block_devices(host_name):
    """Find block devices belonging to a SCSI host, return sorted by size desc."""
    host_num = host_name.replace("host", "")
    devs = []
    for bdev in sorted(glob.glob(f"/sys/class/block/sd*")):
        name = os.path.basename(bdev)
        # Only whole disks (no partitions like sda1)
        if any(c.isdigit() for c in name[2:]):
            continue
        # Check if this block device belongs to our host
        real = os.path.realpath(bdev)
        if f"/host{host_num}/" in real:
            dev_path = f"/dev/{name}"
            size = sysfs_read_int(os.path.join(bdev, "size"))
            size_bytes = size * 512 if size else 0
            # Get SCSI model
            device_dir = os.path.join(bdev, "device")
            vendor = (sysfs_read(os.path.join(device_dir, "vendor")) or "").strip()
            model = (sysfs_read(os.path.join(device_dir, "model")) or "").strip()
            devs.append({
                "name": name,
                "path": dev_path,
                "size_bytes": size_bytes,
                "vendor": vendor,
                "model": model,
            })
    devs.sort(key=lambda d: d["size_bytes"], reverse=True)
    return devs


def run_fio_read(dev_path, rw, bs, size):
    """Run a read-only fio job and return (bw_bytes, iops, lat_ns_mean) or None."""
    cmd = [
        "fio",
        f"--name=ufs-{rw}",
        f"--filename={dev_path}",
        f"--rw={rw}",
        f"--bs={bs}",
        f"--size={size}",
        "--numjobs=1",
        "--ioengine=libaio",
        "--iodepth=32",
        "--direct=1",
        "--readonly",
        "--output-format=json",
    ]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return None
    if proc.returncode != 0:
        return None
    try:
        data = json.loads(proc.stdout)
        job = data["jobs"][0]["read"]
        return (job["bw_bytes"], job["iops"], job["lat_ns"]["mean"])
    except (KeyError, IndexError, json.JSONDecodeError):
        return None


def test_scsi_inquiry(ctx):
    devs = ctx.get("block_devs", [])
    if not devs:
        return "FAIL", "No block devices"
    dev = devs[0]
    try:
        proc = subprocess.run(
            ["sg_inq", dev["path"]],
            capture_output=True, text=True, timeout=10,
        )
    except FileNotFoundError:
        return "FAIL", "sg_inq not found (install sg3-utils)"
    except subprocess.TimeoutExpired:
        return "FAIL", f"sg_inq timed out on {dev['path']}"
    if proc.returncode != 0:
        return "FAIL", f"sg_inq failed on {dev['path']}: {proc.stderr.strip()}"
    # Parse key fields from sg_inq output
    info = {}
    for line in proc.stdout.splitlines():
        line = line.strip()
        for key in ("Vendor identification", "Product identification",
                     "Product revision level", "Unit serial number",
                     "Version descriptor"):
            if line.startswith(key + ":"):
                val = line.split(":", 1)[1].strip()
                info.setdefault(key, val)
    vendor = info.get("Vendor identification", "?")
    product = info.get("Product identification", "?")
    revision = info.get("Product revision level", "?")
    serial = info.get("Unit serial number", "?")
    lines = [
        f"{dev['path']} SCSI INQUIRY:",
        f"  Vendor:   {vendor}",
        f"  Product:  {product}",
        f"  Revision: {revision}",
        f"  Serial:   {serial}",
    ]
    return "PASS", "\n".join(lines)

# ---------------------------------------------------------------------------
#  Test cases — each returns (result, detail_string)
# ---------------------------------------------------------------------------

def test_controller_present(ctx):
    controllers = find_ufs_controllers()
    if not controllers:
        return "FAIL", "No UFS controller found in /sys/class/scsi_host"
    host, plat = controllers[0]
    ctx["host"] = host
    ctx["platform"] = plat
    detail = f"controller={host}"
    if plat:
        detail += f" platform={os.path.basename(plat)}"
    return "PASS", detail


def test_health_eol(ctx):
    plat = ctx.get("platform")
    if not plat:
        return "FAIL", "No platform path"
    val = sysfs_read_int(os.path.join(plat, "health_descriptor/eol_info"))
    if val is None:
        return "FAIL", "Cannot read eol_info"
    labels = {0x01: "normal", 0x02: "warning(10% consumed)", 0x03: "critical"}
    label = labels.get(val, f"unknown(0x{val:02X})")
    result = "PASS" if val <= MAX_EOL_INFO else "FAIL"
    return result, f"eol_info=0x{val:02X} ({label})"


def test_health_lifetime_a(ctx):
    plat = ctx.get("platform")
    if not plat:
        return "FAIL", "No platform path"
    val = sysfs_read_int(os.path.join(plat, "health_descriptor/life_time_estimation_a"))
    if val is None:
        return "FAIL", "Cannot read life_time_estimation_a"
    pct_lo = (val - 1) * 10
    pct_hi = val * 10
    result = "PASS" if val <= MAX_LIFETIME else "FAIL"
    return result, f"life_time_a=0x{val:02X} ({pct_lo}-{pct_hi}% used)"


def test_health_lifetime_b(ctx):
    plat = ctx.get("platform")
    if not plat:
        return "FAIL", "No platform path"
    val = sysfs_read_int(os.path.join(plat, "health_descriptor/life_time_estimation_b"))
    if val is None:
        return "FAIL", "Cannot read life_time_estimation_b"
    pct_lo = (val - 1) * 10
    pct_hi = val * 10
    result = "PASS" if val <= MAX_LIFETIME else "FAIL"
    return result, f"life_time_b=0x{val:02X} ({pct_lo}-{pct_hi}% used)"


def test_power_mode(ctx):
    plat = ctx.get("platform")
    if not plat:
        return "FAIL", "No platform path"
    val = sysfs_read_int(os.path.join(plat, "attributes/current_power_mode"))
    if val is None:
        return "FAIL", "Cannot read current_power_mode"
    tx_gear = (val >> 4) & 0xF
    rx_gear = val & 0xF
    result = "PASS" if val >= MIN_POWER_MODE else "FAIL"
    return result, f"power_mode=0x{val:02X} TX_gear={tx_gear} RX_gear={rx_gear}"


def test_lun_discovery(ctx):
    host = ctx.get("host")
    if not host:
        return "FAIL", "No host"
    devs = find_ufs_block_devices(host)
    if not devs:
        return "FAIL", f"No block devices found for {host}"
    ctx["block_devs"] = devs
    lines = []
    for d in devs:
        lines.append(f"  {d['path']} {d['vendor']} {d['model']} {human_bytes(d['size_bytes'])}")
    detail = f"{len(devs)} LUN(s) discovered:\n" + "\n".join(lines)
    return "PASS", detail


def test_seq_read_bw(ctx):
    devs = ctx.get("block_devs", [])
    if not devs:
        return "FAIL", "No block devices"
    dev = devs[0]  # largest LUN
    result_tuple = run_fio_read(dev["path"], "read", "128k", FIO_SEQ_SIZE)
    if result_tuple is None:
        return "FAIL", f"fio sequential read failed on {dev['path']}"
    bw_bytes, iops, lat_ns = result_tuple
    bw_mbs = bw_bytes / (1024 * 1024)
    ctx["seq_read_mbs"] = bw_mbs
    result = "PASS" if bw_mbs >= MIN_SEQ_READ_MBS else "FAIL"
    return result, (
        f"{dev['path']} sequential 128K read: "
        f"{bw_mbs:.1f} MB/s  {iops:.0f} IOPS  "
        f"lat_avg={lat_ns / 1000:.0f}us  "
        f"(min {MIN_SEQ_READ_MBS} MB/s)"
    )


def test_rand_read_iops(ctx):
    devs = ctx.get("block_devs", [])
    if not devs:
        return "FAIL", "No block devices"
    dev = devs[0]
    result_tuple = run_fio_read(dev["path"], "randread", "4k", FIO_RAND_SIZE)
    if result_tuple is None:
        return "FAIL", f"fio random read failed on {dev['path']}"
    bw_bytes, iops, lat_ns = result_tuple
    bw_mbs = bw_bytes / (1024 * 1024)
    ctx["rand_read_iops"] = iops
    result = "PASS" if iops >= MIN_RAND_READ_IOPS else "FAIL"
    return result, (
        f"{dev['path']} random 4K read: "
        f"{bw_mbs:.1f} MB/s  {iops:.0f} IOPS  "
        f"lat_avg={lat_ns / 1000:.0f}us  "
        f"(min {MIN_RAND_READ_IOPS} IOPS)"
    )


# ---------------------------------------------------------------------------
#  Main — TAP output + log file
# ---------------------------------------------------------------------------

TESTS = [
    ("UFS controller present", test_controller_present),
    ("Health: end-of-life status", test_health_eol),
    ("Health: lifetime estimation A", test_health_lifetime_a),
    ("Health: lifetime estimation B", test_health_lifetime_b),
    ("Link: HS power mode", test_power_mode),
    ("LUN discovery", test_lun_discovery),
    ("SCSI inquiry", test_scsi_inquiry),
    ("Sequential read bandwidth", test_seq_read_bw),
    ("Random read IOPS", test_rand_read_iops),
]


def main():
    if os.geteuid() != 0:
        print("SKIP (must run as root)")
        return 77

    os.makedirs("/tmp/output", exist_ok=True)
    timestamp = int(time.time())
    logfile = f"/tmp/output/ufs_test_{timestamp}.txt"
    log = open(logfile, "w")

    log.write("UFS Storage Test\n")
    log.write(f"Start time: {time.ctime()}\n\n")

    total_tests = len(TESTS)
    pass_count = 0
    fail_count = 0

    print("")
    print("UFS Storage Test")
    print("-" * 60)

    # TAP header
    print(f"1..{total_tests}")

    ctx = {}
    testnum = 1

    for label, fn in TESTS:
        result, detail = fn(ctx)

        # Console + log output
        if result == "PASS":
            pass_count += 1
            print(f"ok {testnum} {label}")
        else:
            fail_count += 1
            print(f"not ok {testnum} {label}")

        # Detail to both console and log
        for line in detail.split("\n"):
            print(f"  # {line}")
            log.write(f"{line}\n")

        log.write(f"  result: {result}\n\n")
        testnum += 1

    log.write("\nSummary\n")
    log.write(f"Pass={pass_count}\n")
    log.write(f"Fail={fail_count}\n")
    log.close()

    print("")
    print(f"Pass : {pass_count}")
    print(f"Fail : {fail_count}")
    print(f"Log  : {logfile}")
    print("")

    outcome = "PASS" if fail_count == 0 else "FAIL"
    print(f"=================== test: UFS Storage Test  outcome: {outcome} ==================")

    return fail_count


if __name__ == "__main__":
    sys.exit(main())
