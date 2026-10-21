#!/bin/sh
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

BOARD=$(tr -d '\0' < /proc/device-tree/model)
VIVADO="20252"


echo $BOARD

if [ "RAVE2 ECP - AMD Versal Gen 2" != "$BOARD" ]; then
	echo "This script is for RAVE2 with ISP, exiting..."
	exit
fi

if [ "$(grep -Foc 'rel-v2026.1' /etc/buildinfo)" -gt 10 ]; then
	echo "RootFS is 2026.1"
	VIVADO="20261"
else
	echo "RootFS is 2025.2"
fi

PHY0="imx623"
PHY1="imx623"
PHY2="imx623"

case "$1" in
    0)
        echo "Using configuration 0: PHY0,1,2 set for IMX623 cameras"
        ;;
    1)
        echo "Using configuration 1: PHY0,1 set for IMX623 cameras, PHY2 set for imx728"
		PHY2="imx728"
        ;;
    2)
        echo "Using configuration 2: PHY0 set for IMX623 cameras, PHY1,2 set for imx728"
		PHY1="imx728"
		PHY2="imx728"
        ;;
    3)
        echo "Using configuration 3: PHY0,1,2 set for IMX728 cameras"
		PHY0="imx728"
		PHY1="imx728"
		PHY2="imx728"
        ;;
    *)
        echo "No input, set defaults: '$1' (expected 0, 1, 2, or 3)"
		echo "Using configuration 0: PHY0,1,2 set for IMX623 cameras"
        ;;
esac

if [ "$2" = "quanta" ]; then
	echo "Using Quanta firmware variant"
	echo isp-r52-6-firmware-rave2-quanta.elf  > /sys/class/remoteproc/remoteproc1/firmware
	echo isp-r52-7-firmware-rave2-quanta.elf  > /sys/class/remoteproc/remoteproc2/firmware
	echo isp-r52-8-firmware-rave2-quanta.elf  > /sys/class/remoteproc/remoteproc3/firmware
elif [ "$2" = "sim" ]; then
	echo "Using Simulator firmware variant"
	echo isp-r52-6-firmware-rave2-sim.elf  > /sys/class/remoteproc/remoteproc1/firmware
	echo isp-r52-7-firmware-rave2-sim.elf  > /sys/class/remoteproc/remoteproc2/firmware
	echo isp-r52-8-firmware-rave2-sim.elf  > /sys/class/remoteproc/remoteproc3/firmware
else
	echo "Using Leopard firmware variant"
	echo isp-r52-6-firmware-rave2.elf  > /sys/class/remoteproc/remoteproc1/firmware
	echo isp-r52-7-firmware-rave2.elf  > /sys/class/remoteproc/remoteproc2/firmware
	echo isp-r52-8-firmware-rave2.elf  > /sys/class/remoteproc/remoteproc3/firmware
fi

echo start > /sys/class/remoteproc/remoteproc1/state
sleep .5
echo start > /sys/class/remoteproc/remoteproc2/state
sleep .5
echo start > /sys/class/remoteproc/remoteproc3/state

KERNELVER=`uname -r`
insmod /lib/modules/$KERNELVER/updates/visp_mbox/visp_mbox.ko
insmod /lib/modules/$KERNELVER/updates/visp/visp.ko
insmod /lib/modules/$KERNELVER/updates/visp_video/visp_video.ko


if [ "$PHY0" = "imx623" ]; then
	echo "Configuring PHY0 for imx623"
	echo 0 hw_mcm=1 mode=2 sensor=imx623 sensor_id=0 > /proc/vsi/isp_subdev0
	echo 1 hw_mcm=1 mode=2 sensor=imx623 sensor_id=1 > /proc/vsi/isp_subdev0
	echo 2 hw_mcm=1 mode=2 sensor=imx623 sensor_id=2 > /proc/vsi/isp_subdev0
	echo 3 hw_mcm=1 mode=2 sensor=imx623 sensor_id=3 > /proc/vsi/isp_subdev0
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

	else
		echo "Vivado version not detected"
	fi
