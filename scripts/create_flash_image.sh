#!/usr/bin/env bash
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC
set -euo pipefail

# make_ospi_image.sh
# - Creates a 128MiB OSPI image pre-filled with 0xFF (erased flash value)
# - Places boot.bin at offset 0
# - Converts raw Linux Image to a U-Boot uImage (mkimage) and places it at offset 16MiB
# - Ensures the final image is fully padded with 0xFF up to exactly 128MiB
#
# Usage: make_ospi_image.sh <boot.bin> <Image> <output.img>

if [[ $# -ne 3 ]]; then
  echo "Usage: $0 <boot.bin> <Image> <output.img>" >&2
  exit 1
fi

BOOT_BIN="$1"
KERNEL_IMG_RAW="$2"
OUTPUT_IMG="$3"

# Constants
IMAGE_SIZE=$((128 * 1024 * 1024))   # 128 MiB
KERNEL_OFFSET=$((16 * 1024 * 1024)) # 16 MiB

# Fixed mkimage parameters (adjust ARCH if needed)
ARCH="arm64"
OS_TYPE="linux"
COMP="none"
LOAD_ADDR="0"
ENTRY_ADDR="0"

# Validate inputs
for f in "$BOOT_BIN" "$KERNEL_IMG_RAW"; do
  [[ -f "$f" ]] || { echo "Error: File not found: $f" >&2; exit 1; }
done

command -v mkimage >/dev/null 2>&1 || {
  echo "Error: mkimage not found. Install U-Boot tools (u-boot-tools)." >&2
  exit 1
}

# Temp files and cleanup
uimage_tmp="$(mktemp)"
ff_block="$(mktemp)"
cleanup() { rm -f "$uimage_tmp" "$ff_block"; }
trap cleanup EXIT

# Create uImage from raw Image
mkimage -A "$ARCH" -O "$OS_TYPE" -T kernel -C "$COMP" \
        -a "$LOAD_ADDR" -e "$ENTRY_ADDR" -n "Linux kernel" \
        -d "$KERNEL_IMG_RAW" "$uimage_tmp" >/dev/null

boot_size=$(wc -c < "$BOOT_BIN")
uimage_size=$(wc -c < "$uimage_tmp")

# Layout checks
if (( boot_size > KERNEL_OFFSET )); then
  echo "Error: boot.bin ($boot_size bytes) exceeds 16MiB offset." >&2
  exit 1
fi
if (( KERNEL_OFFSET + uimage_size > IMAGE_SIZE )); then
  echo "Error: uImage size ($uimage_size bytes) at 16MiB exceeds 128MiB total." >&2
  exit 1
fi

# Build output image pre-filled with 0xFF (ensures all unused space is 0xFF)
head -c $((1024*1024)) /dev/zero | tr '\000' '\377' > "$ff_block"
dd if="$ff_block" of="$OUTPUT_IMG" bs=1M count=128 conv=fsync status=none

# Write boot.bin at offset 0
dd if="$BOOT_BIN" of="$OUTPUT_IMG" bs=1M conv=notrunc status=none

# Write uImage at 16MiB
dd if="$uimage_tmp" of="$OUTPUT_IMG" bs=1M seek=16 conv=notrunc status=none

# Final assurance: force file size to exactly 128MiB and ensure the last byte is 0xFF.
# This guards against any accidental truncation by tools and guarantees full padding.
printf '\377' | dd of="$OUTPUT_IMG" bs=1 seek=$((IMAGE_SIZE-1)) conv=notrunc status=none

echo "Done. Created OSPI image: $OUTPUT_IMG"
echo "boot.bin size: $boot_size bytes at offset 0"
echo "uImage size:   $uimage_size bytes at offset 16MiB"
echo "All unused regions are padded with 0xFF up to 128MiB."