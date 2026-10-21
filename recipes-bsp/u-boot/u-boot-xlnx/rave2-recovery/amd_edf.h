// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: GPL-2.0-only
// Contributors: KC
// Maintainer: KC

#ifndef __CONFIG_EDF_VERSAL2_H
#define __CONFIG_EDF_VERSAL2_H

#include <configs/amd_versal2.h>

#define ENV_MEM_LAYOUT_SETTINGS \
	"card1_rev=A01\0" \
	"board=RAVE2-RECOVERY\0" \
	"bootcmd=run recovery_cmd\0" \
	"recovery_cmd=" \
		"mw.l 0x20000000 0xfeedb007; " \
		"echo Waiting for handshake...; " \
		"while itest.l *0x20000000 != 0xb007ab1e; do sleep 0.1; done; " \
		"echo Handshake OK; " \
		"source 0x20001000\0"
	
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
