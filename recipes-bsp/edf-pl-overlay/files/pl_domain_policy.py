# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

import os
import re

from baremetalconfig_xlnx import get_cpu_node


def is_compat(node, compat_string_to_test):
    if re.search(r"module,pl_domain_policy$", compat_string_to_test):
        return apply_pl_domain_policy
    return ""


def apply_pl_domain_policy(tgt_node, sdt, options):
    processor = os.environ.get("PL_DOMAIN_PROCESSOR", "cortexa78_0")
    cpu_node = get_cpu_node(sdt, {"args": [processor]})
    cluster = cpu_node.parent
    address_map = cluster["address-map"].value
    address_cells = cluster["#ranges-address-cells"].value[0]
    size_cells = cluster["#ranges-size-cells"].value[0]
    entry_cells = address_cells + size_cells + address_cells + 1
    mapped_phandles = {
        address_map[index]
        for index in range(address_cells, len(address_map), entry_cells)
    }

    disabled_labels = []
    for node in sdt.tree["/amba_pl"].subnodes(children_only=True):
        is_enabled_device = "okay" in node.propval("status", list)
        has_unit_address = "@" in node.name
        if is_enabled_device and has_unit_address and node.phandle not in mapped_phandles:
            if not node.label:
                raise RuntimeError(f"Unmapped PL node has no label: {node.abs_path}")
            disabled_labels.append(node.label)

    output = os.path.join(sdt.outdir, "pl-domain-policy.dtsi")
    with open(output, "w") as output_file:
        for label in sorted(disabled_labels):
            output_file.write(f'&{label} {{\n\tstatus = "disabled";\n}};\n')

    return True