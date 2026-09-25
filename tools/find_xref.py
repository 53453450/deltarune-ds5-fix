#!/usr/bin/env python3
"""在 Mach-O 反汇编里按「内容匹配」反查字符串的交叉引用。

为什么需要它：`strings -t d` 给出的偏移不能直接当作 VA 用 —— 在通用二进制上
算出来的地址会落在填充区（0xFF），得到假结果。可靠做法是遍历所有 rip-relative
引用、算出目标 VA，再直接读切片字节做比对。

用法:
    ./find_xref.py <mach-o> "<字符串>" [更多字符串...] [--arch x86_64] [--context N]

例:
    ./find_xref.py Mac_Runner "gamecontrollerdb.txt" "SDL_GAMECONTROLLERCONFIG"
    ./find_xref.py libYoYoGamepad.dylib "VendorID" "ProductID" "VersionNumber"

输出: 命中的目标 VA、引用指令地址、该指令的汇编文本。

限制: 只解析 x86_64 的 `disp(%rip)` 寻址。arm64 用 ADRP+ADD 两指令组合，
      本工具不处理；但通用二进制里两个切片的代码等价，用 --arch x86_64 即可。
"""

import argparse
import re
import struct
import subprocess
import sys

FAT_MAGIC_BE = {0xCAFEBABE, 0xCAFEBABF}
FAT_MAGIC_LE = {0xBEBAFECA, 0xBFBAFECA}
CPU_TYPES = {"x86_64": 0x01000007, "arm64": 0x0100000C, "arm64e": 0x0100000C}
LC_SEGMENT_64 = 0x19

ADDR_RE = re.compile(r"^([0-9a-f]{8,16})\t")
RIP_RE = re.compile(r"(-?0x[0-9a-f]+)\(%rip\)")


def find_slice(raw: bytes, arch: str) -> int:
    """返回指定架构切片的文件偏移；非通用二进制返回 0。"""
    if struct.unpack(">I", raw[:4])[0] not in FAT_MAGIC_BE:
        return 0
    n = struct.unpack(">I", raw[4:8])[0]
    want = CPU_TYPES[arch]
    for i in range(n):
        base = 8 + i * 20
        cputype, _sub, off, _size, _align = struct.unpack(">IIIII", raw[base:base + 20])
        if cputype == want:
            return off
    sys.exit(f"错误: 该文件没有 {arch} 切片")


def find_text_segment(raw: bytes, base: int) -> tuple[int, int]:
    """返回 __TEXT 段的 (vmaddr, fileoff)，fileoff 相对切片起点。"""
    header = raw[base:base + 32]
    if len(header) < 32:
        sys.exit("错误: 文件头不完整")
    _magic, _cpu, _sub, _ft, ncmds, _szcmds, _flags, _res = struct.unpack("<IiiIIIII", header)
    p = base + 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack("<II", raw[p:p + 8])
        if cmd == LC_SEGMENT_64:
            segname = raw[p + 8:p + 24].rstrip(b"\0").decode("latin1")
            vmaddr, _vmsize, fileoff, _filesize = struct.unpack("<QQQQ", raw[p + 24:p + 56])
            if segname == "__TEXT":
                return vmaddr, fileoff
        p += cmdsize
    sys.exit("错误: 找不到 __TEXT 段")


def read_cstring(raw: bytes, pos: int, limit: int = 96) -> bytes:
    if pos < 0 or pos >= len(raw):
        return b""
    end = raw.find(b"\0", pos, pos + limit)
    if end < 0:
        end = min(pos + limit, len(raw))
    return raw[pos:end]


def disassemble(path: str, arch: str) -> str:
    try:
        return subprocess.run(
            ["otool", "-arch", arch, "-tvV", path],
            capture_output=True, text=True, check=True,
        ).stdout
    except FileNotFoundError:
        sys.exit("错误: 找不到 otool（需要安装 Xcode Command Line Tools）")
    except subprocess.CalledProcessError as exc:
        sys.exit(f"错误: otool 失败: {exc.stderr.strip()}")


def main() -> None:
    ap = argparse.ArgumentParser(description="Mach-O rip-relative 交叉引用查找器")
    ap.add_argument("binary")
    ap.add_argument("needles", nargs="+", help="目标字符串（可多个）")
    ap.add_argument("--arch", default="x86_64", choices=sorted(CPU_TYPES))
    ap.add_argument("--context", type=int, default=2,
                    help="命中后额外打印前后各 N 行汇编（默认 2）")
    args = ap.parse_args()

    raw = open(args.binary, "rb").read()
    base = find_slice(raw, args.arch)
    vmaddr, fileoff = find_text_segment(raw, base)

    def va_to_pos(va: int) -> int:
        return base + fileoff + (va - vmaddr)

    lines = disassemble(args.binary, args.arch).splitlines()
    addrs = [int(m.group(1), 16) if (m := ADDR_RE.match(ln)) else None for ln in lines]

    targets = [n.encode() for n in args.needles]
    scanned = hits = 0

    for i, line in enumerate(lines):
        if len(line) < 18 or line[16] != "\t":
            continue
        mm = RIP_RE.search(line)
        if not mm or addrs[i] is None:
            continue
        disp = int(mm.group(1), 16)
        if disp > 0x7FFFFFFF:
            disp -= 0x100000000
        nxt = next((addrs[j] for j in range(i + 1, len(addrs)) if addrs[j] is not None), None)
        if nxt is None:
            continue
        scanned += 1
        target = nxt + disp
        text = read_cstring(raw, va_to_pos(target))
        for needle in targets:
            if text.startswith(needle):
                hits += 1
                print(f'命中 "{needle.decode()}"')
                print(f"    目标 VA   : {target:#x}")
                print(f"    引用指令  : {addrs[i]:#x}")
                print(f"    内容      : {text[:72].decode('latin1')!r}")
                lo = max(0, i - args.context)
                hi = min(len(lines), i + args.context + 1)
                for ln in lines[lo:hi]:
                    print(f"      {ln.strip()}")
                print()
                break

    print(f"扫描 rip-relative 引用 {scanned} 条，命中 {hits} 处。")
    if not hits:
        print("提示: 字符串可能由 objc cfstring 结构间接引用，或该字符串不在本工具覆盖的寻址形态内。")


if __name__ == "__main__":
    main()
