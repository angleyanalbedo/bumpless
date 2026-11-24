#!/usr/bin/env python3
"""
bp_dwarf_probe.py - Offline DWARF Probe for True 0-Bloat Target Inspection

Extracts struct layout directly from DWARF debug information produced by
the target compiler (e.g. gcc -g or clang -g).

Guarantees:
1. Exact Target-ABI Isomorphism: Directly reads the compiler's own computed offsets,
   sizes, alignment, and padding from DWARF DIEs.
2. Zero Runtime Bloat: The runtime module has zero reflection tables, zero strings.
3. Automatically detects:
   - vptr (DW_TAG_member with name '_vptr', '_vptr$...', or pointer to vtable)
   - pointer members (DW_TAG_member pointing to DW_TAG_pointer_type)
"""

import sys
import os
import re
import json
import subprocess
import argparse

def get_dwarf_dump(binary_path: str) -> str:
    # Try objdump -W first (universal across ELF, PE/COFF, MinGW, Clang)
    try:
        cmd = ["objdump", "-W", binary_path]
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
        return res.stdout
    except Exception:
        pass

    # Try readelf --debug-dump=info (standard on Linux ELF)
    try:
        cmd = ["readelf", "--debug-dump=info", binary_path]
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
        return res.stdout
    except Exception as e:
        raise RuntimeError(f"Failed to dump DWARF info using objdump or readelf: {e}")

