# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, JC
# Maintainer: JC

DEPENDS += "u-boot-tools-native"

do_rootfs:append() {
    import subprocess
    import os

    # Where the recipe is & what it's called
    recipe_dir = d.getVar('THISDIR')
    recipe_name = d.getVar('PN')

    def run_git(args):
        result = subprocess.run(
            ['git'] + args,
            cwd=recipe_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            encoding='utf-8'
        )
        if result.returncode != 0:
            bb.warn("Git command failed: git %s\n%s" % (' '.join(args), result.stderr))
            return "<unknown>"
        return result.stdout.strip()

    branch = run_git(['rev-parse', '--abbrev-ref', 'HEAD'])
    messages = run_git(['log', '--oneline', '-n', '3'])

    # U-Boot's script parser treats each line independently and does NOT
    # honour multi-line single-quoted strings, so a single
    #   echo 'Last 3 commits:\n<hash1> ...\n<hash2> ...'
    # gets split: line 2 onward are parsed as their own commands ->
    # "Unknown command '<hash>'" and a cascade of "syntax error".
    # Emit one echo per commit line instead, with each line stripped of
    # any embedded single-quotes that would also break parsing.
    commit_lines = [
        line.replace("'", "")
        for line in messages.splitlines()
        if line.strip()
    ]
    commits_echo = "\n".join(f"echo '  {line}'" for line in commit_lines)

    # U-Boot variables (example; adapt as needed)
    uboot_script = f"""
echo 'SD Card Recipe: {recipe_name}'
echo 'Branch: {branch}'
echo 'Last 3 commits:'
{commits_echo}

"""

    # Ensure /boot exists
    image_boot = os.path.join(d.getVar('IMAGE_ROOTFS'), 'boot')
    os.makedirs(image_boot, exist_ok=True)

    # Write boot.cmd
    bootcmd_path = os.path.join(image_boot, 'boot.cmd')
    with open(bootcmd_path, 'w') as f:
        f.write(uboot_script)

    # Compile to boot.scr (ensure mkimage is in the build host PATH!)
    mkimage = d.getVar('STAGING_BINDIR_NATIVE') + '/mkimage'
    boot_scr_path = os.path.join(image_boot, 'rootfs_id.scr')
    try:
        subprocess.check_call([
            mkimage,
            '-A', 'arm',                  # architecture (use 'arm' or your platform)
            '-T', 'script',
            '-C', 'none',
            '-n', 'Bitbake auto script',
            '-d', bootcmd_path,
            boot_scr_path
        ])
    except Exception as e:
        bb.fatal(f"Failed to create rootfs_id.scr: {e}")
}