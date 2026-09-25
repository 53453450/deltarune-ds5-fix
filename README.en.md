# deltarune-ds5-fix

[中文](README.md) | **English**

DualSense (PS5) controller support fix for **DELTARUNE** on macOS (GameMaker Studio 2).

The **wired (USB)** and **Bluetooth** problems are **two unrelated defects** and are fixed in completely different ways:

| Connection | Symptom | Fix | Touches the app bundle? |
|---|---|---|---|
| Wired USB | Face buttons swapped (shows ✕/◯, but you must press □/✕; △ is fine) | Drop in a mapping file | No — zero risk |
| Bluetooth | No response from any button | Inject a dylib | Yes — breaks the signature |

Full root-cause analysis and evidence: [`docs/DS5_diagnosis_report.md`](docs/DS5_diagnosis_report.md).

---

## Symptoms

### Wired: face buttons swapped

The settings page shows `confirm = ✕` / `cancel = ◯` / `menu = △`, but in practice:

- Confirm requires **□**
- Cancel requires **✕**
- Menu **△** is correct
- **◯ does nothing**

### Bluetooth: no response at all

No buttons, no sticks. Other games (e.g. Control) work fine over Bluetooth on the same machine.

---

## Root cause

### A. Wired — the runtime's embedded mapping table has no DualSense entry

GameMaker's macOS runtime **does not use SDL**. Button remapping relies on a GUID → mapping table
(156 entries) embedded in `Mac_Runner`, and that table has **no DualSense (`054c:0ce6`)** entry.
`SGamepadMapping::FindFromGUID` returns NULL, so the raw enumeration index is passed through
unchanged (identity). No error, no dialog.

The DualSense HID descriptor orders the face buttons `□ ✕ ◯ △` (UsagePage `0x09`, Usage 1..4),
whereas the Xbox/SDL convention expects `✕ ◯ □ △` — **indices 0 and 1 are exactly swapped**.

| GameMaker slot | Game binding | Actually lands on | Screen shows | You must press |
|---|---|---|---|---|
| `gp_face1` | confirm | index 0 = **□** | ✕ | □ |
| `gp_face2` | cancel | index 1 = **✕** | ◯ | ✕ |
| `gp_face3` | *unbound* | index 2 = ◯ | — | nothing |
| `gp_face4` | menu | index 3 = **△** | △ | △ |

### B. Bluetooth — the macOS bridge exposes the whole report as one opaque field

Over Bluetooth, macOS republishes the DualSense as a user-space `IOHIDUserDevice` whose descriptor:

- declares **Report 49 (0x31) as a single opaque vendor field**
  (`usagePage 0xFF00` / `usage 59` / 616 bits) — it is **never decoded into GamePad fields**
- declares the face buttons under **Report ID 1**, but that report is an 80-bit stub and
  **the Bluetooth link never sends it**

| | USB | Bluetooth |
|---|---|---|
| IOKit class | `AppleUserHIDDevice` | `IOHIDUserDevice` |
| Face buttons declared in | Report ID 1 | Report ID 1 |
| Size of Report 1 in `InputReportElements` | **512 bits (complete)** | **80 bits (stub)** |
| Report actually sent over the link | Report 1 | **Report 49 (0x31, 624 bits)** |

⇒ The button elements' values **never update** (stuck at 0) → `IOHIDDeviceRegisterInputValueCallback`
**never fires even once**.

Meanwhile `libYoYoGamepad.dylib`'s symbol table has **no** `IOHIDDeviceRegisterInputReportCallback` —
it only uses element-value callbacks, so over Bluetooth it is completely deaf. SDL/HIDAPI-based games
read **raw reports** and are unaffected (which is exactly why Control works over Bluetooth).

One-line self-check:

    ./tools/hid_report_binding.py --device DualSense

---

## Fix

### Wired (USB): drop in a mapping file

Put this repo's `gamecontrollerdb.txt` into the **game's save directory**:

    ~/Library/Application Support/com.tobyfox.deltarune/gamecontrollerdb.txt

Install / roll back:

    ./install.sh              # install
    ./install.sh --uninstall  # roll back

