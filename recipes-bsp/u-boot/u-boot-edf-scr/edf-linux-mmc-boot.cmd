# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, JC
# Maintainer: JC

# This is a boot script for U-Boot with Linux env for MMC boot mode.
# Generate edf_linux_mmc_boot.scr:
# mkimage -c none -A arm -T script -d edf-linux-mmc-boot.cmd edf_linux_mmc_boot.scr
#
################

setenv kernelname Image
setenv dtname system.dtb

# fdt_addr_r / scriptaddr are intentionally NOT set here. Boards whose
# kernel Image is large enough to overlap the default fdt_addr_r
# (0x22000000) park the DTB higher in their own U-Boot env header
# (e.g. vek385-imx623-sentry / vek385-xylon-sentry amd_edf_no_overlay.h),
# so this shared script never perturbs the load addresses of boards that
# don't need it.

setenv bootpartnum 1
setenv rootpartnum 2
# Root partition selection.
#
# The WIC (erica-default-sd.wks) pins the rootfs partition GUID, so a
# PARTUUID=... reference is the only hardware-agnostic way to name the
# root device. Linux block-device names depend on enumeration order:
# on VEK385 ISP Sentry, sda is UFS LUN0 (NOT the USB stick), so
# root=/dev/sda2 panics with "VFS: Cannot open root device sda2"
# because sda only has one partition. Use the WIC PARTUUID as the
# safe fallback, and try a live `part uuid` lookup first so reflashed
# images with a different GUID still work.
setenv legacy_rootpart "PARTUUID=377ec80e-4852-11f0-9688-325096b39f47"
if part uuid ${devtype} ${devnum}:${rootpartnum} distro_rootpart_uuid; then
       setenv rootpart "PARTUUID=${distro_rootpart_uuid}"
       echo "rootpart from live PARTUUID: ${rootpart}"
else
       setenv rootpart "${legacy_rootpart}"
       echo "rootpart from fixed-UUID fallback: ${rootpart}"
fi

echo "Checking for rootfs info boot partition:${dtname}"
if test -e ${devtype} ${devnum}:${bootpartnum} rootfs_id.scr; then
       echo "Loading rootfs_id.scr at ${fdt_addr_r}"
       fatload ${devtype} ${devnum}:${bootpartnum} ${fdt_addr_r} rootfs_id.scr;
       source ${fdt_addr_r}
else
       echo "Not found"
fi

echo "Checking for kernel:${kernelname}"
if test -e ${devtype} ${devnum}:${bootpartnum} ${kernelname}; then
       echo "Loading ${kernelname} at ${kernel_addr_r}"
       fatload ${devtype} ${devnum}:${bootpartnum} ${kernel_addr_r} ${kernelname};
else
       echo "kernel image ${kernelname} not found on ${devtype} ${devnum}:${bootpartnum}"
       exit
fi

echo "Checking for device tree on boot partition:${dtname}"
if test -e ${devtype} ${devnum}:${bootpartnum} ${dtname}; then
       echo "Loading ${dtname} at ${fdt_addr_r}"
       fatload ${devtype} ${devnum}:${bootpartnum} ${fdt_addr_r} ${dtname};
       dtaddr=${fdt_addr_r}
else
       echo "Using Device tree from OSPI"
       dtaddr=${fdtcontroladdr}
fi

fdt addr ${dtaddr}

# Give the in-RAM DTB headroom so subsequent fdt set/rm and any
# overlays applied below (e.g. VEK385.scr "enable big npu mem")
# don't fail with libfdt FDT_ERR_NOSPACE.
fdt resize 0x10000

if test ${overlay} = "1"; then
       echo "Checking for override overlay file ${card1_name}.scr"
       if test -e ${devtype} ${devnum}:${bootpartnum} ${card1_name}.scr; then
              echo "Override found - sourcing"
              setexpr script2_addr ${scriptaddr} + ${script_size_f}
              fatload ${devtype} ${devnum}:${bootpartnum} ${script2_addr} ${card1_name}.scr
              source ${script2_addr}
       else
              echo "No override found"
       fi
fi

fdt get value bootargs /chosen bootargs
setenv bootargs ${bootargs} root=${rootpart} ro rootwait uio_pdrv_genirq.of_id=generic-uio sdt_filename=${sdt_filename}
echo "Booting kernel with bootargs: ${bootargs}"
booti ${kernel_addr_r} - ${dtaddr}
