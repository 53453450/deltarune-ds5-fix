// 蓝牙 DualSense 报文标定：只打印「变化的字节」，便于对照按键位置
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
static uint8_t prev[128]; static int have=0; static int n=0;
static void on_raw(void*c,IOReturn r,void*s,IOHIDReportType t,uint32_t rid,uint8_t*rep,CFIndex len){
  (void)c;(void)r;(void)s;(void)t;
  if(!have){ memcpy(prev,rep,len); have=1;
    printf("基线 reportID=0x%02x len=%ld\n  ",rid,(long)len);
    for(int i=0;i<len&&i<24;i++) printf("%02x ",rep[i]); printf("\n"); fflush(stdout); return; }
  char d[512]; int p=0, ch=0;
  for(int i=0;i<len&&i<24;i++) if(rep[i]!=prev[i]){ p+=snprintf(d+p,sizeof(d)-p,"[%d]%02x->%02x ",i,prev[i],rep[i]); ch++; }
  if(ch && n++<250){ printf("变化: %s\n",d); fflush(stdout); }
  memcpy(prev,rep,len);
}
static uint8_t buf[512];
static void on_match(void*c,IOReturn r,void*s,IOHIDDeviceRef dev){(void)c;(void)r;(void)s;
  printf("已挂上设备，开始标定\n"); fflush(stdout);
  IOHIDDeviceRegisterInputReportCallback(dev,buf,sizeof(buf),on_raw,NULL);
}
int main(void){
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
  CFRunLoopRunInMode(kCFRunLoopDefaultMode,45.0,false);
  printf("标定结束\n");
  return 0;
}