else # imx728
	echo "Configuring PHY0 for imx728"
	echo 0 hw_mcm=1 mode=0 sensor=imx728 sensor_id=0 > /proc/vsi/isp_subdev0
	echo 1 hw_mcm=1 mode=0 sensor=imx728 sensor_id=1 > /proc/vsi/isp_subdev0
	echo 2 hw_mcm=1 mode=0 sensor=imx728 sensor_id=2 > /proc/vsi/isp_subdev0
	echo 3 hw_mcm=1 mode=0 sensor=imx728 sensor_id=3 > /proc/vsi/isp_subdev0
	if [ "$VIVADO" = "20252" ]; then
		echo 0 xml=/usr/share/isp-support/IMX728/IMX728_ewd_2026_M15.xml  > /proc/vsi/isp_subdev0
		echo 0 auto_json=/usr/share/isp-support/IMX728/auto_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev0
		echo 0 manu_json=/usr/share/isp-support/IMX728/manual_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev0

		echo 1 xml=/usr/share/isp-support/IMX728/IMX728_ewd_2026_M15.xml  > /proc/vsi/isp_subdev0
		echo 1 auto_json=/usr/share/isp-support/IMX728/auto_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev0
		echo 1 manu_json=/usr/share/isp-support/IMX728/manual_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev0

		echo 2 xml=/usr/share/isp-support/IMX728/IMX728_ewd_2026_M15.xml  > /proc/vsi/isp_subdev0
		echo 2 auto_json=/usr/share/isp-support/IMX728/auto_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev0
		echo 2 manu_json=/usr/share/isp-support/IMX728/manual_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev0

		echo 3 xml=/usr/share/isp-support/IMX728/IMX728_ewd_2026_M15.xml  > /proc/vsi/isp_subdev0
		echo 3 auto_json=/usr/share/isp-support/IMX728/auto_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev0
		echo 3 manu_json=/usr/share/isp-support/IMX728/manual_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev0
	elif [ "$VIVADO" = "20261" ]; then
		echo 0 calib=/usr/share/Tuning_files/IMX728/IMX728_v1.0.json  > /proc/vsi/isp_subdev0
		echo 0 auto_json=/usr/share/Tuning_files/IMX728/auto_IMX728_v1.0.json  > /proc/vsi/isp_subdev0
		echo 0 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.0.json  > /proc/vsi/isp_subdev0

		echo 1 calib=/usr/share/Tuning_files/IMX728/IMX728_v1.1.json  > /proc/vsi/isp_subdev0
		echo 1 auto_json=/usr/share/Tuning_files/IMX728/auto_IMX728_v1.1.json  > /proc/vsi/isp_subdev0
		echo 1 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.1.json  > /proc/vsi/isp_subdev0

		echo 2 calib=/usr/share/Tuning_files/IMX728/IMX728_v1.1.json  > /proc/vsi/isp_subdev0
		echo 2 auto_json=/usr/share/Tuning_files/IMX728/auto_IMX728_v1.1.json  > /proc/vsi/isp_subdev0
		echo 2 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.1.json  > /proc/vsi/isp_subdev0

		echo 3 calib=/usr/share/Tuning_files/IMX728/IMX728_v1.1.json  > /proc/vsi/isp_subdev0
		echo 3 auto_json=/usr/share/Tuning_files/IMX728/auto_IMX728_v1.1.json  > /proc/vsi/isp_subdev0
		echo 3 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.1.json  > /proc/vsi/isp_subdev0

	else
		echo "Vivado version not detected"
	fi
fi

