#!/bin/sh
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

BOARD=$(tr -d '\0' < /proc/device-tree/model)
VIVADO="20252"

echo $BOARD

if [ "VEK385 ISP - AMD Versal Gen 2" != "$BOARD" ]; then
        echo "This script is for VEK385 with ISP, exiting..."
        exit
fi

if [ "$(grep -Foc 'rel-v2026.1' /etc/buildinfo)" -gt 10 ]; then
	echo "RootFS is 2026.1"
	VIVADO="20261"
else
	echo "RootFS is 2025.2"
fi

echo "Setting up "
echo "  DES 1,2 for IMX623"
echo "  DES 3,4 for IMX728"
echo "  DES 5,6 for IMX728"
echo "  DES 7 for OX05B1S"

echo isp-r52-6-firmware.elf  > /sys/class/remoteproc/remoteproc1/firmware
echo isp-r52-7-firmware.elf  > /sys/class/remoteproc/remoteproc2/firmware
echo isp-r52-8-firmware.elf  > /sys/class/remoteproc/remoteproc3/firmware

sleep .25
echo start > /sys/class/remoteproc/remoteproc1/state
sleep .5
echo start > /sys/class/remoteproc/remoteproc2/state
sleep .5
echo start > /sys/class/remoteproc/remoteproc3/state
sleep .5


KERNELVER=`uname -r`
insmod /lib/modules/$KERNELVER/updates/visp_mbox/visp_mbox.ko
insmod /lib/modules/$KERNELVER/updates/visp/visp.ko
insmod /lib/modules/$KERNELVER/updates/visp_video/visp_video.ko
sleep .25

echo 0 hw_mcm=1 mode=2 sensor=imx623 sensor_id=0 > /proc/vsi/isp_subdev0
echo 1 hw_mcm=1 mode=2 sensor=imx623 sensor_id=1 > /proc/vsi/isp_subdev0
echo 2 hw_mcm=1 mode=2 sensor=imx623 sensor_id=2 > /proc/vsi/isp_subdev0
echo 3 hw_mcm=1 mode=2 sensor=imx623 sensor_id=3 > /proc/vsi/isp_subdev0

echo 0 sensor=imx728 sensor_id=8 > /proc/vsi/isp_subdev1

echo 0 vc_id=2 hw_mcm=1 mode=1 sensor=ox05b1s sensor_id=11 > /proc/vsi/isp_subdev5
echo 1 vc_id=3 hw_mcm=1 mode=2 sensor=ox05b1s sensor_id=11 > /proc/vsi/isp_subdev5

echo 0 sensor=imx728 sensor_id=4 > /proc/vsi/isp_subdev2
echo 1 sensor=imx728 sensor_id=5 > /proc/vsi/isp_subdev2
echo 2 sensor=imx728 sensor_id=6 > /proc/vsi/isp_subdev2
echo 3 sensor=imx728 sensor_id=7 > /proc/vsi/isp_subdev2

if [ "$VIVADO" = "20252" ]; then
	echo 0 xml=/usr/share/isp-support/IMX623/IMX623_ewd_2026_M15_mode2.xml  > /proc/vsi/isp_subdev0
	echo 0 auto_json=/usr/share/isp-support/IMX623/auto_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev0
	echo 0 manu_json=/usr/share/isp-support/IMX623/manual_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev0

	echo 1 xml=/usr/share/isp-support/IMX623/IMX623_ewd_2026_M15_mode2.xml  > /proc/vsi/isp_subdev0
	echo 1 auto_json=/usr/share/isp-support/IMX623/auto_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev0
	echo 1 manu_json=/usr/share/isp-support/IMX623/manual_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev0

	echo 2 xml=/usr/share/isp-support/IMX623/IMX623_ewd_2026_M15_mode2.xml  > /proc/vsi/isp_subdev0
	echo 2 auto_json=/usr/share/isp-support/IMX623/auto_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev0
	echo 2 manu_json=/usr/share/isp-support/IMX623/manual_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev0

	echo 3 xml=/usr/share/isp-support/IMX623/IMX623_ewd_2026_M15_mode2.xml  > /proc/vsi/isp_subdev0
	echo 3 auto_json=/usr/share/isp-support/IMX623/auto_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev0
	echo 3 manu_json=/usr/share/isp-support/IMX623/manual_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev0

	echo 0 xml=/usr/share/isp-support/IMX728/IMX728_ewd_2026_M15.xml  > /proc/vsi/isp_subdev1
	echo 0 auto_json=/usr/share/isp-support/IMX728/auto_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev1
	echo 0 manu_json=/usr/share/isp-support/IMX728/manual_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev1

	echo 0 xml=/usr/share/isp-support/OX05B1S/OX05B1S_ewd_2026_M15.xml  > /proc/vsi/isp_subdev5
	echo 0 auto_json=/usr/share/isp-support/OX05B1S/auto_OX05B1S_ewd_2026_M15.json  > /proc/vsi/isp_subdev5
	echo 0 manu_json=/usr/share/isp-support/OX05B1S/manual_OX05B1S_ewd_2026_M15.json  > /proc/vsi/isp_subdev5

	echo 1 xml=/usr/share/isp-support/OX05B1S/OX05B1S_ewd_2026_M15.xml  > /proc/vsi/isp_subdev5
	echo 1 auto_json=/usr/share/isp-support/OX05B1S/auto_OX05B1S_bmode_ewd_2026_M15.json  > /proc/vsi/isp_subdev5
	echo 1 manu_json=/usr/share/isp-support/OX05B1S/manual_OX05B1S_bmode_ewd_2026_M15.json  > /proc/vsi/isp_subdev5

	echo 0 xml=/usr/share/isp-support/IMX728/IMX728_ewd_2026_M15.xml  > /proc/vsi/isp_subdev2
	echo 0 auto_json=/usr/share/isp-support/IMX728/auto_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev2
	echo 0 manu_json=/usr/share/isp-support/IMX728/manual_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev2

	echo 1 xml=/usr/share/isp-support/IMX728/IMX728_ewd_2026_M15.xml  > /proc/vsi/isp_subdev2
	echo 1 auto_json=/usr/share/isp-support/IMX728/auto_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev2
	echo 1 manu_json=/usr/share/isp-support/IMX728/manual_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev2

	echo 2 xml=/usr/share/isp-support/IMX728/IMX728_ewd_2026_M15.xml  > /proc/vsi/isp_subdev2
	echo 2 auto_json=/usr/share/isp-support/IMX728/auto_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev2
	echo 2 manu_json=/usr/share/isp-support/IMX728/manual_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev2

	echo 3 xml=/usr/share/isp-support/IMX728/IMX728_ewd_2026_M15.xml  > /proc/vsi/isp_subdev2
	echo 3 auto_json=/usr/share/isp-support/IMX728/auto_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev2
	echo 3 manu_json=/usr/share/isp-support/IMX728/manual_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev2


