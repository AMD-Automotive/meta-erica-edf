#!/usr/bin/env python3
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, LT
# Maintainer: LT

import os
import fcntl
import time
import struct
import re
import ast
import xml.etree.ElementTree as ET
from smbus2 import SMBus, i2c_msg
from pathlib import Path
import argparse, textwrap



"""Parses a register-value line from INI, substituting variables and determining bit widths."""
def parse_register_line(line, variables):
    line = line.split("//")[0].strip()
    if line.startswith("{") and line.endswith("},"):
        line = line[1:-2]
    elif line.startswith("{") and line.endswith("}"):
        line = line[1:-1]

    tokens = line.split(",")
    values = []

    for token in tokens:
        token = token.strip()
        if not token:
            continue

        if token in variables:
            val = variables[token]
        else:
            token = token.replace("}", "").replace(",", "")
            try:
                val = int(token, 0)
            except ValueError:
                raise ValueError(f"Invalid token '{token}' in line: {line}")

        if token.startswith("0x") or token.startswith("0X"):
            num_digits = len(token[2:])
            if num_digits <= 2:
                bits = 8
            elif num_digits <= 4:
                bits = 16
            elif num_digits <= 8:
                bits = 32
            else:
                bits = 64
        else:
            if val <= 0xFF:
                bits = 8
            elif val <= 0xFFFF:
                bits = 16
            elif val <= 0xFFFFFFFF:
                bits = 32
            else:
                bits = 64

        values.append((val, bits))

    return tuple(values)


"""Parses an INI file and extracts commands including delays and write instructions."""
def parse_ini(path, variables,ini_format):
    commands = []
    current_slave = None
    if ini_format == "xylon":
        with open(path) as f:
            lines = f.readlines()

        for line in lines:
            line = line.split("//")[0].strip()
            if not line:
                continue
            if line.startswith("variable"):
                _, name, value = line.split()
                variables[name] = int(value, 0)
            elif line.startswith("delay"):
                match = re.search(r"(\d+(?:\.\d+)?)(ms|s)", line)
                if match:
                    delay_time = float(match.group(1))
                    if match.group(2) == "ms":
                        delay_time /= 1000
                    commands.append(("delay", delay_time))
            elif line.startswith("file"):
                include_path = line.split()[1]
                commands.extend(parse_ini(path.parent / include_path, variables, ini_format))
            elif line.startswith("slave-id"):
                current_slave = line.split()[1]
            elif re.match(r"^\{", line):
                registers = parse_register_line(line, variables)
                commands.append(("write", current_slave, registers))
            elif re.match(r"^0x[0-9A-Fa-f]+,", line):
                # Handle lines like "0x0010, 0x03" (without braces)
                registers = parse_register_line("{" + line + "}", variables)
                commands.append(("write", current_slave, registers))
        return commands
    
    elif ini_format == "ADI":
        with open(path) as f:
            lines = f.readlines()
        for line in lines:
        #Needs to be filled in for ADI format
            return commands
    
    
"""Resolves variable references in the list of parsed commands."""
def apply_variables(commands, variables):
    resolved = []
    for cmd in commands:
        if cmd[0] == "write":
            slave = cmd[1]
            if isinstance(slave, str):
                if slave in variables:
                    slave = variables[slave]
                else:
                    # Try to parse as hex literal if it's not a variable
                    try:
                        slave = int(slave, 0)
                    except ValueError:
                        raise ValueError(f"Unknown variable '{slave}' in command: {cmd}")
            resolved.append(("write", slave, cmd[2]))
        elif cmd[0] == "delay":
            resolved.append(cmd)
        else:
            raise ValueError(f"Unknown command type '{cmd[0]}' in: {cmd}")
    return resolved


