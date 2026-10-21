#!/usr/bin/env python3
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

"""
AMD Versal Gen2 OSPI Flash & SDA Programming Script
Receives OSPI payload and/or WIC image chunks via PCIe BAR and programs
to flash device and /dev/sda respectively.
"""

import sys
import os
import time
import mmap
import fcntl
import struct
import hashlib
import subprocess
import io
import traceback
from datetime import datetime
from pathlib import Path

# Memory mapping
BAR_BASE = 0x20000000
BAR_SIZE = 256 * 1024 * 1024  # 256MB

# Offsets
STATUS_OFFSET = 0x0
SIZE_OFFSET = 0x4
CHUNK_NUMBER_OFFSET = 0x08
TOTAL_CHUNKS_OFFSET = 0x0C
MD5_OFFSET_0 = 0x10
MD5_OFFSET_1 = 0x14
MD5_OFFSET_2 = 0x18
MD5_OFFSET_3 = 0x1C
TOTAL_SIZE_LO_OFFSET = 0x20
TOTAL_SIZE_HI_OFFSET = 0x24
TRANSFER_OFFSET_LO = 0x28
TRANSFER_OFFSET_HI = 0x2C
TRANSFER_FLAGS_OFFSET = 0x30
FAIL_REASON_OFFSET = 0x34
FAIL_DETAIL_OFFSET = 0x38
LOG_MAGIC_OFFSET = 0x3C
EXTENT_DIGEST_OFFSET = 0x40
EXTENT_DIGEST_SIZE = 32
PAYLOAD_OFFSET = 0x1000
LOG_OFFSET = 0x1000

TRANSFER_FLAG_FINAL = 0x1
# Set when EXTENT_DIGEST holds a SHA-256 of this extent, so the target can
# reject a bad transfer before it reaches the device. Old hosts leave it clear.
# SHA-256 rather than MD5 because both ends accelerate it in hardware.
TRANSFER_FLAG_DIGEST = 0x2

UFS_EXTENT_RETRIES = 3
# Unmapped blocks are never written, so sample a few to tell the operator
# whether the destination was actually clean.
ZERO_SAMPLE_BLOCKS = 64

# Kept so a failed cycle can still be read after the BAR log is overwritten.
TARGET_LOG_PATH = "/tmp/programmer-last-cycle.log"

# Written alongside the log so the host can tell a real log from stale payload.
LOG_MAGIC = 0x106DA7A

# Maximum chunk size for SDA transfers
SDA_CHUNK_MAX = 128 * 1024 * 1024  # 128MB

# Magic values - OSPI
FLASH_READY_SIGNAL = 0xF1A54000      # "Flash" - initial ready state
FLASH_GO_SIGNAL = 0x60F1A540         # "goflash" - start programming
FLASH_STATUS_001 = 0xF1A5A001        # "flash 001" - validation passed
FLASH_STATUS_002 = 0xF1A5A002        # "flash 002" - erasing
FLASH_STATUS_003 = 0xF1A5A003        # Erase complete
FLASH_BAD = 0xF1A54BAD               # "flash bad" - validation failed
FLASH_DEAD = 0xF1A54DED              # "flash dead" - device not found
FLASH_SUCCESS = 0xF1A5A999           # "flash 999" - programming complete

# Magic values - SDA
SDA_GO_SIGNAL = 0x605DA000           # "goSDA" - signal from host to program rootfs
SDA_CHUNK_ACK = 0x005DA001           # "SDA 001" - chunk written OK
SDA_BAD = 0x005DABAD                 # "SDA bad" - capacity/validation/write failure
SDA_DEAD = 0x05DADEAD                # "SDA dead" - /dev/sda not found
SDA_SUCCESS = 0x005DA999             # "SDA 999" - all chunks written successfully

# Magic values - variable-offset UFS transfer
UFS_WRITE_GO = 0x605DA100
UFS_WRITE_ACK = 0x005DA101
UFS_WRITE_DATA_DONE = 0x005DA102
UFS_VERIFY_GO = 0x605DA102
UFS_WRITE_RETRY = 0x005DA103
UFS_WRITE_SUCCESS = 0x005DA199
UFS_READ_GO = 0x605DA200
UFS_READ_READY = 0x005DA201
UFS_READ_ACK = 0x605DA201
UFS_READ_DONE = 0x605DA2FF
UFS_READ_SUCCESS = 0x005DA299

# Magic values - zero a UFS span before a sparse write
UFS_ZERO_GO = 0x605DA300
UFS_ZERO_SUCCESS = 0x005DA399

# Reason codes published at FAIL_REASON_OFFSET alongside SDA_BAD / SDA_DEAD, so
# a failure is identifiable without the operator keeping a console open.
FAIL_NONE = 0
FAIL_DEVICE_MISSING = 1
FAIL_CAPACITY_READ = 2
FAIL_IMAGE_TOO_LARGE = 3
FAIL_DEVICE_OPEN = 4
FAIL_CHUNK_OUT_OF_RANGE = 5
FAIL_CHUNK_OUT_OF_ORDER = 6
FAIL_CHUNK_CHECKSUM = 7
FAIL_CHUNK_WRITE = 8
FAIL_EXTENT_OVERLAP = 9
FAIL_EXTENT_INVALID = 10
FAIL_EXTENT_WRITE = 11
FAIL_MANIFEST_TOO_LARGE = 12
FAIL_MANIFEST_PARSE = 13
FAIL_MANIFEST_COVERAGE = 14
FAIL_VERIFY_MISMATCH = 15
FAIL_VERIFY_READ = 16
FAIL_READ_INVALID = 17
FAIL_READ_IO = 18
FAIL_ZERO_INVALID = 19
FAIL_ZERO_IO = 20
FAIL_EXCEPTION = 21
FAIL_EXTENT_CHECKSUM = 22

# BLKZEROOUT / BLKDISCARD, both _IO(0x12, n) taking __u64 range[2].
BLKZEROOUT = 0x127F
BLKDISCARD = 0x1277

