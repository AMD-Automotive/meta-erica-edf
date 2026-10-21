# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, LT, BW, JC
# Maintainer: LT

DESCRIPTION = "ERICA-H Application Image"

require edf-image-common.inc
require rootfs_id.inc

IMAGE_FEATURES += "ssh-server-openssh package-management"
# wic.ufs.zst is what buddy transfers; wic.ufs.bmap lets it skip all-zero chunks.
IMAGE_FSTYPES = "wic tar.gz wic.ufs wic.ufs.zst wic.ufs.bmap"

# Common packages across build versions

IMAGE_INSTALL = "\
    packagegroup-vaiml \
    packagegroup-core-boot \
    packagegroup-core-full-cmdline \
    ${AMD-EDF_IMAGE_FULL_INSTALL} \
    packagegroup-xen \    
    devmem2 \
    mesa-demos \
    packagegroup-base \
    packagegroup-core-boot \
    packagegroup-opencv \
    tcpdump \
    wireshark \
    packagegroup-networking-stack \
    python3-pip\
    python3-multiprocessing \
    python3-numpy \
    python3-shell \
    python3-threading \
    python3-threading \
    python3-pyserial \
    python3-h5py \
    util-linux \
    cpufrequtils \
    smartmontools \
    e2fsprogs \
    packagegroup-lmsensors \
    packagegroup-xilinx-benchmarks \
    packagegroup-self-hosted \
    ${@bb.utils.contains('DISTRO_FEATURES', 'x11 opengl', 'packagegroup-self-hosted', '', d)} \
    ${@bb.utils.contains('DISTRO_FEATURES', 'x11', 'packagegroup-core-x11', '', d)} \
    ${@bb.utils.contains('DISTRO_FEATURES', 'x11', 'packagegroup-xilinx-multimedia', '', d)} \
    ${@bb.utils.contains('DISTRO_FEATURES', 'wayland', 'packagegroup-core-weston', '', d)} \
    ${@bb.utils.contains('DISTRO_FEATURES', 'xen', 'packagegroup-xen', '', d)} \
    bridge-utils \
    nfs-utils \
    nfs-utils-client \
    ${CORE_IMAGE_EXTRA_INSTALL} \
    meson \
    u-boot-tools \
    u-boot-tools-xlnx \
    ${@'libdfx' if 'xilinx-tools' in d.getVar('BBFILE_COLLECTIONS').split() else ''} \
    ${@bb.utils.contains('DISTRO_FEATURES', 'virtualization vmsep', ' packagegroup-container docker-compose', '', d)} \
    ltp \
    ttf-bitstream-vera \
    packagegroup-core-full-cmdline \
    python3-pybind11 \
    python3-graphviz \
    bootgen \
    wolfssl \
    kernel-devsrc \
    lopper \
    packagegroup-xilinx-jupyter \
    packagegroup-xilinx-ros \
    packagegroup-tsn \
    valgrind \
    packagegroup-xilinx-qt \
    packagegroup-vitis-aiml \
    memtester \
    dnf \
    weston-examples \
    ninja \
    cmake \
    packagegroup-self-hosted \
    git \
    util-linux-dev \
    wayland-protocols \
    wayland-tools \
    ssh-rootlogin \
    video-scripts \
    video-config \
    plf-vek385 \
    plf-rave2 \
    opencl-image-resize \
    opencv-dev \
    camera-app-ar0231-4k-display \
    gator-daemon \
    mlpp-kernel-driver \
    ecp-oob-examples \
    vart-ml-dev \
    vart-x-dev \
    ryzenai-wheels-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev \
    ffmpeg-dev \
    extra-cmake-modules-dev \
    yaml-cpp-dev \
    usbutils \
    usbutils-python \
    hwdata \
    libusb1 \
    usbip-tools \
    usb-modeswitch \
    iozone3 \
    bonnie++ \
    pciutils \
    libusbgx \
    perf \
    util-linux \
    ethtool \
    tcpdump \
    lshw \
    iw \
    sysstat \
    i2c-tools \
    strace \
    ltrace \
    mdio-tools \
    libavcodec \
    libavdevice  \
    libavfilter \
    libavformat \
    libavutil \
    libswresample \
    libswscale \
    ep-proxy \
    pcie-video-tester \
    chrony \
    pcie-camera-passthrough-simple \
    "

# Images only applicable for 2025.2 rootfs builds
IMAGE_INSTALL:append:amdtool-2025_2 = " \
    gstreamer1.0-libav \
    environment \
    autostart \
    isp-support \
    vaiml-models \
    mdb5-video-test \
"

# Images only applicable for 2026.1 rootfs builds
IMAGE_INSTALL:append:amdtool-2026_1 = " \
    gstreamer1.0-libav \
    isp-support \
    isp-media-server \
    environment \
    vaiml-models \
    mdb5-video-test \
"

# The generic image machine (amd-cortexa78-mali-common) clears IMAGE_BOOT_FILES,
# which suppresses the automatic pull-in of ${UBOOT_BOOT_SCRIPT}. Install the
# boot script package (boot.scr + xen_boot.scr) into the rootfs /boot explicitly.
# Scoped to 2026.1 so 2025.2 builds are unaffected.
IMAGE_INSTALL:append:amdtool-2026_1 = " ${UBOOT_BOOT_SCRIPT}"

WKS_FILES = "erica-appdev-ufs.wks"

TOOLCHAIN_HOST_TASK:append = " \
        nativesdk-python3-pip \
        nativesdk-python3-numpy \
        nativesdk-python3-setuptools \
        nativesdk-python3-build \
        nativesdk-python3-wheel \
        nativesdk-python3-protobuf \
        nativesdk-python3-pybind11 \
        nativesdk-protobuf \
"

TOOLCHAIN_TARGET_TASK:append = " \
        ryzenai-wheels-dev \
        opencv-dev \
        jansson-dev \
        vart-x-dev \
        vart-ml-dev \
"
TOOLCHAIN_TARGET_TASK:remove = " \
	onnxrt-headers-dev \
"

EXTRA_IMAGE_FEATURES:append = " \
    empty-root-password \
    serial-autologin-root \
    ssh-server-openssh \
    hwcodecs \
    "

inherit core-image

do_rootfs[prefuncs] += "edf_check_rootfs"
