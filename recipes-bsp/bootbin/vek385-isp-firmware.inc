# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

# Pre-built ISP firmware for R52 cores 6, 7, and 8
SRC_URI:append:vek385-isp = " \
    https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/vek385/isp-r52-6-firmware.elf;name=r52-6 \
    https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/vek385/isp-r52-7-firmware.elf;name=r52-7 \
    https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/vek385/isp-r52-8-firmware.elf;name=r52-8 \
"

# Checksums for firmware files
SRC_URI[r52-6.sha256sum] = "fd8606f2c1013d3ad3f651dceda62419a721da67e7a8ca2f437284a82991caf8"
SRC_URI[r52-7.sha256sum] = "9e86e1c18e7254a3176369de1b323531ecffe11cf5c49727594f800f8a213ed4"
SRC_URI[r52-8.sha256sum] = "02e6bfbae3115dccffd30e34c90f5eeff4f4ed296cf52f16a8b838c451a33847"

## Add the firmware to the BIF partitions
#BIF_PARTITION_ATTR:append = " isp-r52-6-fw isp-r52-7-fw isp-r52-8-fw"
#
## Remove these from DEPENDS since they're pre-built binaries, not recipes
#DEPENDS:remove = "isp-r52-6-fw isp-r52-7-fw isp-r52-8-fw"
#
## All R52 firmware share the same image ID to be in the same image block
#BIF_PARTITION_ID[isp-r52-6-fw] = "0x1c000001"
#BIF_PARTITION_ID[isp-r52-7-fw] = "0x1c000001"
#BIF_PARTITION_ID[isp-r52-8-fw] = "0x1c000001"
#
## Set the image name for this image block
#BIF_PARTITION_NAME[0x1c000001] = "ss_rpu"
#
## Configure partition attributes with id, core, cluster, and tcmboot
#BIF_PARTITION_ATTR[isp-r52-6-fw] = "id=0x61, core=r52-0, cluster=3, tcmboot"
#BIF_PARTITION_IMAGE[isp-r52-6-fw] = "${WORKDIR}/isp-r52-6-firmware.elf"
#
#BIF_PARTITION_ATTR[isp-r52-7-fw] = "id=0x62, core=r52-1, cluster=3, tcmboot"
#BIF_PARTITION_IMAGE[isp-r52-7-fw] = "${WORKDIR}/isp-r52-7-firmware.elf"
#
#BIF_PARTITION_ATTR[isp-r52-8-fw] = "id=0x63, core=r52-0, cluster=4, tcmboot"
#BIF_PARTITION_IMAGE[isp-r52-8-fw] = "${WORKDIR}/isp-r52-8-firmware.elf"
