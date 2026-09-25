# deltarune-ds5-fix

DELTARUNE（macOS / GameMaker Studio 2）下 **DualSense (PS5) 面键错位** 的根因分析与修复补丁。

## 症状

游戏设置页显示 `confirm = ✕` / `cancel = ◯` / `menu = △`，但实际操作是：

- 确认要按 **□**
- 取消要按 **✕**
- 菜单 **△** 正常
- **◯ 按下去没反应**

## 根因

一句话：GameMaker 的 macOS 运行时**不用 SDL**。按键重排依赖内嵌在 `Mac_Runner` 里的一张
GUID → 映射表（156 条），而该表**没有 DualSense（`054c:0ce6`）**。
`SGamepadMapping::FindFromGUID` 返回 NULL，枚举索引原样透传（identity），不报错、不弹窗。

DualSense 的 HID 描述符把面键按 `□ ✕ ◯ △` 排列（UsagePage `0x09`，Usage 1..4），
而 Xbox/SDL 语义要求 `✕ ◯ □ △` —— **0 号与 1 号恰好互换**。

叠加 DELTARUNE 自身绑定（`keyconfig_0.ini`），症状完全闭合：

| GameMaker 槽位 | 游戏绑定 | 实际落到 | 屏幕显示 | 需实按 |
|---|---|---|---|---|
| `gp_face1` | confirm | 索引 0 = **□** | ✕ | □ |
| `gp_face2` | cancel | 索引 1 = **✕** | ◯ | ✕ |
| `gp_face3` | *未绑定* | 索引 2 = ◯ | — | 无反应 |
| `gp_face4` | menu | 索引 3 = **△** | △ | △ |

## 修复

把本仓库的 `gamecontrollerdb.txt` 放到**游戏的存档目录**：

    ~/Library/Application Support/com.tobyfox.deltarune/gamecontrollerdb.txt

一键安装 / 回滚：

    ./install.sh              # 安装
    ./install.sh --uninstall  # 回滚

**为什么是这个位置**：反汇编 `Mac_Runner` 的 gamepad 初始化函数，查找顺序为

    "gamecontrollerdb.txt" -> LoadSave::SaveFileExists    (① 存档区，优先)
                           -> LoadSave::BundleFileExists   (② app 包内)
    "SDL_GAMECONTROLLERCONFIG" -> EnvironmentGetVariable   (③ 环境变量兜底)

① 命中即用。因此**不需要改 app bundle、不破坏代码签名、不涉及公证**，删掉文件即完全回滚。

## 运行时 GUID 算法（实测，非猜测）

完整证据见 `docs/DS5_诊断报告.md` §2.6。要点：

- `guid[0..3] = 3`（bus **硬编码 USB**），`guid[4..5] = VendorID` LE，`guid[8..9] = ProductID` LE，
  `guid[12..13] = VersionNumber` LE，其余字节置 0
- **CRC 字段恒为 0** —— Steam 日志里那条带 `crc:5657` 的 GUID **不适用**于此运行时
- fallback 分支（vendor 或 product 读不到，典型是蓝牙）：`guid[0..3] = 5`，
  `guid[4..15]` = IOKit `Product` 属性字符串的前 12 字节

代入本机实测值（`VendorID = 0x054C` / `ProductID = 0x0CE6` / `VersionNumber = 0x0100`）：

    03000000 4c05 0000 e60c 0000 0001 0000
     bus=3    vendor   product  version

    030000004c050000e60c000000010000

蓝牙名称型：`05000000` + `"DualSense Wireless Controller"` 前 12 字节 `DualSense Wi`
→ `050000004475616c53656e7365205769`

按键索引同样来自实测（设备 HID 描述符元素顺序）：

    b0=□ b1=✕ b2=◯ b3=△ b4=L1 b5=R1 b6=L2 b7=R2
    b8=Create b9=Options b10=L3 b11=R3 b12=PS b13=Touchpad
    a0=左X a1=左Y a2=Z(L2) a3=Rz(R2) a4=Rx(右X) a5=Ry(右Y)   十字键 h0

## 目录结构

    README.md
    gamecontrollerdb.txt          可安装的映射（3 条：USB v0x0100 / USB v0 / BT name-fallback）
    install.sh                    安装 / 回滚到存档目录
    docs/DS5_诊断报告.md          完整取证报告（证据链 + 反汇编片段）
    tools/find_xref.py            反汇编 rip-relative 交叉引用查找器（按内容匹配定位字符串引用）

## 环境

- macOS，Sony DualSense（VID `0x054C` / PID `0x0CE6`），USB 连接
- DELTARUNE.app：GameMaker Studio 2 (Mac_Runner) 导出，
  `Developer ID Application: Robert Fox (UY9XU99VUC)`，Hardened Runtime + 公证已 staple
- 映射表规模：156 条 `platform:Mac OS X`；Sony 条目 4 条（PS3 / PS4 v1 / DS4 v2 / DS4 无线适配器），DualSense 0 条

## 未验证

- **蓝牙**：仅补了名称型 fallback 条目，未坐实蓝牙态下运行时能否枚举到该设备。
  需在蓝牙连接状态下重跑 `ioreg -r -c IOHIDDevice`，对比 `PrimaryUsage` 与按钮顺序。
- macOS 上若改用 Steam Input 虚拟手柄（`Steam Virtual GamePad`），该条目在运行时映射表中**已存在且正确**，
  但本补丁与其叠加时的行为未测。

## 免责

本仓库仅包含取证笔记与一份配置补丁，**不包含游戏本体或任何 Toby Fox 的资产**。
