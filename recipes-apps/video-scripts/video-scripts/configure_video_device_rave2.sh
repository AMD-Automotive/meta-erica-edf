#!/bin/sh
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: KC


HSIZE1=1928
VSIZE1=1208
FMT=UYVY
CSI2_RX0_ADDR=b0004000
CSI2_RX1_ADDR=b0006000
CSI2_RX2_ADDR=b0000000

MEDIA0=/dev/media0
media-ctl -d ${MEDIA0} -V '"amba_pl:dummy_src_0@0":0 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA0} -V '"'${CSI2_RX0_ADDR}'.mipi_csi2_rx_subsystem":0 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA0} -V '"'${CSI2_RX0_ADDR}'.mipi_csi2_rx_subsystem":1 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA0} -V '"amba_pl:axis_switch_vcap_pipe2_":1 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA0} -V '"amba_pl:axis_switch_vcap_pipe2_":2 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA0} -V '"amba_pl:axis_switch_vcap_pipe2_":3 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA0} -V '"amba_pl:axis_switch_vcap_pipe2_":4 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'

#v4l2-ctl -c mipi_csi2_rx_subsystem_active=4 -d /dev/v4l-subdev2
v4l2-ctl -d /dev/video0 --set-fmt-video=width=${HSIZE1},height=${VSIZE1},pixelformat=UYVY
v4l2-ctl -d /dev/video1 --set-fmt-video=width=${HSIZE1},height=${VSIZE1},pixelformat=UYVY
v4l2-ctl -d /dev/video2 --set-fmt-video=width=${HSIZE1},height=${VSIZE1},pixelformat=UYVY
v4l2-ctl -d /dev/video3 --set-fmt-video=width=${HSIZE1},height=${VSIZE1},pixelformat=UYVY



MEDIA1=/dev/media1
media-ctl -d ${MEDIA1} -V '"amba_pl:dummy_src_1@0":0 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA1} -V '"'${CSI2_RX1_ADDR}'.mipi_csi2_rx_subsystem":0 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA1} -V '"'${CSI2_RX1_ADDR}'.mipi_csi2_rx_subsystem":1 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA1} -V '"amba_pl:axis_switch_vcap_pipe3_":1 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA1} -V '"amba_pl:axis_switch_vcap_pipe3_":2 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA1} -V '"amba_pl:axis_switch_vcap_pipe3_":3 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA1} -V '"amba_pl:axis_switch_vcap_pipe3_":4 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'

#v4l2-ctl -c mipi_csi2_rx_subsystem_active=4 -d /dev/v4l-subdev2
v4l2-ctl -d /dev/video4 --set-fmt-video=width=${HSIZE1},height=${VSIZE1},pixelformat=UYVY
v4l2-ctl -d /dev/video5 --set-fmt-video=width=${HSIZE1},height=${VSIZE1},pixelformat=UYVY
v4l2-ctl -d /dev/video6 --set-fmt-video=width=${HSIZE1},height=${VSIZE1},pixelformat=UYVY
v4l2-ctl -d /dev/video7 --set-fmt-video=width=${HSIZE1},height=${VSIZE1},pixelformat=UYVY



MEDIA2=/dev/media2
media-ctl -d ${MEDIA2} -V '"amba_pl:dummy_src_2@0":0 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA2} -V '"'${CSI2_RX2_ADDR}'.mipi_csi2_rx_subsystem":0 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA2} -V '"'${CSI2_RX2_ADDR}'.mipi_csi2_rx_subsystem":1 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA2} -V '"amba_pl:axis_switch_vcap_pipe1_":1 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA2} -V '"amba_pl:axis_switch_vcap_pipe1_":2 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA2} -V '"amba_pl:axis_switch_vcap_pipe1_":3 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'
media-ctl -d ${MEDIA2} -V '"amba_pl:axis_switch_vcap_pipe1_":4 [fmt:'${FMT}'/'${HSIZE1}x${VSIZE1}' field:none]'

#v4l2-ctl -c mipi_csi2_rx_subsystem_active=4 -d /dev/v4l-subdev2
v4l2-ctl -d /dev/video8 --set-fmt-video=width=${HSIZE1},height=${VSIZE1},pixelformat=UYVY
v4l2-ctl -d /dev/video9 --set-fmt-video=width=${HSIZE1},height=${VSIZE1},pixelformat=UYVY
v4l2-ctl -d /dev/video10 --set-fmt-video=width=${HSIZE1},height=${VSIZE1},pixelformat=UYVY
v4l2-ctl -d /dev/video11 --set-fmt-video=width=${HSIZE1},height=${VSIZE1},pixelformat=UYVY
