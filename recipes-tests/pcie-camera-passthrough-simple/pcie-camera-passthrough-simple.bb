# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: BW
# Maintainer: BW

SUMMARY = "MDB5 PCIe camera passthrough endpoint setup"
DESCRIPTION = "Endpoint-side camera bring-up script (init_cameras.sh) for the \
pcie-camera-passthrough-simple demo. Selects the per-mode resolution, primes \
the cameras via the ISPs, and launches ep_proxy for the requested mode/config \
so frames stream to the x86 host over PCIe."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://init_cameras.sh \
"

# bash: the script uses a #!/bin/bash shebang and bash-only syntax ((( )), [[ ]]).
# tmux: ep_proxy is launched in a detached tmux session.
RDEPENDS:${PN} += "bash tmux"

S = "${WORKDIR}"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/init_cameras.sh ${D}${bindir}/init_cameras.sh
}