"""Parses inline text commands from initxt configuration sections."""
def parse_initxt_commands(initxt_content, variables):
    commands = []
    lines = initxt_content.strip().split('\n')
    current_slave = None
    
    for line in lines:
        line = line.split("//")[0].strip()  # Remove comments
        if not line:
            continue
            
        if line.startswith("slave-id"):
            # Parse slave-id line: "slave-id DESERIALIZER"
            tokens = line.split()
            if len(tokens) >= 2:
                current_slave = tokens[1]
                
            # Check if there are also register/value pairs on the same line
            # Look for hex values after the slave-id declaration
            hex_matches = re.findall(r'0x[0-9A-Fa-f]+', line)
            if len(hex_matches) >= 2 and current_slave:
                # Extract the hex values and create register/value pairs
                reg_val_line = ", ".join(hex_matches)
                clean_line = "{" + reg_val_line + "}"
                try:
                    registers = parse_register_line(clean_line, variables)
                    commands.append(("write", current_slave, registers))
                except Exception as e:
                    print(f"Warning: Could not parse register line '{reg_val_line}': {e}")
                    
        elif re.match(r"^\s*0x[0-9A-Fa-f]+\s*,", line):
            # Handle register/value pairs like "0x0010, 0x03"
            if current_slave:
                # Clean up the line to match expected format
                clean_line = line.strip()
                if not clean_line.startswith('{'):
                    clean_line = "{" + clean_line + "}"
                try:
                    registers = parse_register_line(clean_line, variables)
                    commands.append(("write", current_slave, registers))
                except Exception as e:
                    print(f"Warning: Could not parse register line '{line}': {e}")
                    
        elif "," in line and any(c in line for c in "0123456789abcdefABCDEF"):
            # Handle lines that contain hex values and commas but might not start with 0x
            # This is a fallback for malformed hex lines
            if current_slave:
                try:
                    # Try to extract hex values from the line
                    hex_matches = re.findall(r'0x[0-9A-Fa-f]+', line)
                    if len(hex_matches) >= 2:
                        # Reconstruct as register, value pairs
                        clean_line = "{" + ", ".join(hex_matches) + "}"
                        registers = parse_register_line(clean_line, variables)
                        commands.append(("write", current_slave, registers))
                except Exception as e:
                    print(f"Warning: Could not parse hex line '{line}': {e}")
    
    return commands


"""Parses the XML file to extract I2C device configuration and variable mappings."""
def parse_xml_config(xml_path):
    tree = ET.parse(xml_path)
    root = tree.getroot()
    board = root.find("Board")
    i2cdev = board.attrib["i2cdev"]
    
    configs = []
    initxt_configs = []

    # Helper to extract variables from a <Variables> element
    def extract_variables(variables_elem):
        return {
            elem.tag: int(elem.text, 0)
            for elem in variables_elem
        } if variables_elem is not None else {}

    # Board-level config (if any)
    board_conf = board.find("Configuration")
    if board_conf is not None:
        ini_path = board_conf.attrib["conf"]
        variables_elem = board_conf.find("Variables")
        variables = extract_variables(variables_elem)
        configs.append((i2cdev, ini_path, variables))

    # Traverse all children in document order to preserve initxt order
    def traverse_device(device_elem, parent_variables, parent_i2cdev):
        device_i2caddr = device_elem.attrib.get("i2caddr", None)
        device_variables = dict(parent_variables)
        if device_i2caddr:
            device_variables.setdefault("DESERIALIZER", int(device_i2caddr, 0))
        # Process all Configuration children in order
        for child in device_elem:
            if child.tag == "Configuration":
                ctype = child.attrib.get("type", "")
                config_phase = child.attrib.get("configPhase", "init")
                if ctype == "inifile":
                    ini_path = child.attrib["conf"]
                    variables_elem = child.find("Variables")
                    merged_vars = dict(device_variables)
                    merged_vars.update(extract_variables(variables_elem))
                    configs.append((parent_i2cdev, ini_path, merged_vars))
                elif ctype == "initxt" and config_phase == "postInit":
                    initxt_content = child.attrib.get("conf", "")
                    merged_vars = dict(device_variables)
                    initxt_configs.append((parent_i2cdev, initxt_content, merged_vars))
            elif child.tag == "Device":
                traverse_device(child, device_variables, parent_i2cdev)

    # Start traversal for all top-level devices
    for dev in board.findall("Device"):
        traverse_device(dev, {}, i2cdev)

    return configs, initxt_configs


