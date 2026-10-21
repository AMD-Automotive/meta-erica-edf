# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT, KC, BW, JC
# Maintainer: LT

FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " \
    file://bsp.cfg \
    file://0001-xilinx-dummy_src-and-switch-Insert-driver.patch \
    file://0001-media-vivid-force-non-coherent-CMA-buffers-for-dma-c.patch \
    \
"

# linux-xlnx provenance for Cadence 1GE flow-control patches:
# 495a2bbbcc49 net: macb: Add RX pause frame support and ethtool pause controls
# 6556503dd049 net: macb: Add TX pause frame support
SRC_URI:append:amdtool-2025_2 = " \
    file://0001-drm-xlnx-mmi-dc-Support-PRIME-cursor-buffers.patch \
    file://0015-dmaengine-mmi-dcdma-Submit-buffer-flip-transfer-imme.patch \
    file://0001-net-macb-Add-RX-pause-frame-support-and-ethtool-pause-controls.patch \
    file://0002-net-macb-Add-TX-pause-frame-support.patch \
    \
"

SRC_URI:append:amdtool-2026_1 = " \
    file://bsp-2026.1.cfg \
    file://0001-remoteproc-xlnx-introduce-new-fw-elf-loader.patch \
    file://0001-net-macb-flush-the-TX-rings-on-resume.patch \
    file://0003-spi-cadence-quadspi-bound-periodic-tuning-wait-to-fix.patch \
"


KERNEL_CONFIG_FRAGMENTS += "bsp.cfg"

KERNEL_CONFIG_FRAGMENTS:append:amdtool-2026_1 = " bsp-2026.1.cfg"

# Force the kernel to rebuild on every bitbake run (never restore compile /
# packaging from sstate). This guarantees the packaged kernel PROVIDES
# (kernel-<abiversion>, e.g. kernel-6.12.40-xilinx-g<sha>) is always
# regenerated from the current source, so it matches freshly-built
# out-of-tree kernel modules. Prevents do_rootfs "nothing provides
# kernel-...-g<sha>" version-skew when a stale kernel package is otherwise
# reused from the shared sstate cache while modules are rebuilt.
do_compile[nostamp] = "1"
do_shared_workdir[nostamp] = "1"
do_package[nostamp] = "1"
do_package_write_rpm[nostamp] = "1"
