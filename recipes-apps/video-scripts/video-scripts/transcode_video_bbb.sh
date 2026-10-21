#/bin/sh
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, LT
# Maintainer: LT

ECP_VER=`grep '^meta-amd-edf[[:space:]]*=' /etc/buildinfo | cut -d= -f2 | cut -d: -f1 | xargs`

mkdir -p ~/video
pushd ~/video

# Display output only for 2025.1
if [[ $ECP_VER = "rel-v2025.2" || $ECP_VER = "rel-v2026.1" ]]; then
  gst-launch-1.0 filesrc location=/usr/share/bbb_1080p_normal_2min_444_8bit.mp4 ! qtdemux ! h264parse ! omxh264dec low-latency=0 ! queue max-size-bytes=0 ! tee name=t ! queue ! omxh264enc qp-mode=auto gop-mode=basic gop-length=60 target-bitrate=30000 control-rate=constant prefetch-buffer=true ! video/x-h264, alignment=au ! queue max-size-bytes=0 ! filesink location=./transcode_yuv4448bit.avc
else
  gst-launch-1.0 filesrc location=/usr/share/bbb_1080p_normal_2min_444_8bit.mp4 ! qtdemux ! h264parse ! omxh264dec low-latency=0 ! queue max-size-bytes=0 ! tee name=t ! queue ! omxh264enc qp-mode=auto gop-mode=basic gop-length=60 target-bitrate=30000 control-rate=constant prefetch-buffer=true ! video/x-h264, alignment=au ! queue max-size-bytes=0 ! filesink location=./transcode_yuv4448bit.avc t. ! queue ! kmssink plane-id=36 bus-id=edd00000.mmi_dc -v
fi
