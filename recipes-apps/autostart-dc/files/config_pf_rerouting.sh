#!/bin/bash
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: TO
# Maintainer: TO

echo "config_pf_rerouting.sh Autostart script"
echo ""
echo "Configuring MDB5 PF0 outbound iATU function-number masquerade"
echo "(rewrites each DMA channel's outbound Requester ID to its owning"
echo " PF, so the x86 host's IOMMU attributes PF0's shared eDMA traffic"
echo " to the correct PF/domain)"
echo ""

# PF0_ATU_CAP_IATU_REGION_CTRL_{1,2}_OFF_OUTBOUND_i, i = 0..15
ATU_CTRL_BASE=0xED820000
ATU_CTRL_STRIDE=512     # 0x200
ATU_NUM_REGIONS=16

# CTRL_1: FUNC_NUM field, bits 22:20
CTRL1_FUNC_NUM_MASK=0x00700000
CTRL1_FUNC_NUM_SHIFT=20

# CTRL_2: FUNC_BYPASS bit 19 (cleared so FUNC_NUM above takes effect);
# REGION_EN (bit 31) and every other field are left untouched.
CTRL2_FUNC_BYPASS_MASK=0x00080000

# Static channel/region -> owning PF table
region_owner_func() {
region=$1
if [ "$region" -le 6 ]; then
echo 1
elif [ "$region" -eq 7 ]; then
echo 3
elif [ "$region" -le 14 ]; then
echo 2
else
echo 3
fi
}

region=0
while [ "$region" -lt "$ATU_NUM_REGIONS" ]; do
offset=$((ATU_CTRL_STRIDE * region))
ctrl1_addr=$(printf '0x%X' $((ATU_CTRL_BASE + offset)))
ctrl2_addr=$(printf '0x%X' $((ATU_CTRL_BASE + offset + 4)))
func=$(region_owner_func "$region")

ctrl1_cur=$(devmem2 "$ctrl1_addr" | grep -o '0x[0-9a-fA-F]\+' | tail -1)
ctrl1_new=$(printf '0x%X' $(( (ctrl1_cur & ~CTRL1_FUNC_NUM_MASK & 0xFFFFFFFF) | (func << CTRL1_FUNC_NUM_SHIFT) )))
devmem2 "$ctrl1_addr" w "$ctrl1_new"

ctrl2_cur=$(devmem2 "$ctrl2_addr" | grep -o '0x[0-9a-fA-F]\+' | tail -1)
ctrl2_new=$(printf '0x%X' $(( ctrl2_cur & ~CTRL2_FUNC_BYPASS_MASK & 0xFFFFFFFF )))
devmem2 "$ctrl2_addr" w "$ctrl2_new"

echo "region $region -> PF$func (CTRL1=$ctrl1_new CTRL2=$ctrl2_new)"

region=$((region + 1))
done

echo ""
echo "MDB5 PF masquerade configuration complete"
