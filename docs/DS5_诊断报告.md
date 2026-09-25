# DELTARUNE (macOS) DualSense 手柄支持诊断报告

- 目标：DELTARUNE.app（Steam AppID 1671210），GameMaker Studio 2 (`Mac_Runner`) 导出
- 设备：Sony DualSense Wireless Controller，VID `0x054C` / PID `0x0CE6`
- 症状：有线下面键错位、蓝牙下完全无响应

> 本报告只记录**根因与证据**。具体修复方式见 [`README.md`](../README.md)。

---

## 1. 结论

两段症状是**两个互不相关的缺陷**：

| 连接方式 | 症状 | 根因 |
|---|---|---|
| 有线（USB） | 面键错位：显示 `confirm=✕ / cancel=◯`，实际要按 `□` / `✕`；`△` 正常 | 运行时内嵌的 GUID→映射表**没有 DualSense 条目**，索引原样透传 |
| 蓝牙 | 按任何键毫无反应 | macOS 蓝牙桥接把整条报文当作**单个不透明字段**透传，从不解码成 GamePad 字段 → 元素值永不更新 → 运行时的元素值回调一次都不触发 |

两者都不是游戏 bug，也不是手柄故障。

---

## 2. 有线连接：面键错位的根因

### 2.1 运行时结构：GameMaker 不用 SDL

| 对象 | 事实 |
|---|---|
| `Contents/MacOS/Mac_Runner` | 通用二进制；含 `Gamepad_Class.cpp`、`libYoYoGamepad.dylib` 加载器、`Unrecognised Controller`、`Unable to parse gamepad mapping value` |
| `Contents/Frameworks/libYoYoGamepad.dylib` | 仅依赖 `IOKit / CoreFoundation / libobjc`，**无 libSDL**。导出 `IOHIDManagerCreate` / `IOHIDDeviceRegisterInputValueCallback` / `SGamepadMapping::FindFromGUID` / `YoYoTranslateGamepadButtonM` |

设备通过 `IOHIDManager` 直接读取，按键通过 `FindFromGUID` 查表重排。

### 2.2 内嵌映射表里没有 DualSense

`Mac_Runner` 内嵌 156 条 `platform:Mac OS X` 映射。Sony 相关只有：

```
030000004c05000068020000...,PS3 Controller
030000004c050000c4050000...,PS4 Controller          (054c:05c4)
030000004c050000cc090000...,Sony DualShock 4 V2     (054c:09cc)
030000004c050000a00b0000...,Sony DualShock 4 Wireless Adaptor
```

**全表 0 条 `054c:0ce6`（DualSense）。** 表里另有 `Steam Virtual GamePad`
（`030000005e0400008e02000001000000`）。

对照表里 PS4 的条目 `a:b1,b:b2,x:b0,y:b3` —— DualSense 与 DS4 的面键 HID 顺序相同，
**只要这条映射在就能正确工作**；缺失时 `FindFromGUID` 返回 NULL，索引原样透传（identity）。

### 2.3 设备侧 HID 实测

```
"VendorID"  = 1356          (0x054C)
"ProductID" = 3302          (0x0CE6)
"Transport" = "USB"
"PrimaryUsagePage" = 1, "PrimaryUsage" = 5   (Generic Desktop / Game Pad)
```

`UsagePage = 0x09 (Button)` 元素的枚举顺序：

| 枚举索引 | Usage | 物理按键 |
|---|---|---|
| 0 | 1 | **□ Square** |
| 1 | 2 | **✕ Cross** |
| 2 | 3 | **◯ Circle** |
| 3 | 4 | **△ Triangle** |
| 4–7 | 5–8 | L1 / R1 / L2 / R2 |
| 8–13 | 9–14 | Create / Options / L3 / R3 / PS / Touchpad |

DualSense 描述符按 `□ ✕ ◯ △` 排列，而 Xbox/SDL 语义要求 `✕ ◯ □ △` —— **0 号与 1 号恰好互换**。

### 2.4 游戏侧绑定

`~/Library/Application Support/com.tobyfox.deltarune/keyconfig_0.ini`：