if [ "$PHY1" = "imx623" ]; then
	echo "Configuring PHY1 for imx623"
	echo 0 hw_mcm=1 mode=2 sensor=imx623 sensor_id=4 > /proc/vsi/isp_subdev2
	echo 1 hw_mcm=1 mode=2 sensor=imx623 sensor_id=5 > /proc/vsi/isp_subdev2
	echo 2 hw_mcm=1 mode=2 sensor=imx623 sensor_id=6 > /proc/vsi/isp_subdev2
	echo 3 hw_mcm=1 mode=2 sensor=imx623 sensor_id=7 > /proc/vsi/isp_subdev2
	if [ "$VIVADO" = "20252" ]; then
		echo 0 xml=/usr/share/isp-support/IMX623/IMX623_ewd_2026_M15_mode2.xml  > /proc/vsi/isp_subdev2
		echo 0 auto_json=/usr/share/isp-support/IMX623/auto_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev2
		echo 0 manu_json=/usr/share/isp-support/IMX623/manual_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev2

		echo 1 xml=/usr/share/isp-support/IMX623/IMX623_ewd_2026_M15_mode2.xml  > /proc/vsi/isp_subdev2
		echo 1 auto_json=/usr/share/isp-support/IMX623/auto_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev2
		echo 1 manu_json=/usr/share/isp-support/IMX623/manual_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev2

		echo 2 xml=/usr/share/isp-support/IMX623/IMX623_ewd_2026_M15_mode2.xml  > /proc/vsi/isp_subdev2
		echo 2 auto_json=/usr/share/isp-support/IMX623/auto_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev2
		echo 2 manu_json=/usr/share/isp-support/IMX623/manual_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev2

		echo 3 xml=/usr/share/isp-support/IMX623/IMX623_ewd_2026_M15_mode2.xml  > /proc/vsi/isp_subdev2
		echo 3 auto_json=/usr/share/isp-support/IMX623/auto_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev2
		echo 3 manu_json=/usr/share/isp-support/IMX623/manual_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev2

	elif [ "$VIVADO" = "20261" ]; then
		echo 0 calib=/usr/share/Tuning_files/IMX623/1920/IMX623_1920x1536_v1.0.json  > /proc/vsi/isp_subdev2
		echo 0 auto_json=/usr/share/Tuning_files/IMX623/1920/auto_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev2
		echo 0 manu_json=/usr/share/Tuning_files/IMX623/1920/manual_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev2

		echo 1 calib=/usr/share/Tuning_files/IMX623/1920/IMX623_1920x1536_v1.0.json  > /proc/vsi/isp_subdev2
		echo 1 auto_json=/usr/share/Tuning_files/IMX623/1920/auto_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev2
		echo 1 manu_json=/usr/share/Tuning_files/IMX623/1920/manual_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev2

		echo 2 calib=/usr/share/Tuning_files/IMX623/1920/IMX623_1920x1536_v1.0.json  > /proc/vsi/isp_subdev2
		echo 2 auto_json=/usr/share/Tuning_files/IMX623/1920/auto_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev2
		echo 2 manu_json=/usr/share/Tuning_files/IMX623/1920/manual_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev2

		echo 3 calib=/usr/share/Tuning_files/IMX623/1920/IMX623_1920x1536_v1.0.json  > /proc/vsi/isp_subdev2
		echo 3 auto_json=/usr/share/Tuning_files/IMX623/1920/auto_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev2
		echo 3 manu_json=/usr/share/Tuning_files/IMX623/1920/manual_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev2

	else
		echo "Vivado version not detected"
	fi
else # imx728
	echo "Configuring PHY1 for imx728"
	echo 0 hw_mcm=1 mode=0 sensor=imx728 sensor_id=4 > /proc/vsi/isp_subdev2
	echo 1 hw_mcm=1 mode=0 sensor=imx728 sensor_id=5 > /proc/vsi/isp_subdev2
	echo 2 hw_mcm=1 mode=0 sensor=imx728 sensor_id=6 > /proc/vsi/isp_subdev2
	echo 3 hw_mcm=1 mode=0 sensor=imx728 sensor_id=7 > /proc/vsi/isp_subdev2
	if [ "$VIVADO" = "20252" ]; then
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
		echo 0 calib=/usr/share/Tuning_files/IMX728/IMX728_v1.1.json  > /proc/vsi/isp_subdev2
		echo 0 auto_json=/usr/share/Tuning_files/IMX728/auto_IMX728_v1.1.json  > /proc/vsi/isp_subdev2
		echo 0 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.1.json  > /proc/vsi/isp_subdev2

		echo 1 calib=/usr/share/Tuning_files/IMX728/IMX728_v1.1.json  > /proc/vsi/isp_subdev2
		echo 1 auto_json=/usr/share/Tuning_files/IMX728/auto_IMX728_v1.1.json  > /proc/vsi/isp_subdev2
		echo 1 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.1.json  > /proc/vsi/isp_subdev2

		echo 2 calib=/usr/share/Tuning_files/IMX728/IMX728_v1.1.json  > /proc/vsi/isp_subdev2
		echo 2 auto_json=/usr/share/Tuning_files/IMX728/auto_IMX728_v1.1.json  > /proc/vsi/isp_subdev2
		echo 2 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.1.json  > /proc/vsi/isp_subdev2

		echo 3 calib=/usr/share/Tuning_files/IMX728/IMX728_v1.1.json  > /proc/vsi/isp_subdev2
		echo 3 auto_json=/usr/share/Tuning_files/IMX728/auto_IMX728_v1.1.json  > /proc/vsi/isp_subdev2
		echo 3 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.1.json  > /proc/vsi/isp_subdev2

	else
		echo "Vivado version not detected"
	fi