UFS_MANIFEST_MAGIC = b"UFSC"
UFS_MANIFEST_VERSION = 1
UFS_MANIFEST_SHA256 = 1
UFS_MANIFEST_HEADER = struct.Struct("<4sHHI")
UFS_MANIFEST_RECORD = struct.Struct("<QQ32s")

# Flash device path
# Default/fallback path uses /dev/mtd/by-name (created by udev). On the
# rave2-recovery image udev is not running, so resolve_flash_device() below
# falls back to /sys/class/mtd/mtd*/name lookup at runtime.
FLASH_DEVICE_DEFAULT = "/dev/mtd/by-name/f1010000.spi.0"
FLASH_PARTITION_NAME = "f1010000.spi.0"
FLASH_DEVICE = FLASH_DEVICE_DEFAULT

# SDA device path
SDA_DEVICE = "/dev/sda"

class LogCapture:
    """Capture all logs with timestamps."""
    def __init__(self):
        self.buffer = io.StringIO()
        self.start_time = datetime.now()

    def log(self, message, timestamp=True):
        """Log a message with optional timestamp."""
        if timestamp:
            elapsed = (datetime.now() - self.start_time).total_seconds()
            timestamp_str = f"[{elapsed:8.3f}s] "
        else:
            timestamp_str = ""

        line = f"{timestamp_str}{message}\n"
        self.buffer.write(line)
        print(line, end='')
        sys.stdout.flush()

    def get_logs(self):
        """Get all captured logs as bytes."""
        return self.buffer.getvalue().encode('utf-8')

def map_bar():
    """Memory map the BAR region. Retries forever instead of exiting; this
    process may be PID 1 on the recovery image and must never die."""
    while True:
        try:
            fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
            mem = mmap.mmap(fd, BAR_SIZE, mmap.MAP_SHARED,
                           mmap.PROT_READ | mmap.PROT_WRITE,
                           offset=BAR_BASE)
            os.close(fd)
            return mem
        except Exception as e:
            print(f"ERROR: Failed to map BAR at 0x{BAR_BASE:08x}: {e}; retrying")
            sys.stdout.flush()
            time.sleep(1)

def resolve_flash_device(logger=None):
    """Find the OSPI MTD partition without relying on udev.

    Scans /sys/class/mtd/mtd*/name for FLASH_PARTITION_NAME. Falls back to
    /dev/mtd/by-name/<name> if available, then /dev/mtd0 if it's the only
    MTD partition.
    """
    global FLASH_DEVICE

    try:
        import glob as _glob
        candidates = sorted(_glob.glob("/sys/class/mtd/mtd[0-9]*"))
        # Filter out mtdNro entries (read-only views)
        candidates = [c for c in candidates if not c.endswith("ro")]
        single = None
        for entry in candidates:
            name_path = os.path.join(entry, "name")
            try:
                with open(name_path) as f:
                    name = f.read().strip()
            except OSError:
                continue
            mtd_basename = os.path.basename(entry)  # e.g. mtd0
            dev_path = f"/dev/{mtd_basename}"
            if name == FLASH_PARTITION_NAME and os.path.exists(dev_path):
                FLASH_DEVICE = dev_path
                if logger:
                    logger.log(f"Resolved OSPI MTD via sysfs: {dev_path} (name='{name}')")
                return dev_path
            single = dev_path
        # Fallback: /dev/mtd/by-name path (works if udev is running)
        if os.path.exists(FLASH_DEVICE_DEFAULT):
            FLASH_DEVICE = FLASH_DEVICE_DEFAULT
            if logger:
                logger.log(f"Resolved OSPI MTD via by-name: {FLASH_DEVICE_DEFAULT}")
            return FLASH_DEVICE_DEFAULT
        # Last resort: single MTD partition
        if len(candidates) == 1 and single and os.path.exists(single):
            FLASH_DEVICE = single
            if logger:
                logger.log(f"Resolved OSPI MTD as only partition: {single}")
            return single
    except Exception as e:
        if logger:
            logger.log(f"WARNING: sysfs MTD lookup failed: {e}")

    FLASH_DEVICE = FLASH_DEVICE_DEFAULT
    if logger:
        logger.log(f"OSPI MTD lookup failed, using default: {FLASH_DEVICE_DEFAULT}")
    return FLASH_DEVICE_DEFAULT

def read_u32(mem, offset):
    """Read 32-bit value from BAR offset."""
    return struct.unpack('<I', mem[offset:offset+4])[0]

def write_u32(mem, offset, value):
    """Write 32-bit value to BAR offset."""
    mem[offset:offset+4] = struct.pack('<I', value)

def read_u64(mem, low_offset, high_offset):
    """Read a little-endian 64-bit value from two control words."""
    return read_u32(mem, low_offset) | (read_u32(mem, high_offset) << 32)

def fail(mem, logger, status, reason, message, detail=0):
    """Record why a transfer failed and return the status to publish.

    The reason and detail land in the control region, so the host can report
    the cause even when nobody was watching the console.
    """
    logger.log(f"ERROR: {message}")
    logger.log(f"Failure reason code {reason}, detail {detail}")
    write_u32(mem, FAIL_REASON_OFFSET, reason)
    write_u32(mem, FAIL_DETAIL_OFFSET, detail & 0xFFFFFFFF)
    return status

def calculate_md5(data):
    """Calculate MD5 hash of data and return as 4 x 32-bit words."""
    md5_hash = hashlib.md5(data).digest()
    words = struct.unpack('<4I', md5_hash)
    return words

def wait_for_signal(mem, logger, signals):
    """Wait indefinitely for one of the given signal values.
    Returns the signal value that was matched."""
    signal_strs = ", ".join(f"0x{s:08x}" for s in signals)
    logger.log("=" * 70)
    logger.log(f"Waiting for signal(s): {signal_strs}")
    logger.log("=" * 70)

    poll_count = 0
    last_log_time = time.time()

    while True:
        value = read_u32(mem, STATUS_OFFSET)
        poll_count += 1

        current_time = time.time()
        if current_time - last_log_time >= 10.0:
            logger.log(f"Still waiting... (polls: {poll_count}, last value: 0x{value:08x})")
            last_log_time = current_time

        if value in signals:
            logger.log(f"Signal 0x{value:08x} received after {poll_count} polls!")
            return value

        time.sleep(0.1)

