#!/usr/bin/env bash
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, BM
# Maintainer: BM

if [[ "${BASH_SOURCE[0]}" != "$0" ]]; then
    printf 'Run this script instead of sourcing it: bash %q\n' "${BASH_SOURCE[0]}" >&2
    return 1
fi

set -eu

yocto_uid="$(id -u)"
yocto_gid="$(id -g)"
if [ "$yocto_uid" -eq 0 ]; then
    printf 'Run this launcher as your ordinary user; Yocto builds require a non-root user.\n' >&2
    exit 1
fi

if [ ! -d "$HOME/.ssh" ]; then
    printf 'SSH directory missing: %s. Set up your SSH key before launching.\n' "$HOME/.ssh" >&2
    exit 1
fi

exec docker run \
    --rm \
    -it \
    --user 0:0 \
    --security-opt label=disable \
    -v "$PWD:/home/build/work" \
    -v "$HOME/.ssh:/home/build/work/.ssh:ro" \
    -w /home/build/work \
    -e YOCTO_UID="$yocto_uid" \
    -e YOCTO_GID="$yocto_gid" \
    docker.io/krisamd/yoctobuild:0.17 \
    bash -c '
        set -eu

        if ! getent group "$YOCTO_GID" >/dev/null; then
            groupadd --gid "$YOCTO_GID" "hostgroup${YOCTO_GID}"
        fi

        if user_entry="$(getent passwd "$YOCTO_UID")"; then
            runtime_user="${user_entry%%:*}"
            # SSH and other programs may use passwd rather than $HOME.
            # Update the account home without moving its contents.
            usermod --home /home/build/work "$runtime_user"
        else
            runtime_user="hostuser${YOCTO_UID}"
            useradd \
                --uid "$YOCTO_UID" \
                --gid "$YOCTO_GID" \
                --home-dir /home/build/work \
                --no-create-home \
                --shell /bin/bash \
                "$runtime_user"
        fi

        exec setpriv \
            --reuid="$YOCTO_UID" \
            --regid="$YOCTO_GID" \
            --init-groups \
            env HOME=/home/build/work USER="$runtime_user" LOGNAME="$runtime_user" \
            bash
    '
