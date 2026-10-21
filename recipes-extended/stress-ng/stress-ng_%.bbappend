# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

# poky's stress-ng declares PROVIDES = "stress". In 2026.1 both stress-ng (via
# rt-tests) and the meta-ros "stress" recipe (via packagegroup-xilinx-ros) end
# up in the same build graph, and two co-built recipes providing "stress" is
# rejected by the runqueue ("Multiple .bb files ... which each provide stress").
# Drop the alias here so meta-ros "stress" is the sole provider; rt-tests still
# depends on stress-ng by name. Scoped to 2026.1 via the amdtool-2026_1 override
# so 2025.1/2025.2 builds are unaffected.
PROVIDES:remove:amdtool-2026_1 = "stress"
