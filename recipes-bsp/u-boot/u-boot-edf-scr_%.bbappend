# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, JC, LT
# Maintainer: LT

FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " \
    file://edf-xen-boot.cmd \
    file://edf-linux-ufs-boot.cmd \
    "

# do_compile builds boot.scr from edf-linux-mmc-boot.cmd, but that file is not
# otherwise in SRC_URI. Add it so the recipe can build once it is pulled into
# the rootfs. Scoped to 2026.1 to leave 2025.2 behaviour unchanged.
SRC_URI:append:amdtool-2026_1 = " file://edf-linux-mmc-boot.cmd"

BOOTPARTNUM = "1"
ROOTPARTNUM = "2"
BOOTDEVTYPE = "usb"
LOADCMD = "fatload"
XEN_KERNEL_LOAD_ADDRESS = "0x0D000000"
XEN_LOAD_ADDRESS = "0x0C000000"
KERNEL_ROOT_SD = "root=/dev/${mountpoint}${ROOTPARTNUM} ro rootwait"
XEN_SERIAL_CONSOLES = "serial1"
KERNEL_IMAGETYPE = "Image"
XEN_IMAGETYPE = "xen"
RAMDISK_IMAGETYPE = "rootfs.cpio.gz"
DEVNUM = "0"
XEN_RAMDISK_LOAD_ADDRESS = "0x2600000"
KERNEL_ROOT_RAMDISK ?= "root=/dev/ram0 rw"
SDBOOTDEV = "0"

# Default Linux boot script uses MMC flow; RAVE2 UFS images must use
# the UFS/scsi flow to boot from UFS without manual env edits.
LINUX_BOOT_CMD = "edf-linux-mmc-boot.cmd"
LINUX_BOOT_CMD:rave2-baseline = "edf-linux-ufs-boot.cmd"
LINUX_BOOT_CMD:rave2-xylon-sentry = "edf-linux-ufs-boot.cmd"
LINUX_BOOT_CMD:rave2-imx623-sentry = "edf-linux-ufs-boot.cmd"
LINUX_BOOT_CMD:rave2-baseline-seg = "edf-linux-ufs-boot.cmd"


do_compile() {

    mkimage -A arm -T script -C none -n "Linux Boot script" -d "${WORKDIR}/${LINUX_BOOT_CMD}" boot.scr
    # Some boot flows still probe legacy *.uimg script names first.
    # Keep a byte-identical alias so both names are always valid.
    cp -f boot.scr boot.scr.uimg

    # For edf-xen-boot.cmd
    sed -e 's/@@KERNEL_IMAGETYPE@@/${KERNEL_IMAGETYPE}/' \
        -e 's/@@XEN_IMAGETYPE@@/${XEN_IMAGETYPE}/' \
        -e 's/@@RAMDISK_IMAGETYPE@@/${RAMDISK_IMAGETYPE}/' \
        -e 's/@@KERNEL_BOOTCMD@@/${KERNEL_BOOTCMD}/' \
        -e 's/@@DOM0_MEM@@/${DOM0_MEM}/' \
        -e 's/@@DOM0_MAX_VCPUS@@/${DOM0_MAX_VCPUS}/' \
        -e 's/@@XEN_LOAD_ADDRESS@@/${XEN_LOAD_ADDRESS}/' \
        -e 's/@@RAMDISK_LOAD_ADDRESS@@/${RAMDISK_LOAD_ADDRESS}/' \
        -e 's:@@XEN_KERNEL_LOAD_ADDRESS@@:${XEN_KERNEL_LOAD_ADDRESS}:' \
        -e 's:@@XEN_DEVICETREE_LOAD_ADDRESS@@:${XEN_DEVICETREE_LOAD_ADDRESS}:' \
        -e 's:@@XEN_RAMDISK_LOAD_ADDRESS@@:${XEN_RAMDISK_LOAD_ADDRESS}:' \
        -e 's:@@XEN_CMDLINE_APPEND@@:${XEN_CMDLINE_APPEND}:' \
        -e 's:@@XEN_SERIAL_CONSOLES@@:${XEN_SERIAL_CONSOLES}:' \
        -e 's/@@BOOTPARTNUM@@/${BOOTPARTNUM}/' \
        -e 's/@@ROOTPARTNUM@@/${ROOTPARTNUM}/' \
        -e 's:@@KERNEL_ROOT_SD@@:${KERNEL_ROOT_SD}:' \
        -e 's:@@KERNEL_ROOT_RAMDISK@@:${KERNEL_ROOT_RAMDISK}:' \
        -e 's/@@SDBOOTDEV@@/${SDBOOTDEV}/' \
        -e 's/@@BOOTDEVTYPE@@/${BOOTDEVTYPE}/' \
        -e 's/@@LOADCMD@@/${LOADCMD}/' \
        "${WORKDIR}/edf-xen-boot.cmd" > "${WORKDIR}/xen-boot.cmd"

	mkimage -A arm -T script -C none -n "Xen Boot script" -d "${WORKDIR}/xen-boot.cmd" xen_boot.scr
    cp -f xen_boot.scr xen_boot.scr.uimg
}

do_install:append() {
    install -m 0644 boot.scr ${D}/boot
    install -m 0644 boot.scr.uimg ${D}/boot
	install -m 0644 xen_boot.scr ${D}/boot
    install -m 0644 xen_boot.scr.uimg ${D}/boot
}