# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

# This is a boot script for U-Boot with Xen parameters for SD boot mode.
# Erica bbappend version
# Generate edf_xen_boot.scr:
# mkimage -c none -A arm -T script -d edf-xen-boot.cmd edf_xen_boot.scr
#
################
xen_name=@@XEN_IMAGETYPE@@
kernel_name=@@KERNEL_IMAGETYPE@@
rootfs_name=@@RAMDISK_IMAGETYPE@@

setenv devtype @@BOOTDEVTYPE@@
setenv devnum @@SDBOOTDEV@@
setenv bootdev mmcblk${devnum}p
setenv bootpart @@BOOTPARTNUM@@
setenv dtname system.dtb

if test ${card1_rev} = "A01"; then setenv mountpoint sda; else setenv mountpoint sdd; fi

echo "Checking for rootfs info boot partition:${dtname}"
if test -e ${devtype} ${devnum}:${bootpartnum} rootfs_id.scr; then
       echo "Loading rootfs_id.scr at ${fdt_addr_r}"
       fatload ${devtype} ${devnum}:${bootpartnum} ${fdt_addr_r} rootfs_id.scr;
       source ${fdt_addr_r}
else
       echo "Not found"
fi

echo "Checking for xen:${xen_name}"
if test -e ${devtype} ${devnum}:${bootpart} ${xen_name}; then
	echo "Loading ${xen_name} at @@XEN_LOAD_ADDRESS@@";
	@@LOADCMD@@ ${devtype} ${devnum}:${bootpart} @@XEN_LOAD_ADDRESS@@ ${xen_name};
fi

echo "Checking for Image:${kernel_name}"
if test -e ${devtype} ${devnum}:${bootpart} ${kernel_name}; then
	echo "Loading ${kernel_name} at @@XEN_KERNEL_LOAD_ADDRESS@@";
	@@LOADCMD@@ ${devtype} ${devnum}:${bootpart} @@XEN_KERNEL_LOAD_ADDRESS@@ ${kernel_name};
	setenv kernel_size 0x$filesize;
fi

echo "Checking for ramdisk:${rootfs_name}"
if test -e ${devtype} ${devnum}:${bootpart} ${rootfs_name} && test "${skip_ramdisk}" != "yes"; then
	echo "Loading ${rootfs_name} at @@XEN_RAMDISK_LOAD_ADDRESS@@";
	@@LOADCMD@@ ${devtype} ${devnum}:${bootpart} @@XEN_RAMDISK_LOAD_ADDRESS@@ ${rootfs_name};
	setenv ramdisk_size 0x$filesize;
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
fdt resize 0x1000

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

fdt set /chosen \#address-cells <0x2>
fdt set /chosen \#size-cells <0x2>
fdt get value xen_bootargs /chosen xen_bootargs
fdt set /chosen xen,xen-bootargs \"${xen_bootargs}\"

# Check that env $ramdisk_size is 0 or not.
if test -n ${ramdisk_size}; then
	fdt mknod /chosen dom0-ramdisk
	fdt set /chosen/dom0-ramdisk compatible  "xen,linux-initrd" "xen,multiboot-module" "multiboot,module"
	fdt set /chosen/dom0-ramdisk reg <0x0 @@XEN_RAMDISK_LOAD_ADDRESS@@ 0x0 ${ramdisk_size}>
	setenv rootfs_param @@KERNEL_ROOT_RAMDISK@@
else
    setenv rootfs_param @@KERNEL_ROOT_SD@@
fi

fdt get value dom0_bootargs /chosen dom0_bootargs
fdt mknod /chosen dom0
fdt set /chosen/dom0 compatible  "xen,linux-zimage" "xen,multiboot-module" "multiboot,module"
fdt set /chosen/dom0 reg <0x0 @@XEN_KERNEL_LOAD_ADDRESS@@ 0x0 ${kernel_size}>
setenv dom0_bootargs_mod ${dom0_bootargs} ${rootfs_param} sdt_filename=${sdt_filename}
fdt set /chosen xen,dom0-bootargs \"${dom0_bootargs_mod}\"

setenv fdt_high 0xffffffffffffffff

echo "Booting Xen with the following args:"
fdt print /chosen

booti @@XEN_LOAD_ADDRESS@@ - ${dtaddr}
