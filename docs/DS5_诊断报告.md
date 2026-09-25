# DELTARUNE (macOS) DualSense 手柄支持诊断报告

- 目标：DELTARUNE.app (AppID 1671210)，GameMaker Studio 2 (Mac_Runner) 导出
- 设备：DualSense Wireless Controller，VID `0x054C` / PID `0x0CE6`，USB 连接，VersionNumber `0x0100`
- 症状：设置页显示 `confirm = ✕` / `cancel = ◯` / `menu = △`，但实际 `confirm = □`、`cancel = ✕`、`menu = △`（正确）；且只有有线能玩

---

## 1. 结论

**根因：GameMaker 的 macOS 运行时不使用 SDL 手柄层，而是走自研 IOKit HID 库 + 内嵌 GUID 映射表；该映射表里没有 DualSense 条目，导致回退到原始 HID 元素顺序。**

DualSense 的 HID 描述符把面键按 `□ ✕ ◯ △` 排列，而 Xbox/SDL 语义要求 `✕ ◯ □ △`。没有映射表做重排，两者的 0/1 号位置正好互换 —— 这精确对应你观察到的「确认/取消互换、三角正常」。

不是游戏 bug，也不是手柄坏；是运行时的手柄映射数据库缺一条 entry。

---

## 2. 证据链

### 2.1 运行时结构：GameMaker 不用 SDL

| 对象 | 事实 |
|---|---|
| `Contents/MacOS/Mac_Runner` | 通用二进制；导出符号含 `Gamepad_Class.cpp`、`libYoYoGamepad.dylib` 加载器、`Unrecognised Controller`、`Unable to parse gamepad mapping value` |
| `Contents/Frameworks/libYoYoGamepad.dylib` | 仅依赖 `IOKit / CoreFoundation / libobjc`，**无 libSDL**。导出 `_IOHIDManagerCreate` / `_IOHIDDeviceRegisterInputValueCallback` / `SGamepadMapping::FindFromGUID` / `YoYoTranslateGamepadButtonM` |

即：设备通过 `IOHIDManager` 直接读取，按键通过 `FindFromGUID` 查表重排。

### 2.2 映射表里没有 DualSense

`Mac_Runner` 内嵌 156 条 `platform:Mac OS X` 映射。Sony 相关只有：

```
030000004c05000068020000...,PS3 Controller
030000004c050000c4050000...,PS4 Controller          (054c:05c4)
030000004c050000cc090000...,Sony DualShock 4 V2     (054c:09cc)
030000004c050000a00b0000...,Sony DualShock 4 Wireless Adaptor
```

**全表 0 条 `054c:0ce6`（DualSense）。** 同时表里存在 `Steam Virtual GamePad`（`030000005e0400008e02000001000000`）—— 说明 Steam Input 的虚拟手柄是被正确识别的，物理 DualSense 则不是。

### 2.3 设备侧 HID 实测（直接从你机器上取）

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

对照表里 PS4 的条目 `a:b1,b:b2,x:b0,y:b3` —— 因为 DualSense 与 DS4 的面键 HID 顺序相同（`□ ✕ ◯ △`），**只要这条映射在，GS5 就能正确工作**。缺失它，`FindFromGUID` 返回空，索引原样透传。

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

（`32769/32770/32771/32772` = GameMaker `gp_face1..4`；键盘段印证了槽位语义：4=确认、5=取消、6=菜单。）

### 2.5 Steam 侧旁证

`~/Library/Application Support/Steam/logs/controller.txt`：

```
SDL Mapping for 54c/ce6: 050057564c050000e60c000000016800,*,a:b0,b:b1,...crc:5657,platform:macOS,
Controller using HIDAPI driver, vid=0x054c, pid=0x0ce6
```

Steam 自己也得靠 `crc:5657` 现造一条映射（官方 SDL_GameControllerDB 里同样没有 DualSense —— SDL 是靠 HIDAPI 原生驱动处理的，不依赖 db）。

### 2.6 映射文件的查找路径与 GUID 算法（反汇编实证）

在 `Mac_Runner` 的 gamepad 初始化函数中（x86_64，`0x1002cffd5`–`0x1002d0069`）：

