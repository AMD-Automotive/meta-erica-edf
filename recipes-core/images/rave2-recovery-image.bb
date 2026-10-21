# recipes-core/images/rave2-recovery-image.bb
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "RAVE2 Recovery Image with FIT bundle"
DESCRIPTION = "Minimal recovery image with kernel+rootfs bundled in FIT format"


require recipes-core/images/core-image-minimal.bb

IMAGE_INSTALL:append = " \
    mtd-utils \
    e2fsprogs-resize2fs \
    parted \
    dosfstools \
    util-linux \
    mtd-utils \
    usbutils \
    devmem2 \
    python3 \
    python3-mmap \
    python3-ctypes \
    python3-setuptools \
    rave2-pcie-programmer \
"



hostname:pn-base-files = "rave2-recovery"
IMAGE_FSTYPES:remove = "wic wic.ufs wic.qemu-sd"

DEPENDS += "u-boot-tools-native virtual/kernel"
do_image_complete[depends] += "virtual/kernel:do_deploy"

python do_create_fit_image() {
    import subprocess
    import os
    import glob
    
    deploy_dir = d.getVar('DEPLOY_DIR_IMAGE')
    workdir = d.getVar('WORKDIR')
    image_name = d.getVar('IMAGE_NAME')
    image_link_name = d.getVar('IMAGE_LINK_NAME')
    machine = d.getVar('MACHINE')
    imgdeploydir = d.getVar('IMGDEPLOYDIR')
    
    # Find kernel (use symlink for stable reference)
    kernel_symlink = os.path.join(deploy_dir, f'Image-{machine}.bin')
    if not os.path.exists(kernel_symlink):
        # Try to find any Image file
        pattern = os.path.join(deploy_dir, 'Image--*.bin')
        matches = glob.glob(pattern)
        if matches:
            kernel_symlink = matches[0]
        else:
            bb.fatal(f"Kernel image not found in {deploy_dir}")
    
    # Find initramfs - it's created in IMGDEPLOYDIR first, then deployed
    cpio_file = None
    
    # Method 1: Look in IMGDEPLOYDIR (where it's initially created)
    if imgdeploydir and os.path.exists(imgdeploydir):
        pattern = os.path.join(imgdeploydir, '*.cpio.gz')
        matches = glob.glob(pattern)
        # Filter out u-boot wrapped version for the source
        matches = [m for m in matches if not m.endswith('.u-boot')]
        if matches:
            cpio_file = matches[0]
            bb.note(f"Found initramfs in IMGDEPLOYDIR: {cpio_file}")
    
    # Method 2: Look in deploy directory
    if not cpio_file:
        # Try with IMAGE_NAME first
        test_path = os.path.join(deploy_dir, f'{image_name}.cpio.gz')
        if os.path.exists(test_path):
            cpio_file = test_path
        else:
            # Try pattern matching
            pattern = os.path.join(deploy_dir, f'{image_link_name}*.cpio.gz')
            matches = glob.glob(pattern)
            matches = [m for m in matches if not m.endswith('.u-boot')]
            if matches:
                cpio_file = matches[0]
    
    if not cpio_file or not os.path.exists(cpio_file):
        bb.fatal(f"Initramfs not found. Searched:\n" +
                f"  IMGDEPLOYDIR: {imgdeploydir}\n" +
                f"  DEPLOY_DIR: {deploy_dir}\n" +
                f"  Pattern: {image_link_name}*.cpio.gz")
    
    its_file = os.path.join(workdir, 'recovery-fit.its')
    fit_image = os.path.join(deploy_dir, f'{image_name}.fitImage')
    
    # Get file sizes for logging
    kernel_size = os.path.getsize(kernel_symlink)
    initramfs_size = os.path.getsize(cpio_file)
    
    bb.note(f"Creating FIT image:")
    bb.note(f"  Kernel:    {kernel_symlink} ({kernel_size} bytes)")
    bb.note(f"  Initramfs: {cpio_file} ({initramfs_size} bytes)")
    
    # Memory layout:
    # - Kernel loads at 0x80000 (standard ARM64)
    # - Ramdisk loads at 0x4000000 (64 MB - safe for most systems)
    
    its_content = f'''
/dts-v1/;

/ {{
    description = "RAVE2 Recovery Image";
    #address-cells = <1>;

    images {{
        kernel {{
            description = "Linux Kernel";
            data = /incbin/("{kernel_symlink}");
            type = "kernel";
            arch = "arm64";
            os = "linux";
            compression = "none";
            load = <0x00080000>;  /* Standard ARM64 kernel load */
            entry = <0x00080000>;
            hash {{
                algo = "sha256";
            }};
        }};

        ramdisk {{
            description = "Recovery Ramdisk";
            data = /incbin/("{cpio_file}");
            type = "ramdisk";
            arch = "arm64";
            os = "linux";
            compression = "gzip";
            load = <0x04000000>;  /* 64 MB - avoids kernel */
            hash {{
                algo = "sha256";
            }};
        }};
    }};

    configurations {{
        default = "config";
        config {{
            description = "RAVE2 Recovery Boot";
            kernel = "kernel";
            ramdisk = "ramdisk";
        }};
    }};
}};
'''
    
    with open(its_file, 'w') as f:
        f.write(its_content)
    
    cmd = ['mkimage', '-f', its_file, fit_image]
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        bb.fatal(f"mkimage failed: {result.stderr}")
    
    bb.note(result.stdout)
    
    symlink = os.path.join(deploy_dir, f'{image_link_name}.fitImage')
    if os.path.exists(symlink):
        os.remove(symlink)
    os.symlink(os.path.basename(fit_image), symlink)
    
    fit_size = os.path.getsize(fit_image)
    bb.note(f"Created FIT image: {fit_image} ({fit_size} bytes)")
    bb.note(f"Memory layout:")
    bb.note(f"  Kernel:  0x00080000")
    bb.note(f"  Ramdisk: 0x04000000")
}

