# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: IC, KC, BW
# Maintainer: KC

SUMMARY = "RAVE2 ptest packagegroups"

inherit packagegroup

PROVIDES = "${PACKAGES}"
PACKAGES = ' \
    packagegroup-rave2-tests \
'
 
RDEPENDS:packagegroup-rave2-tests = "\
    can-test \
    cpu-test \
    usb-test \
    iperf-test \
    iperf10g-test \
    memory-test \
    dp-test \
    ethernet-test \
    versal2-mmi-pcie-check \
    ufs-test \
    swprom-test \
    tpm-test \
    pmod-test \
    mdb5-video-test \
"


RDEPENDS:packagegroup-rave2-tests-ptest = "\
    can-test-ptest \
    usb-test-ptest \
    cpu-test-ptest \
    memory-test-ptest \
    iperf-test-ptest \
    iperf10g-test-ptest \
    dp-test-ptest \
    ethernet-test-ptest \
    rave2-i2c-iob-tests-ptest \
    ufs-test-ptest \
    swprom-test-ptest \
    tpm-test-ptest \
    pmod-test-ptest \
"
EXCLUDE_FROM_WORLD = "1"

