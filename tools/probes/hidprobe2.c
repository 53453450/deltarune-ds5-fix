// hidprobe2.c —— 判定 IOHIDManager 代管路径下，输入值是否真的在流动。
//
// 复刻 libYoYoGamepad.dylib 的用法：IOHIDManager 匹配 -> 设备回调里
// 注册 IOHIDDeviceRegisterInputValueCallback（运行时不调用 IOHIDDeviceOpen）。
//
// 除回调计数外，还做一次「1.5 秒前后元素值快照对比」——不需要人按键，
// 只要设备在持续上报（陀螺仪/触摸板噪声即可），就能判定输入通路是否活着。

#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int g_cb_count = 0;
static int g_cb_printed = 0;

static void on_value(void *ctx, IOReturn res, void *sender, IOHIDValueRef val) {
    (void)ctx; (void)res; (void)sender;
    g_cb_count++;
    if (g_cb_printed < 8) {
        IOHIDElementRef e = IOHIDValueGetElement(val);
        printf("        [cb] usagePage=%ld usage=%ld value=%ld\n",
               (long)IOHIDElementGetUsagePage(e),
               (long)IOHIDElementGetUsage(e),
               (long)IOHIDValueGetIntegerValue(val));
        g_cb_printed++;
    }
}

static void str_prop(IOHIDDeviceRef d, CFStringRef key, char *buf, size_t n) {
    buf[0] = 0;
    CFTypeRef v = IOHIDDeviceGetProperty(d, key);
    if (v && CFGetTypeID(v) == CFStringGetTypeID())
        CFStringGetCString(v, buf, n, kCFStringEncodingUTF8);
}

static void on_match(void *ctx, IOReturn res, void *sender, IOHIDDeviceRef dev) {
    (void)ctx; (void)res; (void)sender;
    char product[128], transport[64];
    str_prop(dev, CFSTR(kIOHIDProductKey), product, sizeof(product));
    str_prop(dev, CFSTR(kIOHIDTransportKey), transport, sizeof(transport));
    printf("  匹配到: %s (%s)\n", product, transport);
    IOHIDDeviceRegisterInputValueCallback(dev, on_value, NULL);
}

static int snapshot(IOHIDDeviceRef dev, CFArrayRef elems, long *vals) {
    int n = 0;
    for (CFIndex i = 0; i < CFArrayGetCount(elems); i++) {
        IOHIDElementRef e = (IOHIDElementRef)CFArrayGetValueAtIndex(elems, i);
        IOHIDValueRef v = NULL;
        if (IOHIDDeviceGetValue(dev, e, &v) == kIOReturnSuccess && v)
            vals[n++] = IOHIDValueGetIntegerValue(v);
        else
            vals[n++] = -999999;
    }
    return n;
}

int main(void) {
    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);

    CFMutableArrayRef arr = CFArrayCreateMutable(NULL, 3, &kCFTypeArrayCallBacks);
    int pages[3] = {1, 1, 1}, usages[3] = {4, 5, 8};
    for (int i = 0; i < 3; i++) {
        CFNumberRef kp = CFNumberCreate(NULL, kCFNumberIntType, &pages[i]);
        CFNumberRef ku = CFNumberCreate(NULL, kCFNumberIntType, &usages[i]);
        const void *k[2] = { CFSTR(kIOHIDDeviceUsagePageKey), CFSTR(kIOHIDDeviceUsageKey) };
        const void *v[2] = { kp, ku };
        CFDictionaryRef d = CFDictionaryCreate(NULL, k, v, 2,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFArrayAppendValue(arr, d);
        CFRelease(d); CFRelease(kp); CFRelease(ku);
    }
    IOHIDManagerSetDeviceMatchingMultiple(mgr, arr);
    IOHIDManagerRegisterDeviceMatchingCallback(mgr, on_match, NULL);
    IOHIDManagerScheduleWithRunLoop(mgr, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOReturn openRes = IOHIDManagerOpen(mgr, kIOHIDOptionsTypeNone);
    printf("IOHIDManagerOpen -> 0x%08x\n\n", openRes);

    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, false);

    CFSetRef set = IOHIDManagerCopyDevices(mgr);
    if (!set || CFSetGetCount(set) == 0) {
        printf("!! 没有匹配到任何设备\n");
        return 1;
    }
    CFIndex n = CFSetGetCount(set);
    IOHIDDeviceRef *list = calloc((size_t)n, sizeof(IOHIDDeviceRef));
    CFSetGetValues(set, (const void **)list);

    for (CFIndex i = 0; i < n; i++) {
        IOHIDDeviceRef dev = list[i];
        CFArrayRef elems = IOHIDDeviceCopyMatchingElements(dev, NULL, kIOHIDOptionsTypeNone);
        CFIndex en = elems ? CFArrayGetCount(elems) : 0;

        int btns = 0;
        for (CFIndex j = 0; j < en; j++)
            if (IOHIDElementGetUsagePage((IOHIDElementRef)CFArrayGetValueAtIndex(elems, j)) == kHIDPage_Button)
                btns++;

        printf("设备 #%ld: 元素 %ld 个（其中 Button usagePage 9 = %d）\n", (long)i, (long)en, btns);

        long *a = calloc((size_t)en, sizeof(long));
        long *b = calloc((size_t)en, sizeof(long));
        int an = snapshot(dev, elems, a);

        g_cb_count = 0;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.5, false);
        int cbDuringWait = g_cb_count;

        int bn = snapshot(dev, elems, b);

        int changed = 0;
        printf("     回调触发: %d 次 / 1.5s\n", cbDuringWait);
        printf("     元素值快照对比(%d vs %d): ", an, bn);
        for (int j = 0; j < an && j < bn; j++) {
            if (a[j] != b[j]) {
                if (changed < 6) {
                    IOHIDElementRef e = (IOHIDElementRef)CFArrayGetValueAtIndex(elems, j);
                    printf("\n        [变化] %ld -> %ld (usagePage=%ld usage=%ld)",
                           a[j], b[j],
                           (long)IOHIDElementGetUsagePage(e), (long)IOHIDElementGetUsage(e));
                }
                changed++;
            }
        }
        printf("\n     变化元素数: %d  ->  %s\n\n", changed,
               changed > 0 ? "输入通路存活" : "输入通路无数据");
        free(a); free(b);
        if (elems) CFRelease(elems);
    }

    free(list);
    CFRelease(set);
    IOHIDManagerClose(mgr, kIOHIDOptionsTypeNone);
    CFRelease(arr);
    CFRelease(mgr);
    return 0;
}