python do_create_pcie_image() {
    import os
    
    deploy_dir = d.getVar('DEPLOY_DIR_IMAGE')
    image_name = d.getVar('IMAGE_NAME')
    image_link_name = d.getVar('IMAGE_LINK_NAME')
    
    # Files we need
    fit_image = os.path.join(deploy_dir, f'{image_link_name}.fitImage')
    boot_scr = os.path.join(deploy_dir, 'recovery-boot.scr')
    
    # Output files
    pcie_image = os.path.join(deploy_dir, f'{image_name}.rootfs.pcieImage')
    pcie_link = os.path.join(deploy_dir, f'{image_link_name}.pcieImage')
    
    # Verify inputs exist
    if not os.path.exists(fit_image):
        bb.fatal(f"FIT image not found: {fit_image}")
    if not os.path.exists(boot_scr):
        bb.fatal(f"Boot script not found: {boot_scr}")
    
    # Read boot script
    with open(boot_scr, 'rb') as f:
        script_data = f.read()
    
    script_size = len(script_data)
    target_offset = 16384  # 0x4000
    
    # Create PCIe image
    with open(pcie_image, 'wb') as out:
        # Write boot script
        out.write(script_data)
        
        # Add padding to reach 0x4000
        padding = target_offset - script_size
        out.write(b'\x00' * padding)
        
        # Append FIT image
        with open(fit_image, 'rb') as f:
            out.write(f.read())
    
    pcie_size = os.path.getsize(pcie_image)
    
    bb.note(f"PCIe image created: {pcie_image}")
    bb.note(f"  Script:  0x0000-0x{script_size:04x} ({script_size} bytes)")
    bb.note(f"  Padding: 0x{script_size:04x}-0x4000 ({padding} bytes)")
    bb.note(f"  FIT:     0x4000-0x{pcie_size:x} ({pcie_size - target_offset} bytes)")
    bb.note(f"  Total:   {pcie_size} bytes")
    
    # Create symlink
    if os.path.exists(pcie_link):
        os.remove(pcie_link)
    os.symlink(os.path.basename(pcie_image), pcie_link)
    bb.note(f"Created symlink: {pcie_link}")
}

addtask do_create_fit_image after do_image_complete before do_build
do_create_fit_image[depends] += "${PN}:do_image_cpio"

addtask do_create_pcie_image after do_create_fit_image before do_build
do_create_pcie_image[depends] += "rave2-recovery-script:do_deploy"