elif [ "$VIVADO" = "20261" ]; then
	echo 0 calib=/usr/share/Tuning_files/IMX623/1920/IMX623_1920x1536_v1.0.json  > /proc/vsi/isp_subdev0
	echo 0 auto_json=/usr/share/Tuning_files/IMX623/1920/auto_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev0
	echo 0 manu_json=/usr/share/Tuning_files/IMX623/1920/manual_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev0

	echo 1 calib=/usr/share/Tuning_files/IMX623/1920/IMX623_1920x1536_v1.0.json  > /proc/vsi/isp_subdev0
	echo 1 auto_json=/usr/share/Tuning_files/IMX623/1920/auto_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev0
	echo 1 manu_json=/usr/share/Tuning_files/IMX623/1920/manual_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev0

	echo 2 calib=/usr/share/Tuning_files/IMX623/1920/IMX623_1920x1536_v1.0.json  > /proc/vsi/isp_subdev0
	echo 2 auto_json=/usr/share/Tuning_files/IMX623/1920/auto_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev0
	echo 2 manu_json=/usr/share/Tuning_files/IMX623/1920/manual_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev0

	echo 3 calib=/usr/share/Tuning_files/IMX623/1920/IMX623_1920x1536_v1.0.json  > /proc/vsi/isp_subdev0
	echo 3 auto_json=/usr/share/Tuning_files/IMX623/1920/auto_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev0
	echo 3 manu_json=/usr/share/Tuning_files/IMX623/1920/manual_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev0

	echo 0 calib=/usr/share/Tuning_files/IMX728/IMX728_v1.0.json  > /proc/vsi/isp_subdev1
	echo 0 auto_json=/usr/share/Tuning_files/IMX728/auto_IMX728_v1.0.json  > /proc/vsi/isp_subdev1
	echo 0 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.0.json  > /proc/vsi/isp_subdev1

	echo 0 calib=/usr/share/Tuning_files/OX05B1S/OX05B1S_v1.0.json  > /proc/vsi/isp_subdev5
	echo 0 auto_json=/usr/share/Tuning_files/OX05B1S/auto_OX05B1S_v1.0.json  > /proc/vsi/isp_subdev5
	echo 0 manu_json=/usr/share/Tuning_files/OX05B1S/manual_OX05B1S_v1.0.json  > /proc/vsi/isp_subdev5

	echo 1 calib=/usr/share/Tuning_files/OX05B1S/OX05B1S_v1.0.json  > /proc/vsi/isp_subdev5
	echo 1 auto_json=/usr/share/Tuning_files/OX05B1S/auto_OX05B1S_v1.0.json  > /proc/vsi/isp_subdev5
	echo 1 manu_json=/usr/share/Tuning_files/OX05B1S/manual_OX05B1S_v1.0.json  > /proc/vsi/isp_subdev5

	echo 0 calib=/usr/share/Tuning_files/IMX728/IMX728_v1.0.json  > /proc/vsi/isp_subdev2
	echo 0 auto_json=/usr/share/Tuning_files/IMX728/auto_IMX728_v1.0.json  > /proc/vsi/isp_subdev2
	echo 0 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.0.json  > /proc/vsi/isp_subdev2

	echo 1 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.0.json  > /proc/vsi/isp_subdev2
	echo 1 auto_json=/usr/share/Tuning_files/IMX728/auto_IMX728_v1.0.json  > /proc/vsi/isp_subdev2
	echo 1 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.0.json  > /proc/vsi/isp_subdev2

	echo 2 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.0.json  > /proc/vsi/isp_subdev2
	echo 2 auto_json=/usr/share/Tuning_files/IMX728/auto_IMX728_v1.0.json  > /proc/vsi/isp_subdev2
	echo 2 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.0.json  > /proc/vsi/isp_subdev2

	echo 3 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.0.json  > /proc/vsi/isp_subdev2
	echo 3 auto_json=/usr/share/Tuning_files/IMX728/auto_IMX728_v1.0.json  > /proc/vsi/isp_subdev2
	echo 3 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.0.json  > /proc/vsi/isp_subdev2

	echo 1 > /sys/module/visp/parameters/fmc_id
else
	echo "Vivado version not detected"
fi

isp_media_server&
sleep 1