```asm
0x1002cffd5  movq  _pGameControllDB(%rip), %rbx            ; 内嵌 156 条
0x1002cffe9  callq SGamepadMapping::CreateFromFileAsString

0x1002cfff5  leaq  "gamecontrollerdb.txt", %rdi
0x1002cfffc  callq LoadSave::SaveFileExists                ; ① 存档区优先
0x1002d000a  je    0x1002d0017
0x1002d0010  callq LoadSave::ReadSaveFile                  ;    ~/Library/Application Support/<bundle id>/

0x1002d0017  callq LoadSave::BundleFileExists              ; ② 否则 app 包内
0x1002d0020  callq LoadSave::ReadBundleFile

0x1002d003d  callq SGamepadMapping::CreateFromFileAsString ; 逐条追加

0x1002d004a  leaq  "SDL_GAMECONTROLLERCONFIG", %rdi
0x1002d0051  callq EnvironmentGetVariable                  ; ③ 环境变量兜底
0x1002d005e  callq SGamepadMapping::CreateFromString
```

**结论：存档目录是第一优先，改动它不需要碰 app 包、不影响签名。**

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
0x4cba  callq _FindMappingFromGUID  ; 查表；查不到 -> mapping = NULL -> identity

; vendor==0 || product==0 分支（典型为蓝牙）
0x4ca0  movl $0x5, guid[0..3]       ; bus = 5
0x4caf  guid[4..15] = 设备名("Product") 前 12 字节
```

代入本机实测值（`VendorID=0x054C`、`ProductID=0x0CE6`、`VersionNumber=0x0100`）：

```
03000000 4c05 0000 e60c 0000 0001 0000
 bus=3    vendor   product  version
