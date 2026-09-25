# deltarune-ds5-fix

DELTARUNE（macOS / GameMaker Studio 2）下 **DualSense (PS5) 手柄** 的支持修复。

**有线（USB）** 与 **蓝牙** 是**两个互不相关的缺陷**，修复方式也完全不同：

| 连接方式 | 症状 | 修复方式 | 是否改动 app 包 |
|---|---|---|---|
| 有线 USB | 面键错位（显示 ✕/◯，实际要按 □/✕；△ 正常） | 补一份映射文件 | ❌ 不用，零风险 |
| 蓝牙 | 按任何键毫无反应 | 注入 dylib | ✅ 需要，会破坏签名 |

根因与完整证据见 [`docs/DS5_诊断报告.md`](docs/DS5_诊断报告.md)。

---

## 症状

### 有线：面键错位

设置页显示 `confirm = ✕` / `cancel = ◯` / `menu = △`，实际操作是：

- 确认要按 **□**
- 取消要按 **✕**
- 菜单 **△** 正常
- **◯ 按下去没反应**

### 蓝牙：完全无响应

按键毫无反应，摇杆也无反应。同期其它游戏（如 Control）蓝牙下正常。

---

## 根因

### A. 有线 —— 运行时内嵌映射表缺 DualSense 条目

GameMaker 的 macOS 运行时**不用 SDL**。按键重排依赖内嵌在 `Mac_Runner` 里的一张
GUID → 映射表（156 条），而该表**没有 DualSense（`054c:0ce6`）**。
`SGamepadMapping::FindFromGUID` 返回 NULL，枚举索引原样透传（identity），不报错、不弹窗。

DualSense 的 HID 描述符把面键按 `□ ✕ ◯ △` 排列（UsagePage `0x09`，Usage 1..4），
而 Xbox/SDL 语义要求 `✕ ◯ □ △` —— **0 号与 1 号恰好互换**。

| GameMaker 槽位 | 游戏绑定 | 实际落到 | 屏幕显示 | 需实按 |
|---|---|---|---|---|
| `gp_face1` | confirm | 索引 0 = **□** | ✕ | □ |
| `gp_face2` | cancel | 索引 1 = **✕** | ◯ | ✕ |
| `gp_face3` | *未绑定* | 索引 2 = ◯ | — | 无反应 |
| `gp_face4` | menu | 索引 3 = **△** | △ | △ |

### B. 蓝牙 —— macOS 桥接层把整条报文当成一个不透明字段

蓝牙下 macOS 把 DualSense 重发布成用户态 `IOHIDUserDevice`，其描述符：

- 把 **Report 49（0x31）声明为单个不透明 vendor 字段**（`usagePage 0xFF00` / `usage 59` / 616 bit）——
  **完全不解码成 GamePad 字段**
- 面键虽然被声明在 **Report ID 1**，但这条 report 只有 80 bit 的桩，**蓝牙链路从不发送它**

| | USB | 蓝牙 |
|---|---|---|
| IOKit 类 | `AppleUserHIDDevice` | `IOHIDUserDevice` |
| 面键声明在 | Report ID 1 | Report ID 1 |
| `InputReportElements` 中 Report 1 尺寸 | **512 bit（完整）** | **80 bit（桩）** |
| 链路实际上行报文 | Report 1 | **Report 49（0x31，624 bit）** |

⇒ 按键元素的值**永远不更新**（恒为 0）→ `IOHIDDeviceRegisterInputValueCallback` 一次都不触发。

而 `libYoYoGamepad.dylib` 的符号表里**没有** `IOHIDDeviceRegisterInputReportCallback` ——
它只会用元素值回调，所以蓝牙下彻底失聪。读**原始报文**的 SDL/HIDAPI 系游戏不受影响
（这正是 Control 蓝牙下正常的原因）。

一键自查：

    ./tools/hid_report_binding.py --device DualSense

---

## 修复

### 有线（USB）：补一份映射文件

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

