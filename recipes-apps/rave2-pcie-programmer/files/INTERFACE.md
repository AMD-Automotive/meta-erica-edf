<!--
Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
SPDX-License-Identifier: MIT
Contributors: KC
Maintainer: KC
-->

# PCIe BAR Programming Interface Specification

## Overview
 
This document describes the host/target interface used to program OSPI flash and
block storage (`/dev/sda`) on an AMD Versal Gen2 target over a PCIe Base Address
Register (BAR).

Two Python scripts co-operate over a 256 MB shared-memory window exposed via
PCIe BAR2:

| Script | Role | Runs on |
|--------|------|---------|
| `program_versal_payload.py` | **Host** — sends images, monitors status | x86 host machine |
| `programmer.py` | **Target** — receives data, programs devices | Versal Gen2 target |

The target script runs as a persistent service and never exits. After each
programming cycle (success or failure) it writes logs and final status to the BAR
then returns to its polling state to await the next command.

---

## PCIe Device Identification

The host locates the target via sysfs using these identifiers:

| Field | Value |
|-------|-------|
| Vendor ID | `0x10EE` |
| Device ID | `0x10EE` |
| Revision | `0x42` |
| BAR Index | 2 |
| BAR Size | 256 MB |

---

## BAR Memory Layout

The 256 MB BAR window is divided into a metadata/control region and a payload
region. Offset `0x1000` and above is reused across phases for payload data and
log output.

### Control Region (`0x0000 – 0x0FFF`)

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| `0x00` | 4 B | `STATUS` | Handshake / state-machine signal |
| `0x04` | 4 B | `SIZE` | Payload or chunk size in bytes |
| `0x08` | 4 B | `CHUNK_NUMBER` | 0-indexed chunk index (SDA only) |
| `0x0C` | 4 B | `TOTAL_CHUNKS` | Logical image span in 128 MB chunks (SDA only) |
| `0x10` | 4 B | `MD5[0]` | MD5 checksum word 0 (little-endian) |
| `0x14` | 4 B | `MD5[1]` | MD5 checksum word 1 |
| `0x18` | 4 B | `MD5[2]` | MD5 checksum word 2 |
| `0x1C` | 4 B | `MD5[3]` | MD5 checksum word 3 |
| `0x20` | 4 B | `TOTAL_SIZE_LO` | Total WIC file size — low 32 bits |
| `0x24` | 4 B | `TOTAL_SIZE_HI` | Total WIC file size — high 32 bits |
| `0x28` | 4 B | `TRANSFER_OFFSET_LO` | Variable UFS byte offset — low 32 bits |
| `0x2C` | 4 B | `TRANSFER_OFFSET_HI` | Variable UFS byte offset — high 32 bits |
| `0x30` | 4 B | `TRANSFER_FLAGS` | Bit 0 `FINAL`, bit 1 `DIGEST` (extent SHA-256 present) |
| `0x34` | 4 B | `FAIL_REASON` | Why the last operation failed; 0 when none |
| `0x38` | 4 B | `FAIL_DETAIL` | Extent or range number for the failure, when known |
| `0x3C` | 4 B | `LOG_MAGIC` | `0x106DA7A` once the target has published a log |
| `0x40` | 32 B | `EXTENT_DIGEST` | SHA-256 of the current extent, when bit 1 of `TRANSFER_FLAGS` is set |
| `0x60–0xFFF` | | *(reserved)* | |

All multi-byte values are **little-endian 32-bit unsigned integers**.

The `TOTAL_SIZE_LO` / `TOTAL_SIZE_HI` pair forms a 64-bit value supporting WIC
images up to 2^64 bytes (effectively unlimited).

### Payload Region (`0x1000 – end of BAR`)

| Usage | Max Size | Notes |
|-------|----------|-------|
| pciImage data | ~256 MB − 0x1000 | Phase 1 only |
| OSPI image data | ~256 MB − 0x1000 | Phase 2 only |
| SDA chunk data | 128 MB | Phase 3, per chunk |
| Variable UFS extent | 128 MB | Phase 4 write or read, per request |
| Device log output | Variable | Written by target after each phase |