"""Writes a value to a specific register over I2C using SMBus."""
def write_data(bus, chipAddress: int, addr: int, regValue: int, reg_num_bytes, data_num_bytes):
    addr_bytes = addr.to_bytes(reg_num_bytes, byteorder='big')
    data_bytes = regValue.to_bytes(data_num_bytes, byteorder='big')
    full_data = addr_bytes + data_bytes
    msg = i2c_msg.write(chipAddress, full_data)
    bus.i2c_rdwr(msg)


def read_data(bus, chipAddress: int, addr: int, reg_num_bytes, data_num_bytes) -> int:
    addr_bytes = addr.to_bytes(reg_num_bytes, byteorder='big')
    write = i2c_msg.write(chipAddress, addr_bytes)
    bus.i2c_rdwr(write)
    # Add a small delay between write and read operations
    time.sleep(0.001)  # 1ms delay
    read = i2c_msg.read(chipAddress, data_num_bytes)
    bus.i2c_rdwr(read)
    data_bytes = bytes(read)
    return int.from_bytes(data_bytes, byteorder='big')


"""Executes a sequence of I2C commands including delays and write operations. Supports optional read-back verification and verbose output. Returns True on success, False on failure."""
def execute_i2c_sequence(i2c_bus, sequence, silent=False, rw_check=False, verbose=False):
    success = True
    with SMBus(i2c_bus) as bus:
        for cmd in sequence:
            if cmd[0] == "delay":
                time.sleep(cmd[1])
            elif cmd[0] == "write":
                slave, values = cmd[1] >> 1, cmd[2]
                if isinstance(values, tuple):
                    values = [values]
                for item in values:
                    try:
                        if len(item) == 8:
                            reg = (item[0][0] << 24) | (item[1][0] << 16) | (item[2][0] << 8) | item[3][0]
                            val = (item[4][0] << 24) | (item[5][0] << 16) | (item[6][0] << 8) | item[7][0]
                            if verbose:
                                print(f"\033[92m✅\033[0m WRITE to 0x{slave:02X} | REG: 0x{reg:0{4*2}X} | VAL: 0x{val:0{4*2}X}")
                            write_data(bus, slave, reg, val, 4, 4)
                            
                            if rw_check:
                                read_val = read_data(bus, slave, reg, 4, 4)
                                # Check if the read value is just the register address (indicating write-only register)
                                if read_val == (reg & 0xFF) or read_val == (reg & 0xFFFF) or read_val == (reg & 0xFFFFFFFF):
                                    if verbose and not silent:
                                        print(f"\033[93m⚠️\033[0m  Register 0x{reg:08X} appears to be write-only (echoed address: 0x{read_val:08X})")
                                elif read_val != val and not silent:
                                    print(f"\033[91m❌\033[0m Mismatch at slave 0x{slave:02X}, reg 0x{reg:08X}: wrote 0x{val:08X}, read 0x{read_val:08X}")
                        elif len(item) == 2:
                            reg_width_bytes = item[0][1] // 8
                            data_width_bytes = item[1][1] // 8
                            reg = item[0][0]
                            val = item[1][0]
                            if verbose:
                                print(f"\033[92m✅\033[0m WRITE to 0x{slave:02X} | REG: 0x{reg:0{reg_width_bytes*2}X} | VAL: 0x{val:0{data_width_bytes*2}X}")
                            write_data(bus, slave, reg, val, reg_width_bytes, data_width_bytes)
                            if rw_check:
                                read_val = read_data(bus, slave, reg, reg_width_bytes, data_width_bytes)
                                # Check if the read value is just the register address (indicating write-only register)
                                if read_val == (reg & 0xFF) or read_val == (reg & 0xFFFF):
                                    if verbose and not silent:
                                        print(f"\033[93m⚠️\033[0m  Register 0x{reg:0{reg_width_bytes*2}X} appears to be write-only (echoed address: 0x{read_val:0{data_width_bytes*2}X})")
                                elif read_val != val and not silent:
                                    print(f"\033[91m❌\033[0m Mismatch at slave 0x{slave:02X}, reg 0x{reg:04X}: wrote 0x{val:04X}, read 0x{read_val:04X}")
                    except Exception as e:
                        if not silent:
                            if verbose:
                                print(f"\033[91m❌\033[0m I2C error at slave 0x{slave:02X}: {e}")
                            else:
                                # Report critical I2C failures even when not in verbose mode
                                print(f"\033[91m❌ I2C WRITE FAILED\033[0m - Slave: 0x{slave:02X}, Error: {e}")
                        # Return failure indication for non-silent mode
                        if not silent:
                            success = False
    return success