> 蓝牙下的键位错位同样由这份文件修正（两种传输方式算出的 GUID 相同）。
> 但注意：**右摇杆的轴映射只对 USB 正确**（详见文末「已知限制」）。

### 蓝牙：注入 dylib

由于元素值回调根本收不到数据，只能绕开它：注入一个桥接 dylib，
**读原始报文 → 解析按键 → 伪造 `IOHIDValueRef` 喂给运行时自己的回调**。
这样运行时的 cookie→索引 记账逻辑照旧执行，我们不需要知道它任何内部结构。

实现在 [`tools/ds5bridge/ds5rawfix.c`](tools/ds5bridge/ds5rawfix.c)：

1. 作为 `libYoYoGamepad.dylib` 的依赖被加载
2. 构造期改写该镜像 `__got` / `__la_symbol_ptr` 中指向
   `IOHIDDeviceRegisterInputValueCallback` 的槽位
3. 额外注册 `IOHIDDeviceRegisterInputReportCallback`，解析 0x31 报文
4. 只处理 `reportID == 0x31`，**不影响 USB**（USB 走 0x01）

安装过程（已执行完毕）：

    # 1. 复制 dylib 到 app 包
    cp tools/ds5bridge/libDS5RawFix.dylib \
       "DELTARUNE.app/Contents/Frameworks/"

    # 2. 给 libYoYoGamepad.dylib 插入依赖（无需环境变量，改完即自动加载）
    ./tools/ds5bridge/add_lc_load_dylib.py \
       "DELTARUNE.app/Contents/Frameworks/libYoYoGamepad.dylib" \
       "@loader_path/libDS5RawFix.dylib"

    # 3. 整个 app 重新 ad-hoc 签名
    codesign --force --deep -s - "DELTARUNE.app"

#### 回滚

| 修的东西 | 回滚方式 |
|---|---|
| 有线（`gamecontrollerdb.txt`） | `./install.sh --uninstall` |
| 蓝牙（注入的 dylib） | Steam → 库 → DELTARUNE → 属性 → 已安装文件 → **验证游戏文件完整性** |

蓝牙之所以必须走 Steam 验证：`Mac_Runner` 与 `libYoYoGamepad.dylib` 已被改动，
Steam 会检测到哈希不符并重新下载原始文件（含原始 Developer ID 签名）。

#### 代价

- **丧失 Developer ID 签名与公证**，变为 ad-hoc 签名
- 重新启用 Gatekeeper 后将被拒绝启动
- 诊断日志写在 `/tmp/ds5rawfix.log`（挂钩成功会打印「挂钩成功 … 按钮元素=14 轴元素=6 十字键元素=1」）

---

## 运行时参数（实测，非猜测）

### GUID 算法

完整证据见 `docs/DS5_诊断报告.md` §2.6。要点：

- `guid[0..3] = 3`（bus **硬编码 USB**），`guid[4..5] = VendorID` LE，`guid[8..9] = ProductID` LE，
  `guid[12..13] = VersionNumber` LE，其余字节置 0
- **CRC 字段恒为 0** —— Steam 日志里那条带 `crc:5657` 的 GUID **不适用**于此运行时
- fallback 分支（vendor 或 product 读不到，典型是蓝牙）：`guid[0..3] = 5`，
  `guid[4..15]` = IOKit `Product` 属性字符串的前 12 字节

代入实测值（`VendorID = 0x054C` / `ProductID = 0x0CE6` / `VersionNumber = 0x0100`）：

    03000000 4c05 0000 e60c 0000 0001 0000
     bus=3    vendor   product  version

    030000004c050000e60c000000010000

蓝牙名称型：`05000000` + `"DualSense Wireless Controller"` 前 12 字节 `DualSense Wi`
→ `050000004475616c53656e7365205769`

