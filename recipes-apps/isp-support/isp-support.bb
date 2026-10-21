# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

SUMMARY = "ISP sensor configuration and support files"
DESCRIPTION = "Configuration files for IMX623, IMX728, and OX05B1S image sensors, and ISP media server"
LICENSE = "CLOSED"

SRC_URI = "\
    https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/2026_06_16_media_server.tar.gz;name=media-server \
    https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/isp-firmware/2026_06_16_media_server_modified_json_xml.tar.gz;name=sensor-config \
"

# Checksum for media_server.tar.gz - update after first download
SRC_URI[media-server.sha256sum] = "084d143d8f84eacbd852f43e307230f01de7ca208851a329ce35646e818d25fe"

# Checksum for 2026_06_16_media_server_modified_json_xml.tar.gz - update after first download
SRC_URI[sensor-config.sha256sum] = "9b978035de3a4bb7eb519474e11fc7d5a21550a043106539231dfbec476109a7"

S = "${WORKDIR}"

# Skip architecture checks for pre-built binaries
INSANE_SKIP:${PN} = "arch already-stripped ldflags dev-so dev-elf"
INSANE_SKIP:${PN}-dev = "dev-elf ldflags"

# Prevent libraries from being split into -dev package
SOLIBS = ".so"
FILES_SOLIBSDEV = ""

do_install() {
    # Install IMX623 sensor files
    install -d ${D}${datadir}/isp-support/IMX623
    install -m 0644 ${WORKDIR}/IMX623/auto_IMX623_ewd_2026_M15.json ${D}${datadir}/isp-support/IMX623/
    install -m 0644 ${WORKDIR}/IMX623/IMX623_ewd_2026_M15.xml ${D}${datadir}/isp-support/IMX623/
    install -m 0644 ${WORKDIR}/IMX623/IMX623_ewd_2026_M15_mode2.xml ${D}${datadir}/isp-support/IMX623/
    install -m 0644 ${WORKDIR}/IMX623/manual_IMX623_ewd_2026_M15.json ${D}${datadir}/isp-support/IMX623/

    # Install IMX728 sensor files
    install -d ${D}${datadir}/isp-support/IMX728
    install -m 0644 ${WORKDIR}/IMX728/auto_IMX728_ewd_2026_M15.json ${D}${datadir}/isp-support/IMX728/
    install -m 0644 ${WORKDIR}/IMX728/IMX728_ewd_2026_M15.xml ${D}${datadir}/isp-support/IMX728/
    install -m 0644 ${WORKDIR}/IMX728/manual_IMX728_ewd_2026_M15.json ${D}${datadir}/isp-support/IMX728/

    # Install OX05B1S sensor files
    install -d ${D}${datadir}/isp-support/OX05B1S
    install -m 0644 ${WORKDIR}/OX05B1S/auto_OX05B1S_bmode_ewd_2026_M15.json ${D}${datadir}/isp-support/OX05B1S/
    install -m 0644 ${WORKDIR}/OX05B1S/auto_OX05B1S_ewd_2026_M15.json ${D}${datadir}/isp-support/OX05B1S/
    install -m 0644 ${WORKDIR}/OX05B1S/manual_OX05B1S_bmode_ewd_2026_M15.json ${D}${datadir}/isp-support/OX05B1S/
    install -m 0644 ${WORKDIR}/OX05B1S/manual_OX05B1S_ewd_2026_M15.json ${D}${datadir}/isp-support/OX05B1S/
    install -m 0644 ${WORKDIR}/OX05B1S/OX05B1S_ewd_2026_M15.xml ${D}${datadir}/isp-support/OX05B1S/
}

# The 2025.2 LIMO media server ships here for every build EXCEPT 2026.1, where
# meta-xilinx's isp-media-server package provides /usr/bin/isp_media_server
# (installing both would collide).
INSTALL_LIMO_MEDIA_SERVER = "1"
INSTALL_LIMO_MEDIA_SERVER:amdtool-2026_1 = "0"

do_install:append() {
    if [ "${INSTALL_LIMO_MEDIA_SERVER}" = "1" ]; then
        install -d ${D}${bindir}
        install -m 0755 ${WORKDIR}/isp_media_server ${D}${bindir}/
        install -d ${D}${libdir}
        install -m 0644 ${WORKDIR}/lib/lib* ${D}${libdir}/
    fi
}

FILES:${PN} = "\
    ${datadir}/isp-support/IMX623/* \
    ${datadir}/isp-support/IMX728/* \
    ${datadir}/isp-support/OX05B1S/* \
    ${bindir}/isp_media_server \
    ${libdir}/lib* \
"
FILES:${PN}:remove:amdtool-2026_1 = "${bindir}/isp_media_server ${libdir}/lib*"

# Runtime dependencies
RDEPENDS:${PN} += "libstdc++"