"""Main entry point: parses CLI args, loads config from XML/INI, and executes I2C sequences."""
def main():
    parser = argparse.ArgumentParser(description="Apply I2C XML+INI config.", 
                                   usage='use "python %(prog)s --help" for more information',
                                   formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--xml_path", type=str, required=True, help="Path to the XML file")
    parser.add_argument("--ini_format", type=str, default="xylon", help="set to xylon for xylon format or ADI for Analog Device/MAXIM *.ini formats")
    parser.add_argument("--silent", action="store_true", help="Suppress output")
    parser.add_argument("--rw_check", action="store_true", help="Enable read/write verification")
    parser.add_argument("--verbose", action="store_true", help="Print detailed write operations")    
    parser.epilog = """Examples:\r
    python video_config.py --xml_path config.xml
    python video_config.py --xml_path config.xml --ini_format xylon
    python video_config.py --xml_path config.xml --rw_check --verbose
    python video_config.py --xml_path config.xml --silent

    Options:
    --xml_path    Path to XML config file (required)
    --ini_format  Format of INI files: 'xylon' or 'ADI' (default: xylon)
    --silent      Suppress output
    --rw_check    Enable read/write verification of all data written
    --verbose     Print all write operations with visual indicators
    """
    args = parser.parse_args()
    
    xml_path = Path(args.xml_path).resolve()
    ini_dir = xml_path.parent
    configs, initxt_configs = parse_xml_config(xml_path)

    overall_success = True

    # Process regular INI file configurations first
    for i2cdev, ini_file, variables in configs:
        bus_num = int(i2cdev.split("-")[-1])
        if not args.silent:
            print(f"\n📡 Configuring {ini_file} on {i2cdev}...")
        commands = parse_ini(ini_dir / ini_file, variables, ini_format=args.ini_format)
        resolved = apply_variables(commands, variables)
        success = execute_i2c_sequence(bus_num, resolved, silent=args.silent, rw_check=args.rw_check, verbose=args.verbose)
        if not success:
            overall_success = False

    # Process initxt configurations (postInit phase)
    for i2cdev, initxt_content, variables in initxt_configs:
        bus_num = int(i2cdev.split("-")[-1])
        if not args.silent:
            print(f"\n📡 Applying postInit initxt configuration on {i2cdev}...")
        commands = parse_initxt_commands(initxt_content, variables)
        resolved = apply_variables(commands, variables)
        success = execute_i2c_sequence(bus_num, resolved, silent=args.silent, rw_check=args.rw_check, verbose=args.verbose)
        if not success:
            overall_success = False

    return overall_success


if __name__ == "__main__":
    success = main()
    if success:
        print(f"\033[92m✅\033[0m CONFIGURATION DONE!")
    else:
        print(f"\033[91m❌ CONFIGURATION FAILED\033[0m - One or more I2C operations failed!")
        exit(1)
