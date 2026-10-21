#!/bin/sh
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

echo 1 > /sys/module/rcupdate/parameters/rcu_cpu_stall_suppress
export XRT_AIARM=true
export LD_LIBRARY_PATH=/usr/lib/python3.12/site-packages/voe/lib/:/usr/lib/python3.12/site-packages/flexmlrt/lib/
export XLNX_ENABLE_CACHE=0
export XRT_ELF_FLOW=1

model-app.elf resnet50_quantized_xint8.onnx input0.bin output0.bin
