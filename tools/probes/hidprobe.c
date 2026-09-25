// hidprobe.c —— 用与 GameMaker 运行时完全相同的匹配条件探测 IOKit HID。
//
// 匹配字典来自 libYoYoGamepad.dylib 反汇编（0x4732-0x4858）：
//   {DeviceUsagePage:1, DeviceUsage:4}   Joystick
//   {DeviceUsagePage:1, DeviceUsage:5}   Game Pad
//   {DeviceUsagePage:1, DeviceUsage:8}   Multi-axis Controller
//
// 输出：IOHIDManager 实际匹配到的设备、其 IOKit 类、能否 Open、按钮元素数。

#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>

static int g_found = 0;

static long num_prop(IOHIDDeviceRef d, CFStringRef key) {
    CFTypeRef v = IOHIDDeviceGetProperty(d, key);
    if (v && CFGetTypeID(v) == CFNumberGetTypeID()) {
        long n = 0;
        CFNumberGetValue(v, kCFNumberLongType, &n);
        return n;
    }
    return -1;
}

static void str_prop(IOHIDDeviceRef d, CFStringRef key, char *buf, size_t n) {
    buf[0] = 0;
    CFTypeRef v = IOHIDDeviceGetProperty(d, key);
    if (v && CFGetTypeID(v) == CFStringGetTypeID()) {
        CFStringGetCString(v, buf, n, kCFStringEncodingUTF8);
    }
}

static int count_input_buttons(IOHIDDeviceRef d) {
    CFArrayRef elems = IOHIDDeviceCopyMatchingElements(d, NULL, kIOHIDOptionsTypeNone);
    if (!elems) return -1;
    int buttons = 0, total = 0;
    for (CFIndex i = 0; i < CFArrayGetCount(elems); i++) {
        IOHIDElementRef e = (IOHIDElementRef)CFArrayGetValueAtIndex(elems, i);
        total++;
        if (IOHIDElementGetUsagePage(e) == kHIDPage_Button) buttons++;
    }
    CFRelease(elems);
    (void)total;
    return buttons;
}

static void describe(IOHIDDeviceRef dev, const char *tag) {
    io_service_t svc = IOHIDDeviceGetService(dev);
    char cls[128] = "?";
    if (svc) IOObjectGetClass(svc, cls);

    char product[128], transport[64];
    str_prop(dev, CFSTR(kIOHIDProductKey), product, sizeof(product));
    str_prop(dev, CFSTR(kIOHIDTransportKey), transport, sizeof(transport));

    printf("  [%s] class=%s\n", tag, cls);
    printf("        Product=%s  Transport=%s\n", product, transport);
    printf("        VID=%ld  PID=%ld  Version=%ld\n",
           num_prop(dev, CFSTR(kIOHIDVendorIDKey)),
           num_prop(dev, CFSTR(kIOHIDProductIDKey)),
           num_prop(dev, CFSTR(kIOHIDVersionNumberKey)));

    IOReturn openRes = IOHIDDeviceOpen(dev, kIOHIDOptionsTypeNone);
    printf("        IOHIDDeviceOpen -> 0x%08x (%s)\n", openRes,
           openRes == kIOReturnSuccess ? "OK" : "FAILED");
    if (openRes == kIOReturnSuccess) {
        printf("        UsagePage=9(Button) 元素数 = %d\n", count_input_buttons(dev));
        IOHIDDeviceClose(dev, kIOHIDOptionsTypeNone);
    }
}

static void on_match(void *ctx, IOReturn res, void *sender, IOHIDDeviceRef dev) {
    (void)ctx; (void)res; (void)sender;
    g_found++;
    describe(dev, "callback");
}

int main(void) {
    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);

    int pages[3] = { 1, 1, 1 };
    int usages[3] = { 4, 5, 8 };
    CFMutableArrayRef arr = CFArrayCreateMutable(NULL, 3, &kCFTypeArrayCallBacks);
    for (int i = 0; i < 3; i++) {
        CFNumberRef kp = CFNumberCreate(NULL, kCFNumberIntType, &pages[i]);
        CFNumberRef ku = CFNumberCreate(NULL, kCFNumberIntType, &usages[i]);
        const void *keys[2] = { CFSTR(kIOHIDDeviceUsagePageKey), CFSTR(kIOHIDDeviceUsageKey) };
        const void *vals[2] = { kp, ku };
        CFDictionaryRef d = CFDictionaryCreate(NULL, keys, vals, 2,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
        CFArrayAppendValue(arr, d);
        CFRelease(d); CFRelease(kp); CFRelease(ku);
    }

    IOHIDManagerSetDeviceMatchingMultiple(mgr, arr);
    IOHIDManagerRegisterDeviceMatchingCallback(mgr, on_match, NULL);
    IOHIDManagerScheduleWithRunLoop(mgr, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOHIDManagerOpen(mgr, kIOHIDOptionsTypeNone);

    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2.5, false);

    printf("\n--- callback 触发次数: %d ---\n", g_found);

    CFSetRef devs = IOHIDManagerCopyDevices(mgr);
    CFIndex n = devs ? CFSetGetCount(devs) : 0;
    printf("--- IOHIDManagerCopyDevices 匹配到: %ld 台 ---\n", (long)n);
    if (n > 0) {
        IOHIDDeviceRef *list = calloc((size_t)n, sizeof(IOHIDDeviceRef));
        CFSetGetValues(devs, (const void **)list);
        for (CFIndex i = 0; i < n; i++) describe(list[i], "copied");
        free(list);
    }
    if (devs) CFRelease(devs);

    IOHIDManagerClose(mgr, kIOHIDOptionsTypeNone);
    CFRelease(arr);
    CFRelease(mgr);
    return 0;
}