```ini
[GAMEPAD_CONTROLS]
4="32769.000000"   ; gp_face1 -> confirm
5="32770.000000"   ; gp_face2 -> cancel
6="32772.000000"   ; gp_face4 -> menu
[KEYBOARD_CONTROLS]
4="90"  ; Z  -> confirm
5="88"  ; X  -> cancel
6="67"  ; C  -> menu
```

（`32769/32770/32771/32772` = GameMaker `gp_face1..4`；键盘段印证槽位语义：4=确认、5=取消、6=菜单。）

### 2.5 症状闭合

运行时无映射 → 枚举索引直接落到 face 槽位：

| GameMaker 槽位 | 游戏绑定 | 实际落到 | 屏幕显示 | 需实按 |
|---|---|---|---|---|
| `gp_face1` | confirm | 索引 0 = **□** | ✕ | □ ✔ |
| `gp_face2` | cancel | 索引 1 = **✕** | ◯ | ✕ ✔ |
| `gp_face3` | *未绑定* | 索引 2 = ◯ | — | 无反应 |
| `gp_face4` | menu | 索引 3 = **△** | △ | △ ✔ |

三处症状全部吻合；「◯ 无反应」也得到解释 —— `gp_face3` 在 DELTARUNE 里没有绑定任何动作。

### 2.6 映射表查找路径与 GUID 算法（反汇编实证）

`Mac_Runner` 的 gamepad 初始化函数（x86_64，`0x1002cffd5`–`0x1002d0069`）：

```asm
0x1002cffd5  movq  _pGameControllDB(%rip), %rbx            ; 内嵌 156 条
0x1002cffe9  callq SGamepadMapping::CreateFromFileAsString

0x1002cfff5  leaq  "gamecontrollerdb.txt", %rdi
0x1002cfffc  callq LoadSave::SaveFileExists                ; ① 存档区优先
0x1002d0010  callq LoadSave::ReadSaveFile                  ;    ~/Library/Application Support/<bundle id>/

0x1002d0017  callq LoadSave::BundleFileExists              ; ② 否则 app 包内
0x1002d0020  callq LoadSave::ReadBundleFile

0x1002d003d  callq SGamepadMapping::CreateFromFileAsString ; 逐条追加

0x1002d004a  leaq  "SDL_GAMECONTROLLERCONFIG", %rdi
0x1002d0051  callq EnvironmentGetVariable                  ; ③ 环境变量兜底
0x1002d005e  callq SGamepadMapping::CreateFromString
```

**存档目录是第一优先，改动它不需要碰 app 包、不影响签名。**

GUID 由 `libYoYoGamepad.dylib` 现场计算（`0x4bed`–`0x4cb6`）：

```asm
0x4bed  leaq "VendorID"      ; IOHIDDeviceGetProperty -> CFNumberGetValue
0x4c12  leaq "ProductID"
0x4c37  leaq "VersionNumber"

0x4c6e  movl $0x3,    guid[0..3]    ; bus 硬编码 3 = USB
0x4c76  movw vendor,  guid[4..5]    ; 小端
0x4c7b  movw $0x0,    guid[6..7]
0x4c82  movw product, guid[8..9]    ; 小端
0x4c87  movw $0x0,    guid[10..11]
0x4c92  movw version, guid[12..13]  ; 小端
0x4c97  movw $0x0,    guid[14..15]
0x4cba  callq _FindMappingFromGUID  ; 查不到 -> mapping = NULL -> identity

; vendor==0 || product==0 分支（典型为蓝牙）
0x4ca0  movl $0x5, guid[0..3]       ; bus = 5
0x4caf  guid[4..15] = 设备名("Product") 前 12 字节
```

代入实测值（`0x054C` / `0x0CE6` / `VersionNumber 0x0100`）：

```
03000000 4c05 0000 e60c 0000 0001 0000
 bus=3    vendor   product  version
→ 030000004c050000e60c000000010000
```

蓝牙名称型 fallback：`05000000` + `"DualSense Wireless Controller"` 前 12 字节 `DualSense Wi`
→ `050000004475616c53656e7365205769`

> 注意 **CRC 字段恒为 0**；Steam 日志里那条带 `crc:5657` 的 GUID 不适用。

### 2.7 Steam 侧旁证

`~/Library/Application Support/Steam/logs/controller.txt`：

