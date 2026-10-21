#!/bin/bash
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: TO
# Maintainer: TO

# config-camera-leopard_imx623.sh - Configure Leopard Imaging Sony IMX623 cameras.

set -euo pipefail

echo "config-camera-leopard_imx623.sh - configuring Leopard Imaging Sony IMX623 cameras"
init_cameras_minimal.sh -m dmabuf -c 1 -i 1 -t leopard
echo "config-camera-leopard_imx623.sh - complete"