The payload region is reused sequentially — never concurrently across phases.

---

## Signal Values

All signals are written to `STATUS` at offset `0x00`. Every value is a 32-bit
magic constant chosen to be visually distinctive in hex dumps.

### Phase 1: pciImage Loading

| Signal | Value | Direction | Meaning |
|--------|-------|-----------|---------|
| `HOST_READY` | `0xFEEDB007` | Target → Host | Target is ready to receive pciImage |
| `PAYLOAD_READY` | `0xB007AB1E` | Host → Target | pciImage transfer complete |

### Phase 2: OSPI Programming

| Signal | Value | Direction | Meaning |
|--------|-------|-----------|---------|
| `FLASH_READY` | `0xF1A54000` | Target → Host | Target ready for OSPI payload |
| `FLASH_GO` | `0x60F1A540` | Host → Target | OSPI payload loaded, begin programming |
| `FLASH_STATUS_001` | `0xF1A5A001` | Target → Host | MD5 validation passed |
| `FLASH_STATUS_002` | `0xF1A5A002` | Target → Host | Erasing flash |
| `FLASH_STATUS_003` | `0xF1A5A003` | Target → Host | Erase complete, programming flash |
| `FLASH_BAD` | `0xF1A54BAD` | Target → Host | Validation failed |
| `FLASH_DEAD` | `0xF1A54DED` | Target → Host | Flash device not found / programming failed |
| `FLASH_SUCCESS` | `0xF1A5A999` | Target → Host | OSPI programming complete |

### Phase 3: SDA Programming

| Signal | Value | Direction | Meaning |
|--------|-------|-----------|---------|
| `SDA_GO` | `0x605DA000` | Host → Target | Chunk loaded, begin writing to /dev/sda |
| `SDA_CHUNK_ACK` | `0x005DA001` | Target → Host | Chunk written successfully, send next |
| `SDA_BAD` | `0x005DABAD` | Target → Host | Capacity / validation / write failure |
| `SDA_DEAD` | `0x05DADEAD` | Target → Host | `/dev/sda` not found |
| `SDA_SUCCESS` | `0x005DA999` | Target → Host | All chunks written successfully |

### Phase 4: Variable-offset UFS Transfer

| Signal | Value | Direction | Meaning |
|--------|-------|-----------|---------|
| `UFS_WRITE_GO` | `0x605DA100` | Host → Target | Write extent described by offset, size and flags |
| `UFS_WRITE_ACK` | `0x005DA101` | Target → Host | Extent written; send next extent |
| `UFS_WRITE_RETRY` | `0x005DA103` | Target → Host | Extent failed its SHA-256 in the BAR; resend the same extent |
| `UFS_WRITE_DATA_DONE` | `0x005DA102` | Target → Host | Final data extent written and flushed; send checksum manifest |
| `UFS_VERIFY_GO` | `0x605DA102` | Host → Target | Verify written UFS ranges using manifest in payload region |
| `UFS_WRITE_SUCCESS` | `0x005DA199` | Target → Host | Every manifest range passed SHA-256 verification |
| `UFS_READ_GO` | `0x605DA200` | Host → Target | Start readback at requested offset and size |
| `UFS_READ_READY` | `0x005DA201` | Target → Host | Requested data and MD5 are available in the BAR |
| `UFS_READ_ACK` | `0x605DA201` | Host → Target | Data consumed; process next offset and size |
| `UFS_READ_DONE` | `0x605DA2FF` | Host → Target | End the current readback session |
| `UFS_READ_SUCCESS` | `0x005DA299` | Target → Host | Readback session ended successfully |

`SDA_BAD` and `SDA_DEAD` also report validation/device failures during either
variable UFS operation. Whenever either is published, `FAIL_REASON` names the
cause and `FAIL_DETAIL` carries the extent or range number where that is
meaningful.

