#!/bin/bash
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: TO
# Maintainer: TO

# config-camera-quanta_imx623.sh - Configure Quanta Sony IMX623 cameras.

set -euo pipefail

echo "config-camera-quanta_imx623.sh - configuring Quanta Sony IMX623 cameras"
init_cameras_minimal.sh -m dmabuf -c 1 -i 1 -t quanta
echo "config-camera-quanta_imx623.sh - complete"
