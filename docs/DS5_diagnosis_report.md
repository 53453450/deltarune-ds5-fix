# DELTARUNE (macOS) DualSense Controller Support — Diagnostic Report

[中文](DS5_诊断报告.md) | **English**

- Target: DELTARUNE.app (Steam AppID 1671210), exported by GameMaker Studio 2 (`Mac_Runner`)
- Device: Sony DualSense Wireless Controller, VID `0x054C` / PID `0x0CE6`
- Symptoms: face buttons swapped when wired; no response at all over Bluetooth

> This report records the **root cause and evidence only**.
> For how to fix it, see [`../README.en.md`](../README.en.md).

---

## 1. Conclusion

The two symptoms are **two unrelated defects**:

| Connection | Symptom | Root cause |
|---|---|---|
| Wired (USB) | Face buttons swapped: shows `confirm=✕ / cancel=◯`, but you must press `□` / `✕`; `△` is fine | The runtime's embedded GUID→mapping table has **no DualSense entry**, so indices are passed through unchanged |
| Bluetooth | No response from any button | The macOS Bluetooth bridge passes the whole report through as a **single opaque field** and never decodes it into GamePad fields → element values never update → the runtime's element-value callback never fires |

Neither is a game bug, and the controller is not faulty.

---

## 2. Wired: root cause of the swapped face buttons

### 2.1 Runtime structure: GameMaker does not use SDL

| Object | Fact |
|---|---|
| `Contents/MacOS/Mac_Runner` | Universal binary; contains `Gamepad_Class.cpp`, the `libYoYoGamepad.dylib` loader, `Unrecognised Controller`, `Unable to parse gamepad mapping value` |
| `Contents/Frameworks/libYoYoGamepad.dylib` | Depends only on `IOKit / CoreFoundation / libobjc`, **no libSDL**. Exports `IOHIDManagerCreate` / `IOHIDDeviceRegisterInputValueCallback` / `SGamepadMapping::FindFromGUID` / `YoYoTranslateGamepadButtonM` |

So the device is read directly via `IOHIDManager`, and buttons are remapped by looking up `FindFromGUID`.

### 2.2 The embedded mapping table has no DualSense

`Mac_Runner` embeds 156 `platform:Mac OS X` mappings. The only Sony-related ones are:

```
030000004c05000068020000...,PS3 Controller
030000004c050000c4050000...,PS4 Controller          (054c:05c4)
030000004c050000cc090000...,Sony DualShock 4 V2     (054c:09cc)
030000004c050000a00b0000...,Sony DualShock 4 Wireless Adaptor
```

**Zero entries for `054c:0ce6` (DualSense).** The table also contains `Steam Virtual GamePad`
(`030000005e0400008e02000001000000`).

Compare the PS4 entry `a:b1,b:b2,x:b0,y:b3` — the DualSense has the same face-button HID order as the
DS4, so **simply having that entry present would make it work correctly**; without it
`FindFromGUID` returns NULL and the index is passed through unchanged (identity).

### 2.3 Device-side HID measurements

```
"VendorID"  = 1356          (0x054C)
"ProductID" = 3302          (0x0CE6)
"Transport" = "USB"
"PrimaryUsagePage" = 1, "PrimaryUsage" = 5   (Generic Desktop / Game Pad)
```

Enumeration order of `UsagePage = 0x09 (Button)` elements:

| Enum index | Usage | Physical button |
|---|---|---|
| 0 | 1 | **□ Square** |
| 1 | 2 | **✕ Cross** |
| 2 | 3 | **◯ Circle** |
| 3 | 4 | **△ Triangle** |
| 4–7 | 5–8 | L1 / R1 / L2 / R2 |
| 8–13 | 9–14 | Create / Options / L3 / R3 / PS / Touchpad |

The DualSense descriptor orders them `□ ✕ ◯ △`, while the Xbox/SDL convention expects
`✕ ◯ □ △` — **indices 0 and 1 are exactly swapped**.

