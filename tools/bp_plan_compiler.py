#!/usr/bin/env python3
"""
bp_plan_compiler.py - Offline CopyPlan Compiler & Interval Coalescer

Key Responsibilities:
1. Matches struct fields between Old Version and New Version.
2. Target-ABI Isomorphism: Consumes layout data derived from the target compiler/DWARF.
3. Zero Bloat for Raw POD: The live module does not carry string tables or reflection metadata.
4. Explicit Safety Guard:
   - SKIPS vptr (virtual table pointer): Prevents overwriting new vtable with unloaded old vtable!
   - SKIPS raw/internal pointers: Prevents dangling pointers referencing deallocated memory!
5. Interval Coalescing: Merges adjacent matching byte ranges into single memcpy operations.
6. Emits:
   - Serialized binary .bp_plan file
   - C header/source with static const BpCopyPlan table for embedded zero-allocation deployment.
"""

import sys
import json
import struct
import zlib
import argparse
from typing import List, Dict, Any, Tuple

BP_PLAN_MAGIC = 0x4E414C50504D5542  # ASCII: "BUMPPLAN"
BP_PLAN_VERSION = 2

class CopyOp:
    def __init__(self, dst_off: int, src_off: int, length: int, fields: List[str]):
        self.dst_off = dst_off
        self.src_off = src_off
        self.len = length
        self.fields = list(fields)

    def __repr__(self):
        return f"CopyOp(dst=0x{self.dst_off:04X}, src=0x{self.src_off:04X}, len={self.len}, fields={self.fields})"

