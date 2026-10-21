#!/bin/bash
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: TO
# Maintainer: TO

# config-camera-tpg.sh - Configure the Test Pattern Generator (TPG)
# camera source by launching the vivid V4L2 setup.

set -euo pipefail

echo "config-camera-tpg.sh - configuring Test Pattern Generator sources"
ep_vivid_setup.sh 4 1792 1080 NV12 30
