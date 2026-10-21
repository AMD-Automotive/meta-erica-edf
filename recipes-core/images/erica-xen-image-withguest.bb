# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

DESCRIPTION = "ERICA-H Xen Minimal Image"

require edf-image-common.inc
require rootfs_id.inc

IMAGE_FEATURES += "ssh-server-openssh package-management"
IMAGE_FSTYPES = "wic"

IMAGE_INSTALL = "\
    packagegroup-core-boot \
    packagegroup-core-full-cmdline \
    ${AMD-EDF_IMAGE_COMMON_INSTALL} \
    packagegroup-xen \    
    devmem2 \
    domu-configs \
    "
WKS_FILES = "${WORKDIR}/erica-sd-rootfs-domu-rootfs.wks"
WIC_CREATE_EXTRA_ARGS = "--no-fstab-update"

# WKS_FILES = "erica-default-sd.wks"

TOOLCHAIN_HOST_TASK:append = "\
    "

EXTRA_IMAGE_FEATURES:append = " \
    empty-root-password \
    serial-autologin-root \
    "

inherit core-image

DEPENDS += "domu-rootfs"


do_rootfs[prefuncs] += "edf_check_rootfs"

python do_generate_wks () {
    import os
    import re

    # Expand paths
    staging_incdir = d.getVar('STAGING_INCDIR')
    guest_dir = os.path.abspath(os.path.join(staging_incdir, "../../guestimage"))

    wks_in  = d.expand('${THISDIR}/erica-sd-rootfs-domu-rootfs.wks.in')
    wks_out = d.expand('${WORKDIR}/erica-sd-rootfs-domu-rootfs.wks')

    part_entries = []
    pattern = re.compile(r'^partition_(\d+)\.img$')

    # Scan guestimage dir
    if os.path.exists(guest_dir):
        files = os.listdir(guest_dir)
        partitions = []
        for filename in files:
            match = pattern.match(filename)
            if match:
                n = int(match.group(1))
                img_path = os.path.join(guest_dir, filename)
                txt_path = os.path.join(guest_dir, f"partition_{n}.img.txt")
                # Set default filesystem type
                fstype = "ext4"
                if os.path.exists(txt_path):
                    with open(txt_path, 'r') as f:
                        for line in f:
                            m = re.match(r'Format:(\S+)', line.strip())
                            if m:
                                raw_fstype = m.group(1).lower()
                                if raw_fstype == "fat32":
                                    fstype = "vfat"
                                    guestroot = "false"
                                else:
                                    fstype = raw_fstype
                                    guestroot = "true"
                                break
                partitions.append((n, img_path, fstype))

        partitions.sort(key=lambda x: x[0]) # Sort by number

        for n, img_path, fstype in partitions:
            entry = f'part /guest{n} --source rawcopy --sourceparams="file={img_path}" --fstype={fstype} '
                        
            part_entries.append(entry)

    guest_partitions = '\n'.join(part_entries)

    with open(wks_in, 'r') as f:
        contents = f.read()
    contents = contents.replace('@GUEST_PARTITIONS@', guest_partitions)

    with open(wks_out, 'w') as f:
        f.write(contents)

    #entry = f'part /guest{n} --source rawcopy --sourceparams="file={path}" --align 4 --label guest{n}'
    # Set the WKS_FILE variable for the build
    #d.setVar('WKS_FILE', wks_out)
}

do_generate_wks[dirs] = "${WORKDIR}"

addtask do_generate_wks before do_image_wic after do_rootfs