#include "../native/msi_rear_protocol.h"
#include <iostream>
#include <cstdlib>
using namespace ymcc::msi::rearProtocol;
static unsigned checks=0;
static void check(bool ok,const char* label){++checks;if(!ok){std::cerr<<"FAIL "<<label<<"\n";std::exit(1);}}
int main(){
    for(uint16_t fw:std::array<uint16_t,8>{0x163,0x166,0x167,0x211,0x217,0x219,0x308,0x411}) {
        for(Route route:{Route::XInputKeyboard,Route::DInputButtons}) {
            const auto r=reports(fw,route);const bool kb=route==Route::XInputKeyboard,legacy=fw==0x163||fw==0x211;
            check(r.valid,"exact firmware supported");
            for(unsigned key=0;key<2;++key){
                const auto& p=key?r.m2:r.m1;
                check(p[0]==0x0f&&p[1]==0&&p[2]==0&&p[3]==0x3c&&p[4]==0x21&&p[5]==1,"unshifted MSI report header");
                check(p[6]==key,"M1/M2 address selector");
                const uint8_t expected=uint8_t((key?(legacy?0x1f:0x63):(legacy?0x7a:0xba))+(kb?1:0));
                check(p[7]==expected,"HC deviceVersion bank address");
                check(p[8]==(kb?0x07:0x02)&&p[9]==(kb?0x04:0x01)&&p[10]==0,"XInput keyboard vs DInput button role");
                if(kb)check(p[11]==(key?0x7d:0x7a)&&p[12]==0xff&&p[15]==0xff,"F11/F14 only native bank");
                else check(p[11]==0&&p[12]==0&&p[15]==0,"DInput payload does not encode keyboard macro");
                for(unsigned i=kb?16:11;i<64;++i)check(p[i]==0,"zero-padding exact 64 bytes");
            }
            check(r.sync[0]==0x0f&&r.sync[3]==0x3c&&r.sync[4]==0x22,"Sync report unshifted too");
        }
    }
    for(uint16_t fw:std::array<uint16_t,8>{0,0x162,0x164,0x210,0x410,0x412,0x419,0xffff})check(!reports(fw,Route::DInputButtons).valid,"unknown firmware still refused");
    check(!reports(0x411,Route(99)).valid,"invalid role refused");
    for(unsigned bits=0;bits<256;++bits){
        NativeGateFacts f;f.knownBoard=bits&1;f.inputEnabled=bits&2;f.powerReady=bits&4;
        f.exitRequested=bits&8;f.virtualTargetEnabled=bits&16;f.managedDinputArmed=bits&32;f.returnOwed=bits&64;
        f.requestPower=f.currentPower=5;f.rearMappingRequested=bits&128;
        check(nativePreparationAllowed(f)==(bits==135),"native intent cannot bypass virtual/exit/power/family gates");
    }
    NativeGateFacts f{true,true,true,false,false,false,false,4,5,true};check(!nativePreparationAllowed(f),"stale native power token refused");
    f.requestPower=f.currentPower=0;check(!nativePreparationAllowed(f),"zero native power token refused");
    NativePreparation q;check(q.shouldQueue(true),"first native entry requests preparation");q.queueAccepted(true);
    for(unsigned i=0;i<100;++i)check(!q.shouldQueue(true),"no repeated writes per BUS tick");
    check(!q.shouldQueue(false)&&q.shouldQueue(true),"virtual or suspended boundary permits next explicit native entry");
    q.queueAccepted(false);check(q.shouldQueue(true),"rejected queue is not false success");
    const uint64_t front=(1ull<<25)|(1ull<<26),rear=(1ull<<22)|(1ull<<23),other=1ull<<28,mask=front|rear|other;
    for(bool known:{false,true})for(bool wmi:{false,true})check(keyboardConsumeMask(mask,known,wmi,front)==(wmi&&!known?rear|other:mask),"MSI-only QS keyboard suppression independent from WMI action dedup");
    std::cout<<"MSI_REAR_PROTOCOL_PURE_OK checks="<<checks<<" hardware_io=none\n";
}
