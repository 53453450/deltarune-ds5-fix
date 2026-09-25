// 蓝牙 DualSense 报文标定：写入日志文件，含心跳与变化字节统计
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#define LOGP "/tmp/ds5cal.log"
static FILE *lg;
static uint8_t prev[128]; static int have=0; static double t0;
static int everchg[128];
static double now(void){struct timeval tv;gettimeofday(&tv,NULL);
  return tv.tv_sec+tv.tv_usec/1e6-t0;}

static void on_raw(void*c,IOReturn r,void*s,IOHIDReportType t,uint32_t rid,uint8_t*rep,CFIndex len){
  (void)c;(void)r;(void)s;(void)t;
  if(!have){ memcpy(prev,rep,len); have=1;
    char b[400]; int p=0;
    for(int i=0;i<len&&i<26;i++) p+=snprintf(b+p,sizeof(b)-p,"%02x ",rep[i]);
    fprintf(lg,"[%7.2f] 基线 reportID=0x%02x len=%ld\n    %s\n",now(),rid,(long)len,b); fflush(lg); return; }
  char d[600]; int p=0,ch=0;
  for(int i=0;i<len&&i<26;i++) if(rep[i]!=prev[i]){ p+=snprintf(d+p,sizeof(d)-p,"[%d]%02x->%02x ",i,prev[i],rep[i]); ch++; everchg[i]++; }
  if(ch) { fprintf(lg,"[%7.2f] %s\n",now(),d); fflush(lg); }
  memcpy(prev,rep,len);
}
static uint8_t buf[512];
static void on_match(void*c,IOReturn r,void*s,IOHIDDeviceRef dev){(void)c;(void)r;(void)s;
  char nm[128]={0}; CFTypeRef p=IOHIDDeviceGetProperty(dev,CFSTR(kIOHIDProductKey));
  if(p&&CFGetTypeID(p)==CFStringGetTypeID()) CFStringGetCString(p,nm,sizeof(nm),kCFStringEncodingUTF8);
  fprintf(lg,"[%7.2f] 已挂上设备: %s\n",now(),nm); fflush(lg);
  IOHIDDeviceRegisterInputReportCallback(dev,buf,sizeof(buf),on_raw,NULL);
}
int main(int argc,char**argv){
  double dur = argc>1?atof(argv[1]):90.0;
  lg=fopen(LOGP,"w"); if(!lg){printf("无法写 %s\n",LOGP);return 1;}
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
  fprintf(lg,"监听 %.0f 秒，日志 -> %s\n",dur,LOGP); fflush(lg);
  CFRunLoopRunInMode(kCFRunLoopDefaultMode,dur,false);
  fprintf(lg,"\n=== 汇总：曾经变化过的字节索引 ===\n");
  for(int i=0;i<128;i++) if(everchg[i]) fprintf(lg,"  byte %-3d 变化 %d 次\n",i,everchg[i]);
  fflush(lg); fclose(lg);
  printf("完成，日志在 %s\n",LOGP);
  return 0;
}
