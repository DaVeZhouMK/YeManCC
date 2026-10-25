// Mapping research only. No output(), device writers, HWND creation, threads,
// keyboard/mouse injection, product startup, or configuration-file writes.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "screen_touchpads.h"
#include <iostream>
using namespace ymcc::screenpads;
struct Workload {
    const char* name; Mode mode; TouchFeedbackMode feedback; bool adapter=false;
    bool snapshotsOnly=false; int contacts=1; bool disabled=false; bool jitter=false;
    bool single=false;
};
struct Sample { double wallUs=0,cpuMs=0; unsigned long long checksum=0,pulses=0; };
static double cpuMs() {
    FILETIME creation{},exit{},kernel{},user{};
    GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user);
    return ((double)(((ULONGLONG)kernel.dwHighDateTime<<32)|kernel.dwLowDateTime)+
            (double)(((ULONGLONG)user.dwHighDateTime<<32)|user.dwLowDateTime))/10000.;
}
static unsigned long long contactHash(const Contact& c) {
    return (unsigned short)c.x+((unsigned long long)(unsigned short)c.y<<16)+
        ((unsigned long long)c.down<<32)+((unsigned long long)c.click<<33)+(unsigned)c.pressure;
}
static Sample run(const Workload& w,int count) {
    Core local;
    Core& c=w.adapter?core:local;
    c.cancel();c.config=profileDefault(0);c.config.enabled=!w.disabled;
    c.config.left=c.config.right=c.config.single=w.mode;
    c.config.layout=w.single?Layout::Single:Layout::Dual;c.config.touchFeedback=w.feedback;
    if(w.adapter) {
        enabled.store(!w.disabled);nativeMode.store(nativePad(w.mode));
        available.store(w.mode==Mode::Deck);psAvailable.store(w.mode==Mode::DualSense);
        activeProfile.store(0);appliedProfile.store(0);sizes[0]=sizes[1]={1000,1000};
    }
    for(int side=0;side<w.contacts;side++)c.event(side,side+1,0,400,400,1000,1000,1000);
    unsigned long long checksum=0,pulses=0;
    const double cpuBefore=cpuMs();const auto before=std::chrono::steady_clock::now();
    for(int i=0;i<count;i++) {
        if(!w.snapshotsOnly) {
            const int side=w.contacts==2?i&1:0;
            const float x=w.jitter?400+(i%2?.3f:-.3f):200+(i%400)*1.5f;
            const float y=w.jitter?400:200+((i/400)%400)*1.5f;
            Action a;
            if(w.adapter) {std::lock_guard<std::mutex> lock(mutex);a=c.event(side,side+1,1,x,y,1000,1000,1000+i*8ull);}
            else a=c.event(side,side+1,1,x,y,1000,1000,1000+i*8ull);
            checksum+=(unsigned)(a.dx+2000)+(unsigned)(a.dy+2000)+a.keys+a.hapticContacts+a.hapticStrengthPercent;
            pulses+=a.hapticPulses;
        }
        if(w.adapter) {const auto s=snapshot();checksum+=contactHash(s.left)+contactHash(s.right)+(unsigned)s.present;}
    }
    return {std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-before).count(),
        cpuMs()-cpuBefore,checksum,pulses};
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    const Workload workloads[]={
        {"core-mouse-feedback-off",Mode::Mouse,TouchFeedbackMode::Off},
        {"core-mouse-feedback-medium",Mode::Mouse,TouchFeedbackMode::Medium},
        {"core-mouse-feedback-strong",Mode::Mouse,TouchFeedbackMode::Strong},
        {"core-mouse-sub-noise-medium",Mode::Mouse,TouchFeedbackMode::Medium,false,false,1,false,true},
        {"core-wasd-medium",Mode::Wasd,TouchFeedbackMode::Medium},
        {"core-arrows-medium",Mode::Arrows,TouchFeedbackMode::Medium},
        {"core-deck-two-contacts",Mode::Deck,TouchFeedbackMode::Medium,false,false,2},
        {"core-ps5-two-contacts",Mode::DualSense,TouchFeedbackMode::Medium,false,false,2},
        {"adapter-idle-disabled",Mode::Off,TouchFeedbackMode::Medium,true,true,0,true},
        {"adapter-idle-mouse",Mode::Mouse,TouchFeedbackMode::Medium,true,true,0},
        {"adapter-idle-deck",Mode::Deck,TouchFeedbackMode::Medium,true,true,0},
        {"adapter-deck-one-contact-snapshot",Mode::Deck,TouchFeedbackMode::Medium,true,true,1},
        {"adapter-ps5-two-contact-snapshot",Mode::DualSense,TouchFeedbackMode::Medium,true,true,2},
        {"adapter-deck-event-and-snapshot",Mode::Deck,TouchFeedbackMode::Medium,true,false,2},
        {"adapter-ps5-event-and-snapshot",Mode::DualSense,TouchFeedbackMode::Medium,true,false,2},
        {"adapter-ps5-single-event-and-snapshot",Mode::DualSense,TouchFeedbackMode::Medium,true,false,2,false,false,true},
    };
    constexpr int count=1000000,rounds=7;
    Json rows=Json::array();bool ok=true;
    for(const auto& w:workloads) {
        run(w,10000);std::array<double,rounds> wall{},cpu{};
        unsigned long long expected=0,pulses=0;bool stable=true;
        for(int round=0;round<rounds;round++) {
            const auto r=run(w,count);wall[round]=r.wallUs;cpu[round]=r.cpuMs;
            if(!round){expected=r.checksum;pulses=r.pulses;}
            else stable=stable&&expected==r.checksum&&pulses==r.pulses;
        }
        ok=ok&&stable;std::sort(wall.begin(),wall.end());std::sort(cpu.begin(),cpu.end());
        rows.push_back({{"workload",w.name},{"iterations",count},{"rounds",rounds},
            {"medianWallUs",wall[rounds/2]},{"medianCpuMs",cpu[rounds/2]},
            {"wallUsPerIteration",wall[rounds/2]/count},{"stableAcrossRounds",stable},
            {"checksum",expected},{"pulses",pulses}});
    }
    Json result={{"ok",ok},{"scope","Core plus production snapshot/normalization and uncontended adapter mutex; NOT end-to-end YMCC CPU"},
        {"hardwareWrites",0},{"systemInputInjected",false},{"windowsCreated",0},{"productDeployed",false},
        {"notMeasured",Json::array({"Windows pointer delivery","concurrent BUS lock contention","JSON/BUS serialization","InputHost and device I/O","visible cursor/UI rendering"})},
        {"rows",rows}};
    std::ofstream output(std::filesystem::path(argv[1]),std::ios::binary);output<<result.dump(2);
    std::cout<<result.dump()<<"\n";return ok&&output.good()?0:3;
}
