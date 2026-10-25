#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "screen_touchpads.h"
#include <iostream>
using namespace ymcc::screenpads;
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;Json cases=Json::array();bool ok=true;
    auto check=[&](const char* name,bool pass){ok=ok&&pass;cases.push_back({{"name",name},{"ok",pass}});};
    check("queued-source-tick-is-preserved",pointerEventTick(100,1000)==100);
    check("32-bit-tick-wrap-is-expanded",pointerEventTick(0xfffffff0u,0x100000010ULL)==0xfffffff0ULL);
    check("zero-is-valid-at-wrap",pointerEventTick(0,0x100000064ULL)==0x100000000ULL);
    check("future-or-invalid-tick-uses-dispatch-time",pointerEventTick(500,400)==400);
    for(Mode mode:{Mode::Mouse,Mode::Deck,Mode::DualSense}){
        Core c;c.config.enabled=true;c.config.left=mode;
        c.event(0,1,0,100,100,200,200,pointerEventTick(100,1000));
        auto up=c.event(0,1,2,100,100,200,200,pointerEventTick(900,1000));
        check((std::string("delayed-long-hold-is-not-tap-")+modeName(mode)).c_str(),!up.tap&&!c.clicks[0].active);
        c.cancel();c.event(0,1,0,100,100,200,200,pointerEventTick(100,1000));
        up=c.event(0,1,2,100,100,200,200,pointerEventTick(125,1000));
        check((std::string("delayed-real-short-tap-survives-")+modeName(mode)).c_str(),mode==Mode::Mouse?up.tap:c.snapshot(1100).left.click);
    }
    std::ofstream out(std::filesystem::path(argv[1]),std::ios::binary);out<<Json{{"ok",ok},{"cases",cases},{"hardwareWrites",0},{"systemInputInjected",false}}.dump(2);
    std::cout<<"POINTER_EVENT_TIME checks="<<cases.size()<<" passed="<<ok<<"\n";return ok&&out.good()?0:3;
}
