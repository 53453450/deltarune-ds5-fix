// ds5rawfix.c —— 修复 macOS 蓝牙 DualSense 在 GameMaker 运行时下「按键全灭」的问题。
//
// 原理
// ----
// macOS 为蓝牙 DualSense 发布的 IOHIDUserDevice 把整条 0x31 报文当作一个不透明的
// vendor 字段（page 0xFF00 / usage 59 / 616 bit）原样透传，**没有解码成 GamePad 字段**；
// 描述符里的 Report 1（X/Y/Hat/Button）永远收不到数据。
// 于是 GameMaker 运行时（只用 IOHIDDeviceRegisterInputValueCallback，且从不调用
// IOHIDDeviceRegisterInputReportCallback）在蓝牙下拿不到任何按键。
//
// 做法：拦截运行时的回调注册，额外挂一个原始报文回调；从 0x31 报文里解出按键/摇杆，
// **伪造对应的 IOHIDValueRef 并直接调用运行时自己的回调**。
// 运行时的 cookie→索引 记账逻辑照旧执行，我们无需知道它任何内部结构。
//
// 报文布局（实测标定，report[0] = 报文 ID）
// -----------------------------------------
//   [0]  0x31 报文 ID
//   [1]  seq
//   [2]  LX   [3] LY   [4] RX   [5] RY
//   [6]  L2   [7] R2
//   [8]  0x01（保留字节，恒定）
//   [9]  buttons0: bit0-3 = 十字键(0=N,1=NE,2=E,3=SE,4=S,5=SW,6=W,7=NW,8=松开)
//                  bit4=□ bit5=✕ bit6=◯ bit7=△
//   [10] buttons1: bit0=L1 bit1=R1 bit2=L2 bit3=R2
//                  bit4=Create bit5=Options bit6=L3 bit7=R3
//   [11] buttons2: bit0=PS bit1=触摸板 bit2=Mute
//
// 应急开关：创建 /tmp/ds5rawfix.off 即停用。日志：/tmp/ds5rawfix.log

#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDElement.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDValue.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <libkern/OSCacheControl.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DYLD_INTERPOSE(_replacement, _replacee)                              \
    __attribute__((used)) static struct {                                    \
        const void *replacement;                                             \
        const void *replacee;                                                \
    } _interpose_##_replacee                                                 \
        __attribute__((section("__DATA,__interpose"))) = {                   \
            (const void *)(unsigned long)&_replacement,                      \
            (const void *)(unsigned long)&_replacee};

#define MAXDEV 4
#define OFF_FILE "/tmp/ds5rawfix.off"
#define LOG_FILE "/tmp/ds5rawfix.log"

typedef void (*IOHIDValueCallback)(void *context, IOReturn result, void *sender,
                                   IOHIDValueRef value);
typedef struct {
    IOHIDDeviceRef dev;
    IOHIDValueCallback cb;
    void *ctx;
    IOHIDElementRef btn[16]; /* page 9, usage 1..16 -> btn[usage-1] */
    IOHIDElementRef ax[6];   /* page 1, usage 0x30..0x35 -> ax[usage-0x30] */
    IOHIDElementRef hat;     /* page 1, usage 0x39 */
    int registered;
    uint8_t rawbuf[256];
} Slot;

static Slot g_slots[MAXDEV];
static int g_nslot = 0;
static int g_logged = 0;
static int g_disabled = 0;
static int g_fed = 0;

