# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, BW
# Maintainer: BW

DESCRIPTION = "Rave2 test image"

require edf-image-common.inc
require rootfs_id.inc

IMAGE_FSTYPES = "wic wic.ufs tar.gz"

# Common packages across build versions

IMAGE_INSTALL = "\
    packagegroup-core-boot \
    packagegroup-core-full-cmdline \
    ${AMD-EDF_IMAGE_FULL_INSTALL} \
    ptest-runner \
    packagegroup-meta-python3-tests \
    packagegroup-security-tpm2 \
    python3-mutablerecords \
    python3-openhtf \
    python3-tornado \
    python3-netifaces \
    python3-sockjs \
    python3-asn1crypto \
    python3-pip\
    python3-multiprocessing \
    python3-numpy \
    python3-shell \
    python3-threading \
    python3-threading \
    python3-pyserial \
    python3-h5py \
    python3-pybind11 \
    python3-graphviz \
    packagegroup-rave2-tests \
    packagegroup-rave2-tests-ptest \
    versal2-power-test-ptest \
    versal2-mmi-pcie-check-ptest \
    manufacturing-ptests \
    ufs-utils \
    fio \
    hdparm \
    blktrace \
    strace \
    util-linux \
    ssh-rootlogin \
    rsync \
    avahi-daemon \
    rave2-networkd-conf \
    stress-ng \
    ep-proxy \
"

IMAGE_INSTALL:append:amdtool-2026_1 = " ${UBOOT_BOOT_SCRIPT}"

WKS_FILES = "erica-default-sd.wks"

EXTRA_IMAGE_FEATURES:append = " \
    empty-root-password \
    allow-empty-password \
    serial-autologin-root \
    ssh-server-openssh \
    "

inherit core-image

# Disable NTP for manufacturing — avoids clock jumps during timed stress tests
disable_timesyncd() {
    ln -sf /dev/null ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/systemd-timesyncd.service
}
ROOTFS_POSTPROCESS_COMMAND:append = " disable_timesyncd;"

do_rootfs[prefuncs] += "edf_check_rootfs"
