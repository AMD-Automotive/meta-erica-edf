#/bin/sh
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

gst-launch-1.0 -v -e \
v4l2src device=/dev/video0 ! video/x-raw,width=1928,height=1208,framerate=30/1 ! queue ! videoscale ! videoconvert ! video/x-raw,width=640,height=480,format=BGRA ! waylandsink sync=false \
v4l2src device=/dev/video1 ! video/x-raw,width=1928,height=1208,framerate=30/1 ! queue ! videoscale ! videoconvert ! video/x-raw,width=640,height=480,format=BGRA ! waylandsink sync=false \
v4l2src device=/dev/video2 ! video/x-raw,width=1928,height=1208,framerate=30/1 ! queue ! videoscale ! videoconvert ! video/x-raw,width=640,height=480,format=BGRA ! waylandsink sync=false \
v4l2src device=/dev/video3 ! video/x-raw,width=1928,height=1208,framerate=30/1 ! queue ! videoscale ! videoconvert ! video/x-raw,width=640,height=480,format=BGRA ! waylandsink sync=false 