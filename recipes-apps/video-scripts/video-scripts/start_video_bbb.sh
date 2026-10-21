#/bin/sh
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, LT
# Maintainer: LT

pushd ~/video
gst-launch-1.0 -v filesrc location=transcode_yuv4448bit.avc ! h264parse ! omxh264dec ! video/x-raw,width=1920,height=1080 ! identity drop-allocation=true ! videoscale ! videoconvert ! video/x-raw,width=640,height=480,format=BGRA ! waylandsink sync=false
popd