fi

if [ "$PHY2" = "imx623" ]; then
	echo "Configuring PHY2 for imx623"
	echo 0 hw_mcm=1 mode=2 sensor=imx623 sensor_id=8 > /proc/vsi/isp_subdev4
	echo 1 hw_mcm=1 mode=2 sensor=imx623 sensor_id=9 > /proc/vsi/isp_subdev4
	echo 2 hw_mcm=1 mode=2 sensor=imx623 sensor_id=10 > /proc/vsi/isp_subdev4
	echo 3 hw_mcm=1 mode=2 sensor=imx623 sensor_id=11 > /proc/vsi/isp_subdev4
	if [ "$VIVADO" = "20252" ]; then
		echo 0 xml=/usr/share/isp-support/IMX623/IMX623_ewd_2026_M15_mode2.xml  > /proc/vsi/isp_subdev4
		echo 0 auto_json=/usr/share/isp-support/IMX623/auto_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev4
		echo 0 manu_json=/usr/share/isp-support/IMX623/manual_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev4

		echo 1 xml=/usr/share/isp-support/IMX623/IMX623_ewd_2026_M15_mode2.xml  > /proc/vsi/isp_subdev4
		echo 1 auto_json=/usr/share/isp-support/IMX623/auto_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev4
		echo 1 manu_json=/usr/share/isp-support/IMX623/manual_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev4

		echo 2 xml=/usr/share/isp-support/IMX623/IMX623_ewd_2026_M15_mode2.xml  > /proc/vsi/isp_subdev4
		echo 2 auto_json=/usr/share/isp-support/IMX623/auto_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev4
		echo 2 manu_json=/usr/share/isp-support/IMX623/manual_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev4

		echo 3 xml=/usr/share/isp-support/IMX623/IMX623_ewd_2026_M15_mode2.xml  > /proc/vsi/isp_subdev4
		echo 3 auto_json=/usr/share/isp-support/IMX623/auto_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev4
		echo 3 manu_json=/usr/share/isp-support/IMX623/manual_IMX623_ewd_2026_M15.json  > /proc/vsi/isp_subdev4

	elif [ "$VIVADO" = "20261" ]; then
		echo 0 calib=/usr/share/Tuning_files/IMX623/1920/IMX623_1920x1536_v1.0.json  > /proc/vsi/isp_subdev4
		echo 0 auto_json=/usr/share/Tuning_files/IMX623/1920/auto_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev4
		echo 0 manu_json=/usr/share/Tuning_files/IMX623/1920/manual_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev4

		echo 1 calib=/usr/share/Tuning_files/IMX623/1920/IMX623_1920x1536_v1.0.json  > /proc/vsi/isp_subdev4
		echo 1 auto_json=/usr/share/Tuning_files/IMX623/1920/auto_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev4
		echo 1 manu_json=/usr/share/Tuning_files/IMX623/1920/manual_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev4

		echo 2 calib=/usr/share/Tuning_files/IMX623/1920/IMX623_1920x1536_v1.0.json  > /proc/vsi/isp_subdev4
		echo 2 auto_json=/usr/share/Tuning_files/IMX623/1920/auto_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev4
		echo 2 manu_json=/usr/share/Tuning_files/IMX623/1920/manual_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev4

		echo 3 calib=/usr/share/Tuning_files/IMX623/1920/IMX623_1920x1536_v1.0.json  > /proc/vsi/isp_subdev4
		echo 3 auto_json=/usr/share/Tuning_files/IMX623/1920/auto_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev4
		echo 3 manu_json=/usr/share/Tuning_files/IMX623/1920/manual_IMX623_1920_v1.0.json  > /proc/vsi/isp_subdev4

	else
		echo "Vivado version not detected"
	fi