### Phase 4C: UFS Zeroing

| Signal | Value | Direction | Meaning |
|--------|-------|-----------|---------|
| `UFS_ZERO_GO` | `0x605DA300` | Host → Target | Zero the span at `TRANSFER_OFFSET` for `TOTAL_SIZE` bytes |
| `UFS_ZERO_SUCCESS` | `0x005DA399` | Target → Host | Span zeroed and flushed |

### Failure reason codes

Published at `FAIL_REASON` alongside `SDA_BAD` or `SDA_DEAD`.

| Code | Meaning | Code | Meaning |
|------|---------|------|---------|
| 1 | `/dev/sda` not present | 12 | manifest exceeded the BAR payload |
| 2 | could not read capacity | 13 | manifest malformed |
| 3 | image larger than `/dev/sda` | 14 | manifest did not match written extents |
| 4 | could not open `/dev/sda` | 15 | SHA-256 mismatch on read-back |
| 5 | chunk number out of range | 16 | read error while verifying |
| 6 | chunk number did not advance | 17 | readback range outside device |
| 7 | chunk MD5 mismatch | 18 | read error serving a readback |
| 8 | chunk write failed | 19 | zero span outside device |
| 9 | extent overlapped the previous | 20 | error zeroing `/dev/sda` |
| 10 | extent outside the device | 21 | unhandled exception |
| 11 | extent write failed | 22 | extent corrupt in BAR after retries |

---

## Protocol Sequence

### Phase 1: pciImage Loading

This phase is always executed. It loads the initial PCIe boot image to the
target.

```
Host                              Target
 │                                  │
 │    Poll STATUS for HOST_READY    │
 │◄─────────── 0xFEEDB007 ─────────│ Target writes HOST_READY
 │                                  │
 │  Write pciImage to 0x1000        │
 │  Write PAYLOAD_READY to STATUS   │
 │──────────── 0xB007AB1E ─────────►│
 │                                  │
```

### Phase 2: OSPI Programming (Optional)

If an OSPI file is provided, this phase programs the OSPI flash on the target.

```
Host                              Target
 │                                  │
 │    Poll STATUS for FLASH_READY   │
 │◄─────────── 0xF1A54000 ─────────│ Target writes FLASH_READY
 │                                  │
 │  Write OSPI image to 0x1000      │
 │  Write SIZE, MD5[0..3]           │
 │  Write FLASH_GO to STATUS        │
 │──────────── 0x60F1A540 ─────────►│
 │                                  │
 │    Poll STATUS for progress      │
 │◄─────────── 0xF1A5A001 ─────────│ Validation passed
 │◄─────────── 0xF1A5A002 ─────────│ Erasing flash
 │◄─────────── 0xF1A5A003 ─────────│ Erase complete, programming
 │◄─────────── 0xF1A5A999 ─────────│ SUCCESS
 │        or   0xF1A54BAD           │ Validation failed
 │        or   0xF1A54DED           │ Device error
 │                                  │
 │  Read logs from 0x1000           │
 │  Read log size from SIZE (0x04)  │
 │                                  │
```

**Target OSPI logic:**

1. Read `SIZE` and `MD5[0..3]` from the control region
2. Read payload from `0x1000`, compute MD5, compare
3. If mismatch → write `FLASH_BAD`, write logs, return to poll
4. Write `FLASH_STATUS_001` (validation ok)
5. Check flash device exists, check capacity
6. Write `FLASH_STATUS_002` (erasing), run `flash_eraseall`
7. Write `FLASH_STATUS_003` (erase complete), run `flashcp`
8. On success → `FLASH_SUCCESS`; on failure → erase first sector, `FLASH_DEAD`
9. Write logs to `0x1000`, write log size to `SIZE`, write final status

### Phase 3: SDA Programming (Optional)

