#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
static IOHIDDeviceRef g_dev;
static void on_match(void *c, IOReturn r, void *s, IOHIDDeviceRef d){ (void)c;(void)r;(void)s; g_dev=d; }
int main(void){
    IOHIDManagerRef m=IOHIDManagerCreate(NULL,kIOHIDOptionsTypeNone);
    CFMutableArrayRef a=CFArrayCreateMutable(NULL,3,&kCFTypeArrayCallBacks);
    int pg[3]={1,1,1},us[3]={4,5,8};
    for(int i=0;i<3;i++){
        CFNumberRef kp=CFNumberCreate(NULL,kCFNumberIntType,&pg[i]);
        CFNumberRef ku=CFNumberCreate(NULL,kCFNumberIntType,&us[i]);
        const void*k[2]={CFSTR(kIOHIDDeviceUsagePageKey),CFSTR(kIOHIDDeviceUsageKey)};
        const void*v[2]={kp,ku};
        CFDictionaryRef d=CFDictionaryCreate(NULL,k,v,2,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        CFArrayAppendValue(a,d); CFRelease(d);CFRelease(kp);CFRelease(ku);
    }
    IOHIDManagerSetDeviceMatchingMultiple(m,a);
    IOHIDManagerRegisterDeviceMatchingCallback(m,on_match,NULL);
    IOHIDManagerScheduleWithRunLoop(m,CFRunLoopGetCurrent(),kCFRunLoopDefaultMode);
    IOHIDManagerOpen(m,kIOHIDOptionsTypeNone);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode,1.0,false);
    if(!g_dev){printf("no device\n");return 1;}
    CFArrayRef e=IOHIDDeviceCopyMatchingElements(g_dev,NULL,kIOHIDOptionsTypeNone);
    CFIndex n=e?CFArrayGetCount(e):0;
    printf("总元素 %ld\n",(long)n);
    printf("--- 原始顺序 (idx type page usage cookie) ---\n");
    for(CFIndex i=0;i<n;i++){
        IOHIDElementRef el=(IOHIDElementRef)CFArrayGetValueAtIndex(e,i);
        printf("%3ld  %4d %5ld %4ld %4ld\n",(long)i,
          (int)IOHIDElementGetType(el),(long)IOHIDElementGetUsagePage(el),
          (long)IOHIDElementGetUsage(el),(long)IOHIDElementGetCookie(el));
    }
    printf("--- 运行时视角: 只保留 type 1..4 且 page 1..12 ---\n");
    int bi=0,ai=0;
    for(CFIndex i=0;i<n;i++){
        IOHIDElementRef el=(IOHIDElementRef)CFArrayGetValueAtIndex(e,i);
        int t=(int)IOHIDElementGetType(el); long p=IOHIDElementGetUsagePage(el), u=IOHIDElementGetUsage(el);
        if(t>=1&&t<=4&&p>=1&&p<=12){
            if(p==9)  printf("  b%-3d <- type=%d page=%ld usage=%ld\n",bi++,t,p,u);
            else      printf("  a%-3d <- type=%d page=%ld usage=%ld\n",ai++,t,p,u);
        }
    }
    return 0;
}