def validate_payload(mem, logger):
    """Validate payload size and MD5 checksum."""
    logger.log("")
    logger.log("=" * 70)
    logger.log("Validating payload...")
    logger.log("=" * 70)

    payload_size = read_u32(mem, SIZE_OFFSET)
    logger.log(f"Payload size: {payload_size} bytes ({payload_size / (1024*1024):.2f} MB)")

    if payload_size == 0:
        logger.log("ERROR: Payload size is zero")
        return False, None, None

    if payload_size > (BAR_SIZE - PAYLOAD_OFFSET):
        logger.log(f"ERROR: Payload too large for BAR")
        return False, None, None

    expected_md5 = []
    for offset in [MD5_OFFSET_0, MD5_OFFSET_1, MD5_OFFSET_2, MD5_OFFSET_3]:
        expected_md5.append(read_u32(mem, offset))

    expected_md5_bytes = struct.pack('<4I', *expected_md5)
    logger.log(f"Expected MD5: {expected_md5_bytes.hex()}")

    logger.log(f"Reading {payload_size} bytes from offset 0x{PAYLOAD_OFFSET:x}...")
    payload_data = bytes(mem[PAYLOAD_OFFSET:PAYLOAD_OFFSET + payload_size])

    actual_md5 = calculate_md5(payload_data)
    actual_md5_bytes = struct.pack('<4I', *actual_md5)
    logger.log(f"Calculated MD5: {actual_md5_bytes.hex()}")

    if actual_md5 == tuple(expected_md5):
        logger.log("MD5 validation: PASSED")
        return True, payload_size, payload_data
    else:
        logger.log("MD5 validation: FAILED")
        return False, None, None

def check_flash_device(logger):
    """Check if flash device exists and is accessible."""
    logger.log("")
    logger.log("=" * 70)
    logger.log("Checking flash device...")
    logger.log("=" * 70)
    resolve_flash_device(logger)
    logger.log(f"Device path: {FLASH_DEVICE}")

    if not os.path.exists(FLASH_DEVICE):
        logger.log(f"ERROR: Flash device not found at {FLASH_DEVICE}")
        return False, None

    logger.log("Flash device found")

    try:
        mtd_name = os.path.basename(os.path.realpath(FLASH_DEVICE))
        logger.log(f"MTD device: {mtd_name}")

        with open("/proc/mtd", "r") as f:
            for line in f:
                if mtd_name in line:
                    parts = line.split()
                    if len(parts) >= 2:
                        flash_size = int(parts[1], 16)
                        logger.log(f"Flash capacity: {flash_size} bytes ({flash_size / (1024*1024):.2f} MB)")
                        return True, flash_size

        logger.log("WARNING: Could not determine flash size from /proc/mtd")
        return True, None
    except Exception as e:
        logger.log(f"WARNING: Error reading flash size: {e}")
        return True, None

def erase_flash(logger):
    """Erase entire flash device using flash_eraseall."""
    logger.log("")
    logger.log("=" * 70)
    logger.log("Erasing flash device...")
    logger.log("=" * 70)

    try:
        cmd = ["flash_eraseall", FLASH_DEVICE]
        logger.log(f"Command: {' '.join(cmd)}")

        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True
        )

        for line in process.stdout:
            logger.log(line.rstrip(), timestamp=False)

        process.wait()

        if process.returncode == 0:
            logger.log("Flash erase: SUCCESS")
            return True
        else:
            logger.log(f"Flash erase: FAILED (exit code {process.returncode})")
            return False
    except Exception as e:
        logger.log(f"Flash erase: EXCEPTION - {e}")
        return False

def program_flash(payload_data, payload_size, logger):
    """Program flash device using flashcp."""
    logger.log("")
    logger.log("=" * 70)
    logger.log("Programming flash device...")
    logger.log("=" * 70)
    logger.log(f"Writing {payload_size} bytes to {FLASH_DEVICE}")

    temp_file = "/tmp/ospi_payload.bin"
    try:
        with open(temp_file, "wb") as f:
            f.write(payload_data)
        logger.log(f"Temporary file created: {temp_file}")
    except Exception as e:
        logger.log(f"ERROR: Failed to create temporary file: {e}")
        return False

    try:
        cmd = ["flashcp", temp_file, FLASH_DEVICE]
        logger.log(f"Command: {' '.join(cmd)}")

        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True
        )

        for line in process.stdout:
            logger.log(line.rstrip(), timestamp=False)

        process.wait()

        try:
            os.remove(temp_file)
            logger.log(f"Temporary file removed: {temp_file}")
        except:
            pass

        if process.returncode == 0:
            logger.log("Flash programming: SUCCESS")
            return True
        else:
            logger.log(f"Flash programming: FAILED (exit code {process.returncode})")
            return False
    except Exception as e:
        logger.log(f"Flash programming: EXCEPTION - {e}")
        try:
            os.remove(temp_file)
        except:
            pass
        return False

def erase_first_sector(logger):
    """Attempt to erase first sector of flash to prevent bad boots."""
    logger.log("")
    logger.log("=" * 70)
    logger.log("Attempting to erase first flash sector...")
    logger.log("=" * 70)

    try:
        cmd = ["flash_erase", FLASH_DEVICE, "0", "1"]
        logger.log(f"Command: {' '.join(cmd)}")

        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True
        )

        for line in process.stdout:
            logger.log(line.rstrip(), timestamp=False)

        process.wait()

        if process.returncode == 0:
            logger.log("First sector erase: SUCCESS")
        else:
            logger.log(f"First sector erase: FAILED (exit code {process.returncode})")
    except Exception as e:
        logger.log(f"First sector erase: EXCEPTION - {e}")

