// 发布虚拟手柄 IOHIDUserDevice（SDK 无公开头文件，手动声明符号）并周期发送输入报文。
#include <IOKit/hid/IOHIDKeys.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct __IOHIDUserDevice *IOHIDUserDeviceRef;
extern IOHIDUserDeviceRef IOHIDUserDeviceCreate(CFAllocatorRef, CFDictionaryRef);
extern IOReturn IOHIDUserDeviceHandleReport(IOHIDUserDeviceRef, const uint8_t *, CFIndex);
extern void IOHIDUserDeviceSetDispatchQueue(IOHIDUserDeviceRef, dispatch_queue_t);
extern void IOHIDUserDeviceActivate(IOHIDUserDeviceRef _Nullable) __attribute__((weak_import));

static const unsigned char kDesc[] = {
 0x05,0x01, 0x09,0x05, 0xa1,0x01,
 0x09,0x30, 0x09,0x31, 0x09,0x32, 0x09,0x35,
 0x15,0x00, 0x26,0xff,0x00, 0x75,0x08, 0x95,0x04, 0x81,0x02,
 0x09,0x39, 0x15,0x00, 0x25,0x07, 0x35,0x00, 0x46,0x3b,0x01, 0x65,0x14,
 0x75,0x04, 0x95,0x01, 0x81,0x42, 0x65,0x00,
 0x05,0x09, 0x19,0x01, 0x29,0x0e, 0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x0e, 0x81,0x02,
 0x06,0x00,0xff, 0x09,0x01, 0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x06, 0x81,0x03,
 0xc0
};

int main(void){
    int vid=0x054C, pid=0x0CE6, ver=0x0100, pup=1, pu=5;
    CFNumberRef nVid=CFNumberCreate(NULL,kCFNumberIntType,&vid);
    CFNumberRef nPid=CFNumberCreate(NULL,kCFNumberIntType,&pid);
    CFNumberRef nVer=CFNumberCreate(NULL,kCFNumberIntType,&ver);
    CFNumberRef nPup=CFNumberCreate(NULL,kCFNumberIntType,&pup);
    CFNumberRef nPu =CFNumberCreate(NULL,kCFNumberIntType,&pu);
    CFDataRef desc=CFDataCreate(NULL,kDesc,sizeof(kDesc));
    const void*keys[]={CFSTR(kIOHIDReportDescriptorKey),CFSTR("VendorID"),CFSTR("ProductID"),
                       CFSTR("VersionNumber"),CFSTR("PrimaryUsagePage"),CFSTR("PrimaryUsage"),
                       CFSTR(kIOHIDProductKey),CFSTR(kIOHIDTransportKey),CFSTR("SerialNumber")};
    const void*vals[]={desc,nVid,nPid,nVer,nPup,nPu,
                       CFSTR("DS5Bridge Virtual Pad"),CFSTR("Virtual"),CFSTR("BRIDGE-0001")};
    CFDictionaryRef props=CFDictionaryCreate(NULL,keys,vals,9,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);

    IOHIDUserDeviceRef dev=IOHIDUserDeviceCreate(kCFAllocatorDefault,props);
    if(!dev){ printf("!! IOHIDUserDeviceCreate 失败\n"); return 1; }
    if(IOHIDUserDeviceActivate) IOHIDUserDeviceSetDispatchQueue(dev,dispatch_get_main_queue());
    printf("虚拟设备已发布 %04x:%04x\n",vid,pid); fflush(stdout);

    unsigned char rep[7];
    for(int t=0;t<12;t++){
        memset(rep,0,sizeof(rep));
        rep[0]=0x80; rep[1]=0x80;
        unsigned btn=(t%2)?0x2u:0x1u;
        rep[4]=(unsigned char)(btn<<4);
        IOReturn r=IOHIDUserDeviceHandleReport(dev,rep,sizeof(rep));
        printf("  发送 #%2d byte4=0x%02x -> 0x%08x\n",t,rep[4],r); fflush(stdout);
        usleep(500000);
    }
    printf("结束\n");
    return 0;
}
