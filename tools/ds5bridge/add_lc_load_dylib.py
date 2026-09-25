#!/usr/bin/env python3
"""把 libDS5RawFix.dylib 插入 Mac_Runner/libYoYoGamepad.dylib 的加载列表。

在 Mach-O 头部空闲区插入一个 LC_LOAD_DYLIB，不移动任何既有数据
（切片里 sizeofcmds 末尾到第一个 section 之间通常有对齐填充，够放）。

用法:
    ./add_lc_load_dylib.py <目标 dylib> <依赖路径> [--dry-run]
"""

import argparse
import struct
import sys

LC_SEGMENT_64 = 0x19
LC_LOAD_DYLIB = 0x0C
MH_MAGIC_64 = 0xFEEDFACF
FAT_MAGICS = (0xCAFEBABE, 0xCAFEBABF)


def align8(n: int) -> int:
    return (n + 7) & ~7


def make_lc(path: str) -> bytes:
    name = path.encode() + b"\0"
    cmdsize = align8(24 + len(name))
    lc = struct.pack("<IIIIII", LC_LOAD_DYLIB, cmdsize, 24, 0, 0, 0) + name
    return lc + b"\0" * (cmdsize - len(lc))


def slice_offsets(raw: bytes):
    """返回 [(slice_file_offset, slice_size)]；非 fat 返回 [(0, len)]。"""
    if struct.unpack(">I", raw[:4])[0] not in FAT_MAGICS:
        return [(0, len(raw))]
    n = struct.unpack(">I", raw[4:8])[0]
    out = []
    for i in range(n):
        b = 8 + i * 20
        _cpu, _sub, off, size, _al = struct.unpack(">IIIII", raw[b:b + 20])
        out.append((off, size))
    return out


def patch_slice(raw: bytearray, base: int, lc: bytes, label: str, dry: bool) -> bool:
    magic, _cpu, _sub, _ft, ncmds, sizeofcmds, _fl, _res = struct.unpack(
        "<IiiIIIII", raw[base:base + 32])
    if magic != MH_MAGIC_64:
        print(f"  [{label}] 不是 MH_MAGIC_64，跳过")
        return False

    first_section_off = None
    p = base + 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack("<II", raw[p:p + 8])
        if cmd == LC_SEGMENT_64:
            segname = raw[p + 8:p + 24].rstrip(b"\0").decode("latin1")
            nsects = struct.unpack("<I", raw[p + 64:p + 68])[0]
            q = p + 72
            for _s in range(nsects):
                _name = raw[q:q + 16].rstrip(b"\0").decode("latin1")
                _addr, _size, offset = struct.unpack("<QQI", raw[q + 32:q + 52])
                # 只看 __TEXT 段：__bss 之类的 offset 为 0，会把最小值带偏
                if segname == "__TEXT" and offset and (
                        first_section_off is None or offset < first_section_off):
                    first_section_off = offset
                q += 80
        p += cmdsize

    write_at = base + 32 + sizeofcmds
    limit = base + first_section_off if first_section_off else len(raw)
    room = limit - write_at
    print(f"  [{label}] ncmds={ncmds} sizeofcmds={sizeofcmds} "
          f"首 section 偏移={first_section_off} 可用余量={room} 字节，需要={len(lc)}")

    if room < len(lc):
        print(f"  [{label}] ✗ 余量不足")
        return False
    if dry:
        print(f"  [{label}] (dry-run) 将在文件偏移 {write_at} 写入 LC_LOAD_DYLIB")
        return True

    raw[write_at:write_at + len(lc)] = lc
    struct.pack_into("<I", raw, base + 16, ncmds + 1)          # ncmds
    struct.pack_into("<I", raw, base + 20, sizeofcmds + len(lc))  # sizeofcmds
    print(f"  [{label}] ✓ 已插入（ncmds -> {ncmds + 1}, "
          f"sizeofcmds -> {sizeofcmds + len(lc)}）")
    return True


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("target")
    ap.add_argument("dep_path")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    raw = bytearray(open(args.target, "rb").read())
    lc = make_lc(args.dep_path)
    print(f"目标: {args.target}")
    print(f"依赖: {args.dep_path}  (LC 大小 {len(lc)} 字节)")

    ok = 0
    for i, (off, _size) in enumerate(slice_offsets(bytes(raw))):
        if patch_slice(raw, off, lc, f"slice{i}@0x{off:x}", args.dry_run):
            ok += 1

    if not args.dry_run and ok:
        open(args.target, "wb").write(raw)
        print(f"已写回 {args.target}")
    elif ok == 0:
        sys.exit("没有任何切片被修改")


if __name__ == "__main__":
    main()
