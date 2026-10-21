# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "Prepare partition files for DOMU guest images"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = " \
        https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/ERICA-H/meta-erica-h/fcca28_b8af54_b80df0_vek280-svai-rgbir-development-image.wic.gz;name=first\
"
SRC_URI[first.sha256sum] = "b75f7025fd72cf8a5e8f1c50f788f08b13fc1b45afd49bcd49a1f818913e5789"

S="${WORKDIR}/wic"
DEPENDS += "parted-native"

do_compile() {
        echo "Force rebuild"
        offset=0
        for wicfile in "${WORKDIR}"/*.wic; do
                parted -s "$wicfile" unit b print > ${S}/partinfo.txt
                
                # Find partition lines (start with a number)
                awk '/^[[:space:]]*[0-9]/ { print }' ${S}/partinfo.txt > lines.txt
                
                while read line; do
                        set -- $line
                        partnum=$1
                        start=$2
                        size=$4
                        format=$6
                        # Remove 'B' from offsets (parted uses 12345B)
                        start=${start%B}
                        size=${size%B}
                        partition_name="partition_$(expr $partnum + $offset).img"
                        rem_start=$(expr $start % 1024) || true
                        rem_size=$(expr $size % 1024) || true

                        echo "Filename:$(basename $wicfile)" > "${partition_name}.txt"
                        echo "Partition:$partnum"          >> "${partition_name}.txt"
                        echo "Start:$start"                >> "${partition_name}.txt"
                        echo "Size:$size"                  >> "${partition_name}.txt"
                        echo "Format:$format"              >> "${partition_name}.txt"
                
                        if [ "$rem_start" -eq 0 ] && [ "$rem_size" -eq 0 ]; then
                                start=$(expr $start / 1024)
                                size=$(expr $size / 1024)
                                bs=1024
                        else
                                echo "Partition size is not divisible by 1k - using DD in byte mode - this will be slow!"
                                bs=1
                        fi

                        dd if="$wicfile" of="$partition_name" bs="${bs}" skip="$start" count="$size"

                done < lines.txt

                offset=$(expr $offset + $partnum)
        done
}

do_install(){
        echo "Install"
        install -d ${D}/guestimage
        for f in ${S}/partition*; do
                if [ -f "$f" ]; then
                        install -m 0644 "$f" ${D}/guestimage
                fi
        done
}


SYSROOT_DIRS += "/guestimage"
FILES:${PN} += "/"

