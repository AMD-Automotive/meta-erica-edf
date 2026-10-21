# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

# Override ISP firmware with updated versions from Artifactory
FILESEXTRAPATHS:prepend := "${THISDIR}:"

# Every ELF is downloaded under a name that carries its toolset and drop date
# (RPU-master: the drop variables below). DL_DIR is shared by every CI job, so
# two entries that fetch different files must never cache under the same name,
# or each job evicts the other's copy and has to download again. The installed
# names in /lib/firmware are set by do_install and do not change.
SRC_URI:amd-cortexa78-mali-common:amdtool-2025_2 = "\
    https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/vek385/2026_06_23/isp-r52-6-firmware.elf;name=r52-6-vek385;downloadfilename=isp-r52-6-firmware-vek385-2025_2-2026_06_23.elf \
    https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/vek385/2026_06_23/isp-r52-7-firmware.elf;name=r52-7-vek385;downloadfilename=isp-r52-7-firmware-vek385-2025_2-2026_06_23.elf \
    https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/vek385/2026_06_23/isp-r52-8-firmware.elf;name=r52-8-vek385;downloadfilename=isp-r52-8-firmware-vek385-2025_2-2026_06_23.elf \
    https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/rave2/2026_07_07/isp-r52-6-firmware.elf;name=r52-6-rave2;downloadfilename=isp-r52-6-firmware-rave2-2025_2-2026_07_07.elf \
    https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/rave2/2026_07_07/isp-r52-7-firmware.elf;name=r52-7-rave2;downloadfilename=isp-r52-7-firmware-rave2-2025_2-2026_07_07.elf \
    https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/rave2/2026_07_07/isp-r52-8-firmware.elf;name=r52-8-rave2;downloadfilename=isp-r52-8-firmware-rave2-2025_2-2026_07_07.elf \
    https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/rave2/2026_08_02/isp-r52-6-firmware.elf;name=r52-6-rave2-quanta;downloadfilename=isp-r52-6-firmware-rave2-quanta-2025_2-2026_08_02.elf \
    https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/rave2/2026_08_02/isp-r52-7-firmware.elf;name=r52-7-rave2-quanta;downloadfilename=isp-r52-7-firmware-rave2-quanta-2025_2-2026_08_02.elf \
    https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/rave2/2026_08_02/isp-r52-8-firmware.elf;name=r52-8-rave2-quanta;downloadfilename=isp-r52-8-firmware-rave2-quanta-2025_2-2026_08_02.elf \
"

