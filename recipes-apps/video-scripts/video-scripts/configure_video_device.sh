#!/bin/sh
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, LT
# Maintainer: LT


HSIZE1=1928
VSIZE1=1208
FMT=UYVY
FMT2=UYVY
CSI2_RX0_ADDR=b0042000
CSI2_RX1_ADDR=b0040000

# Pipes 0/1 (/dev/media0 -> /dev/video0, /dev/video1)
MEDIA1=/dev/media1
MEDIA0=/dev/media0
media-ctl -d ${MEDIA0} -V '"amba_pl:dummy_src_0@0":0 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA0} -V '"'${CSI2_RX0_ADDR}'.mipi_csi2_rx_subsystem":0 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA0} -V '"'${CSI2_RX0_ADDR}'.mipi_csi2_rx_subsystem":1 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA0} -V '"amba_pl:axis_switch_vcap_pipe0_":1 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA0} -V '"amba_pl:axis_switch_vcap_pipe0_":2 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'

#v4l2-ctl -c mipi_csi2_rx_subsystem_active=4 -d /dev/v4l-subdev2
v4l2-ctl -d /dev/video0 --set-fmt-video=width=${HSIZE1},height=${VSIZE1},pixelformat=UYVY
v4l2-ctl -d /dev/video1 --set-fmt-video=width=${HSIZE1},height=${VSIZE1},pixelformat=UYVY

media-ctl -d ${MEDIA1} -V '"amba_pl:dummy_src_1@0":0 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA1} -V '"'${CSI2_RX1_ADDR}'.mipi_csi2_rx_subsystem":0 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA1} -V '"'${CSI2_RX1_ADDR}'.mipi_csi2_rx_subsystem":1 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA1} -V '"amba_pl:axis_switch_vcap_pipe1_":1 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA1} -V '"amba_pl:axis_switch_vcap_pipe1_":2 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'

v4l2-ctl -d /dev/video2 --set-fmt-video=width=${HSIZE1},height=${VSIZE1},pixelformat=UYVY
v4l2-ctl -d /dev/video3 --set-fmt-video=width=${HSIZE1},height=${VSIZE1},pixelformat=UYVY

