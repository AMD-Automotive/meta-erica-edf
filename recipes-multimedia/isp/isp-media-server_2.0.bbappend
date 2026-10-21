# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

# Align isp-media-server to the MATCHED 26.1 build that the working ecs_demo
# reference design runs: "ISP MediaServer Release:26.1 (Apr 29 2026)".

SRC_URI = "https://edf.amd.com/sswreleases/isp-firmware/2026.1/04302026/isp-media-server_04292026.tar.gz"
SRC_URI[sha256sum] = "e38f89efd09bf559d226ef331dbb7310aef638eebe799fabde48021ff5f6badb"

# Additional calibration files: IMX623_1920x1536_v1.0.json, IMX728_v1.1.json,
# auto_IMX728_v1.1.json and manual_IMX728_v1.1.json. They are closed-source
# tuning data, covered by the same licence as the media server (LICENSE =
# "CLOSED" in the base recipe), so they are fetched from Artifactory rather
# than kept in the layer. The tarball unpacks the four files into ${WORKDIR}.
SRC_URI += "https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/2026.1/tuning/2026_10_12/2026_10_12_isp_media_server_tuning.tar.gz;name=tuning"
SRC_URI[tuning.sha256sum] = "6c0b90af5a2ad34f9cc651ab7c68d14b130d45c837ec2884a48ec22af7826872"

do_install:append() {
    install -d ${D}${datadir}/Tuning_files/IMX623/1920
    install -m 0644 ${WORKDIR}/IMX623_1920x1536_v1.0.json \
        ${D}${datadir}/Tuning_files/IMX623/1920/IMX623_1920x1536_v1.0.json
    install -m 0644 ${WORKDIR}/auto_IMX728_v1.1.json \
        ${D}${datadir}/Tuning_files/IMX728/auto_IMX728_v1.1.json
    install -m 0644 ${WORKDIR}/IMX728_v1.1.json \
        ${D}${datadir}/Tuning_files/IMX728/IMX728_v1.1.json
    install -m 0644 ${WORKDIR}/manual_IMX728_v1.1.json \
        ${D}${datadir}/Tuning_files/IMX728/manual_IMX728_v1.1.json
}
