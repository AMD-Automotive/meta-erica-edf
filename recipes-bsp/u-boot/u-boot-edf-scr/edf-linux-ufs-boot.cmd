# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, JC, LT
# Maintainer: LT

# This is a boot script for U-Boot with Linux env for UFS boot mode.
# Generate edf_linux_ufs_boot.scr:
# mkimage -c none -A arm -T script -d edf-linux-ufs-boot.cmd edf_linux_ufs_boot.scr
#
################

# UFS Boot
setenv devtype scsi
setenv devnum 0

setenv kernelname Image
setenv dtname system.dtb
setenv loadcmd load

# fdt_addr_r / scriptaddr come from each board's U-Boot env header, not
# from this shared script: rave2-xylon-sentry parks the DTB at 0x40000000 in
# rave2-xylon-sentry/amd_edf.h, while rave2-baseline keeps its default. This
# keeps one board's address needs from being forced on the other.

setenv bootpartnum 1
setenv rootpartnum 2
# Always use PARTUUID for the legacy fallback. The WIC pins the rootfs
# partition GUID, so this works regardless of which Linux block-device
# name the kernel assigns (sda vs sdd vs sdb). Never fall back to
# /dev/sda2 -- on UFS systems sda is LUN0 and has no partition 2,
# causing the kernel to panic with "VFS: Cannot open root device sda2".
setenv legacy_rootpart "PARTUUID=377ec80e-4852-11f0-9688-325096b39f47"

echo "Checking for rootfs info boot partition:${dtname}"
if ${loadcmd} ${devtype} ${devnum}:${bootpartnum} ${fdt_addr_r} rootfs_id.scr; then
       echo "Loading rootfs_id.scr at ${fdt_addr_r}"
       source ${fdt_addr_r}
else
       echo "Not found"
fi

echo "Checking for kernel:${kernelname}"
if ${loadcmd} ${devtype} ${devnum}:${bootpartnum} ${kernel_addr_r} ${kernelname}; then
	echo "Loading ${kernelname} at ${kernel_addr_r}"
elif ${loadcmd} ${devtype} ${devnum}:${rootpartnum} ${kernel_addr_r} /boot/${kernelname}; then
	echo "Loading /boot/${kernelname} at ${kernel_addr_r}"
else
	echo "kernel image not found on ${devtype} ${devnum}:${bootpartnum} or /boot on :${rootpartnum}"
	exit
fi

echo "Checking for device tree on boot partition:${dtname}"
if ${loadcmd} ${devtype} ${devnum}:${bootpartnum} ${fdt_addr_r} ${dtname}; then
       echo "Loading ${dtname} at ${fdt_addr_r}"
       dtaddr=${fdt_addr_r}
elif ${loadcmd} ${devtype} ${devnum}:${rootpartnum} ${fdt_addr_r} /boot/${dtname}; then
       echo "Loading /boot/${dtname} at ${fdt_addr_r}"
       dtaddr=${fdt_addr_r}
else
       echo "Using Device tree from OSPI"
       dtaddr=${fdtcontroladdr}
fi

fdt addr ${dtaddr}

# Give the in-RAM DTB headroom for fdt set/rm and overlay application.
fdt resize 0x10000

if test ${overlay} = "1"; then
       echo "Checking for override overlay file ${card1_name}.scr"
       setexpr script2_addr ${scriptaddr} + ${script_size_f}
       if ${loadcmd} ${devtype} ${devnum}:${bootpartnum} ${script2_addr} ${card1_name}.scr; then
              echo "Override found - sourcing"
              source ${script2_addr}
       elif ${loadcmd} ${devtype} ${devnum}:${rootpartnum} ${script2_addr} /boot/${card1_name}.scr; then
              echo "Override found in /boot - sourcing"
              source ${script2_addr}
       else
              echo "No override found"
       fi
fi

if part uuid ${devtype} ${devnum}:${rootpartnum} distro_rootpart_uuid; then
       setenv rootpart "PARTUUID=${distro_rootpart_uuid}"
       echo "rootpart from live PARTUUID: ${rootpart}"
else
       setenv rootpart "${legacy_rootpart}"
       echo "rootpart from fixed-UUID fallback: ${rootpart}"
fi

fdt get value bootargs /chosen bootargs
setenv bootargs ${bootargs} root=${rootpart} ro rootwait uio_pdrv_genirq.of_id=generic-uio sdt_filename=${sdt_filename}
echo "Booting kernel with bootargs: ${bootargs}"
booti ${kernel_addr_r} - ${dtaddr}