If a WIC file is provided, this phase writes it to `/dev/sda` on the target in
128 MB chunks. The host may omit unmapped chunks from a sparse image. Sent
`CHUNK_NUMBER` values must be strictly increasing but do not need to be
contiguous. `TOTAL_CHUNKS` remains the logical number of chunks in the complete
image, and the transfer completes when chunk `TOTAL_CHUNKS - 1` is received.

```
Host                              Target
 │                                  │
 │  ┌─── For each chunk ──────┐    │
 │  │                          │    │
 │  │ Write chunk to 0x1000    │    │
 │  │ Write SIZE, CHUNK_NUMBER │    │
 │  │ Write TOTAL_CHUNKS       │    │
 │  │ Write MD5[0..3]          │    │
 │  │ (Chunk 0 only:           │    │
 │  │  TOTAL_SIZE_LO/HI)       │    │
 │  │                          │    │
 │  │ Write SDA_GO to STATUS   │    │
 │  │──── 0x605DA000 ─────────►│    │
 │  │                          │    │
 │  │                          │    │ Validate MD5
 │  │                          │    │ Write to /dev/sda at
 │  │                          │    │   CHUNK_NUMBER × 128MB
 │  │                          │    │
 │  │ [intermediate chunk]     │    │
 │  │◄─── 0x005DA001 ─────────│    │ SDA_CHUNK_ACK
 │  │                          │    │
 │  │ [last chunk]             │    │
 │  │◄─── 0x005DA999 ─────────│    │ SDA_SUCCESS
 │  │  or  0x005DABAD          │    │ SDA_BAD (write/validation fail)
 │  │  or  0x05DADEAD          │    │ SDA_DEAD (device missing)
 │  │                          │    │
 │  └──────────────────────────┘    │
 │                                  │
 │  Read logs from 0x1000           │
 │                                  │
```

**Target SDA logic (first transmitted chunk):**

1. Read `TOTAL_SIZE_LO/HI` → compute 64-bit total file size
2. Read `TOTAL_CHUNKS`
3. Check `/dev/sda` exists → if not, write `SDA_DEAD`, return to poll
4. Read capacity from `/sys/block/sda/size` (sectors × 512)
5. If `total_size > capacity` → write `SDA_BAD`, return to poll
6. Open `/dev/sda` with `O_WRONLY | O_SYNC`
7. Validate the chunk number and MD5, then write to `/dev/sda` at
  `CHUNK_NUMBER × 128 MB`
8. If more chunks → write `SDA_CHUNK_ACK`, wait for next `SDA_GO`

**Target SDA logic (subsequent chunks):**

1. Wait for `SDA_GO`
2. Read `CHUNK_NUMBER`, `SIZE`, `MD5[0..3]`
3. Require an in-range `CHUNK_NUMBER` greater than the previously received
  chunk, allowing gaps for sparse images; validate MD5
4. Write to `/dev/sda` at `CHUNK_NUMBER × 128 MB`
5. If `CHUNK_NUMBER == TOTAL_CHUNKS - 1` → write `SDA_SUCCESS`; otherwise →
  write `SDA_CHUNK_ACK`
6. Any failure → write `SDA_BAD`, return to main poll loop

### Phase 4A: Variable-offset Sparse UFS Programming

This is the preferred bmap programming protocol. The host converts mapped bmap
block ranges into adjacent byte extents, coalesces contiguous ranges, and splits
only extents larger than 128 MB. Unmapped bytes are not transferred or written.

The host accepts Yocto/bmaptool format `2.0` bmaps with SHA-256 checksums. Before
programming it verifies the bmap file's own `BmapFileChecksum`, image/block count
consistency, mapped block count, strictly increasing in-bounds ranges, and every
range digest. Unsupported versions, algorithms, malformed metadata, duplicate or
overlapping ranges, and corrupt self-checksums are rejected.

For every extent the host writes:

- `TRANSFER_OFFSET_LO/HI`: absolute byte offset on `/dev/sda`
- `SIZE`: extent length, from 1 byte through 128 MB
- `TRANSFER_FLAGS`: `FINAL` on the last extent, zero otherwise
- payload bytes at `0x1000`