### 2.4 Game-side bindings

`~/Library/Application Support/com.tobyfox.deltarune/keyconfig_0.ini`:

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

(`32769/32770/32771/32772` are GameMaker's `gp_face1..4`; the keyboard section confirms the slot
semantics: 4=confirm, 5=cancel, 6=menu.)

### 2.5 The symptom closes

With no mapping, the enum indices land directly on the face slots:

| GameMaker slot | Game binding | Actually lands on | Screen shows | You must press |
|---|---|---|---|---|
| `gp_face1` | confirm | index 0 = **□** | ✕ | □ ✔ |
| `gp_face2` | cancel | index 1 = **✕** | ◯ | ✕ ✔ |
| `gp_face3` | *unbound* | index 2 = ◯ | — | nothing |
| `gp_face4` | menu | index 3 = **△** | △ | △ ✔ |

All three symptoms match. "◯ does nothing" is explained too — `gp_face3` is not bound to any action
in DELTARUNE.

### 2.6 Mapping-file lookup path and GUID algorithm (from disassembly)

The gamepad init function in `Mac_Runner` (x86_64, `0x1002cffd5`–`0x1002d0069`):

```asm
0x1002cffd5  movq  _pGameControllDB(%rip), %rbx            ; the 156 embedded entries
0x1002cffe9  callq SGamepadMapping::CreateFromFileAsString

0x1002cfff5  leaq  "gamecontrollerdb.txt", %rdi
0x1002cfffc  callq LoadSave::SaveFileExists                ; (1) save dir first
0x1002d0010  callq LoadSave::ReadSaveFile                  ;     ~/Library/Application Support/<bundle id>/

0x1002d0017  callq LoadSave::BundleFileExists              ; (2) otherwise inside the app bundle
0x1002d0020  callq LoadSave::ReadBundleFile

0x1002d003d  callq SGamepadMapping::CreateFromFileAsString ; appended entry by entry

0x1002d004a  leaq  "SDL_GAMECONTROLLERCONFIG", %rdi
0x1002d0051  callq EnvironmentGetVariable                  ; (3) environment variable fallback
0x1002d005e  callq SGamepadMapping::CreateFromString
```

**The save directory has top priority, so changing it requires no app bundle edits and does not
affect the signature.**

The GUID is computed at runtime by `libYoYoGamepad.dylib` (`0x4bed`–`0x4cb6`):

```asm
0x4bed  leaq "VendorID"      ; IOHIDDeviceGetProperty -> CFNumberGetValue
0x4c12  leaq "ProductID"
0x4c37  leaq "VersionNumber"

0x4c6e  movl $0x3,    guid[0..3]    ; bus hard-coded to 3 = USB
0x4c76  movw vendor,  guid[4..5]    ; little-endian
0x4c7b  movw $0x0,    guid[6..7]
0x4c82  movw product, guid[8..9]    ; little-endian
0x4c87  movw $0x0,    guid[10..11]
0x4c92  movw version, guid[12..13]  ; little-endian
0x4c97  movw $0x0,    guid[14..15]
0x4cba  callq _FindMappingFromGUID  ; miss -> mapping = NULL -> identity

; vendor==0 || product==0 branch (typically Bluetooth)
0x4ca0  movl $0x5, guid[0..3]       ; bus = 5
0x4caf  guid[4..15] = first 12 bytes of the "Product" name
```

Plugging in the measured values (`0x054C` / `0x0CE6` / `VersionNumber 0x0100`):

```
03000000 4c05 0000 e60c 0000 0001 0000
 bus=3    vendor   product  version
→ 030000004c050000e60c000000010000
```

Bluetooth name-based fallback: `05000000` + first 12 bytes of `"DualSense Wireless Controller"`
(`DualSense Wi`) → `050000004475616c53656e7365205769`

> Note the **CRC field is always 0**; the GUID carrying `crc:5657` seen in Steam's log does not apply here.

### 2.7 Steam-side corroboration

