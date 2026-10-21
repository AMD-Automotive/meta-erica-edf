# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: IC, KC
# Maintainer: KC

SUMMARY = "Meta-python ptest packagegroups"

inherit packagegroup

PROVIDES = "${PACKAGES}"
PACKAGES = ' \
    packagegroup-meta-python3-tests \
'

# Note that python3-cvxopt requires Fortran support. To enable this,
# add the following to your local.conf:
# FORTRAN:forcevariable = ",fortran"
RDEPENDS:packagegroup-meta-python3-tests = "\
    python3-contextlib2 \
    python3-aiofiles \
    python3-aiohttp \
    python3-aiohttp-jinja2 \
    python3-aiohue \
    python3-aiosignal \
"


EXCLUDE_FROM_WORLD = "1"