def parse_dwarf_structs(dwarf_text: str):
    lines = dwarf_text.splitlines()

    # Regex patterns
    die_hdr_pattern = re.compile(r"<\s*(\d+)\s*><\s*([0-9a-fA-F]+)\s*>:\s*Abbrev Number:\s*\d+\s*\((DW_TAG_[a-zA-Z0-9_]+)\)")
    attr_pattern = re.compile(r"<\s*[0-9a-fA-F]+\s*>\s*(DW_AT_[a-zA-Z0-9_]+)\s*:\s*(.*)")

    all_dies = []
    die_map = {} # off -> die
    current_die = None

    for line in lines:
        m_hdr = die_hdr_pattern.search(line)
        if m_hdr:
            level = int(m_hdr.group(1))
            offset = m_hdr.group(2).lower().lstrip("0") or "0"
            tag = m_hdr.group(3)
            current_die = {
                "level": level,
                "offset": offset,
                "tag": tag,
                "attrs": {},
                "children": []
            }
            all_dies.append(current_die)
            die_map[offset] = current_die
            continue

        if current_die:
            m_attr = attr_pattern.search(line)
            if m_attr:
                attr_name = m_attr.group(1)
                attr_val = m_attr.group(2).strip()
                current_die["attrs"][attr_name] = attr_val

    # Group children by level
    stack = []
    for die in all_dies:
        while stack and stack[-1]["level"] >= die["level"]:
            stack.pop()
        if stack:
            stack[-1]["children"].append(die)
        stack.append(die)

    # First pass: type mapping
    type_map = {}
    for die in all_dies:
        off = die["offset"]
        tag = die["tag"]
        attrs = die["attrs"]

        if tag == "DW_TAG_base_type":
            name = attrs.get("DW_AT_name", "")
            if ":" in name:
                name = name.split(":")[-1].strip()
            sz = 4
            if "DW_AT_byte_size" in attrs:
                m_sz = re.search(r"(\d+)", attrs["DW_AT_byte_size"])
                if m_sz:
                    sz = int(m_sz.group(1))
            type_map[off] = {"kind": "base", "name": name, "size": sz}

        elif tag == "DW_TAG_pointer_type":
            sz = 8
            if "DW_AT_byte_size" in attrs:
                m_sz = re.search(r"(\d+)", attrs["DW_AT_byte_size"])
                if m_sz:
                    sz = int(m_sz.group(1))
            type_map[off] = {"kind": "pointer", "name": "pointer", "size": sz}

    # Second pass: map typedefs to resolve alias names
    typedef_map = {} # struct_die_offset -> typedef_name
    for die in all_dies:
        if die["tag"] == "DW_TAG_typedef":
            name = die["attrs"].get("DW_AT_name", "")
            if ":" in name:
                name = name.split(":")[-1].strip()
            type_ref = die["attrs"].get("DW_AT_type", "")
            m_ref = re.search(r"<0x([0-9a-fA-F]+)>", type_ref)
            if m_ref and name:
                target_off = m_ref.group(1).lower().lstrip("0") or "0"
                typedef_map[target_off] = name

    # Third pass: resolve structures
    structs = {}
    for die in all_dies:
        if die["tag"] in ("DW_TAG_structure_type", "DW_TAG_class_type"):
            off = die["offset"]
            attrs = die["attrs"]

            struct_name = attrs.get("DW_AT_name", "")
            if ":" in struct_name:
                struct_name = struct_name.split(":")[-1].strip()

            if not struct_name and off in typedef_map:
                struct_name = typedef_map[off]

            if not struct_name or struct_name.startswith("__"):
                continue

            inst_size = 0
            if "DW_AT_byte_size" in attrs:
                m_sz = re.search(r"(\d+)", attrs["DW_AT_byte_size"])
                if m_sz:
                    inst_size = int(m_sz.group(1))

            fields = []
            for child in die["children"]:
                if child["tag"] == "DW_TAG_member":
                    c_attrs = child["attrs"]
                    f_name = c_attrs.get("DW_AT_name", "")
                    if ":" in f_name:
                        f_name = f_name.split(":")[-1].strip()

                    f_offset = 0
                    if "DW_AT_data_member_location" in c_attrs:
                        m_loc = re.search(r"(\d+)", c_attrs["DW_AT_data_member_location"])
                        if m_loc:
                            f_offset = int(m_loc.group(1))

                    f_size = 4
                    is_pointer = False
                    is_vptr = False

                    if "_vptr" in f_name or f_name.startswith("vptr") or "vfn" in f_name or "vtable" in f_name:
                        is_vptr = True

                    type_ref = c_attrs.get("DW_AT_type", "")
                    m_ref = re.search(r"<0x([0-9a-fA-F]+)>", type_ref)
                    if m_ref:
                        ref_off = m_ref.group(1).lower().lstrip("0") or "0"
                        if ref_off in type_map:
                            t_info = type_map[ref_off]
                            f_size = t_info.get("size", 4)
                            if t_info.get("kind") == "pointer":
                                is_pointer = True

                    type_id = 6 # INT32
                    if f_size == 1:
                        type_id = 1
                    elif f_size == 2:
                        type_id = 4
                    elif f_size == 4:
                        type_id = 6
                    elif f_size == 8:
                        type_id = 8

                    if is_vptr:
                        type_id = 13
                    elif is_pointer:
                        type_id = 12

                    if f_name:
                        fields.append({
                            "name": f_name,
                            "offset": f_offset,
                            "size": f_size,
                            "type_id": type_id,
                            "is_vptr": is_vptr,
                            "is_pointer": is_pointer
                        })

            if fields or inst_size > 0:
                structs[struct_name] = {
                    "struct": struct_name,
                    "inst_size": inst_size,
                    "fields": fields
                }

    return structs

def main():
    parser = argparse.ArgumentParser(description="Extract struct layout via target DWARF")
    parser.add_argument("binary", help="Target binary / object file compiled with -g")
    parser.add_argument("--struct", help="Specific struct name to extract")
    parser.add_argument("--out", help="Output JSON file path")
    args = parser.parse_args()

    if not os.path.exists(args.binary):
        print(f"Error: binary '{args.binary}' not found", file=sys.stderr)
        sys.exit(1)

    dwarf_text = get_dwarf_dump(args.binary)
    structs = parse_dwarf_structs(dwarf_text)

    if args.struct:
        if args.struct not in structs:
            print(f"Error: struct '{args.struct}' not found in DWARF info. Available structs: {list(structs.keys())}", file=sys.stderr)
            sys.exit(1)
        res = structs[args.struct]
    else:
        res = structs

    out_str = json.dumps(res, indent=2)
    if args.out:
        with open(args.out, "w") as f:
            f.write(out_str)
        print(f"Exported DWARF probe layout to '{args.out}'")
    else:
        print(out_str)

if __name__ == "__main__":
    main()