def fnv1a_64(data: bytes, hash_val: int = 0xcbf29ce484222325) -> int:
    for b in data:
        hash_val ^= b
        hash_val = (hash_val * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return hash_val

def compute_layout_hash(fields: List[Dict[str, Any]]) -> int:
    h = 0xcbf29ce484222325
    for f in fields:
        name_bytes = f["name"].encode("utf-8")
        h = fnv1a_64(name_bytes, h)
        h = fnv1a_64(struct.pack("<I", f["offset"]), h)
        h = fnv1a_64(struct.pack("<I", f["size"]), h)
        h = fnv1a_64(struct.pack("<H", f["type_id"]), h)
    return h

def compile_plan(src_meta: Dict[str, Any], dst_meta: Dict[str, Any], target_fn_id: int) -> Tuple[List[CopyOp], int, int]:
    src_fields = {f["name"]: f for f in src_meta["fields"]}
    dst_fields = {f["name"]: f for f in dst_meta["fields"]}

    # Identify matching fields
    matching = []
    skipped_vptrs = []
    skipped_ptrs = []

    for name, df in dst_fields.items():
        if name not in src_fields:
            continue
        sf = src_fields[name]

        # Check vptr
        if df.get("is_vptr", False) or sf.get("is_vptr", False) or name == "_vptr":
            skipped_vptrs.append((name, sf["offset"], df["offset"]))
            continue

        # Check pointer
        if df.get("is_pointer", False) or sf.get("is_pointer", False):
            skipped_ptrs.append((name, sf["offset"], df["offset"]))
            continue

        # Check type and size compatibility
        if df["size"] == sf["size"]:
            matching.append({
                "name": name,
                "src_off": sf["offset"],
                "dst_off": df["offset"],
                "size": df["size"]
            })

    # Sort matching fields by destination offset to enable contiguous coalescing
    matching.sort(key=lambda x: x["dst_off"])

    if skipped_vptrs:
        for name, soff, doff in skipped_vptrs:
            print(f"  [SAFETY GUARD] Explicitly skipped vptr '{name}' (src_off=0x{soff:X}, dst_off=0x{doff:X}) -> preserved fresh vtable")
    if skipped_ptrs:
        for name, soff, doff in skipped_ptrs:
            print(f"  [SAFETY GUARD] Explicitly skipped pointer '{name}' (src_off=0x{soff:X}, dst_off=0x{doff:X}) -> avoids dangling pointer")

    # Coalesce adjacent intervals
    coalesced: List[CopyOp] = []
    total_bytes = 0

    for m in matching:
        src_off = m["src_off"]
        dst_off = m["dst_off"]
        sz = m["size"]
        total_bytes += sz

        if coalesced:
            last = coalesced[-1]
            # Check if adjacent in BOTH source and destination!
            if (last.src_off + last.len == src_off) and (last.dst_off + last.len == dst_off):
                last.len += sz
                last.fields.append(m["name"])
                continue

        coalesced.append(CopyOp(dst_off, src_off, sz, [m["name"]]))

    return coalesced, total_bytes, len(matching)

def export_binary_plan(plan_file: str, target_fn_id: int, src_hash: int, dst_hash: int,
                       src_size: int, dst_size: int, ops: List[CopyOp], total_bytes: int):
    # Header format:
    # uint64 magic
    # uint32 version
    # uint32 target_fn_id
    # uint64 src_layout_hash
    # uint64 dst_layout_hash
    # uint32 src_inst_size
    # uint32 dst_inst_size
    # uint32 copy_op_count
    # uint32 total_copy_bytes
    # uint32 reserved
    payload = b"".join(struct.pack("<III", op.dst_off, op.src_off, op.len) for op in ops)
    plan_crc32 = zlib.crc32(payload) & 0xFFFFFFFF if payload else 0

    hdr = struct.pack(
        "<QIIQQIIIII",
        BP_PLAN_MAGIC,
        BP_PLAN_VERSION,
        target_fn_id,
        src_hash,
        dst_hash,
        src_size,
        dst_size,
        len(ops),
        total_bytes,
        plan_crc32
    )
    with open(plan_file, "wb") as f:
        f.write(hdr)
        f.write(payload)
    print(f"  [EMIT] Serialized binary plan to '{plan_file}' ({len(hdr) + len(payload)} bytes, CRC32=0x{plan_crc32:08X})")

def export_c_header(c_file: str, plan_var_name: str, target_fn_id: int,
                     src_hash: int, dst_hash: int, src_size: int, dst_size: int,
                     ops: List[CopyOp], total_bytes: int):
    with open(c_file, "w") as f:
        f.write("/* Auto-generated by bp_plan_compiler.py - DO NOT EDIT MANUALLY */\n")
        f.write("#ifndef " + plan_var_name.upper() + "_H\n")
        f.write("#define " + plan_var_name.upper() + "_H\n\n")
        f.write('#include "bumpless.h"\n\n')

        f.write(f"static const BpCopyOp {plan_var_name}_ops[{max(1, len(ops))}] = {{\n")
        for op in ops:
            field_comment = ", ".join(op.fields)
            f.write(f"    {{ {op.dst_off}u, {op.src_off}u, {op.len}u }}, /* [{field_comment}] */\n")
        if not ops:
            f.write("    { 0, 0, 0 }\n")
        f.write("};\n\n")

        f.write(f"static const BpCopyPlan {plan_var_name} = {{\n")
        f.write(f"    .target_fn_id     = {target_fn_id}u,\n")
        f.write(f"    .src_layout_hash  = 0x{src_hash:016X}ULL,\n")
        f.write(f"    .dst_layout_hash  = 0x{dst_hash:016X}ULL,\n")
        f.write(f"    .src_inst_size    = {src_size}u,\n")
        f.write(f"    .dst_inst_size    = {dst_size}u,\n")
        f.write(f"    .copy_op_count    = {len(ops)}u,\n")
        f.write(f"    .total_copy_bytes = {total_bytes}u,\n")
        f.write(f"    .copy_ops         = {plan_var_name}_ops\n")
        f.write("};\n\n")
        f.write("#endif\n")
    print(f"  [EMIT] Generated C static plan to '{c_file}'")

def main():
    parser = argparse.ArgumentParser(description="Compile Bumpless Precompiled CopyPlan")
    parser.add_argument("--src", required=True, help="Old version layout JSON")
    parser.add_argument("--dst", required=True, help="New version layout JSON")
    parser.add_argument("--fn-id", type=int, default=1, help="Target function slot ID")
    parser.add_argument("--out-bin", help="Output .bp_plan binary file")
    parser.add_argument("--out-c", help="Output .h static C table file")
    parser.add_argument("--var-name", default="g_copy_plan", help="C variable name for static plan")
    args = parser.parse_args()

    with open(args.src, "r") as f:
        src_meta = json.load(f)
    with open(args.dst, "r") as f:
        dst_meta = json.load(f)

    src_hash = int(src_meta.get("layout_hash", "0"), 16) or compute_layout_hash(src_meta["fields"])
    dst_hash = int(dst_meta.get("layout_hash", "0"), 16) or compute_layout_hash(dst_meta["fields"])
    src_size = src_meta["inst_size"]
    dst_size = dst_meta["inst_size"]

    print(f"[bp_plan_compiler] Compiling CopyPlan for fn_id={args.fn_id}:")
    print(f"  Src: {src_meta['struct']} (size={src_size}B, hash=0x{src_hash:016X})")
    print(f"  Dst: {dst_meta['struct']} (size={dst_size}B, hash=0x{dst_hash:016X})")

    if src_hash == dst_hash and src_size == dst_size:
        print("  -> FAST PATH: Struct layout identical, 0-copy pointer swap!")
        ops = []
        total_bytes = 0
        matching_count = len(src_meta["fields"])
    else:
        ops, total_bytes, matching_count = compile_plan(src_meta, dst_meta, args.fn_id)
        print(f"  -> Coalesced {matching_count} matching fields into {len(ops)} memcpy operations ({total_bytes} bytes total)")
        for i, op in enumerate(ops):
            print(f"     Op #{i+1}: dst_off={op.dst_off} src_off={op.src_off} len={op.len} -> fields: {op.fields}")

    if args.out_bin:
        export_binary_plan(args.out_bin, args.fn_id, src_hash, dst_hash, src_size, dst_size, ops, total_bytes)
    if args.out_c:
        export_c_header(args.out_c, args.var_name, args.fn_id, src_hash, dst_hash, src_size, dst_size, ops, total_bytes)

if __name__ == "__main__":
    main()