# 2026.1 toolset firmware (vek385 + rave2 ISP). The RPU-master (sentry) drop
# is handled by meta-erica-sentry's isp-firmware_%.bbappend
# (SRC_URI:append/do_install:append/FILES:append), which also carries the
# ISP_RPUMASTER_DROP_RAVE2/VEK385 variables.
SRC_URI:amd-cortexa78-mali-common:amdtool-2026_1 = "\
    https://mkmartifactory.amd.com/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/2026.1/vek385/2026_09_10/isp-r52-6-firmware.elf;name=r52-6-vek385-2026_1;downloadfilename=isp-r52-6-firmware-vek385-2026_1-2026_09_10.elf \
    https://mkmartifactory.amd.com/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/2026.1/vek385/2026_09_10/isp-r52-7-firmware.elf;name=r52-7-vek385-2026_1;downloadfilename=isp-r52-7-firmware-vek385-2026_1-2026_09_10.elf \
    https://mkmartifactory.amd.com/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/2026.1/vek385/2026_09_10/isp-r52-8-firmware.elf;name=r52-8-vek385-2026_1;downloadfilename=isp-r52-8-firmware-vek385-2026_1-2026_09_10.elf \
    https://mkmartifactory.amd.com/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/2026.1/rave2/2026_09_10/isp-r52-6-firmware-rave2.elf;name=r52-6-rave2-2026_1;downloadfilename=isp-r52-6-firmware-rave2-2026_1-2026_09_10.elf \
    https://mkmartifactory.amd.com/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/2026.1/rave2/2026_09_10/isp-r52-7-firmware-rave2.elf;name=r52-7-rave2-2026_1;downloadfilename=isp-r52-7-firmware-rave2-2026_1-2026_09_10.elf \
    https://mkmartifactory.amd.com/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/2026.1/rave2/2026_09_10/isp-r52-8-firmware-rave2.elf;name=r52-8-rave2-2026_1;downloadfilename=isp-r52-8-firmware-rave2-2026_1-2026_09_10.elf \
    https://mkmartifactory.amd.com/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/2026.1/rave2/2026_09_10/isp-r52-6-firmware-rave2-sim.elf;name=r52-6-rave2-2026_1-sim;downloadfilename=isp-r52-6-firmware-rave2-sim-2026_1-2026_09_10.elf \
    https://mkmartifactory.amd.com/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/2026.1/rave2/2026_09_10/isp-r52-7-firmware-rave2-sim.elf;name=r52-7-rave2-2026_1-sim;downloadfilename=isp-r52-7-firmware-rave2-sim-2026_1-2026_09_10.elf \
    https://mkmartifactory.amd.com/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/2026.1/rave2/2026_09_10/isp-r52-8-firmware-rave2-sim.elf;name=r52-8-rave2-2026_1-sim;downloadfilename=isp-r52-8-firmware-rave2-sim-2026_1-2026_09_10.elf \
    https://mkmartifactory.amd.com/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/2026.1/rave2/2026_08_12/isp-r52-6-firmware-rave2-quanta.elf;name=r52-6-rave2-2026_1-quanta;downloadfilename=isp-r52-6-firmware-rave2-quanta-2026_1-2026_08_12.elf \
    https://mkmartifactory.amd.com/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/2026.1/rave2/2026_08_12/isp-r52-7-firmware-rave2-quanta.elf;name=r52-7-rave2-2026_1-quanta;downloadfilename=isp-r52-7-firmware-rave2-quanta-2026_1-2026_08_12.elf \
    https://mkmartifactory.amd.com/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/2026.1/rave2/2026_08_12/isp-r52-8-firmware-rave2-quanta.elf;name=r52-8-rave2-2026_1-quanta;downloadfilename=isp-r52-8-firmware-rave2-quanta-2026_1-2026_08_12.elf \
"

# Checksums for firmware files
SRC_URI[r52-6-vek385.sha256sum] = "61f8b5f065d2d382e6b18bc8655c6dfb232ed840b3b8f92ce79e4ffbb9c2edca"
SRC_URI[r52-7-vek385.sha256sum] = "ac26ee81b94369cbe2459e54dc508ba44193b294ee628509be8b2f2e3ea0c7c3"
SRC_URI[r52-8-vek385.sha256sum] = "113fe43f94463eda3f4c9ec684c0d493cf9f7a9f6388e6974da554431470aa44"
SRC_URI[r52-6-rave2.sha256sum] = "7409895659e376dc1489bc3f90013d470e581114026e3bdbcdb3a558a7e04fb1"
SRC_URI[r52-7-rave2.sha256sum] = "e5c607630cdd5520b66020f69146c3f07c8679c56f2338676d9abff77710dce8"
SRC_URI[r52-8-rave2.sha256sum] = "bbf848fdabaec023cadc3961d4c23341cc469a2e547153ef7be2a186cd6eb2fe"
SRC_URI[r52-6-rave2-quanta.sha256sum] = "81072748e8b5feb79f476382ca461e4f08cd7ac9fdd9b0da3677298e1e2ea649"
SRC_URI[r52-7-rave2-quanta.sha256sum] = "f6be966c0eaaae2f7bf99b7681f78f1250f4f08d661ee0a593a8e7288fc77d6e"
SRC_URI[r52-8-rave2-quanta.sha256sum] = "1c944c516bede888579abd81497b8ff84d282f65eb0e4c95aa3c49ab373704d5"