```
SDL Mapping for 54c/ce6: 050057564c050000e60c000000016800,*,a:b0,b:b1,...crc:5657,platform:macOS,
Controller using HIDAPI driver, vid=0x054c, pid=0x0ce6
```

Steam 自己也得靠 `crc:5657` 现造一条映射 —— 官方 SDL_GameControllerDB 里同样没有 DualSense
（SDL 靠 HIDAPI 原生驱动处理，不依赖 db）。

---

## 3. 蓝牙连接：完全无响应的根因

### 3.1 已排除（全部为实测，非推断）

| 假设 | 实测 | 结论 |
|---|---|---|
| 蓝牙没连上 | `Services: 0x800020 <HID ACL>`，USB 树为空 | ✗ |
| IOKit 匹配不到设备 | 用运行时同款匹配字典 `{1,4}/{1,5}/{1,8}` 写探针 → 匹配 1 台 | ✗ |
| 设备打不开 | `IOHIDDeviceOpen -> 0x0`，Button 元素 14 个 | ✗ |
| 输入通路死了 | vendor 页事件稳定 ~64 次/秒 | ✗ |
| vendor 页元素挤占索引 | 运行时只认 `type∈{1,2,3}` ∧ `page∈1..12`，vendor 页一律丢弃 | ✗ |
| 计数与填充两趟不一致 | `CountHIDElements` 与 `CollectHIDElements` 过滤规则**逐指令一致** | ✗ |
| 按钮索引空间不同 | 蓝牙与 USB 都是 14 个按钮、usage 顺序 1..14 | ✗ |
| 运行时 IOKit 调用顺序有竞态 | A/B 在**各自独立进程**复现 canonical 与 runner 两种顺序，都能拿到设备 | ✗ |
| 游戏崩溃 | `DiagnosticReports` 无相关记录 | ✗ |
| 补的 `gamecontrollerdb.txt` 导致 | 用户实测：有无该文件表现一致 | ✗ |
| 系统/驱动层问题 | 同期另一款游戏（Control）蓝牙下操作正常 | ✗ |

### 3.2 实测到的真实差异

| | USB | 蓝牙 |
|---|---|---|
| IOKit 设备类 | `AppleUserHIDDevice` | **`IOHIDUserDevice`** |
| Transport | USB | Bluetooth |
| `DeviceUsagePairs` | `{1,5}` | `{1,5}`（相同，匹配不受影响）|
| 元素总数 / 按钮 | 123 / 14 | 132 / 14 |
| 轴顺序 | `X Y Z Rz Rx Ry Hat` | **`X Y Z Rz Hat Rx Ry`** |

轴顺序差异只会影响右摇杆，**解释不了按钮全灭**。

### 3.3 根因（已确认）

实测判据：按键时按钮监听探针**零输出**，而同窗口 vendor 页事件 19130 条（~64/秒）。
即：**上报在流，但按钮元素的 value 从不变化。**

比对两种传输方式的 HID 描述符与输入报文绑定：

| | USB | 蓝牙 |
|---|---|---|
| `ReportDescriptor` | 289 字节 | 320 字节 |
| 描述符声明的 Report ID | `[1, 2, 5, 8, 9, 10, 11, 12, 32, 33, 34, …]` | `[1, **49**, 50, 51, 52, 53, 54, 55, 56, 57, 5, 8, …]` |
| 面键声明所在 Report | **Report ID 1** | **Report ID 1** |
| `InputReportElements` 里 Report 1 的尺寸 | **512 bit**（64 字节，完整） | **80 bit**（10 字节，桩）|
| 链路实际上行报文 | Report 1 | **Report 49（0x31，624 bit / 78 字节）** |

**机制**：

1. 蓝牙下 macOS 把物理设备重发布成一个用户态 `IOHIDUserDevice`；
2. 该描述符把 Report 49 声明为**单个不透明 vendor 字段**（`usagePage 0xFF00` / `usage 59` / 616 bit），
   完全不解码成 GamePad 字段；
