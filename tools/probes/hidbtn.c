// 蓝牙态按钮事件监听（输出已修正，数组开到 256 避免越界读）
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <sys/time.h>
static double t0;
static double now(void){struct timeval tv;gettimeofday(&tv,NULL);
  return (tv.tv_sec*1000.0+tv.tv_usec/1000.0-t0)/1000.0;}
static int seen_b[256], seen_a[256], nb=0, na=0, nv=0, nraw=0;
static void on_value(void*c,IOReturn r,void*s,IOHIDValueRef v){(void)c;(void)r;(void)s;
  IOHIDElementRef e=IOHIDValueGetElement(v);
  long p=IOHIDElementGetUsagePage(e),u=IOHIDElementGetUsage(e),x=IOHIDValueGetIntegerValue(v);
  if(p==9){ if(u>=0&&u<256&&!seen_b[u]){seen_b[u]=1;nb++;}
    printf("[%6.2fs] 按钮 usage=%-3ld -> %ld\n",now(),u,x); fflush(stdout);}
  else if(p==1){ if(u>=0&&u<256&&!seen_a[u]){seen_a[u]=1;na++;}
    printf("[%6.2fs] 轴   usage=0x%-3lx -> %ld\n",now(),u,x); fflush(stdout);}
  else nv++;
}
static void on_match(void*c,IOReturn r,void*s,IOHIDDeviceRef d){(void)c;(void)r;(void)s;
  IOHIDDeviceRegisterInputValueCallback(d,on_value,NULL);
  printf("== 已挂钩，开始 %.0f 秒监听 ==\n",0.0); fflush(stdout);}
int main(int argc,char**argv){
  double dur=argc>1?atof(argv[1]):60.0;
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
  struct timeval tv;gettimeofday(&tv,NULL);t0=tv.tv_sec*1000.0+tv.tv_usec/1000.0;
  printf("== 监听 %.0f 秒，请逐个按键 ==\n",dur); fflush(stdout);
  CFRunLoopRunInMode(kCFRunLoopDefaultMode,dur,false);
  printf("\n===== 汇总 =====\n");
  printf("出现过的按钮 usage : "); for(int i=0;i<256;i++) if(seen_b[i]) printf("%d ",i);
  printf("\n出现过的轴   usage : "); for(int i=0;i<256;i++) if(seen_a[i]) printf("0x%x ",i);
  printf("\nvendor 页事件 %d 条\n",nv);
  printf("按钮种类 %d，轴种类 %d\n",nb,na);
  printf("%s\n", nb>0 ? "=> 按钮事件可达第三方进程" : "=> 按钮事件未送达第三方进程");
  return 0;
}