`~/Library/Application Support/Steam/logs/controller.txt`:

```
SDL Mapping for 54c/ce6: 050057564c050000e60c000000016800,*,a:b0,b:b1,...crc:5657,platform:macOS,
Controller using HIDAPI driver, vid=0x054c, pid=0x0ce6
```

Steam has to synthesise a mapping with `crc:5657` itself — the official SDL_GameControllerDB has no
DualSense entry either (SDL handles it with its native HIDAPI driver and does not need the DB).

---

## 3. Bluetooth: root cause of the total lack of response

### 3.1 Ruled out (all measured, not inferred)

| Hypothesis | Measurement | Verdict |
|---|---|---|
| Bluetooth not connected | `Services: 0x800020 <HID ACL>`, USB tree empty | ✗ |
| IOKit does not match the device | Probe using the runtime's exact matching dict `{1,4}/{1,5}/{1,8}` → 1 device matched | ✗ |
| Device cannot be opened | `IOHIDDeviceOpen -> 0x0`, 14 Button elements | ✗ |
| Input path dead | vendor-page events steady at ~64/s | ✗ |
| Vendor-page elements shift indices | Runtime only accepts `type∈{1,2,3}` ∧ `page∈1..12`; vendor pages are dropped | ✗ |
| Count/fill two-pass mismatch | `CountHIDElements` and `CollectHIDElements` filters are **instruction-for-instruction identical** | ✗ |
| Different button index space | Both Bluetooth and USB give 14 buttons in usage order 1..14 | ✗ |
| IOKit call-order race in the runtime | A/B reproduced `canonical` and `runner` orders in **separate processes**; both acquire the device | ✗ |
| Game crash | No relevant entries in `DiagnosticReports` | ✗ |
| The added `gamecontrollerdb.txt` caused it | User-verified: identical behaviour with and without the file | ✗ |
| System/driver problem | Another game (Control) works over Bluetooth at the same time | ✗ |

### 3.2 Actual measured differences

| | USB | Bluetooth |
|---|---|---|
| IOKit class | `AppleUserHIDDevice` | **`IOHIDUserDevice`** |
| Transport | USB | Bluetooth |
| `DeviceUsagePairs` | `{1,5}` | `{1,5}` (identical — matching is unaffected) |
| Total elements / buttons | 123 / 14 | 132 / 14 |
| Axis order | `X Y Z Rz Rx Ry Hat` | **`X Y Z Rz Hat Rx Ry`** |

The axis-order difference only affects the right stick and **cannot explain all buttons being dead**.

### 3.3 Root cause (confirmed)

The decisive measurement: while pressing buttons the button-event probe produced **zero output**,
while the same window saw 19130 vendor-page events (~64/s).
In other words: **reports are flowing, but no button element's value ever changes.**

Comparing the HID descriptor and input-report binding of both transports:

| | USB | Bluetooth |
|---|---|---|
| `ReportDescriptor` | 289 bytes | 320 bytes |
| Report IDs declared | `[1, 2, 5, 8, 9, 10, 11, 12, 32, 33, 34, …]` | `[1, **49**, 50, 51, 52, 53, 54, 55, 56, 57, 5, 8, …]` |
| Report the face buttons are declared in | **Report ID 1** | **Report ID 1** |
| Size of Report 1 in `InputReportElements` | **512 bits** (64 bytes, complete) | **80 bits** (10 bytes, stub) |
| Report actually sent over the link | Report 1 | **Report 49 (0x31, 624 bits / 78 bytes)** |

**Mechanism**:

1. Over Bluetooth, macOS republishes the physical device as a user-space `IOHIDUserDevice`;
2. Its descriptor declares Report 49 as a **single opaque vendor field**
   (`usagePage 0xFF00` / `usage 59` / 616 bits) and never decodes it into GamePad fields;
3. The face buttons *are* declared under **Report ID 1**, but that report is only an 80-bit stub and
   **the Bluetooth link never sends it**;