**Why this location**: disassembling the gamepad init function in `Mac_Runner` shows the lookup order is

    "gamecontrollerdb.txt" -> LoadSave::SaveFileExists    (1. save dir, first)
                           -> LoadSave::BundleFileExists   (2. inside the app bundle)
    "SDL_GAMECONTROLLERCONFIG" -> EnvironmentGetVariable   (3. environment variable fallback)

(1) wins if present. So this requires **no app bundle changes, no signature breakage, no notarization
involvement** — deleting the file fully reverts it.

> The same file also corrects the button order over Bluetooth (both transports compute the same GUID).
> Note however that the **right-stick axis mapping is only correct for USB** (see "Known limitations").

### Bluetooth: inject a dylib

Since element-value callbacks receive nothing at all, the only way around it is to bypass them:
inject a bridge dylib that **reads raw reports → parses buttons → fabricates `IOHIDValueRef`s and
feeds them to the runtime's own callback**. This way the runtime performs its own cookie→index
bookkeeping and we need to know nothing about its internals.

Implementation: [`tools/ds5bridge/ds5rawfix.c`](tools/ds5bridge/ds5rawfix.c)

1. Loaded as a dependency of `libYoYoGamepad.dylib`
2. At construction time it rewrites the slots in that image's `__got` / `__la_symbol_ptr`
   that point to `IOHIDDeviceRegisterInputValueCallback`
3. It additionally registers `IOHIDDeviceRegisterInputReportCallback` to parse 0x31 reports
4. It only handles `reportID == 0x31`, so **USB is unaffected** (USB uses 0x01)

Installation (already performed):

    # 1. copy the dylib into the app bundle
    cp tools/ds5bridge/libDS5RawFix.dylib \
       "DELTARUNE.app/Contents/Frameworks/"

    # 2. add the dependency to libYoYoGamepad.dylib (no env var needed, loads automatically)
    ./tools/ds5bridge/add_lc_load_dylib.py \
       "DELTARUNE.app/Contents/Frameworks/libYoYoGamepad.dylib" \
       "@loader_path/libDS5RawFix.dylib"

    # 3. re-sign the whole app ad-hoc
    codesign --force --deep -s - "DELTARUNE.app"

#### Rolling back

| What was changed | How to roll back |
|---|---|
| Wired (`gamecontrollerdb.txt`) | `./install.sh --uninstall` |
| Bluetooth (injected dylib) | Steam → Library → DELTARUNE → Properties → Installed Files → **Verify integrity of game files** |

Bluetooth must go through Steam verification: `Mac_Runner` and `libYoYoGamepad.dylib` have been
modified, so Steam detects the hash mismatch and re-downloads the originals (including the original
Developer ID signature).

#### Cost

- **Loses the Developer ID signature and notarization**, becoming ad-hoc signed
- Will be refused on launch if Gatekeeper is re-enabled
- Diagnostic log is written to `/tmp/ds5rawfix.log` (a successful hook prints
  "挂钩成功 … 按钮元素=14 轴元素=6 十字键元素=1")

---

## Runtime internals (measured, not guessed)

### GUID algorithm

Full evidence in `docs/DS5_diagnosis_report.md` §2.6. Key points:

- `guid[0..3] = 3` (bus is **hard-coded to USB**), `guid[4..5] = VendorID` LE,
  `guid[8..9] = ProductID` LE, `guid[12..13] = VersionNumber` LE, all other bytes zero
- **The CRC field is always 0** — the GUID with `crc:5657` seen in Steam's log **does not apply** here
- Fallback branch (when vendor or product cannot be read, typically Bluetooth): `guid[0..3] = 5`,
  `guid[4..15]` = first 12 bytes of the IOKit `Product` property string

Plugging in the measured values (`VendorID = 0x054C` / `ProductID = 0x0CE6` / `VersionNumber = 0x0100`):

    03000000 4c05 0000 e60c 0000 0001 0000
     bus=3    vendor   product  version

    030000004c050000e60c000000010000

Bluetooth name-based form: `05000000` + first 12 bytes of `"DualSense Wireless Controller"` = `DualSense Wi`
→ `050000004475616c53656e7365205769`