def get_sda_capacity(logger):
    """Get /dev/sda capacity in bytes from sysfs."""
    logger.log("")
    logger.log("=" * 70)
    logger.log("Checking /dev/sda...")
    logger.log("=" * 70)

    if not os.path.exists(SDA_DEVICE):
        logger.log(f"ERROR: {SDA_DEVICE} not found")
        return None

    logger.log(f"{SDA_DEVICE} exists")
    log_sda_identity(logger)

    try:
        with open("/sys/block/sda/size", "r") as f:
            sectors = int(f.read().strip())
        capacity = sectors * 512
        logger.log(f"SDA capacity: {capacity} bytes ({capacity / (1024*1024*1024):.2f} GB)")
        return capacity
    except Exception as e:
        logger.log(f"ERROR: Could not read SDA capacity: {e}")
        return None

def log_sda_identity(logger):
    """Record which UFS part and firmware this was, for after-the-fact triage."""
    fields = {
        "model": "/sys/block/sda/device/model",
        "vendor": "/sys/block/sda/device/vendor",
        "revision": "/sys/block/sda/device/rev",
        "logical block": "/sys/block/sda/queue/logical_block_size",
        "physical block": "/sys/block/sda/queue/physical_block_size",
        "write cache": "/sys/block/sda/queue/write_cache",
        "discard max": "/sys/block/sda/queue/discard_max_bytes",
    }
    for label, path in fields.items():
        try:
            with open(path) as handle:
                logger.log(f"  {label}: {handle.read().strip()}")
        except OSError:
            pass