else # imx728
	echo "Configuring PHY2 for imx728"
	echo 0 hw_mcm=1 mode=0 sensor=imx728 sensor_id=8 > /proc/vsi/isp_subdev4
	echo 1 hw_mcm=1 mode=0 sensor=imx728 sensor_id=9 > /proc/vsi/isp_subdev4
	echo 2 hw_mcm=1 mode=0 sensor=imx728 sensor_id=10 > /proc/vsi/isp_subdev4
	echo 3 hw_mcm=1 mode=0 sensor=imx728 sensor_id=11 > /proc/vsi/isp_subdev4
	if [ "$VIVADO" = "20252" ]; then
		echo 0 xml=/usr/share/isp-support/IMX728/IMX728_ewd_2026_M15.xml  > /proc/vsi/isp_subdev4
		echo 0 auto_json=/usr/share/isp-support/IMX728/auto_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev4
		echo 0 manu_json=/usr/share/isp-support/IMX728/manual_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev4

		echo 1 xml=/usr/share/isp-support/IMX728/IMX728_ewd_2026_M15.xml  > /proc/vsi/isp_subdev4
		echo 1 auto_json=/usr/share/isp-support/IMX728/auto_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev4
		echo 1 manu_json=/usr/share/isp-support/IMX728/manual_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev4

		echo 2 xml=/usr/share/isp-support/IMX728/IMX728_ewd_2026_M15.xml  > /proc/vsi/isp_subdev4
		echo 2 auto_json=/usr/share/isp-support/IMX728/auto_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev4
		echo 2 manu_json=/usr/share/isp-support/IMX728/manual_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev4

		echo 3 xml=/usr/share/isp-support/IMX728/IMX728_ewd_2026_M15.xml  > /proc/vsi/isp_subdev4
		echo 3 auto_json=/usr/share/isp-support/IMX728/auto_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev4
		echo 3 manu_json=/usr/share/isp-support/IMX728/manual_IMX728_ewd_2026_M15.json  > /proc/vsi/isp_subdev4
	elif [ "$VIVADO" = "20261" ]; then
		echo 0 calib=/usr/share/Tuning_files/IMX728/IMX728_v1.1.json  > /proc/vsi/isp_subdev4
		echo 0 auto_json=/usr/share/Tuning_files/IMX728/auto_IMX728_v1.1.json  > /proc/vsi/isp_subdev4
		echo 0 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.1.json  > /proc/vsi/isp_subdev4

		echo 1 calib=/usr/share/Tuning_files/IMX728/IMX728_v1.1.json  > /proc/vsi/isp_subdev4
		echo 1 auto_json=/usr/share/Tuning_files/IMX728/auto_IMX728_v1.1.json  > /proc/vsi/isp_subdev4
		echo 1 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.1.json  > /proc/vsi/isp_subdev4

		echo 2 calib=/usr/share/Tuning_files/IMX728/IMX728_v1.1.json  > /proc/vsi/isp_subdev4
		echo 2 auto_json=/usr/share/Tuning_files/IMX728/auto_IMX728_v1.1.json  > /proc/vsi/isp_subdev4
		echo 2 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.1.json  > /proc/vsi/isp_subdev4

		echo 3 calib=/usr/share/Tuning_files/IMX728/IMX728_v1.1.json  > /proc/vsi/isp_subdev4
		echo 3 auto_json=/usr/share/Tuning_files/IMX728/auto_IMX728_v1.1.json  > /proc/vsi/isp_subdev4
		echo 3 manu_json=/usr/share/Tuning_files/IMX728/manual_IMX728_v1.1.json  > /proc/vsi/isp_subdev4

	else
		echo "Vivado version not detected"
	fi
fi


if [ "$VIVADO" = "20261" ]; then
	echo 1 > /sys/module/visp/parameters/fmc_id
fi

isp_media_server&