3. 面键虽被声明在 **Report ID 1**，但这条 report 只有 80 bit 的桩，**蓝牙链路从不发送它**；
4. 于是按键元素的值**永远得不到更新**（恒为 0）→ `IOHIDDeviceRegisterInputValueCallback` **一次都不触发**；
5. 唯一在变的是那个 `0xFF00/usage 59` 元素 —— **它就是整条 78 字节报文本身**，所以一直在动。

这解释了全部现象：

- **USB 正常**：`InputReportElements` 里 Report 1 是 512 bit 的完整报文，元素与上报对齐
- **Control 蓝牙正常**：SDL / HIDAPI 读的是**原始输入报文**，自己解析 0x31 的字节，不依赖元素值回调
- **DELTARUNE 蓝牙失聪**：`libYoYoGamepad.dylib` 的符号表里**没有** `IOHIDDeviceRegisterInputReportCallback`，
  只用元素值回调（且从不调用 `IOHIDDeviceOpen`，完全依赖 `IOHIDManagerOpen` 代管）
- **改映射文件无效**：映射只重排索引，改变不了「元素值根本不更新」

### 3.4 蓝牙 0x31 报文布局（实测标定）

`report[0]` 即报文 ID：

| 字节 | 内容 |
|---|---|
| `[0]` | `0x31` 报文 ID |
| `[1]` | seq（每帧 +0x10） |
| `[2]`–`[5]` | LX / LY / RX / RY |
| `[6]`–`[7]` | L2 / R2 模拟量 |
| `[8]` | `0x01` 保留字节，恒定 |
| `[9]` | **buttons0**：低 4 位 = 十字键（0=N,1=NE,2=E,3=SE,4=S,5=SW,6=W,7=NW，**8=松开**）<br>`0x10=□  0x20=✕  0x40=◯  0x80=△` |
| `[10]` | **buttons1**：`0x01=L1 0x02=R1 0x04=L2 0x08=R2`<br>`0x10=Create 0x20=Options 0x40=L3 0x80=R3` |
| `[11]` | **buttons2**：`0x01=PS 0x02=触摸板 0x04=Mute` |

标定方法：连续记录报文并**差分打印变化字节**，逐个按键对照（见 `tools/probes/rawcal2.c`）。

---

## 4. 复现探针

| 文件 | 用途 |
|---|---|
| `tools/probes/hidprobe.c` | 匹配 + `IOHIDDeviceOpen` + 按钮元素计数 |
| `tools/probes/hidprobe2.c` | 输入通路存活判定（元素值快照对比，无需按键） |
| `tools/probes/hidlist.c` | 导出「运行时视角」的元素索引表（按 `type`/`page` 过滤） |
| `tools/probes/hidorder.c` | A/B 复现运行时的 IOKit 调用顺序（`runner` / `canonical`） |
| `tools/probes/hidbtn.c` | 按钮/轴事件记录，判定事件是否送达第三方 HID 客户端 |
| `tools/probes/rawdump.c` | 转储原始输入报文 |
| `tools/probes/rawcal.c` / `rawcal2.c` | 报文差分标定（后者写日志文件，无需复制粘贴） |
| `tools/probes/vdev.c` / `vsub.c` | 验证 `IOHIDUserDeviceCreate` 与虚拟设备的元素可达性 |
| `tools/find_xref.py` | 反汇编 rip-relative 交叉引用查找器（按内容匹配定位字符串引用） |
| `tools/hid_report_binding.py` | 一键排查「元素声明在哪条 report vs 链路实际上行哪条 report」 |

---

## 附：环境快照

| 项 | 值 |
|---|---|
| 游戏 | DELTARUNE，Steam AppID 1671210，`LastPlayed` 2026-09-25 |
| 运行时分片 | `Mac_Runner` 通用二进制（x86_64 + arm64），SDK 13.3，`CFBundleShortVersionString 1.0.0` |
| 原始签名 | `Developer ID Application: Robert Fox (UY9XU99VUC)`，Hardened Runtime，公证票据已 staple，sealed resources 493 files |
| 章节数据 | `chapter1_mac` … `chapter5_mac`，各自独立 `game.ios` + `options.ini` |
| 映射表规模 | 156 条 `platform:Mac OS X` 条目；Sony 条目 4 条，DualSense 0 条 |
| 主机 | macOS 27.0 (26A428)，Apple Silicon |