SRC_URI[r52-6-vek385-2026_1.sha256sum] = "76fa72f49d45cfb200259ffafd290dd4d703e955a6f26f1b1a227c1e4b901599"
SRC_URI[r52-7-vek385-2026_1.sha256sum] = "9958140417dfc09b7b26d24639767b21cd7706b1e4a5b1ef8f7557077e772b2d"
SRC_URI[r52-8-vek385-2026_1.sha256sum] = "adf2521f9e2d7ca3148056d51685561baba71d6448866249ba9c1598acc447b5"

SRC_URI[r52-6-rave2-2026_1.sha256sum] = "f408de6dbd56d18af9a3ad9da7c8289ab7e984670018d4a3ee37482561e162a6"
SRC_URI[r52-7-rave2-2026_1.sha256sum] = "1dd9cfc6b3b0a6ffdbd6436d7102af5f4e61dc35d9faa5396a813471b875782d"
SRC_URI[r52-8-rave2-2026_1.sha256sum] = "65c6040accec5de4c8cb3892f7da7f8fa4c922ff0f96365c21974d3a2a4906c4"
SRC_URI[r52-6-rave2-2026_1-sim.sha256sum] = "0619fd365aa10b2691177f55ba3d518e26d96fe680fd5fd9b97a91f8f910e220"
SRC_URI[r52-7-rave2-2026_1-sim.sha256sum] = "5ac91eef9f2f52d93522b4f2c9ec4219e0fc084a010700d37e0c24bb9d858a4a"
SRC_URI[r52-8-rave2-2026_1-sim.sha256sum] = "dd1cdd6f04501234ad134cf738d2eab193b795fb732121039a3e0187dce1a47e"
SRC_URI[r52-6-rave2-2026_1-quanta.sha256sum] = "89b2a930c9ba7a1308a53d332a2be45272ced69c99dcaabe21f12f3065acf155"
SRC_URI[r52-7-rave2-2026_1-quanta.sha256sum] = "7a190d4b5fadd41743d3d02615ae4a47f0b0b8e40917152e96e4193bb90c1dca"
SRC_URI[r52-8-rave2-2026_1-quanta.sha256sum] = "63f672bbc4428b30fa8c1f68dbcbe2431efb9793e47d011140d4dffb8c764833"

# Override do_install to use the new firmware files
do_install:amd-cortexa78-mali-common:amdtool-2025_2() {
    install -d ${D}${base_libdir}/firmware
    install -Dm 0644 ${WORKDIR}/isp-r52-6-firmware-vek385-2025_2-2026_06_23.elf ${D}${base_libdir}/firmware/isp-r52-6-firmware.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-7-firmware-vek385-2025_2-2026_06_23.elf ${D}${base_libdir}/firmware/isp-r52-7-firmware.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-8-firmware-vek385-2025_2-2026_06_23.elf ${D}${base_libdir}/firmware/isp-r52-8-firmware.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-6-firmware-rave2-2025_2-2026_07_07.elf ${D}${base_libdir}/firmware/isp-r52-6-firmware-rave2.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-7-firmware-rave2-2025_2-2026_07_07.elf ${D}${base_libdir}/firmware/isp-r52-7-firmware-rave2.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-8-firmware-rave2-2025_2-2026_07_07.elf ${D}${base_libdir}/firmware/isp-r52-8-firmware-rave2.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-6-firmware-rave2-quanta-2025_2-2026_08_02.elf ${D}${base_libdir}/firmware/isp-r52-6-firmware-rave2-quanta.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-7-firmware-rave2-quanta-2025_2-2026_08_02.elf ${D}${base_libdir}/firmware/isp-r52-7-firmware-rave2-quanta.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-8-firmware-rave2-quanta-2025_2-2026_08_02.elf ${D}${base_libdir}/firmware/isp-r52-8-firmware-rave2-quanta.elf
}