def sample_unmapped_zero(fd, written_extents, logger):
    """Warn when gaps between written extents still hold data.

    A sparse write never touches these blocks. If they are not zero then the
    destination was not clean, and the new filesystem has inherited metadata
    from whatever was here before. Only a sample is read, so this costs
    kilobytes regardless of image size.
    """
    spans = merge_extents(written_extents)
    gaps = []
    for index in range(1, len(spans)):
        start, end = spans[index - 1][1], spans[index][0]
        if end > start:
            gaps.append((start, end))
    if not gaps:
        logger.log("Unmapped-gap check: the written extents are contiguous")
        return

    checked = dirty = 0
    for index in range(ZERO_SAMPLE_BLOCKS):
        start, end = gaps[index % len(gaps)]
        offset = start + ((index // len(gaps)) * 4096) % max(end - start, 1)
        offset = min(offset, end - 1) & ~0xFFF
        try:
            os.lseek(fd, offset, os.SEEK_SET)
            block = os.read(fd, 4096)
        except OSError:
            continue
        checked += 1
        if block.strip(b"\0"):
            dirty += 1
    if not checked:
        return
    total_gap = sum(end - start for start, end in gaps)
    logger.log(f"Unmapped-gap check: {len(gaps)} gaps totalling "
               f"{total_gap / (1024*1024):.1f} MiB are never written by a sparse flash")
    if dirty:
        logger.log(f"WARNING: {dirty}/{checked} sampled unmapped blocks are not zero.")
        logger.log("WARNING: the destination was not clean, so this image has "
                   "inherited data from whatever was on the device before.")
        logger.log("WARNING: reprogram with zeroing enabled to avoid this.")
    else:
        logger.log(f"Unmapped-gap check: all {checked} sampled blocks are zero")

def write_all(fd, data):
    """Write all bytes, raising if the device stops making progress."""
    view = memoryview(data)
    while view:
        written = os.write(fd, view)
        if written == 0:
            raise OSError("short UFS write")
        view = view[written:]

def read_exact_fd(fd, size):
    """Read exactly size bytes, raising on premature EOF."""
    data = bytearray()
    while len(data) < size:
        block = os.read(fd, size - len(data))
        if not block:
            raise OSError(f"short UFS read: expected {size}, got {len(data)}")
        data.extend(block)
    return bytes(data)

def merge_extents(extents):
    """Normalize sorted or unsorted byte extents into non-overlapping ranges."""
    merged = []
    for offset, size in sorted(extents):
        end = offset + size
        if merged and offset <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((offset, end))
    return merged

def parse_ufs_manifest(data, capacity):
    """Parse a compact SHA-256 range manifest from the BAR payload."""
    if len(data) < UFS_MANIFEST_HEADER.size:
        raise ValueError("UFS checksum manifest is truncated")
    magic, version, algorithm, count = UFS_MANIFEST_HEADER.unpack_from(data)
    if magic != UFS_MANIFEST_MAGIC or version != UFS_MANIFEST_VERSION:
        raise ValueError("unsupported UFS checksum manifest")
    if algorithm != UFS_MANIFEST_SHA256:
        raise ValueError("unsupported UFS checksum algorithm")
    expected_size = UFS_MANIFEST_HEADER.size + count * UFS_MANIFEST_RECORD.size
    if len(data) != expected_size:
        raise ValueError(f"UFS checksum manifest size {len(data)} != {expected_size}")

    records = []
    previous_end = 0
    for index in range(count):
        offset, size, digest = UFS_MANIFEST_RECORD.unpack_from(
            data, UFS_MANIFEST_HEADER.size + index * UFS_MANIFEST_RECORD.size
        )
        if size == 0 or offset < previous_end or offset + size > capacity:
            raise ValueError(f"invalid UFS checksum range {index}: offset={offset} size={size}")
        records.append((offset, size, digest))
        previous_end = offset + size
    if not records:
        raise ValueError("UFS checksum manifest has no ranges")
    return records

def verify_ufs_manifest(fd, records, logger):
    """Hash UFS ranges and compare them with manifest SHA-256 digests.

    Returns None when every range matches, otherwise a
    (reason, range number, message) tuple naming the first failure.
    """
    for index, (offset, size, expected) in enumerate(records, 1):
        os.lseek(fd, offset, os.SEEK_SET)
        digest = hashlib.sha256()
        remaining = size
        while remaining:
            try:
                block = os.read(fd, min(4 * 1024 * 1024, remaining))
            except OSError as error:
                return (FAIL_VERIFY_READ, index,
                        f"read error verifying range {index} at offset {offset}: {error}")
            if not block:
                return (FAIL_VERIFY_READ, index,
                        f"short read verifying range {index} at offset {offset}")
            digest.update(block)
            remaining -= len(block)
        if digest.digest() != expected:
            return (FAIL_VERIFY_MISMATCH, index,
                    f"UFS checksum mismatch for range {index} at offset {offset}, "
                    f"length {size}: expected {expected.hex()}, read {digest.hexdigest()}")
        logger.log(f"Verified UFS checksum range {index}/{len(records)} at offset {offset}")
    return None

def handle_sda_programming(mem, logger):
    """Handle chunked WIC programming to /dev/sda.
    Returns the final status code for this SDA session."""
    logger.log("")
    logger.log("=" * 70)
    logger.log("SDA PROGRAMMING - Chunked WIC transfer")
    logger.log("=" * 70)

    # Read total file size (64-bit)
    total_size_lo = read_u32(mem, TOTAL_SIZE_LO_OFFSET)
    total_size_hi = read_u32(mem, TOTAL_SIZE_HI_OFFSET)
    total_size = total_size_lo | (total_size_hi << 32)
    total_chunks = read_u32(mem, TOTAL_CHUNKS_OFFSET)

    logger.log(f"Total WIC size: {total_size} bytes ({total_size / (1024*1024*1024):.2f} GB)")
    logger.log(f"Total chunks: {total_chunks}")

    # Check /dev/sda exists
    sda_capacity = get_sda_capacity(logger)
    if sda_capacity is None:
        return fail(mem, logger, SDA_DEAD, FAIL_DEVICE_MISSING,
                    f"{SDA_DEVICE} not accessible")

    # Check capacity
    if total_size > sda_capacity:
        logger.log(f"  WIC size: {total_size / (1024*1024*1024):.2f} GB")
        logger.log(f"  SDA capacity: {sda_capacity / (1024*1024*1024):.2f} GB")
        return fail(mem, logger, SDA_BAD, FAIL_IMAGE_TOO_LARGE,
                    "WIC image too large for /dev/sda", total_size >> 20)

    logger.log("Capacity check: PASSED")

    # Open /dev/sda for direct writing
    try:
        sda_fd = os.open(SDA_DEVICE, os.O_RDWR | os.O_SYNC)
    except Exception as e:
        return fail(mem, logger, SDA_DEAD, FAIL_DEVICE_OPEN,
                    f"Failed to open {SDA_DEVICE} for writing: {e}")

    try:
        previous_chunk = -1
        while True:
            chunk_number = read_u32(mem, CHUNK_NUMBER_OFFSET)
            chunk_size = read_u32(mem, SIZE_OFFSET)

            logger.log(f"Processing chunk {chunk_number}/{total_chunks} ({chunk_size} bytes)")

            if chunk_number >= total_chunks:
                return fail(mem, logger, SDA_BAD, FAIL_CHUNK_OUT_OF_RANGE,
                            f"Chunk {chunk_number} is outside total chunk count "
                            f"{total_chunks}", chunk_number)

            if chunk_number <= previous_chunk:
                return fail(mem, logger, SDA_BAD, FAIL_CHUNK_OUT_OF_ORDER,
                            f"Chunk {chunk_number} is not after previous chunk "
                            f"{previous_chunk}", chunk_number)

            # Validate chunk
            valid, _, chunk_data = validate_payload(mem, logger)
            if not valid:
                return fail(mem, logger, SDA_BAD, FAIL_CHUNK_CHECKSUM,
                            f"Chunk {chunk_number} validation failed", chunk_number)

            # Write chunk directly to /dev/sda
            write_offset = chunk_number * SDA_CHUNK_MAX
            try:
                os.lseek(sda_fd, write_offset, os.SEEK_SET)
                os.write(sda_fd, chunk_data)
            except Exception as e:
                return fail(mem, logger, SDA_BAD, FAIL_CHUNK_WRITE,
                            f"Failed to write chunk {chunk_number} to "
                            f"{SDA_DEVICE}: {e}", chunk_number)

            logger.log(f"Chunk {chunk_number} written successfully at offset {write_offset}")

            if chunk_number == total_chunks - 1:
                logger.log("")
                logger.log("=" * 70)
                logger.log("SDA PROGRAMMING COMPLETED SUCCESSFULLY")
                logger.log("=" * 70)
                return SDA_SUCCESS

            previous_chunk = chunk_number
            write_u32(mem, STATUS_OFFSET, SDA_CHUNK_ACK)
            logger.log(f"Sent SDA_CHUNK_ACK for chunk {chunk_number}")
            wait_for_signal(mem, logger, [SDA_GO_SIGNAL])

    except Exception as e:
        logger.log(traceback.format_exc(), timestamp=False)
        return fail(mem, logger, SDA_BAD, FAIL_EXCEPTION,
                    f"SDA programming exception: {e}")
    finally:
        os.close(sda_fd)

    return fail(mem, logger, SDA_BAD, FAIL_EXCEPTION,
                "SDA programming loop exited unexpectedly")

def handle_ufs_write(mem, logger):
    """Write strictly increasing variable-offset extents to /dev/sda."""
    logger.log("")
    logger.log("=" * 70)
    logger.log("UFS PROGRAMMING - Variable-offset transfer")
    logger.log("=" * 70)

    capacity = get_sda_capacity(logger)
    if capacity is None:
        return fail(mem, logger, SDA_DEAD, FAIL_CAPACITY_READ,
                    f"Could not determine {SDA_DEVICE} capacity")

    try:
        sda_fd = os.open(SDA_DEVICE, os.O_RDWR | os.O_SYNC)
    except Exception as e:
        return fail(mem, logger, SDA_DEAD, FAIL_DEVICE_OPEN,
                    f"Failed to open {SDA_DEVICE} for writing: {e}")

    previous_end = 0
    written_extents = []
    index = 0
    retries = 0
    total_bytes = 0
    session_start = time.time()
    try:
        while True:
            offset = read_u64(mem, TRANSFER_OFFSET_LO, TRANSFER_OFFSET_HI)
            size = read_u32(mem, SIZE_OFFSET)
            flags = read_u32(mem, TRANSFER_FLAGS_OFFSET)
            index += 1
            extent_start = time.time()

            if offset < previous_end:
                return fail(mem, logger, SDA_BAD, FAIL_EXTENT_OVERLAP,
                            f"UFS extent {index} offset {offset} overlaps previous "
                            f"end {previous_end}", index)
            if size == 0 or size > SDA_CHUNK_MAX or offset + size > capacity:
                return fail(mem, logger, SDA_BAD, FAIL_EXTENT_INVALID,
                            f"Invalid UFS extent {index} offset={offset} size={size} "
                            f"capacity={capacity}", index)

            read_start = time.time()
            payload = bytes(mem[PAYLOAD_OFFSET:PAYLOAD_OFFSET + size])
            read_elapsed = time.time() - read_start

            # Checking here means a corrupted transfer is resent rather than
            # written, so the device is never left holding bad data.
            if flags & TRANSFER_FLAG_DIGEST:
                expected = bytes(
                    mem[EXTENT_DIGEST_OFFSET:EXTENT_DIGEST_OFFSET + EXTENT_DIGEST_SIZE]
                )
                if hashlib.sha256(payload).digest() != expected:
                    retries += 1
                    logger.log(f"WARNING: UFS extent {index} at offset {offset} failed "
                               f"its SHA-256 in the BAR; requesting resend "
                               f"({retries}/{UFS_EXTENT_RETRIES})")
                    if retries > UFS_EXTENT_RETRIES:
                        return fail(mem, logger, SDA_BAD, FAIL_EXTENT_CHECKSUM,
                                    f"UFS extent {index} at offset {offset} still "
                                    f"corrupt after {UFS_EXTENT_RETRIES} resends", index)
                    index -= 1
                    write_u32(mem, STATUS_OFFSET, UFS_WRITE_RETRY)
                    wait_for_signal(mem, logger, [UFS_WRITE_GO])
                    continue
            retries = 0

            write_start = time.time()
            try:
                os.lseek(sda_fd, offset, os.SEEK_SET)
                write_all(sda_fd, payload)
            except OSError as e:
                return fail(mem, logger, SDA_BAD, FAIL_EXTENT_WRITE,
                            f"Failed to write UFS extent {index} at offset "
                            f"{offset}: {e}", index)
            write_elapsed = time.time() - write_start
            previous_end = offset + size
            written_extents.append((offset, size))
            total_bytes += size
            logger.log(
                f"UFS extent {index} offset={offset} size={size} written "
                f"(BAR read {read_elapsed:.2f}s, device write {write_elapsed:.2f}s, "
                f"{size / (1024*1024) / max(write_elapsed, 1e-6):.0f} MB/s)"
            )

            if flags & TRANSFER_FLAG_FINAL:
                os.fsync(sda_fd)
                elapsed = time.time() - session_start
                logger.log(
                    f"UFS writes flushed: {total_bytes / (1024**3):.2f} GiB in "
                    f"{elapsed:.1f}s ({total_bytes / (1024*1024) / max(elapsed, 1e-6):.0f} MB/s)"
                )
                sample_unmapped_zero(sda_fd, written_extents, logger)
                write_u32(mem, STATUS_OFFSET, UFS_WRITE_DATA_DONE)
                wait_for_signal(mem, logger, [UFS_VERIFY_GO])

                manifest_size = read_u32(mem, SIZE_OFFSET)
                if manifest_size > BAR_SIZE - PAYLOAD_OFFSET:
                    return fail(mem, logger, SDA_BAD, FAIL_MANIFEST_TOO_LARGE,
                                f"UFS checksum manifest is too large: {manifest_size}",
                                manifest_size)
                manifest = bytes(mem[PAYLOAD_OFFSET:PAYLOAD_OFFSET + manifest_size])
                try:
                    records = parse_ufs_manifest(manifest, capacity)
                except ValueError as e:
                    return fail(mem, logger, SDA_BAD, FAIL_MANIFEST_PARSE,
                                f"UFS checksum manifest rejected: {e}")
                manifest_extents = [(offset, size) for offset, size, _digest in records]
                if merge_extents(manifest_extents) != merge_extents(written_extents):
                    return fail(mem, logger, SDA_BAD, FAIL_MANIFEST_COVERAGE,
                                "UFS checksum manifest does not cover exactly the "
                                f"written extents ({len(records)} manifest ranges vs "
                                f"{len(written_extents)} written extents)")
                bad_range = verify_ufs_manifest(sda_fd, records, logger)
                if bad_range is not None:
                    reason, number, detail = bad_range
                    return fail(mem, logger, SDA_BAD, reason, detail, number)
                return UFS_WRITE_SUCCESS

            write_u32(mem, STATUS_OFFSET, UFS_WRITE_ACK)
            wait_for_signal(mem, logger, [UFS_WRITE_GO])
    except Exception as e:
        logger.log(traceback.format_exc(), timestamp=False)
        return fail(mem, logger, SDA_BAD, FAIL_EXCEPTION,
                    f"UFS write exception: {e}", index)
    finally:
        os.close(sda_fd)

def handle_ufs_zero(mem, logger):
    """Zero a span of /dev/sda so a following sparse write starts from zeros.

    bmap-guided programming only writes mapped ranges, so any block the image
    leaves as a hole keeps whatever the previous image put there. Zeroing the
    image span first restores the empty-destination assumption bmap relies on.
    """
    logger.log("")
    logger.log("=" * 70)
    logger.log("UFS ZEROING - Preparing device for a sparse write")
    logger.log("=" * 70)

    capacity = get_sda_capacity(logger)
    if capacity is None:
        return fail(mem, logger, SDA_DEAD, FAIL_CAPACITY_READ,
                    f"Could not determine {SDA_DEVICE} capacity")

    offset = read_u64(mem, TRANSFER_OFFSET_LO, TRANSFER_OFFSET_HI)
    length = read_u64(mem, TOTAL_SIZE_LO_OFFSET, TOTAL_SIZE_HI_OFFSET)
    logger.log(f"Requested zero span offset={offset} length={length} "
               f"({length / (1024*1024*1024):.2f} GB)")

    if length == 0 or offset + length > capacity:
        return fail(mem, logger, SDA_BAD, FAIL_ZERO_INVALID,
                    f"Invalid zero span offset={offset} length={length} "
                    f"capacity={capacity}")

    try:
        sda_fd = os.open(SDA_DEVICE, os.O_RDWR | os.O_SYNC)
    except Exception as e:
        return fail(mem, logger, SDA_DEAD, FAIL_DEVICE_OPEN,
                    f"Failed to open {SDA_DEVICE} for zeroing: {e}")

    start = time.time()
    try:
        try:
            fcntl.ioctl(sda_fd, BLKZEROOUT, struct.pack("<QQ", offset, length))
            logger.log(f"BLKZEROOUT completed in {time.time() - start:.1f}s")
        except OSError as e:
            logger.log(f"BLKZEROOUT unavailable ({e}); writing zeros instead")
            block = bytes(4 * 1024 * 1024)
            os.lseek(sda_fd, offset, os.SEEK_SET)
            remaining = length
            while remaining:
                write_all(sda_fd, block[:min(len(block), remaining)])
                remaining -= min(len(block), remaining)
            logger.log(f"Zero fill completed in {time.time() - start:.1f}s")
        os.fsync(sda_fd)
    except OSError as e:
        return fail(mem, logger, SDA_BAD, FAIL_ZERO_IO,
                    f"Failed to zero {SDA_DEVICE}: {e}")
    except Exception as e:
        logger.log(traceback.format_exc(), timestamp=False)
        return fail(mem, logger, SDA_BAD, FAIL_EXCEPTION,
                    f"UFS zero exception: {e}")
    finally:
        os.close(sda_fd)

    logger.log("UFS zeroing complete")
    return UFS_ZERO_SUCCESS

def handle_ufs_read(mem, logger):
    """Serve variable-offset /dev/sda reads through the BAR payload window."""
    logger.log("")
    logger.log("=" * 70)
    logger.log("UFS READBACK - Variable-offset transfer")
    logger.log("=" * 70)

    capacity = get_sda_capacity(logger)
    if capacity is None:
        return fail(mem, logger, SDA_DEAD, FAIL_CAPACITY_READ,
                    f"Could not determine {SDA_DEVICE} capacity")

    try:
        sda_fd = os.open(SDA_DEVICE, os.O_RDONLY)
    except Exception as e:
        return fail(mem, logger, SDA_DEAD, FAIL_DEVICE_OPEN,
                    f"Failed to open {SDA_DEVICE} for reading: {e}")

    try:
        while True:
            offset = read_u64(mem, TRANSFER_OFFSET_LO, TRANSFER_OFFSET_HI)
            size = read_u32(mem, SIZE_OFFSET)
            logger.log(f"Reading UFS extent offset={offset} size={size}")

            if size == 0 or size > SDA_CHUNK_MAX or offset + size > capacity:
                return fail(mem, logger, SDA_BAD, FAIL_READ_INVALID,
                            f"Invalid UFS read offset={offset} size={size} "
                            f"capacity={capacity}")

            os.lseek(sda_fd, offset, os.SEEK_SET)
            data = read_exact_fd(sda_fd, size)

            mem[PAYLOAD_OFFSET:PAYLOAD_OFFSET + size] = data
            md5_words = calculate_md5(data)
            for control_offset, value in zip(
                [MD5_OFFSET_0, MD5_OFFSET_1, MD5_OFFSET_2, MD5_OFFSET_3], md5_words
            ):
                write_u32(mem, control_offset, value)
            write_u32(mem, STATUS_OFFSET, UFS_READ_READY)

            signal = wait_for_signal(mem, logger, [UFS_READ_ACK, UFS_READ_DONE])
            if signal == UFS_READ_DONE:
                return UFS_READ_SUCCESS
    except Exception as e:
        logger.log(traceback.format_exc(), timestamp=False)
        return fail(mem, logger, SDA_BAD, FAIL_READ_IO,
                    f"UFS read exception: {e}")
    finally:
        os.close(sda_fd)

def write_logs_to_bar(mem, logger):
    """Write logs to BAR at offset 0x1000."""
    log_data = logger.get_logs()
    log_size = len(log_data)

    print("")
    print("=" * 70)
    print(f"Writing {log_size} bytes of logs to BAR offset 0x{LOG_OFFSET:x}")
    print("=" * 70)

    available = BAR_SIZE - LOG_OFFSET
    if log_size > available:
        log_data = log_data[-available:]
        log_size = len(log_data)
        print(f"Log truncated to the last {log_size} bytes to fit the BAR")

    mem[LOG_OFFSET:LOG_OFFSET + log_size] = log_data
    write_u32(mem, SIZE_OFFSET, log_size)
    write_u32(mem, LOG_MAGIC_OFFSET, LOG_MAGIC)

    # The BAR holds one log, which the next cycle overwrites. Keep a copy so a
    # failure stays readable after the operator has closed the host console.
    try:
        with open(TARGET_LOG_PATH, "wb") as handle:
            handle.write(logger.get_logs())
    except OSError as e:
        print(f"Could not write {TARGET_LOG_PATH}: {e}")

    print(f"Log size written to offset 0x{SIZE_OFFSET:x}: {log_size} bytes")

def main():
    if os.geteuid() != 0:
        print("ERROR: This script must be run as root")
        print("Try: sudo {}".format(sys.argv[0]))
        sys.exit(1)

    print("=" * 70)
    print("AMD Versal Gen2 OSPI/SDA Programming Service")
    print("=" * 70)

    print("Mapping PCIe BAR...")
    mem = map_bar()
    print("BAR mapped successfully")

    cycle_count = 0
    first_cycle = True

    while True:
        cycle_count += 1
        logger = LogCapture()
        logger.log(f"Starting programming cycle #{cycle_count}")
        logger.log(f"BAR base address: 0x{BAR_BASE:08x}")
        logger.log(f"BAR size: {BAR_SIZE / (1024*1024):.0f} MB")

        final_status = FLASH_DEAD

        try:
            # Signal ready only once on first loop
            if first_cycle:
                logger.log("")
                logger.log("=" * 70)
                logger.log("Signaling ready to host...")
                logger.log("=" * 70)
                write_u32(mem, STATUS_OFFSET, FLASH_READY_SIGNAL)
                logger.log(f"Wrote 0x{FLASH_READY_SIGNAL:08x} to offset 0x{STATUS_OFFSET:x}")
                first_cycle = False
            else:
                inherited_status = read_u32(mem, STATUS_OFFSET)
                logger.log(f"Preserving previous host status: 0x{inherited_status:08x}")

            signal = wait_for_signal(
                mem, logger,
                [FLASH_GO_SIGNAL, SDA_GO_SIGNAL, UFS_WRITE_GO, UFS_READ_GO, UFS_ZERO_GO]
            )

            # Only now is the previous cycle's result safe to discard: the host
            # has issued a new command, so it has finished reading those logs.
            write_u32(mem, FAIL_REASON_OFFSET, FAIL_NONE)
            write_u32(mem, FAIL_DETAIL_OFFSET, 0)
            write_u32(mem, LOG_MAGIC_OFFSET, 0)

            if signal == FLASH_GO_SIGNAL:
                # ===== OSPI PROGRAMMING =====
                logger.log("")
                logger.log("=" * 70)
                logger.log("OSPI FLASH PROGRAMMING")
                logger.log("=" * 70)

                valid, payload_size, payload_data = validate_payload(mem, logger)
                if not valid:
                    logger.log("Payload validation failed")
                    final_status = FLASH_BAD
                else:
                    write_u32(mem, STATUS_OFFSET, FLASH_STATUS_001)
                    logger.log(f"Set status to 0x{FLASH_STATUS_001:08x} (flash 001 - validation passed)")

                    flash_exists, flash_size = check_flash_device(logger)
                    if not flash_exists:
                        logger.log("Flash device not accessible")
                        final_status = FLASH_DEAD
                    elif flash_size is not None and payload_size > flash_size:
                        logger.log("ERROR: Payload too large for flash")
                        logger.log(f"  Payload size: {payload_size / (1024*1024):.2f} MB")
                        logger.log(f"  Flash size: {flash_size / (1024*1024):.2f} MB")
                        final_status = FLASH_BAD
                    else:
                        write_u32(mem, STATUS_OFFSET, FLASH_STATUS_002)
                        logger.log(f"Set status to 0x{FLASH_STATUS_002:08x} (flash 002 - erasing)")

                        if not erase_flash(logger):
                            logger.log("Flash erase failed")
                            final_status = FLASH_DEAD
                        else:
                            write_u32(mem, STATUS_OFFSET, FLASH_STATUS_003)
                            logger.log(f"Set status to 0x{FLASH_STATUS_003:08x} (flash 003 - erase complete)")

                            if not program_flash(payload_data, payload_size, logger):
                                logger.log("Flash programming failed")
                                erase_first_sector(logger)
                                final_status = FLASH_DEAD
                            else:
                                logger.log("")
                                logger.log("=" * 70)
                                logger.log("FLASH PROGRAMMING COMPLETED SUCCESSFULLY")
                                logger.log("=" * 70)
                                final_status = FLASH_SUCCESS

            elif signal == SDA_GO_SIGNAL:
                # ===== SDA PROGRAMMING =====
                final_status = handle_sda_programming(mem, logger)

            elif signal == UFS_WRITE_GO:
                final_status = handle_ufs_write(mem, logger)

            elif signal == UFS_READ_GO:
                final_status = handle_ufs_read(mem, logger)

            elif signal == UFS_ZERO_GO:
                final_status = handle_ufs_zero(mem, logger)

        except KeyboardInterrupt:
            logger.log("")
            logger.log("Keyboard interrupt received; keeping service alive")
            final_status = FLASH_DEAD
        except Exception as e:
            logger.log("")
            logger.log(f"Unexpected exception: {e}")
            logger.log(traceback.format_exc(), timestamp=False)
            write_u32(mem, FAIL_REASON_OFFSET, FAIL_EXCEPTION)
            final_status = FLASH_DEAD
        finally:
            write_logs_to_bar(mem, logger)
            write_u32(mem, STATUS_OFFSET, final_status)

            logger.log("")
            logger.log(f"Final status: 0x{final_status:08x}")

            status_messages = {
                FLASH_SUCCESS: "Result: OSPI SUCCESS",
                FLASH_BAD: "Result: OSPI VALIDATION FAILED",
                FLASH_DEAD: "Result: OSPI PROGRAMMING FAILED",
                SDA_SUCCESS: "Result: SDA SUCCESS",
                SDA_BAD: "Result: SDA FAILED",
                SDA_DEAD: "Result: SDA DEVICE NOT FOUND",
                UFS_WRITE_SUCCESS: "Result: UFS WRITE SUCCESS",
                UFS_READ_SUCCESS: "Result: UFS READ SUCCESS",
                UFS_ZERO_SUCCESS: "Result: UFS ZERO SUCCESS",
            }
            logger.log(status_messages.get(final_status, f"Result: UNKNOWN (0x{final_status:08x})"))

            print("")
            print("=" * 70)
            print("Logs written to BAR")
            print("=" * 70)

            logger.log("Cycle complete, returning to polling state")
            time.sleep(0.1)

if __name__ == "__main__":
    # main() already loops forever and traps all exceptions per cycle, but
    # guard the outer call so any unexpected exit (or failure inside the
    # one-shot setup before the main loop) does not kill PID 1 on the
    # recovery image.
    while True:
        try:
            main()
        except SystemExit:
            print("programmer: SystemExit caught; restarting in 5s")
            sys.stdout.flush()
            time.sleep(5)
        except KeyboardInterrupt:
            print("programmer: KeyboardInterrupt; restarting in 5s")
            sys.stdout.flush()
            time.sleep(5)
        except BaseException as _e:
            print(f"programmer: fatal exception {_e!r}; restarting in 5s")
            import traceback as _tb
            _tb.print_exc()
            sys.stdout.flush()
            time.sleep(5)
