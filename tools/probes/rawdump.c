// 转储蓝牙 DualSense 的原始输入报文，用于确认报文 ID 与字段偏移
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
static int n=0;
static void on_raw(void*c,IOReturn r,void*s,IOHIDReportType t,uint32_t rid,uint8_t*rep,CFIndex len){
  (void)c;(void)r;(void)s;(void)t;
  if(n++>=4) return;
  printf("reportID=0x%02x (%u)  len=%ld\n   bytes:",rid,rid,(long)len);
  for(int i=0;i<len && i<16;i++) printf(" %02x",rep[i]);
  printf("\n   LX=%u LY=%u RX=%u RY=%u L2=%u R2=%u | btn0=0x%02x btn1=0x%02x btn2=0x%02x\n",
    rep[0],rep[1],rep[2],rep[3],rep[4],rep[5],rep[7],rep[8],rep[9]);
  fflush(stdout);
}
static uint8_t buf[512];
static void on_match(void*c,IOReturn r,void*s,IOHIDDeviceRef d){(void)c;(void)r;(void)s;
  printf("device matched\n"); fflush(stdout);
  IOHIDDeviceRegisterInputReportCallback(d,buf,sizeof(buf),on_raw,NULL);
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
  CFRunLoopRunInMode(kCFRunLoopDefaultMode,4.0,false);
  printf("结束\n");
  return 0;
}
