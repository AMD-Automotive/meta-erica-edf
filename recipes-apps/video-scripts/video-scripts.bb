# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, LT
# Maintainer: LT

SUMMARY = "Video setup and encode test scripts"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://./ \
        https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/ERICA-H/videos/bbb_1080p_normal_2min_444_8bit.mp4 \
"
SRC_URI[sha256sum] = "900047e1c2ce4b6dbfef43de305060d9f365fa7e45429d9a4a0ac1d0975a88f5"

S = "${WORKDIR}"

do_install() {
	install -d ${D}${bindir}
	install -m 0755 ${S}/check_display.sh ${D}${bindir}/check_display
	install -m 0755 ${S}/configure_video_device_rave2.sh ${D}${bindir}/configure_video_device_rave2
	install -m 0755 ${S}/reset_deser_0_rave2.sh ${D}${bindir}/reset_deser_0_rave2
	install -m 0755 ${S}/reset_deser_1_rave2.sh ${D}${bindir}/reset_deser_1_rave2
	install -m 0755 ${S}/start_camera_stream_0123_rave2.sh ${D}${bindir}/start_camera_stream_0123_rave2
	install -m 0755 ${S}/start_camera_stream_4567_rave2.sh ${D}${bindir}/start_camera_stream_4567_rave2
	install -m 0755 ${S}/configure_video_device.sh ${D}${bindir}/configure_video_device
	install -m 0755 ${S}/configure_isp_vek385.sh ${D}${bindir}/configure_isp_vek385
	install -m 0755 ${S}/start_stream_0_1.sh ${D}${bindir}/start_stream_0_1
	install -m 0755 ${S}/start_stream_2_3.sh ${D}${bindir}/start_stream_2_3
	install -m 0755 ${S}/start_video_bbb.sh ${D}${bindir}/start_video_bbb
	install -m 0755 ${S}/start_weston.sh ${D}${bindir}/start_weston
	install -m 0755 ${S}/transcode_video_bbb.sh ${D}${bindir}/transcode_video_bbb
	install -m 0755 ${S}/configure_isp_rave2.sh ${D}${bindir}/configure_isp_rave2
	install -d ${D}${datadir}
	install -m 0644 ${S}/bbb_1080p_normal_2min_444_8bit.mp4 ${D}${datadir}
}

FILES:${PN} = "${datadir}/* ${bindir}/*"