→ 030000004c050000e60c000000010000
```

蓝牙 fallback：`05 000000` + `"DualSense Wireless Controller"` 前 12 字节 `DualSense Wi`
→ `050000004475616c53656e7365205769`

---

## 3. 症状映射推导

运行时无映射 → 枚举索引直接落到 face 槽位：

| GameMaker 槽位 | 绑定动作 | 实际落到 | 游戏显示 | 你实际要按 |
|---|---|---|---|---|
| `gp_face1` | confirm | 索引 0 = **□ Square** | ✕ | **□** ✔ 吻合 |
| `gp_face2` | cancel | 索引 1 = **✕ Cross** | ◯ | **✕** ✔ 吻合 |
| `gp_face3` | *(未绑定)* | 索引 2 = **◯ Circle** | — | 按下无反应 |
| `gp_face4` | menu | 索引 3 = **△ Triangle** | △ | **△** ✔ 吻合 |

三处症状全部对上。注意「◯ 无反应」也解释了 —— `gp_face3` 在 DELTARUNE 里没有绑定任何动作。

---

## 4. 蓝牙为什么不能用

仅指出口径，供后续复现：

- 手柄在系统层已配对：`DualSense Wireless Controller` / `Address 0C:27:56:2A:D3:30` / VID `0x054C` / PID `0x0CE6` / Minor Type `Gamepad`，当前状态 Not Connected。
- 但 **Steam 过去确实在蓝牙下识别过它**：`controller.txt` 中 09-09、09-11、09-17、09-21、09-23 的记录 GUID 前缀是 `0500`（bus 0x05 = Bluetooth），09-25 今天这条是 `0300`（USB）。
- 所以「连不上蓝牙」不是配对问题，而是**游戏侧在蓝牙下拿不到可用的手柄**。

推测方向（未坐实，需实测）：蓝牙下 macOS 暴露的 DualSense 报告描述符与 USB 不同，YoYo 库的 `DeviceUsagePage/DeviceUsage` 匹配与元素枚举可能落空，或枚举出的按钮顺序再次变化。映射表里也没有任何 Sony 的蓝牙（`0500...`）条目可兜底。

**建议的验证步骤**：把手柄切到蓝牙后，重跑一次设备描述符枚举，对比蓝牙下的 `PrimaryUsage` 与 `UsagePage=9` 元素顺序 —— 这条需要手柄处于蓝牙态才能做。

---

## 5. 修复方案

### 方案 A｜改 `keyconfig_0.ini`（零风险，立刻可用）

把游戏侧的槽位映射错开一格，绕开运行时的错误透传：

```ini
[GAMEPAD_CONTROLS]
4="32770.000000"   ; confirm -> gp_face2 -> 索引1 = ✕ Cross
5="32771.000000"   ; cancel  -> gp_face3 -> 索引2 = ◯ Circle
6="32772.000000"   ; menu    -> gp_face4 -> 索引3 = △ Triangle（不变）
```

- 优点：纯配置、可秒回滚、不动签名、立刻生效。
- 缺点：只在「DualSense 无映射」这一前提下正确；将来若 Steam Input 生效或换用已映射手柄，会反过来错。
- 注意：游戏若在设置页里重置默认，会写回 32769/32770，需重改。改前备份原文件。

### 方案 B｜补一条 `gamecontrollerdb.txt`（从源头修好）—— ✅ 已实施

放置位置不再靠猜：反汇编已证明运行时**先查存档区**（见 2.6），因此落在

```
~/Library/Application Support/com.tobyfox.deltarune/gamecontrollerdb.txt
```

即可生效，**不需要碰 app 包、不破坏签名、对代码签名与公证零影响**。已安装，内容为：

```
030000004c050000e60c000000010000,DualSense Wireless Controller,a:b1,b:b2,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a2,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a3,rightx:a4,righty:a5,start:b9,touchpad:b13,x:b0,y:b3,platform:Mac OS X,
030000004c050000e60c000000000000,DualSense Wireless Controller,...（同型号 version=0 固件变体）
050000004475616c53656e7365205769,DualSense Wireless Controller,...（蓝牙 fallback，名称型 GUID）
```

三条按 2.6 的实测算法推导；GUID 不再是猜测，主条目 `030000004c050000e60c000000010000` 与本机属性逐字段吻合。
其余两条是零成本保险（不匹配的行会被忽略）。

- 优点：修在源头，图标与实际按键同时变正确；蓝牙路径也一并覆盖；完全可逆（删掉该文件即回到原状）。
- 回滚：删除 `~/Library/Application Support/com.tobyfox.deltarune/gamecontrollerdb.txt`。
- 若仍无效：说明 GUID 与推算不符，改走方案 C 或 D，或把 `keyconfig_0.ini` 备份后交给我重推。

### 方案 C｜Steam Input 强制开启

`localconfig.vdf` 中 DELTARUNE：

```
"1671210" { "UseSteamControllerConfig" "0" }   ; 0 = Forced Off
```

（同账号下 The Long Dark / Hollow Knight 为 `2` = 跟随全局默认。`0` 表示 Steam Input 对 DELTARUNE 被**强制关闭**。）

改为 `Steam → 库 → DELTARUNE → 属性 → 控制器 → 强制开启` 后，手柄会被 Steam 虚拟化为 `Steam Virtual GamePad`，而这条 **在 GameMaker 的映射表里是存在的、且映射正确**（`a:b0,b:b1,x:b2,y:b3`）。

- 优点：GUI 一键、不改文件、不改签名；且虚拟手柄走 Steam 自己的链路，**蓝牙态也可能一并解决**。
- 风险：macOS 上 Steam Input 不一定向游戏隐藏物理设备，可能造成双重输入；需实测。
- 副作用：图标可能从 PS 风格变成 Xbox 风格（取决于游戏如何判型）。

### 方案 D｜维持现状，游戏内重映射

如果 DELTARUNE 的 KEY CONFIG 页支持手柄重绑定，直接在游戏内改。但 `keyconfig_0.ini` 存的只是槽位→`gp_*` 常量，改不出「跨一档」的组合，实际等价于方案 A。

---

## 6. 状态与验证

已实施 **方案 B**：`~/Library/Application Support/com.tobyfox.deltarune/gamecontrollerdb.txt`

**验证方式**：启动游戏 → 进设置页 → 按 ✕ 应为确认、◯ 应为取消、△ 应为菜单。
（运行时对该文件的加载是「一次性读入 + 追加到内嵌表」，无需重装游戏。）

若生效但想回滚：删除该文件即可，游戏侧与 app 包都未被改动。

若未生效，按序降级：
1. 方案 C（Steam Input 强制开启）
2. 方案 A（改 `keyconfig_0.ini` 槽位）
3. 若两者都不行，把 `keyconfig_0.ini` 与新的 `controller.txt` 片段给我，重新推 GUID

### 未完成项

- **蓝牙**：仅给出 fallback GUID 条目，未坐实「蓝牙下运行时能否枚举到该设备」。若蓝牙仍不可用，需要在蓝牙连接状态下重跑一次 HID 描述符枚举（`ioreg -r -c IOHIDDevice`），对比 `PrimaryUsage` 与按钮顺序。

---

## 7. 蓝牙专章：为什么蓝牙下完全没有响应

### 7.1 结论

**蓝牙下 macOS 重发布的 `IOHIDUserDevice` 把面键声明在 Report ID 1，而蓝牙链路实际上行的是
Report ID 49（0x31）——按键元素的值永不更新，元素值回调一次都不触发。**

GameMaker 运行时只用元素值回调，因此在蓝牙下彻底收不到输入；这是 **macOS 蓝牙 HID 桥接层的
报文绑定缺陷，游戏侧无解**（改映射文件无效）。系统侧、驱动侧、设备侧本身全部正常。

### 7.2 已排除（全部为实测，非推断）

| 假设 | 实测 | 结论 |
|---|---|---|
| 蓝牙没连上 | `Services: 0x800020 <HID ACL>`，USB 树为空 | ✗ |
| IOKit 匹配不到设备 | 用运行时同款匹配字典 `{1,4}/{1,5}/{1,8}` 写探针 → 匹配 1 台 | ✗ |
| 设备打不开 | `IOHIDDeviceOpen -> 0x0`，Button 元素 14 个 | ✗ |
| 输入通路死了 | `vendor 页` 事件稳定 ~64 次/秒 | ✗ |
| vendor 页元素挤占索引 | 运行时只认 `type∈{1,2,3}` ∧ `page∈1..12`，vendor 页一律丢弃 | ✗ |
| 计数与填充两趟不一致 | `CountHIDElements` 与 `CollectHIDElements` 过滤规则**逐指令一致** | ✗ |
| 按钮索引空间不同 | 蓝牙与 USB 都是 14 个按钮、usage 顺序 1..14 | ✗ |
| 运行时的 IOKit 调用顺序有竞态 | A/B 在**各自独立进程**中复现 canonical 与 runner 两种顺序，**都能拿到设备** | ✗ |
| 游戏崩溃 | `~/Library/Logs/DiagnosticReports` 无 DELTARUNE/Mac_Runner 记录 | ✗ |
| 我们加的 gamecontrollerdb.txt | 用户实测：有无该文件表现一致 | ✗ |
| 系统/驱动层问题 | 同期另一款游戏（Control）蓝牙下操作正常 | ✗ |

### 7.3 实测到的真实差异

| | USB | 蓝牙 |
|---|---|---|
| IOKit 设备类 | `AppleUserHIDDevice` | **`IOHIDUserDevice`** |
| Transport | USB | Bluetooth |
| `DeviceUsagePairs` | `{1,5}` | `{1,5}`（相同）|
| 元素总数 / 按钮 | 123 / 14 | 132 / 14 |
| 轴顺序 | `X Y Z Rz Rx Ry Hat` | **`X Y Z Rz Hat Rx Ry`** |

轴顺序差异只会毁掉右摇杆，**解释不了按钮全灭**。

### 7.4 根因（已确认）

实测判据：按键时 `hidbtn` 探针**零输出**，而同窗口 vendor 页事件 19130 条（~64/秒）。
即：**上报在流，但按钮元素的 value 从不变化。**

比对两种传输方式的 HID 描述符与输入报文绑定：

| | USB | 蓝牙 |
|---|---|---|
| IOKit 类 | `AppleUserHIDDevice` | `IOHIDUserDevice` |
| `ReportDescriptor` | 289 字节 | 320 字节 |
| 描述符声明的 Report ID | `[1, 2, 5, 8, 9, 10, 11, 12, 32, 33, 34, …]` | `[1, **49**, 50, 51, 52, 53, 54, 55, 56, 57, 5, 8, …]` |
| 按钮 1..N 声明所在 Report | **Report ID 1** | **Report ID 1** |
| `InputReportElements` 里 Report 1 的尺寸 | **512 bit**（64 字节，完整） | **80 bit**（10 字节，桩） |
| 实际链路上行的报文 | Report 1 | **Report 49（0x31，624 bit / 78 字节）** |

**机制**：

1. 蓝牙下 macOS 把物理设备重发布成一个用户态 `IOHIDUserDevice`；
2. 该描述符把面键声明在 **Report ID 1**，但这条 report 只有 80 bit 的桩，**蓝牙链路从不发送它**；
3. 链路实际发送的是 **Report ID 49（0x31）**；
4. 于是按键元素的值**永远得不到更新**（恒为 0）→ `IOHIDDeviceRegisterInputValueCallback` **一次都不触发**；
5. 唯一在变的是 `page=0xFF00 usage=59` 那个自增计数器元素 —— 它被绑定在 Report 49 上，所以能更新。

这解释了全部现象：

- **USB 正常**：`InputReportElements` 里 Report 1 是 512 bit 的完整报文，元素与实际上报对齐
- **Control 蓝牙正常**：SDL / HIDAPI 读的是**原始输入报文**（自己解析 report 49 的字节），不依赖元素值回调
- **DELTARUNE 蓝牙失聪**：`libYoYoGamepad.dylib` 的符号表里**没有** `IOHIDDeviceRegisterInputReportCallback`，
  它**只用元素值回调**（且从不调用 `IOHIDDeviceOpen`，完全依赖 `IOHIDManagerOpen` 代管）
- **改映射文件无效**：映射只重排索引，改变不了「元素值根本不更新」这件事

结论：**这是 macOS 蓝牙 HID 桥接层的报文绑定缺陷，游戏侧无解。**

### 7.5 可行的修复方向

游戏侧无解（元素值回调根本收不到数据），只能从**让游戏看到一个健康的 HID 设备**入手：

| 方案 | 做法 | 评价 |
|---|---|---|
| **A. 用有线** | 直接插线 | 已验证可用。最稳，代价是占用一个 USB 口 |
| **B. Steam Input 强制开启** | `localconfig.vdf` → `apps` → `1671210` → `UseSteamControllerConfig` 由 `0` 改 `1`（**改前必须完全退出 Steam**），或 GUI：库 → DELTARUNE → 属性 → 控制器 → 强制开启 | GameMaker 内嵌映射表里**本就有 `Steam Virtual GamePad` 且映射正确**（`030000005e0400008e02000001000000`, `a:b0,b:b1,x:b2,y:b3`）。虚拟手柄由 Steam 自己造，报文与元素对齐，绕开整条蓝牙桥接 |
| C. 等 Apple 修 | — | 这是 macOS 侧缺陷，非 Valve / 非 Toby Fox 的问题 |

方案 B 有一个已知风险：macOS 上 Steam Input 未必对游戏隐藏物理设备，游戏可能同时看到
「物理 DualSense」与「虚拟手柄」两台设备，槽位分配顺序不确定。若出现该情况，回退到方案 A。

### 7.6 复现用的探针

| 文件 | 用途 |
|---|---|
| `hidprobe.c` | 匹配 + `IOHIDDeviceOpen` + 按钮元素计数 |
| `hidprobe2.c` | 输入通路存活判定（元素值快照对比） |
| `hidlist.c` | 导出「运行时视角」的元素索引表（按 type/page 过滤） |
| `hidorder.c` | A/B 复现运行时的 IOKit 调用顺序 |
| `hidbtn.c` | 按钮/轴事件记录（判定事件是否送达第三方进程）——**本案的定案证据** |
| `tools/hid_report_binding.py`（仓库内） | 一键排查「元素声明在哪条 report vs 链路实际上行哪条 report」的错配 |

---

## 8. 修复路线评估：改 bundle 能修到什么程度

### 8.1 路线一：自己发布一个健康的虚拟手柄 —— ❌ 不可行

思路：注入代码读物理手柄的**原始报文**（这条路是通的），再以正确描述符重发布成
`IOHIDUserDevice`，绕开 macOS 那个错配的桥接设备。

实测挡在权限上：

| 测试 | 结果 |
|---|---|
| 无 entitlement 调 `IOHIDUserDeviceCreate` | 安静返回 NULL |
| 带 `com.apple.developer.hid.virtual.device` + ad-hoc 签名 | 进程被内核 **SIGKILL**（exit 137） |
| 带无害 entitlement（`com.apple.security.cs.allow-jit`）+ ad-hoc 签名（对照组） | 正常运行 |

⇒ 被杀**只因为** `com.apple.developer.hid.virtual.device` 这一个受限 entitlement。
它需要 Apple 签发的 provisioning profile，自建工具拿不到。

**关键对照**：本机 HID 树里存在两个 Valve（`0x28DE`）的 `IOHIDUserDevice` ——
`Keyboard-1` / `Mouse-1`，由 Steam Input 的桌面配置发布。
说明虚拟 HID 设备在 macOS 上**确实可行**，只是需要 Apple 授予的权限，Steam 有、我们没有。

**推论：Steam Input 强制开启这条路线是真实可行的**（Steam 能造出健康虚拟手柄），
而不是纯推测。风险仍是「游戏可能同时看到物理与虚拟两台设备、槽位顺序不定」。

### 8.2 路线二：注入 dylib 直接驱动运行时内部状态 —— ⚠️ 可行但工程量大

已把所需的全部反汇编信息挖齐：

**注入点**（`libYoYoGamepad.dylib` 0x4d9c）：

```asm
leaq  _onGamepadValueChanged(%rip), %rsi
movq  %r14, %rdi                     ; IOHIDDeviceRef
movq  %r15, %rdx                     ; ← 第 3 参数 = GMGamePad 对象指针
callq _IOHIDDeviceRegisterInputValueCallback
```

**设备对象（GMGamePad）布局**：

| 偏移 | 含义 |
|---|---|
| `+0x28` | 映射条目指针（`FindFromGUID` 结果）|
| `+0x30` | numButtons |
| `+0x34` | numAxes |
| `+0x38` | numHats |
| `+0x40` | axisState（float[]，按轴序号）|
| `+0x48` | buttonValue（float[]，按按钮序号）|
| `+0x50` | hatState（int[]，值为 `g_masks[hatValue]`）|
| `+0x58` | IOHIDDeviceRef |
| `+0x60` | axes 数组（32 B/项，cookie 在 +0）|
| `+0x68` | buttons 数组（8 B/项，cookie 在 +0）|
| `+0x70` | hats 数组（8 B/项，cookie 在 +0）|

**`_onGamepadValueChanged` 的写入规则**（0x5830–0x5a26）：

- 轴：按 cookie 匹配 → `axisState[i] = (val - min) / (max - min) * 2 - 1`（min/max 动态跟踪，存在 axes[i]+8 / +0x10）
- 按键：按 cookie 匹配 → `buttonValue[i] = (float)val`
- 十字键：按 cookie 匹配 → `hatState[i] = g_masks[val - logicalMin]`

**`g_masks`（arm64 分片，9 项）** = `[1, 3, 2, 6, 4, 12, 8, 9, 0]`
即 hat 值 0..7 展开为位掩码，bit0=上、bit1=右、bit2=下、bit3=左，8=中立→0。

**`gp_*` → 内部索引**（x86_64 分片，VA `0x7bc0`，16 项）：

```
face1..4 -> 0,1,2,3        shoulderl -> 4      shoulderlb -> 5
shoulderr -> 0x5004(轴4)   shoulderrb -> 0x5005(轴5)
select -> 8   start -> 9   stickl -> 10   stickr -> 11
padu -> 12    padd -> 13   padl -> 14     padr -> 15
```

（`0x5000 | n` 表示「按轴 n 读取」，故肩键右/右扳机走模拟量。轴序：
`gp_axislh..gp_axisrv` → 0..3，右扳机 → 4。）

**实施代价**：

1. 写 interpose dylib（`DYLD_INTERPOSE` 挂 `IOHIDDeviceRegisterInputValueCallback`，拿到 GMGamePad*），
   额外注册 `IOHIDDeviceRegisterInputReportCallback`，解析 0x31/0x01 报文后写入上表各数组
2. 必须**重新 ad-hoc 签名**整个 app（`--deep -s -`）—— 因为 hardened runtime 会
   ①阻止 `DYLD_INSERT_LIBRARIES` ②做 library validation 拒绝加载被改过的 dylib
3. 用 `Info.plist` 的 `LSEnvironment` 或 Steam 启动项注入 `DYLD_INSERT_LIBRARIES`

**副作用**：丧失 Developer ID 签名与公证；重新启用 Gatekeeper 后将被拒绝启动。
可用 Steam「验证文件完整性」恢复原状。

### 8.3 建议顺序

1. **先试 Steam Input 强制开启**（免费、1 分钟、且已有 Steam 自造虚拟设备的实证）。若槽位顺序出问题再往下走
2. 仍不行再上 8.2 的注入方案（规格已备齐）
3. 或者直接用有线


---

## 附：环境快照

| 项 | 值 |
|---|---|
| 游戏 | DELTARUNE，AppID 1671210，Playtime 130 min，`LastPlayed` 2026-09-25 |
| 运行时分片 | `Mac_Runner` 通用二进制（x86_64 + arm64），SDK 13.3，`CFBundleShortVersionString 1.0.0` |
| 签名 | `Developer ID Application: Robert Fox (UY9XU99VUC)`，Hardened Runtime，公证票据已 staple，`spctl: accepted`，sealed resources 493 files |
| 章节数据 | `chapter1_mac` … `chapter5_mac`，各自独立 `game.ios` + `options.ini` |
| 映射表规模 | 156 条 `platform:Mac OS X` 条目；Sony 条目 4 条，DualSense 0 条 |
