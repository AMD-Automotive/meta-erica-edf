# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, AK
# Maintainer: AK

# Append base-pdi to also provide pld.pdi into /boot/
do_install:append () {
    PDI_PATH=${PDI_PATH}
    echo "*** PDI_PATH=${PDI_PATH}"
    echo "*** Workdir=${WORKDIR}"
    PLD_PDI="${PDI_PATH%_boot.pdi}_pld.pdi"
    echo "${PLD_PDI}"

    if [ -f ${PDI_PATH} ];then
        echo "Installing pl pdi"
        install -d ${D}/boot
        install -m 0644 "${PLD_PDI}" ${D}/boot/pl-design.pdi

        echo "Installing base files for PL pdi"
        BASE_PATH=$(dirname "$PDI_PATH")
        find "$BASE_PATH" -type f \( -iname '*.rcdo' -o -iname '*.rnpi' \) -exec install -m 0644 {} "${D}/boot/" \;
        find "$BASE_PATH" -type f \( -iname '*_pld.bif' \) -exec install -m 0644 {} "${D}/boot/pl.bif" \;
    else
        echo "Error - couldn't fine ${PDI_PATH}"
    fi

}