The host then writes `UFS_WRITE_GO`. The target validates that each extent is
within device capacity and does not overlap or precede the prior extent. When
bit 1 of `TRANSFER_FLAGS` is set it also checks the extent against the SHA-256
in `EXTENT_DIGEST` **before** writing, so a corrupted transfer never reaches the
device: the target answers `UFS_WRITE_RETRY` and the host resends the same
extent, up to three times, after which it reports reason 22. Where an extent
covers a whole bmap range the host reuses that range's existing digest, so most
extents cost it no hashing. It returns `UFS_WRITE_ACK` for intermediate
extents. For the extent carrying `FINAL`, it flushes `/dev/sda` to stable
storage, samples a few unmapped blocks to report whether the destination was
clean, and returns `UFS_WRITE_DATA_DONE`.

The host then places a checksum manifest at `0x1000`, writes its byte length to
`SIZE`, and writes `UFS_VERIFY_GO`. The target requires the manifest ranges to
cover exactly the union of byte extents written in this session, reads those
ranges from `/dev/sda`, and validates each SHA-256 before returning
`UFS_WRITE_SUCCESS`.

The little-endian manifest format is:

| Field | Format | Description |
|-------|--------|-------------|
| Header | `<4sHHI` | Magic `UFSC`, version `1`, algorithm `1` (SHA-256), record count |
| Record | `<QQ32s` | Absolute byte offset, byte length, SHA-256 digest |

Records must be sorted, non-overlapping, non-empty, and within UFS capacity.
For a complete bmap range, Ryzen copies its `chksum` directly into the manifest.
When a future partition-only update clips a bmap range, Ryzen calculates a new
SHA-256 over only the transmitted intersection and uses that digest instead.
Requested update ranges are merged if they overlap, intersected with mapped
bmap ranges, and sent in ascending byte-offset order. Unmapped portions of a
requested partition range are neither transmitted nor included in the manifest.

Host examples:

```sh
# Complete bmap-guided image update
program_versal_payload - image.wic.ufs.zst image.wic.ufs.bmap

# Update mapped bytes intersecting one partition/range
program_versal_payload - image.wic.ufs.zst image.wic.ufs.bmap \
  --ufs-range 0x20000000 0x80000000
```

The legacy Phase 3 fixed-chunk protocol remains supported for hosts that do not
provide a bmap.

### Zeroing before a sparse write

A bmap only lists the blocks that hold data in the image. Everything else is a
hole, and a sparse write never touches it, so those blocks keep whatever the
previous image left there. Holes are not only free space: an ext4 image leaves
zero-filled *in-use* metadata unmapped too, including inode bitmaps for empty
block groups, reserved GDT blocks, and lazily initialised inode tables. On a
blank device those read back as zeros and the filesystem is correct; on a device
that already held another image they keep stale metadata, and the SHA-256
manifest cannot detect it because it only covers mapped ranges.

`--zero-before-write` closes that gap by asking the target to zero the span
first, which restores the empty-destination assumption bmap relies on. The
target uses `BLKZEROOUT` and falls back to writing zeros if the device rejects
the ioctl. When partition ranges are also selected, only those ranges are
zeroed.

```sh
# Switching from one image to another on a device that has been used
program_versal_payload - image.wic.ufs.zst image.wic.ufs.bmap --zero-before-write
```

### Phase 4B: UFS Readback

The host starts a session by writing an absolute byte offset to
`TRANSFER_OFFSET_LO/HI`, a `SIZE` from 1 byte through 128 MB, and
`UFS_READ_GO`. The target validates the request against `/dev/sda` capacity,
reads the requested bytes into the BAR payload region, writes `MD5[0..3]`, then
returns `UFS_READ_READY`.

After copying and verifying the payload, the host either:

- writes the next offset and size, then `UFS_READ_ACK`; or
- writes `UFS_READ_DONE` to finish the session.