### 元素索引（设备 HID 描述符枚举顺序）

    b0=□ b1=✕ b2=◯ b3=△ b4=L1 b5=R1 b6=L2 b7=R2
    b8=Create b9=Options b10=L3 b11=R3 b12=PS b13=Touchpad
    USB 轴序：a0=左X a1=左Y a2=Z(L2) a3=Rz(R2) a4=Rx(右X) a5=Ry(右Y) a6=Hat

### 蓝牙 0x31 报文布局（实测标定）

| 字节 | 内容 |
|---|---|
| `[0]` | `0x31` 报文 ID |
| `[1]` | seq |
| `[2]`–`[5]` | LX / LY / RX / RY |
| `[6]`–`[7]` | L2 / R2 模拟量 |
| `[8]` | `0x01` 保留字节 |
| `[9]` | buttons0：低 4 位=十字键（0=N…7=NW，**8=松开**）；`0x10=□ 0x20=✕ 0x40=◯ 0x80=△` |
| `[10]` | buttons1：`0x01=L1 0x02=R1 0x04=L2 0x08=R2` + `0x10=Create 0x20=Options 0x40=L3 0x80=R3` |
| `[11]` | buttons2：`0x01=PS 0x02=触摸板 0x04=Mute` |

---

## 已知限制

- **蓝牙下右摇杆轴映射错位**：蓝牙的轴枚举顺序是 `X Y Z Rz Hat Rx Ry`（十字键插在 a4），
  与 USB 的 `X Y Z Rz Rx Ry Hat` 不同。而两种传输算出的 GUID 相同，映射表无法区分，
  所以 `gamecontrollerdb.txt` 里的 `rightx:a4, righty:a5` **只对 USB 正确**。
  DELTARUNE 不使用右摇杆，实际无影响。
- **蓝牙下十字键走 hat 元素**，`gamecontrollerdb.txt` 里的 `dpup:h0.1` 等两项同样存在
  类似的传输相关差异；DELTARUNE 的十字键由 `gp_padu..padr` 直接读取，实测可用。
- Steam Input 虚拟手柄路线未验证（本次选择了注入方案）。

---

## 目录结构

    README.md
    gamecontrollerdb.txt              有线修复：可安装的映射（3 条变体）
    install.sh                        有线修复：安装 / 回滚到存档目录
    docs/DS5_诊断报告.md              根因与证据（含反汇编片段）
    tools/find_xref.py                反汇编 rip-relative 交叉引用查找器
    tools/hid_report_binding.py       HID 元素/报文绑定诊断
    tools/probes/                     复现探针源码
    tools/ds5bridge/                  蓝牙修复：桥接 dylib 与其源码
        ds5rawfix.c                   桥接实现
        libDS5RawFix.dylib            编译产物（双架构）
        add_lc_load_dylib.py          给 Mach-O 插入 LC_LOAD_DYLIB

### 探针

| 文件 | 用途 |
|---|---|
| `hidprobe.c` | 匹配 + `IOHIDDeviceOpen` + 按钮元素计数 |
| `hidprobe2.c` | 输入通路存活判定（元素值快照对比，不需要人按键） |
| `hidlist.c` | 导出「运行时视角」的元素索引表（按 `type∈{1,2,3}` ∧ `page∈1..12` 过滤） |
| `hidorder.c` | A/B 复现运行时的 IOKit 调用顺序（`hidorder runner` / `canonical`） |
| `hidbtn.c` | 按钮/轴事件记录，判定事件是否送达第三方 HID 客户端 |
| `rawdump.c` | 转储原始输入报文 |
| `rawcal.c` / `rawcal2.c` | 报文差分标定（后者写 `/tmp/ds5cal.log`） |
| `vdev.c` / `vsub.c` | 验证虚拟 HID 设备的元素可达性 |

编译（只需 Xcode Command Line Tools）：

    clang -framework IOKit -framework CoreFoundation -o hidbtn tools/probes/hidbtn.c

---

## 免责

本仓库仅包含取证笔记、配置补丁与一个自用的二进制补丁，**不包含游戏本体或任何 Toby Fox 的资产**。
