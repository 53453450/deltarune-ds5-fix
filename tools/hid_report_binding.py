#!/usr/bin/env python3
"""诊断「HID 元素已声明但值永不更新」——查描述符与输入报文的绑定关系。

背景：macOS 会把蓝牙手柄重发布成用户态 `IOHIDUserDevice`。这种设备可能把按键
元素声明在某条 Report ID 上，而链路实际上行的是**另一条** Report ID，导致按键
元素的值永远不更新 → 依赖「元素值回调」的代码（IOHIDDeviceRegisterInputValueCallback）
彻底收不到输入，而读「原始报文」的代码（HIDAPI/SDL）却一切正常。

本工具就是用来一眼看出这个错配的。

用法:
    ./hid_report_binding.py [--device 关键词]

默认抓取 `ioreg -r -c IOHIDDevice -l`，按设备输出：
  - IOKit 类（内核态 AppleUserHIDDevice / 用户态 IOHIDUserDevice）
  - ReportDescriptor 字节数
  - 描述符里声明的全部 Report ID
  - 按钮（UsagePage 9, Usage Minimum 1）声明在哪条 Report ID 下
  - InputReportElements：每条 Report ID 的实际报文尺寸

判据：如果「按钮所在的 Report ID」在 InputReportElements 里的尺寸很小（几十 bit 的桩），
      而另有一条尺寸合理的 Report ID，那基本就是本次遇到的错配。
"""

import argparse
import re
import subprocess
import sys


def ioreg_dump() -> str:
    try:
        return subprocess.run(["ioreg", "-r", "-c", "IOHIDDevice", "-l"],
                              capture_output=True, text=True, check=True).stdout
    except FileNotFoundError:
        sys.exit("错误: 找不到 ioreg")
    except subprocess.CalledProcessError as exc:
        sys.exit(f"错误: ioreg 失败: {exc.stderr.strip()}")


def blocks(text: str):
    lines = text.splitlines()
    starts = [i for i, l in enumerate(lines) if re.match(r"^\s*[|+ ]*\+-o\s", l)]
    for k, s in enumerate(starts):
        e = starts[k + 1] if k + 1 < len(starts) else len(lines)
        yield lines[s].strip(), "\n".join(lines[s:e])


def parse_report_ids(desc_hex: str):
    return [int(x, 16) for x in re.findall(r"85([0-9a-f]{2})", desc_hex)]


def button_report_id(desc_hex: str):
    """找 '05 09 19 01 ...'（UsagePage=Button, Usage Minimum=1）之前最近的 Report ID 声明。"""
    i = desc_hex.find("05091901")
    if i < 0:
        i = desc_hex.find("0509")          # 退化：只找按钮页
        if i < 0:
            return None
    j = desc_hex.rfind("85", 0, i)
    return int(desc_hex[j + 2:j + 4], 16) if j > 0 else None


def main() -> None:
    ap = argparse.ArgumentParser(description="HID 元素/报文绑定诊断")
    ap.add_argument("--device", default="", help="只显示 Product 含该关键词的设备")
    ap.add_argument("--dump", default="", help="改用已保存的 ioreg 文本，而不是现场抓取")
    args = ap.parse_args()

    text = open(args.dump, encoding="utf-8", errors="replace").read() if args.dump else ioreg_dump()

    seen = 0
    for head, body in blocks(text):
        if args.device and args.device not in body:
            continue
        m = re.search(r'"Product"\s*=\s*("?[^,\n]+)', body)
        if not m:
            continue
        product = m.group(1).strip().strip('"')
        cls = re.search(r"<class (\w+)", head)
        cls = cls.group(1) if cls else "?"
        transport = re.search(r'"Transport"\s*=\s*"([^"]+)"', body)

        rd = re.search(r'"ReportDescriptor"\s*=\s*<([0-9a-f]+)>', body)
        if not rd:
            continue
        desc = rd.group(1)
        rids = parse_report_ids(desc)
        btn_rid = button_report_id(desc)

        ire = re.search(r'"InputReportElements"\s*=\s*\((.*?)\)\s*\n', body, re.S)
        sizes = {}
        if ire:
            for rid, _ck, size in re.findall(
                    r'"ReportID"=(\d+),"ElementCookie"=(\d+),"Size"=(\d+)', ire.group(1)):
                sizes[int(rid)] = int(size)

        seen += 1
        print(f"===== {product}  [{cls}]"
              f"{'  Transport=' + transport.group(1) if transport else ''} =====")
        print(f"  ReportDescriptor : {len(desc) // 2} 字节")
        print(f"  声明的 Report ID : {rids}")
        print(f"  按钮(Page9/Min1) 所在 Report ID : {btn_rid}")
        if sizes:
            print("  InputReportElements（报文尺寸, bit）:")
            for rid in sorted(sizes, key=lambda r: -sizes[r]):
                mark = "  <-- 按钮所在" if rid == btn_rid else ""
                print(f"     ReportID {rid:>3} : {sizes[rid]:>4} bit ({sizes[rid] // 8} 字节){mark}")
            if btn_rid is not None and btn_rid in sizes:
                biggest = max(sizes, key=lambda r: sizes[r])
                if btn_rid != biggest and sizes[btn_rid] <= 128:
                    print(f"  !! 可疑：按钮绑在 ReportID {btn_rid}（{sizes[btn_rid]} bit 的桩），"
                          f"而链路有效报文很可能是 ReportID {biggest}（{sizes[biggest]} bit）")
                    print("     → 元素值可能永不更新；读原始报文的程序（HIDAPI/SDL）不受影响，")
                    print("        依赖元素值回调的程序（如 GameMaker 运行时）会完全收不到输入。")
        else:
            print("  InputReportElements : 无")
        print()

    if seen == 0:
        print("没有匹配到带 ReportDescriptor 的设备。接上手柄再试。")


if __name__ == "__main__":
    main()
