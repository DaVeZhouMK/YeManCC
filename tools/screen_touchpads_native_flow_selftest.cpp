#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
static ULONGLONG fixtureTick=1000;
static unsigned clockCalls=0,inputCalls=0;
static ULONGLONG WINAPI fixtureClock(){++clockCalls;return fixtureTick;}
static UINT WINAPI fixtureSendInput(UINT,LPINPUT,int){++inputCalls;return 0;}
static BOOL WINAPI fixtureInvalidate(HWND,const RECT*,BOOL){return TRUE;}
#define GetTickCount64 fixtureClock
#define SendInput fixtureSendInput
#define InvalidateRect fixtureInvalidate
#include "screen_touchpads.h"
#undef GetTickCount64
#undef SendInput
#undef InvalidateRect
#include <iostream>

int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    using namespace ymcc::screenpads;
    Json cases=Json::array();bool ok=true,ownUi=false;
    const auto check=[&](const char* name,bool pass){ok=ok&&pass;cases.push_back({{"name",name},{"ok",pass}});};
    const auto readFlow=[](){return state(true).at("nativePadFlow");};
    const auto pointer=[](UINT message,UINT32 id,float x,DWORD flags=0,DWORD sourceTick=0){
        handlePointer(nullptr,0,message,id,x,30,flags,sourceTick,sourceTick!=0);
    };
    dryRun=true;feedbackOnlyForegroundFixture=&ownUi;
    core.config=profileDefault(1);core.config.enabled=true;core.config.autoHideSeconds=0;
    enabled=true;nativeMode=true;available=true;activeProfile=appliedProfile=1;
    sizes={SIZE{200,200},SIZE{200,200}};
    using Reason=NativeTransportReason;
    const NativeTransportObservation blocked{1,false},ready{1,true},replaced{2,true,Reason::SnapshotAdmission};
    const auto withdrawal=[&](){return readFlow().at("lastTransportWithdrawal");};
    check("regular settings state does not include export counters",!state().contains("nativePadFlow"));
    check("blocked target cannot expose a contact",!snapshot(&blocked).present);
    check("idle transport changes have no withdrawal evidence",withdrawal().is_null());
    pointer(WM_POINTERDOWN,1,30);
    pointer(WM_POINTERDOWN,1,30);
    pointer(WM_POINTERDOWN,7,30,POINTER_FLAG_CANCELED);
    auto flow=readFlow();
    check("blocked DOWN counted once, not duplicate or canceled DOWN",flow.at("blockedFeedbackDowns")==1&&flow.at("admittedDowns")==0&&feedbackOnlyContacts.mask()==1);
    fixtureTick=1020;
    check("ready target cannot replay the blocked DOWN",!snapshot(&ready).present);
    pointer(WM_POINTERUPDATE,1,80);
    pointer(WM_POINTERUP,1,80);
    check("blocked gesture remains neutral through UP",!snapshot(&ready).present&&feedbackOnlyContacts.mask()==0);

    pointer(WM_POINTERDOWN,2,30,0,1019);
    flow=readFlow();
    check("pre-ready queued DOWN observed without admission",flow.at("predatesReadyDowns")==1&&!core.pads[0].down);
    ownUi=true;pointer(WM_POINTERDOWN,3,30);pointer(WM_POINTERUP,3,30);ownUi=false;
    flow=readFlow();
    check("foreground feedback is not a transport denial",flow.at("foregroundFeedbackDowns")==1&&flow.at("blockedFeedbackDowns")==1);

    fixtureTick=1031;pointer(WM_POINTERDOWN,4,30);
    pointer(WM_POINTERDOWN,4,30);
    check("fresh DOWN is admitted once",readFlow().at("admittedDowns")==1&&core.pads[0].down);
    check("local reads are not BUS frame snapshots",snapshot().left.down&&readFlow().at("frameSnapshotsWithContacts")==0);
    clockCalls=0;
    check("native frame snapshot exposes a held touch without clock",snapshot(&ready).left.down&&clockCalls==0);
    flow=readFlow();
    check("active frame sample carries the observed epoch",flow.at("frameSnapshotsWithContacts")==1&&flow.at("lastContactSnapshotEpoch")==1);

    fixtureTick=1100;
    clockCalls=0;
    check("replacing a target drops its held touch",!snapshot(&replaced).present&&nativeTransport.discardedGestures==1);
    check("replacement reuses its existing ready clock",clockCalls==1);
    const auto replacement=withdrawal();
    check("replacement records both epochs and held pad",replacement==Json({{"tick",1100},{"fromEpoch",1},{"toEpoch",2},
        {"fromReady",true},{"toReady",true},{"heldMask",1},{"tapMask",0},{"reason","snapshot-admission"}}));
    flow=readFlow();
    check("empty newer epoch does not relabel the last touch",flow.at("lastContactSnapshotEpoch")==1&&flow.at("frameSnapshotsWithContacts")==1);
    pointer(WM_POINTERUP,4,70);
    pointer(WM_POINTERDOWN,5,40);
    check("fresh DOWN works on the replacement",snapshot(&replaced).left.down&&readFlow().at("lastContactSnapshotEpoch")==2);
    pointer(WM_POINTERUPDATE,5,100);
    pointer(WM_POINTERUP,5,100);
    clockCalls=0;bool neutral=true;
    for(int i=0;i<10000;++i)neutral=neutral&&!snapshot(&replaced).present;
    flow=readFlow();
    check("empty snapshots do not add clocks or touch counts",neutral&&clockCalls==0&&flow.at("frameSnapshotsWithContacts")==2);
    check("ordinary UP and idle snapshots preserve the last withdrawal",withdrawal()==replacement);

    fixtureTick=1200;pointer(WM_POINTERDOWN,10,30);
    const NativeTransportObservation uiNeutral{2,false,Reason::SemanticNeutral};
    clockCalls=0;
    check("withdrawing a live swipe samples the clock once",!snapshot(&uiNeutral).present&&clockCalls==1);
    observeNativeTransport(2,false,Reason::FrameNotAccepted);
    const auto neutralWithdrawal=withdrawal();
    check("duplicate closure cannot overwrite a semantic-neutral withdrawal",neutralWithdrawal.at("reason")=="semantic-neutral"&&
        neutralWithdrawal.at("tick")==1200&&neutralWithdrawal.at("heldMask")==1&&neutralWithdrawal.at("toReady")==false);
    fixtureTick=1210;snapshot(&replaced);pointer(WM_POINTERUP,10,80);pointer(WM_POINTERDOWN,11,30);
    fixtureTick=1220;observeNativeTransport(2,false,Reason::PipeFault);
    const auto fault=withdrawal();
    check("a pipe fault after fresh DOWN is distinct from semantic neutral",fault.at("reason")=="pipe-fault"&&fault.at("tick")==1220&&fault.at("fromEpoch")==2);
    fixtureTick=1230;snapshot(&replaced);pointer(WM_POINTERUP,11,80);pointer(WM_POINTERDOWN,12,30);
    fixtureTick=1240;pointer(WM_POINTERUP,12,30);
    observeNativeTransport(2,false,Reason::FrameNotAccepted);
    const auto tap=withdrawal();
    check("an interrupted tap is not counted as a discarded swipe",tap.at("reason")=="frame-not-accepted"&&tap.at("heldMask")==0&&
        tap.at("tapMask")==1&&nativeTransport.discardedGestures==3);

    Core lateBlock;lateBlock.config=core.config;lateBlock.nativeOutputBlocked=true;
    auto outcome=NativeDownResult::Other;
    lateBlock.event(0,6,0,30,30,200,200,1101,1101,&outcome);
    check("final core guard classifies a late block",outcome==NativeDownResult::TransportBlocked&&!lateBlock.pads[0].down);
    lateBlock.nativeOutputBlocked=false;lateBlock.config.left=Mode::Wasd;
    outcome=NativeDownResult::Other;lateBlock.event(0,6,0,30,30,200,200,1102,1102,&outcome);
    check("ordinary keyboard DOWN is not native admission",outcome==NativeDownResult::Other&&lateBlock.pads[0].down);
    check("no system input calls or cursor compensation",inputCalls==0&&injectedCount==0&&cursorQueries==0);
    feedbackOnlyForegroundFixture=nullptr;
    const Json result={{"ok",ok},{"cases",cases},{"nativePadFlow",readFlow()},{"systemInputCalls",inputCalls}};
    std::ofstream out(std::filesystem::path(argv[1]),std::ios::binary);out<<result.dump(2)<<'\n';
    std::cout<<"native pad flow: "<<cases.size()<<" checks; ok="<<ok<<'\n';
    return ok&&out.good()?0:3;
}