# Same install for the 2026.1 toolset
do_install:amd-cortexa78-mali-common:amdtool-2026_1() {
    install -d ${D}${base_libdir}/firmware
    install -Dm 0644 ${WORKDIR}/isp-r52-6-firmware-vek385-2026_1-2026_09_10.elf ${D}${base_libdir}/firmware/isp-r52-6-firmware.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-7-firmware-vek385-2026_1-2026_09_10.elf ${D}${base_libdir}/firmware/isp-r52-7-firmware.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-8-firmware-vek385-2026_1-2026_09_10.elf ${D}${base_libdir}/firmware/isp-r52-8-firmware.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-6-firmware-rave2-2026_1-2026_09_10.elf ${D}${base_libdir}/firmware/isp-r52-6-firmware-rave2.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-7-firmware-rave2-2026_1-2026_09_10.elf ${D}${base_libdir}/firmware/isp-r52-7-firmware-rave2.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-8-firmware-rave2-2026_1-2026_09_10.elf ${D}${base_libdir}/firmware/isp-r52-8-firmware-rave2.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-6-firmware-rave2-sim-2026_1-2026_09_10.elf ${D}${base_libdir}/firmware/isp-r52-6-firmware-rave2-sim.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-7-firmware-rave2-sim-2026_1-2026_09_10.elf ${D}${base_libdir}/firmware/isp-r52-7-firmware-rave2-sim.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-8-firmware-rave2-sim-2026_1-2026_09_10.elf ${D}${base_libdir}/firmware/isp-r52-8-firmware-rave2-sim.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-6-firmware-rave2-quanta-2026_1-2026_08_12.elf ${D}${base_libdir}/firmware/isp-r52-6-firmware-rave2-quanta.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-7-firmware-rave2-quanta-2026_1-2026_08_12.elf ${D}${base_libdir}/firmware/isp-r52-7-firmware-rave2-quanta.elf
    install -Dm 0644 ${WORKDIR}/isp-r52-8-firmware-rave2-quanta-2026_1-2026_08_12.elf ${D}${base_libdir}/firmware/isp-r52-8-firmware-rave2-quanta.elf
}

# Ship the additional rave2 firmware files in the package
FILES:${PN}:append:amd-cortexa78-mali-common:amdtool-2025_2 = " \
    ${base_libdir}/firmware/isp-r52-6-firmware-rave2.elf \
    ${base_libdir}/firmware/isp-r52-7-firmware-rave2.elf \
    ${base_libdir}/firmware/isp-r52-8-firmware-rave2.elf \
    ${base_libdir}/firmware/isp-r52-6-firmware-rave2-quanta.elf \
    ${base_libdir}/firmware/isp-r52-7-firmware-rave2-quanta.elf \
    ${base_libdir}/firmware/isp-r52-8-firmware-rave2-quanta.elf \
"
FILES:${PN}:append:amd-cortexa78-mali-common:amdtool-2026_1 = " \
    ${base_libdir}/firmware/isp-r52-6-firmware-rave2.elf \
    ${base_libdir}/firmware/isp-r52-7-firmware-rave2.elf \
    ${base_libdir}/firmware/isp-r52-8-firmware-rave2.elf \
    ${base_libdir}/firmware/isp-r52-6-firmware-rave2-sim.elf \
    ${base_libdir}/firmware/isp-r52-7-firmware-rave2-sim.elf \
    ${base_libdir}/firmware/isp-r52-8-firmware-rave2-sim.elf \
    ${base_libdir}/firmware/isp-r52-6-firmware-rave2-quanta.elf \
    ${base_libdir}/firmware/isp-r52-7-firmware-rave2-quanta.elf \
    ${base_libdir}/firmware/isp-r52-8-firmware-rave2-quanta.elf \
"
