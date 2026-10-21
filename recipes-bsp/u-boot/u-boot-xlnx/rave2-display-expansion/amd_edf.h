// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: GPL-2.0-only
// Contributors: KC, LT
// Maintainer: KC

#ifndef __CONFIG_EDF_VERSAL2_H
#define __CONFIG_EDF_VERSAL2_H

#include <configs/amd_versal2.h>

#define ENV_MEM_LAYOUT_SETTINGS \
	"card1_rev=A01\0" \
	"board=RAVE2\0" \
	"bootcmd=bootmenu\0" \
	"fdt_addr_r=0x40000000\0" \
	"fdt_size_r=0x400000\0" \
	"pxefile_addr_r=0x10000000\0" \
	"kernel_addr_r=0x18000000\0" \
	"kernel_size_r=0x10000000\0" \
	"kernel_comp_addr_r=0x30000000\0" \
	"kernel_comp_size=0x3C00000\0" \
	"ramdisk_addr_r=0x02100000\0" \
	"script_size_f=0x80000\0" \
	"bootmenu_0=Linux Distroboot USB=setenv boot_targets scsi usb0; run distro_bootcmd\0" \
	"bootmenu_1=Xen   Distroboot USB=setenv boot_scripts xen_boot.scr; setenv boot_targets scsi usb0; run distro_bootcmd\0" \
	"bootmenu_2=Boot USB  kernel with USB rootfs=run usb_usb_boot\0" \
	"bootmenu_3=Boot USB  kernel with USB rootfs No DHCP=run usb_usb_boot_noip\0" \
	"bootmenu_4=Boot OSPI kernel with NFS rootfs=run ospi_nfs_boot\0" \
	"bootmenu_5=Boot USB  kernel with NFS rootfs=run usb_nfs_boot\0" \
	"bootmenu_delay=5\0" \
	"bootcmd_scsi=run scsi_boot\0" \
	"mod_fdtaddr=0x72100000\0" \
	"image_addr=0x70000000\0" \
	"nfsroot=192.168.0.10:/bigscratch/nfs/3\0" \
	"ipaddr=192.168.0.42\0" \
	"ipgw=192.168.0.1\0" \
	"ipns=8.8.8.8\0" \
	"copy_image_from_ospi=sf probe;sf read ${image_addr} 0x01000000 0x02800000;iminfo ${image_addr}\0" \
	"copy_image_from_usb=usb start;fatload usb 0:1 ${image_addr} Image\0" \
	"setup_copy_fdt_pl=cp.b $fdtcontroladdr ${mod_fdtaddr} 0x100000; fdt addr ${mod_fdtaddr}\0" \
	"usb_usb_boot=run copy_image_from_usb;run setup_copy_fdt_pl;fdt set /chosen bootargs \"console=ttyAMA0 earlycon=pl011,mmio32,0xf1920000,115200n8 clk_ignore_unused ip=dhcp root=/dev/sda2 rw rootwait\";booti ${image_addr} - ${mod_fdtaddr}\0" \
	"usb_usb_boot_noip=run copy_image_from_usb;run setup_copy_fdt_pl;fdt set /chosen bootargs \"console=ttyAMA0 earlycon=pl011,mmio32,0xf1920000,115200n8 clk_ignore_unused ip=off root=/dev/sda2 rw rootwait\";booti ${image_addr} - ${mod_fdtaddr}\0" \
	"usb_nfs_boot=run copy_image_from_usb;run setup_copy_fdt_pl;fdt set /chosen bootargs \"console=ttyAMA0 earlycon=pl011,mmio32,0xf1920000,115200n8 clk_ignore_unused ip=${ipaddr}::${ipgw}:255.255.255.0:rave2:eth0:off nameserver=${ipns} root=/dev/nfs nfsroot=${nfsroot},vers=3,tcp,nolock rw\";booti ${image_addr} - ${mod_fdtaddr}\0" \
	"ospi_nfs_boot=run copy_image_from_ospi;run setup_copy_fdt_pl;fdt set /chosen bootargs \"console=ttyAMA0 earlycon=pl011,mmio32,0xf1920000,115200n8 clk_ignore_unused ip=${ipaddr}::${ipgw}:255.255.255.0:rave2:eth0:off nameserver=${ipns} root=/dev/nfs nfsroot=${nfsroot},vers=3,tcp,nolock rw\";bootm ${image_addr} - ${mod_fdtaddr}\0" \
	"ospi_nfs_boot_pl=run copy_image_from_ospi;run setup_copy_fdt_pl;fdt set /chosen bootargs \"console=ttyAMA0 earlycon=pl011,mmio32,0xf1920000,115200n8 clk_ignore_unused ip=${ipaddr}::${ipgw}:255.255.255.0:rave2:eth0:off nameserver=${ipns} root=/dev/nfs nfsroot=${nfsroot},vers=3,tcp,nolock rw\";bootm ${image_addr} - ${mod_fdtaddr}\0" \
	"ospi_usb_boot=run copy_image_from_ospi;run setup_copy_fdt_pl;fdt set /chosen bootargs \"console=ttyAMA0 earlycon=pl011,mmio32,0xf1920000,115200n8 clk_ignore_unused ip=dhcp root=/dev/sda2 rw rootwait\";bootm ${image_addr} - ${mod_fdtaddr}\0" \
	"ospi_usb_boot_nonet=run copy_image_from_ospi;run setup_copy_fdt_pl;fdt set /chosen bootargs \"console=ttyAMA0 earlycon=pl011,mmio32,0xf1920000,115200n8 clk_ignore_unused ip=off root=/dev/sda2 rw rootwait\";bootm ${image_addr} - ${mod_fdtaddr}\0"
	
/* Initial environment variables */
#ifndef CFG_EXTRA_ENV_SETTINGS
#define CFG_EXTRA_ENV_SETTINGS \
    ENV_MEM_LAYOUT_SETTINGS \
    BOOTENV
#endif

/* Define image type guid, this should match with the capsule-metadata recipe value*/
#define XILINX_BOOT_IMAGE_GUID \
	EFI_GUID(0xcb27e54d, 0x08f3a, 0x4c77, 0x8a, 0x72, \
		 0x1c, 0x76, 0xd2, 0xd4, 0xe9, 0x38)
#endif /* __CONFIG_EDF_VERSAL_H */
