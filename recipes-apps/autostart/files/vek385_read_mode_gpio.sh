#!/bin/bash
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: TO
# Maintainer: TO

echo "======================"
echo "VEK385 GPIO Feature selection"
echo "======================"
echo ""
v=$(devmem2 0xb2030000 | grep -o '0x[0-9a-fA-F]\+' | tail -1)

mode1=$((${v} & 1))
mode0=$(((${v} & 2)>>1))
spare1=$(((${v} & 4)>>2))
spare2=$(((${v} & 8)>>3))
echo "SW6 | 4 | 3 | 2 | 1 |"
echo "    | $mode1 | $mode0 | $spare1 | $spare2 |"
echo "      |   |   |   '- Spare2"
echo "      |   |   '----- Spare1"
echo "      |   '--------- Camera mode [0]"
echo "      '------------- Camera mode [1]"
echo " Camera mode [1:0]:"
echo " 00: Do not load PLF" 
echo " 01: 'Old' Firmware"
echo " 10: 'New' Firmware"
echo " 11: Simulator"
echo ""

if [ ${mode1}${mode0} -eq "00" ]; then
        echo "**** Setting to not load PLF ****"
        echo "NO_PLF" > /etc/PLF_MODE
fi
if [ ${mode1}${mode0} -eq "01" ]; then
        echo "**** Setting to use 'Old' Firmware ****"
        echo "OLD_FIRMWARE" > /etc/PLF_MODE
fi
if [ ${mode1}${mode0} -eq "10" ]; then
        echo "**** Setting to use 'New' Firmware ****"
        echo "NEW_FIRMWARE" > /etc/PLF_MODE
fi
if [ ${mode1}${mode0} -eq "11" ]; then
        echo "**** Setting to use the simulator ****"
        echo "SIMULATOR" > /etc/PLF_MODE
fi

echo "Spare GPIO 1 = ${spare1}"
echo "Spare GPIO 2 = ${spare2}"

echo ${spare1} > /etc/GPIO_SPARE1
echo ${spare2} > /etc/GPIO_SPARE2

echo ""
echo "==================="
echo