The target returns `UFS_READ_SUCCESS` after `UFS_READ_DONE`. Large dumps are
formed by repeating requests of at most 128 MB. Partition-table inspection can
request only the required leading sectors, for example offset `0`, length
`0x100000` for the first MiB.

```sh
program_versal_payload --read-ufs 0 0x100000 /tmp/ufs-first-mib.bin
```

---

## Target Device Paths

| Device | Path | Usage |
|--------|------|-------|
| OSPI flash | `/dev/mtd/by-name/f1010000.spi.0` | Phase 2 — OSPI programming |
| Block storage | `/dev/sda` | Phase 3 — WIC programming |
| Capacity check | `/sys/block/sda/size` | Sectors × 512 = bytes |
| BAR memory | `/dev/mem` at `0x20000000` | Target-side BAR access |

---

## Host CLI Usage

```
program_versal_payload.py [ospi_file] [wic_file] [bmap_file] [options]
```

The pciImage is not a positional argument; it is taken from
`/lib/firmware/versal/pcieImage`. Pass `-` for any positional slot to skip that
phase.

| Argument | Required | Description |
|----------|----------|-------------|
| `ospi_file` | No | OSPI flash image; `-` skips Phase 2 |
| `wic_file` | No | WIC disk image for `/dev/sda`; `-` skips Phase 3/4 |
| `bmap_file` | No | bmap for sparse programming; selects Phase 4 over Phase 3 |

| Option | Description |
|--------|-------------|
| `--zero-before-write` | Zero the target span before a sparse write; requires a bmap |
| `--ufs-range OFFSET LENGTH` | Program only this byte range; repeatable, requires a bmap |
| `--read-ufs OFFSET LENGTH OUTPUT` | Read a UFS byte range into a local file |

`OFFSET` and `LENGTH` accept decimal or `0x`-prefixed values.
Both scripts require root privileges.

### Examples

```sh
# OSPI only
sudo program_versal_payload.py ospi.bin -

# Full rootfs, no bmap (legacy fixed-chunk transfer)
sudo program_versal_payload.py - rootfs.wic

# Sparse bmap-guided programming
sudo program_versal_payload.py - image.wic.ufs.zst image.wic.ufs.bmap

# Sparse programming onto a device that already held another image
sudo program_versal_payload.py - image.wic.ufs.zst image.wic.ufs.bmap \
  --zero-before-write

# Update only the bytes intersecting one partition
sudo program_versal_payload.py - image.wic.ufs.zst image.wic.ufs.bmap \
  --ufs-range 0x20000000 0x80000000

# Read the first MiB back for partition-table inspection
sudo program_versal_payload.py --read-ufs 0 0x100000 /tmp/ufs-first-mib.bin
```

---

## Target Diagnostics

A failure must be identifiable after the fact, without anyone having kept a
console open. The target therefore publishes, every cycle:

- `FAIL_REASON` and `FAIL_DETAIL`, cleared at the start of each cycle so a stale
  value cannot be mistaken for a fresh one.
- `LOG_MAGIC`, written only once a real log has been placed at `LOG_OFFSET`.
  The log region doubles as the payload window, so without this the host cannot
  tell a log from leftover image data. The host reports "no device log" rather
  than printing payload bytes.
- A copy of the cycle log at `/tmp/programmer-last-cycle.log` on the target,
  which survives the next cycle overwriting the BAR.
- `/dev/sda` identity: model, vendor, revision, logical and physical block size,
  write cache mode and maximum discard size.
- Per-extent BAR read time, device write time and throughput, plus a whole-
  session total.
- An unmapped-gap sample after the final extent. A sparse write never touches
  the gaps between extents, so the target reads a small sample of them and warns
  when they are not zero, which means the destination was not clean and the new
  filesystem has inherited data from whatever was there before.

---

## Timeouts

