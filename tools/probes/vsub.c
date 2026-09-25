// 订阅 {UsagePage:1,Usage:5} 设备，登记元素值回调，报告按钮事件是否到达。
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
static int nb=0,nv=0;
static void on_value(void*c,IOReturn r,void*s,IOHIDValueRef v){(void)c;(void)r;(void)s;
  IOHIDElementRef e=IOHIDValueGetElement(v);
  long p=IOHIDElementGetUsagePage(e),u=IOHIDElementGetUsage(e),x=IOHIDValueGetIntegerValue(v);
  if(p==9){nb++;printf("  [订阅方] 按钮 usage=%ld -> %ld\n",u,x);fflush(stdout);} else nv++;
}
static void on_match(void*c,IOReturn r,void*s,IOHIDDeviceRef d){(void)c;(void)r;(void)s;
  char nm[128]={0}; CFTypeRef p=IOHIDDeviceGetProperty(d,CFSTR(kIOHIDProductKey));
  if(p&&CFGetTypeID(p)==CFStringGetTypeID()) CFStringGetCString(p,nm,sizeof(nm),kCFStringEncodingUTF8);
  printf("  [订阅方] 匹配到: %s\n",nm); fflush(stdout);
  IOHIDDeviceRegisterInputValueCallback(d,on_value,NULL);
}
int main(int argc,char**argv){
    double dur=argc>1?atof(argv[1]):8.0;
    IOHIDManagerRef m=IOHIDManagerCreate(NULL,kIOHIDOptionsTypeNone);
    CFMutableArrayRef a=CFArrayCreateMutable(NULL,3,&kCFTypeArrayCallBacks);
    int pg[3]={1,1,1},us[3]={4,5,8};
    for(int i=0;i<3;i++){CFNumberRef kp=CFNumberCreate(NULL,kCFNumberIntType,&pg[i]);
      CFNumberRef ku=CFNumberCreate(NULL,kCFNumberIntType,&us[i]);
      const void*k[2]={CFSTR(kIOHIDDeviceUsagePageKey),CFSTR(kIOHIDDeviceUsageKey)};
      const void*v[2]={kp,ku};
      CFDictionaryRef d=CFDictionaryCreate(NULL,k,v,2,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
      CFArrayAppendValue(a,d);CFRelease(d);CFRelease(kp);CFRelease(ku);}
    IOHIDManagerSetDeviceMatchingMultiple(m,a);
    IOHIDManagerRegisterDeviceMatchingCallback(m,on_match,NULL);
    IOHIDManagerScheduleWithRunLoop(m,CFRunLoopGetCurrent(),kCFRunLoopDefaultMode);
    IOHIDManagerOpen(m,kIOHIDOptionsTypeNone);
    printf("  [订阅方] 监听 %.0f 秒\n",dur); fflush(stdout);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode,dur,false);
    printf("\n  [订阅方] 按钮事件 %d 次，其它页事件 %d 次 => %s\n",nb,nv,
        nb>0?"元素值回调【可达】":"元素值回调【收不到】");
    return 0;
}
