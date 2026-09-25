// hidorder.c —— A/B 复现 GameMaker 运行时的 IOKit 初始化顺序问题。
//
//   canonical : Create -> SetMatching -> RegisterCallback -> Schedule -> Open   (SDL/HIDAPI/Apple 示例)
//   runner    : Create -> Open -> Schedule -> SetMatching -> RegisterCallback   (libYoYoGamepad.dylib 0x46de-0x48e5)
//
// 两者都注册同一套匹配条件 {DeviceUsagePage:1, DeviceUsage:4/5/8}，
// 跑相同长度的 run loop，对比「设备匹配回调是否对已存在的设备触发」。

#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_cb = 0;
static char g_name[128];

static void str_prop(IOHIDDeviceRef d, CFStringRef k, char *b, size_t n) {
    b[0] = 0;
    CFTypeRef v = IOHIDDeviceGetProperty(d, k);
    if (v && CFGetTypeID(v) == CFStringGetTypeID())
        CFStringGetCString(v, b, n, kCFStringEncodingUTF8);
}

static void on_match(void *c, IOReturn r, void *s, IOHIDDeviceRef d) {
    (void)c; (void)r; (void)s;
    g_cb++;
    char nm[128], tr[64];
    str_prop(d, CFSTR(kIOHIDProductKey), nm, sizeof(nm));
    str_prop(d, CFSTR(kIOHIDTransportKey), tr, sizeof(tr));
    snprintf(g_name, sizeof(g_name), "%s (%s)", nm, tr);
}

static CFMutableArrayRef make_match(void) {
    CFMutableArrayRef a = CFArrayCreateMutable(NULL, 3, &kCFTypeArrayCallBacks);
    int pg[3] = {1, 1, 1}, us[3] = {4, 5, 8};
    for (int i = 0; i < 3; i++) {
        CFNumberRef kp = CFNumberCreate(NULL, kCFNumberIntType, &pg[i]);
        CFNumberRef ku = CFNumberCreate(NULL, kCFNumberIntType, &us[i]);
        const void *k[2] = { CFSTR(kIOHIDDeviceUsagePageKey), CFSTR(kIOHIDDeviceUsageKey) };
        const void *v[2] = { kp, ku };
        CFDictionaryRef d = CFDictionaryCreate(NULL, k, v, 2,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFArrayAppendValue(a, d);
        CFRelease(d); CFRelease(kp); CFRelease(ku);
    }
    return a;
}

static int run(const char *phase) {
    g_cb = 0; g_name[0] = 0;
    IOHIDManagerRef m = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);

    if (strcmp(phase, "canonical") == 0) {
        CFMutableArrayRef a = make_match();
        IOHIDManagerSetDeviceMatchingMultiple(m, a);
        IOHIDManagerRegisterDeviceMatchingCallback(m, on_match, NULL);
        IOHIDManagerScheduleWithRunLoop(m, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        IOHIDManagerOpen(m, kIOHIDOptionsTypeNone);
        CFRelease(a);
    } else { /* runner 顺序 */
        IOHIDManagerOpen(m, kIOHIDOptionsTypeNone);
        IOHIDManagerScheduleWithRunLoop(m, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        CFMutableArrayRef a = make_match();
        IOHIDManagerSetDeviceMatchingMultiple(m, a);
        IOHIDManagerRegisterDeviceMatchingCallback(m, on_match, NULL);
        /* 运行时还会注册 removal 回调，此处省略（不影响本测试） */
        CFRelease(a);
    }

    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2.0, false);

    CFSetRef s = IOHIDManagerCopyDevices(m);
    CFIndex n = s ? CFSetGetCount(s) : 0;
    printf("  [%-9s] 匹配回调触发 %d 次 ; IOHIDManagerCopyDevices = %ld 台 %s\n",
           phase, g_cb, (long)n, g_cb ? g_name : "");
    if (s) CFRelease(s);
    IOHIDManagerClose(m, kIOHIDOptionsTypeNone);
    CFRelease(m);
    return g_cb;
}

int main(int argc, char **argv) {
    if (argc > 1) { printf("单独测试 phase=%s\n", argv[1]); return run(argv[1]) > 0 ? 0 : 1; }
    printf("匹配条件 = {DeviceUsagePage:1, DeviceUsage:4/5/8}   run loop 各 2.0s\n\n");
    int c = run("canonical");
    int r = run("runner");
    printf("\n结论: canonical=%d, runner=%d -> %s\n", c, r,
           (c > 0 && r == 0) ? "运行时顺序对『已连接的手柄』拿不到设备匹配事件" :
           (c > 0 && r > 0) ? "两种顺序都能拿到，顺序不是本因" : "两种顺序都拿不到");
    return 0;
}