4. So the button elements' values **never update** (stuck at 0) →
   `IOHIDDeviceRegisterInputValueCallback` **never fires even once**;
5. The only element that does change is that `0xFF00/usage 59` one — **it *is* the entire 78-byte
   report**, which is why it keeps moving.

This explains every observation:

- **USB works**: in `InputReportElements` Report 1 is a complete 512-bit report, so elements align with
  what is actually sent
- **Control works over Bluetooth**: SDL/HIDAPI read the **raw input report** and parse the 0x31 bytes
  themselves, without relying on element-value callbacks
- **DELTARUNE is deaf over Bluetooth**: `libYoYoGamepad.dylib`'s symbol table has **no**
  `IOHIDDeviceRegisterInputReportCallback`; it only uses element-value callbacks (and never calls
  `IOHIDDeviceOpen`, relying entirely on `IOHIDManagerOpen`)
- **Changing the mapping file does nothing**: a mapping only reorders indices; it cannot make an
  element value update

### 3.4 Bluetooth 0x31 report layout (measured)

`report[0]` is the report ID:

| Byte | Contents |
|---|---|
| `[0]` | `0x31` report ID |
| `[1]` | seq (+0x10 per frame) |
| `[2]`–`[5]` | LX / LY / RX / RY |
| `[6]`–`[7]` | L2 / R2 analog |
| `[8]` | `0x01` reserved byte, constant |
| `[9]` | **buttons0**: low nibble = D-pad (0=N,1=NE,2=E,3=SE,4=S,5=SW,6=W,7=NW, **8=released**)<br>`0x10=□  0x20=✕  0x40=◯  0x80=△` |
| `[10]` | **buttons1**: `0x01=L1 0x02=R1 0x04=L2 0x08=R2`<br>`0x10=Create 0x20=Options 0x40=L3 0x80=R3` |
| `[11]` | **buttons2**: `0x01=PS 0x02=Touchpad 0x04=Mute` |

Calibration method: record reports continuously and **diff-print the changed bytes**, pressing one
button at a time (see `tools/probes/rawcal2.c`).

---

## 4. Reproduction probes

| File | Purpose |
|---|---|
| `tools/probes/hidprobe.c` | match + `IOHIDDeviceOpen` + button element count |
| `tools/probes/hidprobe2.c` | input-path liveness check (element value snapshots, no button press needed) |
| `tools/probes/hidlist.c` | dump the "runtime's view" of element indices (filtered by `type`/`page`) |
| `tools/probes/hidorder.c` | A/B reproduction of the runtime's IOKit call order (`runner` / `canonical`) |
| `tools/probes/hidbtn.c` | button/axis event recorder; determines whether events reach third-party HID clients |
| `tools/probes/rawdump.c` | dump raw input reports |
| `tools/probes/rawcal.c` / `rawcal2.c` | report diff calibration (the latter writes a log file) |
| `tools/probes/vdev.c` / `vsub.c` | verify `IOHIDUserDeviceCreate` and virtual-device element reachability |
| `tools/find_xref.py` | disassembly rip-relative xref finder (locates string references by content matching) |
| `tools/hid_report_binding.py` | one-shot check for "which report an element is declared in vs which one the link actually sends" |

---

## Appendix: environment snapshot

| Item | Value |
|---|---|
| Game | DELTARUNE, Steam AppID 1671210, `LastPlayed` 2026-09-25 |
| Runtime slices | `Mac_Runner` universal binary (x86_64 + arm64), SDK 13.3, `CFBundleShortVersionString 1.0.0` |
| Original signature | `Developer ID Application: Robert Fox (UY9XU99VUC)`, Hardened Runtime, notarization ticket stapled, sealed resources 493 files |
| Chapter data | `chapter1_mac` … `chapter5_mac`, each with its own `game.ios` + `options.ini` |
| Mapping table | 156 `platform:Mac OS X` entries; 4 Sony entries, 0 DualSense |
| Host | macOS 27.0 (26A428), Apple Silicon |