| Timeout | Value | Applies to |
|---------|-------|------------|
| `READY_TIMEOUT` | 30 s | Waiting for `HOST_READY` |
| `FLASH_READY_TIMEOUT` | 120 s | Waiting for `FLASH_READY` |
| `PROGRAMMING_TIMEOUT` | 500 s | OSPI programming monitor |
| `SDA_CHUNK_TIMEOUT` | 300 s | A single chunk or extent |
| `UFS_VERIFY_TIMEOUT` | 1800 s | Whole-image read-back and SHA-256 pass |
| `UFS_ZERO_TIMEOUT` | 1800 s | Zeroing a span, including the write fallback |
| `SDA_CHUNK_TIMEOUT` | 300 s | Per-chunk ACK during SDA transfer |
| `STATUS_POLL_INTERVAL` | 0.5 s | Host-side polling interval |

The target has no timeouts — it polls indefinitely, matching its role as a
persistent service.

---

## Error Handling

### OSPI Errors

| Condition | Signal | Recovery |
|-----------|--------|----------|
| MD5 mismatch | `FLASH_BAD` | Target writes logs, returns to poll |
| Flash device missing | `FLASH_DEAD` | Target writes logs, returns to poll |
| Payload too large for flash | `FLASH_BAD` | Target writes logs, returns to poll |
| Erase failure | `FLASH_DEAD` | Target writes logs, returns to poll |
| Programming failure | `FLASH_DEAD` | Target erases first sector (safety), writes logs, returns to poll |

### SDA Errors

| Condition | Signal | Recovery |
|-----------|--------|----------|
| `/dev/sda` not found | `SDA_DEAD` | Target writes logs, returns to poll |
| WIC larger than SDA capacity | `SDA_BAD` | Target writes logs, returns to poll |
| Chunk MD5 mismatch | `SDA_BAD` | Target writes logs, returns to poll |
| Write failure to `/dev/sda` | `SDA_BAD` | Target writes logs, returns to poll |
| Duplicate, decreasing, or out-of-range chunk number | `SDA_BAD` | Target writes logs, returns to poll |

In all error cases, the target writes its accumulated logs to offset `0x1000`
and the log size to offset `0x04`, then writes the error signal to `STATUS`.
The host collects and displays these logs.

---

## Log Collection

After each programming cycle (OSPI or SDA), the target writes its timestamped
logs to the BAR:

1. Log text (UTF-8) is written starting at offset `0x1000`
2. Log size in bytes is written to offset `0x04` (`SIZE`)
3. Final status signal is written to offset `0x00` (`STATUS`)

The host reads `SIZE` to determine log length, then reads that many bytes from
`0x1000` to display the target's console output.

---

## Target Service Behaviour

The target script (`programmer.py`) operates as a persistent service:

- Maps the BAR once at startup via `/dev/mem`
- On its **first** cycle, writes `FLASH_READY` (`0xF1A54000`) to `STATUS`
- On **subsequent** cycles, preserves the previous final status (success or
  error) so the host can read it before sending the next command
- Polls for either `FLASH_GO` or `SDA_GO` — whichever arrives determines the
  phase to execute
- After each cycle (success or failure), writes logs and final status, then
  immediately returns to polling
- Handles `KeyboardInterrupt` gracefully without exiting
- Never calls `sys.exit()` after initial startup checks

---

## Data Integrity

All payload transfers are protected by MD5 checksums:

- **OSPI**: Host computes MD5 of the entire image and writes it to `MD5[0..3]`.
  Target reads the payload, computes MD5, and compares before programming.

- **SDA**: Each 128 MB chunk has its own MD5. Host computes per-chunk MD5 and
  writes to `MD5[0..3]`. Target validates each chunk independently before
  writing to disk.

MD5 is stored as four little-endian 32-bit words (16 bytes total) derived from
the standard 128-bit MD5 digest.

---

## Host Progress Reporting

During SDA transfer, the host prints a progress line to stdout approximately
every 5% of the total transfer, plus the first and last chunks. Each line
includes:

- Chunk number / total chunks
- Chunk size
- BAR write time
- Cumulative percentage
- Average transfer speed (MB/s)