### Element indices (device HID descriptor enumeration order)

    b0=□ b1=✕ b2=◯ b3=△ b4=L1 b5=R1 b6=L2 b7=R2
    b8=Create b9=Options b10=L3 b11=R3 b12=PS b13=Touchpad
    USB axis order: a0=LeftX a1=LeftY a2=Z(L2) a3=Rz(R2) a4=Rx(RightX) a5=Ry(RightY) a6=Hat

### Bluetooth 0x31 report layout (measured)

| Byte | Contents |
|---|---|
| `[0]` | `0x31` report ID |
| `[1]` | seq |
| `[2]`–`[5]` | LX / LY / RX / RY |
| `[6]`–`[7]` | L2 / R2 analog |
| `[8]` | `0x01` reserved byte |
| `[9]` | buttons0: low nibble = D-pad (0=N…7=NW, **8=released**); `0x10=□ 0x20=✕ 0x40=◯ 0x80=△` |
| `[10]` | buttons1: `0x01=L1 0x02=R1 0x04=L2 0x08=R2` + `0x10=Create 0x20=Options 0x40=L3 0x80=R3` |
| `[11]` | buttons2: `0x01=PS 0x02=Touchpad 0x04=Mute` |

---

## Known limitations

- **Right-stick axis mapping is wrong over Bluetooth**: the Bluetooth axis enumeration order is
  `X Y Z Rz Hat Rx Ry` (the Hat is inserted at a4), unlike USB's `X Y Z Rz Rx Ry Hat`.
  Both transports compute the *same* GUID, so the mapping table cannot tell them apart —
  therefore `rightx:a4, righty:a5` in `gamecontrollerdb.txt` is **only correct for USB**.
  DELTARUNE does not use the right stick, so there is no practical impact.
- **The D-pad goes through a hat element over Bluetooth**, so the `dpup:h0.1`-style entries in
  `gamecontrollerdb.txt` have a similar transport-dependent caveat; DELTARUNE reads the D-pad via
  `gp_padu..padr` directly and works in practice.
- The Steam Input virtual-gamepad route was not verified (the injection route was chosen instead).

---

## Repository layout

    README.md / README.en.md          this document (Chinese / English)
    gamecontrollerdb.txt              wired fix: installable mapping (3 variants)
    install.sh                        wired fix: install / roll back to the save directory
    docs/DS5_诊断报告.md                root cause and evidence (Chinese)
    docs/DS5_diagnosis_report.md      root cause and evidence (English)
    tools/find_xref.py                disassembly rip-relative xref finder
    tools/hid_report_binding.py       HID element/report binding diagnostics
    tools/probes/                     reproduction probe sources
    tools/ds5bridge/                  Bluetooth fix: bridge dylib and its source
        ds5rawfix.c                   the bridge implementation
        libDS5RawFix.dylib            build artifact (universal)
        add_lc_load_dylib.py          inserts an LC_LOAD_DYLIB into a Mach-O

### Probes

| File | Purpose |
|---|---|
| `hidprobe.c` | match + `IOHIDDeviceOpen` + button element count |
| `hidprobe2.c` | input-path liveness check (element value snapshots, no button press needed) |
| `hidlist.c` | dump the "runtime's view" of element indices (filtered by `type∈{1,2,3}` ∧ `page∈1..12`) |
| `hidorder.c` | A/B reproduction of the runtime's IOKit call order (`hidorder runner` / `canonical`) |
| `hidbtn.c` | button/axis event recorder; determines whether events reach third-party HID clients |
| `rawdump.c` | dump raw input reports |
| `rawcal.c` / `rawcal2.c` | report diff calibration (the latter writes `/tmp/ds5cal.log`) |
| `vdev.c` / `vsub.c` | verify element reachability of a virtual HID device |

Build (only Xcode Command Line Tools needed):

    clang -framework IOKit -framework CoreFoundation -o hidbtn tools/probes/hidbtn.c

---

## Disclaimer

This repository contains only forensic notes, a configuration patch and a self-use binary patch.
It **does not contain the game itself or any of Toby Fox's assets**.
