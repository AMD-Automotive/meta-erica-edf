#!/usr/bin/env bash
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT
# Short-Description: Resize the root filesystem to fill partition

ROOT_PART=$(mount | sed -n 's|^\(.*\) on / .*|\1|p')

# Derive the parent device and partition number from sysfs rather than slicing
# the device name. "cut -c 1-8" only ever produced a valid device for /dev/sdXN
# (it yields "/dev/mmc" for mmcblk0p2), and "tail -c 1" loses the first digit of
# a two-digit partition. sysfs handles sdb2, mmcblk0p2 and nvme0n1p2 alike.
PART_SYS=/sys/class/block/$(basename "$(readlink -f "$ROOT_PART")")
ROOT_DEV=/dev/$(basename "$(dirname "$(readlink -f "$PART_SYS")")")
PART_NUM=$(cat "$PART_SYS/partition" 2>/dev/null)

set -e

msg() {
    printf '%b' "$1" >&2
}

# Map a partition number to its device node. Kernel names differ between sdXN,
# mmcblk0pN and nvme0n1pN, so match on the sysfs "partition" attribute instead
# of concatenating strings.
part_dev() {
	local disk=$1 want=$2 p
	for p in /sys/class/block/"$disk"/*/partition; do
		[ -r "$p" ] || continue
		if [ "$(cat "$p")" = "$want" ]; then
			echo "/dev/$(basename "$(dirname "$p")")"
			return 0
		fi
	done
	return 1
}

# True when the partition already reaches the end of the disk, so that a reboot
# does not re-run parted on an already-grown volume. sysfs start/size are always
# in 512B units regardless of the device's logical block size. The 2048-sector
# slack covers the GPT backup header parted leaves at the tail.
partition_fills_disk() {
	local disk=$1 part=$2 start size disk_size
	start=$(cat /sys/class/block/"$disk"/"$part"/start 2>/dev/null) || return 1
	size=$(cat /sys/class/block/"$disk"/"$part"/size 2>/dev/null) || return 1
	disk_size=$(cat /sys/class/block/"$disk"/size 2>/dev/null) || return 1
	[ $((start + size + 2048)) -ge "$disk_size" ]
}

# Match on major:minor, not the device path: the kernel reports the rootfs as
# "/dev/root" in /proc/mounts while "mount" prints the resolved /dev/sdaN, so a
# name comparison silently reports root as unmounted.
partition_is_mounted() {
	local disk=$1 part=$2 devno
	devno=$(cat /sys/class/block/"$disk"/"$part"/dev 2>/dev/null) || return 1
	awk -v d="$devno" '$3 == d { found = 1 } END { exit !found }' /proc/self/mountinfo
}

do_expand_rootfs() 
{
	local target=$1
	local part_num=$2

	# Fix GPT backup header location if parted prompts for it (mismatched disk size)
	echo "Fix" | parted ${target} ---pretend-input-tty print > /dev/null 2>&1
 
	if [ -n "${MOUNTED}" ]; then
		echo -e "yes\n100%" | parted ${target} ---pretend-input-tty  resizepart ${part_num} > /dev/null 2>&1
	else
		parted ${target} resizepart ${part_num} 100% > /dev/null 2>&1
	fi
}

ext4_resize() 
{
	# Accept any block device that sysfs resolved a partition number for. The
	# old test whitelisted only /dev/sda and /dev/sdd, so a root on /dev/sdb2
	# (vek385) was skipped silently -- and on boards where /dev/sda is a data
	# volume rather than the rootfs it would have resized the wrong disk.
	if [ ! -b "$ROOT_DEV" ] || [ -z "$PART_NUM" ]; then
		msg "cannot derive a parent device/partition for root ($ROOT_PART). Don't know how to expand\n"
		return 1
	fi

	# Only the last partition can absorb the free tail. On the legacy two-part
	# layout that is root; on boot/root/data it is data, and growing root would
	# consume /data instead.
	local disk last_part target
	disk=$(basename "$ROOT_DEV")
	last_part=$(cat /sys/class/block/"$disk"/*/partition 2>/dev/null | sort -n | tail -1)
	if [ -z "$last_part" ]; then
		msg "no partitions found on $ROOT_DEV\n"
		return 1
	fi

	target=$(part_dev "$disk" "$last_part") || {
		msg "cannot map partition $last_part on $ROOT_DEV to a device node\n"
		return 1
	}

	local part_name
	part_name=$(basename "$target")

	if partition_fills_disk "$disk" "$part_name"; then
		msg "$target already extends to the end of $ROOT_DEV, nothing to do\n"
		return 0
	fi

	# do_expand_rootfs picks its parted invocation from MOUNTED, so re-evaluate
	# it for whichever partition we are actually growing.
	if partition_is_mounted "$disk" "$part_name"; then
		MOUNTED=1
	else
		MOUNTED=""
	fi

	do_expand_rootfs "$ROOT_DEV" "$last_part" && resize2fs "$target" > /dev/null 2>&1
}

report() {
    if [ "$ret" -eq '0' ]; then
        printf "\33[32m[✔]\33[0m\n"
    else
        printf "\33[31m[✘]\33[0m\n"
    fi
}

call_ext4_auto_resize() {
    msg "Auto resize ext4 partition ..."
    # Keep stdout quiet but let diagnostics on stderr reach the journal -- the
    # old "2>&1" hid the reason for every refusal, so a no-op still logged [✔].
    # The && / || form keeps a non-zero return from tripping "set -e" before
    # report() can run.
    ext4_resize >/dev/null && ret=0 || ret="$?"
    report

}

main() {
    	call_ext4_auto_resize
}

main "$@"