static void logline(const char *fmt, ...) {
    if (g_logged > 300) return;
    FILE *f = fopen(LOG_FILE, g_logged == 0 ? "w" : "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
    g_logged++;
}

static Slot *slot_for(IOHIDDeviceRef dev) {
    for (int i = 0; i < g_nslot; i++)
        if (g_slots[i].dev == dev) return &g_slots[i];
    return NULL;
}

/* (page,usage) -> element；过滤规则与运行时一致：type ∈{1,2,3} 且 usagePage ∈1..12，
   按 CopyMatchingElements 顺序取首次出现者（运行时靠 cookie 去重，同理）。 */
static void build_elements(Slot *s) {
    CFArrayRef elems = IOHIDDeviceCopyMatchingElements(s->dev, NULL, kIOHIDOptionsTypeNone);
    if (!elems) return;
    CFIndex n = CFArrayGetCount(elems);
    for (CFIndex i = 0; i < n; i++) {
        IOHIDElementRef e = (IOHIDElementRef)CFArrayGetValueAtIndex(elems, i);
        int type = (int)IOHIDElementGetType(e);
        long page = IOHIDElementGetUsagePage(e);
        long usage = IOHIDElementGetUsage(e);
        if (type < 1 || type > 3) continue;
        if (page < 1 || page > 12) continue;
        if (page == 9 && usage >= 1 && usage <= 16) {
            if (!s->btn[usage - 1]) s->btn[usage - 1] = e;
        } else if (page == 1) {
            if (usage == 0x39) {
                if (!s->hat) s->hat = e;
            } else if (usage >= 0x30 && usage <= 0x35) {
                if (!s->ax[usage - 0x30]) s->ax[usage - 0x30] = e;
            }
        }
    }
    CFRelease(elems);
}

static void feed(Slot *s, IOHIDElementRef e, CFIndex value) {
    if (!e || !s->cb) return;
    IOHIDValueRef v = IOHIDValueCreateWithIntegerValue(kCFAllocatorDefault, e,
                                                      mach_absolute_time(), value);
    if (!v) return;
    s->cb(s->ctx, kIOReturnSuccess, s->dev, v);
    CFRelease(v);
    g_fed++;
}

static void on_raw(void *context, IOReturn result, void *sender, IOHIDReportType type,
                   uint32_t reportID, uint8_t *report, CFIndex length) {
    (void)context; (void)result; (void)type;
    if (g_disabled) return;
    if (reportID != 0x31 || length < 12) return;
    Slot *s = slot_for((IOHIDDeviceRef)sender);
    if (!s && g_nslot == 1) s = &g_slots[0];
    if (!s || !s->cb) return;

    uint8_t b0 = report[9], b1 = report[10], b2 = report[11];

    static const struct { uint8_t bit; int usage; } face[] = {
        {0x10, 1}, {0x20, 2}, {0x40, 3}, {0x80, 4},        /* □ ✕ ◯ △ */
    };
    for (unsigned i = 0; i < sizeof(face) / sizeof(face[0]); i++)
        feed(s, s->btn[face[i].usage - 1], (b0 & face[i].bit) ? 1 : 0);

    static const struct { uint8_t bit; int usage; } sh[] = {
        {0x01, 5}, {0x02, 6}, {0x04, 7}, {0x08, 8},        /* L1 R1 L2 R2 */
        {0x10, 9}, {0x20, 10}, {0x40, 11}, {0x80, 12},     /* Create Options L3 R3 */
    };
    for (unsigned i = 0; i < sizeof(sh) / sizeof(sh[0]); i++)
        feed(s, s->btn[sh[i].usage - 1], (b1 & sh[i].bit) ? 1 : 0);

    static const struct { uint8_t bit; int usage; } misc[] = {
        {0x01, 13}, {0x02, 14}, {0x04, 15},                /* PS 触摸板 Mute */
    };
    for (unsigned i = 0; i < sizeof(misc) / sizeof(misc[0]); i++)
        feed(s, s->btn[misc[i].usage - 1], (b2 & misc[i].bit) ? 1 : 0);

    feed(s, s->hat, (CFIndex)(b0 & 0x0F));                 /* 0..7 方向，8 松开 */

    feed(s, s->ax[0x30 - 0x30], report[2]);                /* X  -> 左摇杆 X */
    feed(s, s->ax[0x31 - 0x30], report[3]);                /* Y  -> 左摇杆 Y */
    feed(s, s->ax[0x33 - 0x30], report[4]);                /* Rx -> 右摇杆 X */
    feed(s, s->ax[0x34 - 0x30], report[5]);                /* Ry -> 右摇杆 Y */
    feed(s, s->ax[0x32 - 0x30], report[6]);                /* Z  -> L2 模拟 */
    feed(s, s->ax[0x35 - 0x30], report[7]);                /* Rz -> R2 模拟 */
}

static void my_IOHIDDeviceRegisterInputValueCallback(IOHIDDeviceRef device,
                                                     IOHIDValueCallback callback,
                                                     void *context);

/* ------------------------------------------------------------------ *
 * 挂钩方式：直接改写 libYoYoGamepad.dylib 的 __got 槽位
 *
 * 为什么不用 DYLD_INTERPOSE：dyld 只对「插入镜像」（DYLD_INSERT_LIBRARIES）
 * 应用 __interpose 段；本 dylib 是以 LC_LOAD_DYLIB 依赖方式加载的，
 * interpose 不会生效（实测对照确认）。故改为构造期改写目标镜像的 GOT。
 * ------------------------------------------------------------------ */

static int g_hooked = 0;
static int g_patched_slots = 0;

static void patch_sect(const struct mach_header_64 *hdr, const char *seg, const char *sect,
                       void *target, void *replacement) {
    unsigned long sz = 0;
    intptr_t *slots = (intptr_t *)getsectiondata(hdr, seg, sect, &sz);
    if (!slots || sz < sizeof(intptr_t)) return;

    vm_address_t page = (vm_address_t)slots & ~(vm_address_t)(vm_page_size - 1);
    vm_size_t span = (vm_address_t)slots + sz - page;
    kern_return_t kr = vm_protect(mach_task_self(), page, span, 0,
                                  VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);

    size_t n = sz / sizeof(intptr_t);
    int local = 0;
    for (size_t i = 0; i < n; i++) {
        if (slots[i] == (intptr_t)target) {
            slots[i] = (intptr_t)replacement;
            local++;
        }
    }
    if (local) {
        sys_icache_invalidate((void *)page, span);
        g_patched_slots += local;
        logline("[ds5rawfix] 改写 %s,%s 槽位 %d 个 (vm_protect=0x%x)", seg, sect, local, kr);
    }
    if (kr == KERN_SUCCESS)
        vm_protect(mach_task_self(), page, span, 0, VM_PROT_READ);
}

static void install_hook(void) {
    void *real = dlsym(RTLD_DEFAULT, "IOHIDDeviceRegisterInputValueCallback");
    logline("[ds5rawfix] 原函数地址 = %p", real);
    if (!real) return;
    if ((void *)real == (void *)&my_IOHIDDeviceRegisterInputValueCallback) {
        logline("[ds5rawfix] 符号已被替换为自己，跳过");
        return;
    }

    uint32_t n = _dyld_image_count();
    int hit = 0;
    for (uint32_t i = 0; i < n; i++) {
        const char *nm = _dyld_get_image_name(i);
        if (!nm || !strstr(nm, "libYoYoGamepad")) continue;
        const struct mach_header *h = _dyld_get_image_header(i);
        if (!h || h->magic != MH_MAGIC_64) {
            logline("[ds5rawfix] %s 不是 64 位 mach-o，跳过", nm);
            continue;
        }
        hit++;
        logline("[ds5rawfix] 目标镜像 %s @ %p", nm, (void *)h);
        const struct mach_header_64 *h64 = (const struct mach_header_64 *)h;
        patch_sect(h64, "__DATA_CONST", "__got", real,
                   (void *)&my_IOHIDDeviceRegisterInputValueCallback);
        patch_sect(h64, "__DATA", "__got", real,
                   (void *)&my_IOHIDDeviceRegisterInputValueCallback);
        patch_sect(h64, "__DATA", "__la_symbol_ptr", real,
                   (void *)&my_IOHIDDeviceRegisterInputValueCallback);
        patch_sect(h64, "__AUTH_CONST", "__auth_got", real,
                   (void *)&my_IOHIDDeviceRegisterInputValueCallback);
    }
    if (!hit) logline("[ds5rawfix] !! 未找到 libYoYoGamepad 镜像（共 %u 个镜像）", n);
    else logline("[ds5rawfix] 完成，共改写 %d 个槽位", g_patched_slots);
}

static void my_IOHIDDeviceRegisterInputValueCallback(IOHIDDeviceRef device,
                                                     IOHIDValueCallback callback,
                                                     void *context) {
    if (g_hooked) return;   /* 防重入 */
    g_hooked = 1;

    /* 直接按名字调用原实现（本镜像的绑定指向 IOKit，不会递归） */
    IOHIDDeviceRegisterInputValueCallback(device, callback, context);

    if (g_disabled || !callback) { g_hooked = 0; return; }

    Slot *s = slot_for(device);
    if (!s) {
        if (g_nslot >= MAXDEV) { g_hooked = 0; return; }
        s = &g_slots[g_nslot++];
        memset(s, 0, sizeof(*s));
        s->dev = device;
        build_elements(s);
        int nb = 0, na = 0;
        for (int i = 0; i < 16; i++) if (s->btn[i]) nb++;
        for (int i = 0; i < 6; i++) if (s->ax[i]) na++;
        logline("[ds5rawfix] 挂钩成功 dev=%p ctx=%p 按钮元素=%d 轴元素=%d 十字键元素=%d",
                (void *)device, context, nb, na, s->hat ? 1 : 0);
    }
    s->cb = callback;
    s->ctx = context;
    if (!s->registered) {
        s->registered = 1;
        IOHIDDeviceRegisterInputReportCallback(device, s->rawbuf, sizeof(s->rawbuf),
                                               on_raw, NULL);
    }
    g_hooked = 0;
}

DYLD_INTERPOSE(my_IOHIDDeviceRegisterInputValueCallback,
               IOHIDDeviceRegisterInputValueCallback)

__attribute__((constructor)) static void ds5rawfix_init(void) {
    if (access(OFF_FILE, F_OK) == 0) g_disabled = 1;
    logline("[ds5rawfix] 已载入 (pid=%d)%s", (int)getpid(),
            g_disabled ? " —— 已被 /tmp/ds5rawfix.off 停用" : "");
    if (!g_disabled) install_hook();
}
