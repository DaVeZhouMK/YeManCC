// Pure production Core benchmark. No HWND, injected input, device, output
// callback, timer-resolution change or installed-product startup.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "screen_touchpads.h"
#include <iostream>
#include <numeric>
using namespace ymcc::screenpads;
static double cpuMs(){FILETIME c{},e{},k{},u{};GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);return ((double)((ULONGLONG)k.dwHighDateTime<<32|k.dwLowDateTime)+(double)((ULONGLONG)u.dwHighDateTime<<32|u.dwLowDateTime))/10000.0;}
struct Sample{double wallUs=0,cpu=0;unsigned long long pulses=0,checksum=0;};
static Sample sample(int kind,int count){
    Core core;core.config=profileDefault(0);core.config.enabled=true;core.config.left=core.config.right=Mode::Mouse;
    core.config.touchFeedback=TouchFeedbackMode::Minimal;
    core.event(0,1,0,400,400,1000,1000,0);
    unsigned long long pulses=0,checksum=0;const double beforeCpu=cpuMs();const auto before=std::chrono::steady_clock::now();
    for(int i=0;i<count;++i){float x=400,y=400;
        if(kind==1)x+=i%2?.3f:-.3f;
        if(kind==2)x=200+(i%400)*.25f;
        if(kind==3){x=200+(i%80)*5.f;y=400+((i/80)%2)*5.f;}
        if(kind==4)x=y=200+(i%100)*1.f;
        const auto action=core.event(0,1,1,x,y,1000,1000,100+i*4ull);
        pulses+=action.hapticPulses;checksum+=(unsigned long long)(action.dx+2000)+(unsigned long long)(action.dy+2000)+action.hapticContacts;
    }
    return {std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-before).count(),cpuMs()-beforeCpu,pulses,checksum};
}
int wmain(int argc,wchar_t** argv){if(argc!=2)return 2;Json rows=Json::array();constexpr int count=1000000;
    const char* names[]={"stationary","sub-jitter","slow-axis","rapid-axis","diagonal"};
    for(int kind=0;kind<5;++kind){sample(kind,10000);std::array<double,5> wall{},cpu{};unsigned long long checksum=0,pulses=0;
        for(int round=0;round<5;++round){const auto result=sample(kind,count);wall[round]=result.wallUs;cpu[round]=result.cpu;checksum+=result.checksum;pulses=result.pulses;}
        std::sort(wall.begin(),wall.end());std::sort(cpu.begin(),cpu.end());
        rows.push_back({{"workload",names[kind]},{"events",count},{"rounds",5},{"medianWallUs",wall[2]},{"medianCpuMs",cpu[2]},{"pulses",pulses},{"checksum",checksum}});
    }
    Json result={{"ok",true},{"scope","pure production Core only; not end-to-end YMCC CPU"},{"hardwareWrites",0},{"systemInputInjected",false},{"rows",rows}};
    std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<result.dump(2);std::cout<<result.dump()<<"\n";return file.good()?0:3;
}
