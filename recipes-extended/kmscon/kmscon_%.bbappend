# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

# Do not start the kmscon KMS virtual terminal (kmsconvt@tty1) on boot.

do_install:append:amdtool-2026_1() {
    # Remove the upstream boot-time enable symlink
    rm -f ${D}${sysconfdir}/systemd/system/getty.target.wants/kmsconvt@tty1.service

    # Mask so nothing (getty.target, udev hotplug) can start it
    install -d ${D}${sysconfdir}/systemd/system
    ln -sf /dev/null ${D}${sysconfdir}/systemd/system/kmsconvt@tty1.service
}

FILES:${PN}:append:amdtool-2026_1 += "${sysconfdir}/systemd/system/kmsconvt@tty1.service"
