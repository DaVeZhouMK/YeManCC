#define NOMINMAX
#include <windows.h>
static ULONGLONG fixtureTick=1000;
static unsigned systemInputCalls=0;
static ULONGLONG WINAPI fixtureClock(){return fixtureTick;}
static UINT WINAPI fixtureSendInput(UINT,LPINPUT,int){++systemInputCalls;return 0;}
static BOOL WINAPI fixtureInvalidate(HWND,const RECT*,BOOL){return TRUE;}
#define GetTickCount64 fixtureClock
#define SendInput fixtureSendInput
#define InvalidateRect fixtureInvalidate
#include "screen_touchpads.h"
#undef GetTickCount64
#undef SendInput
#undef InvalidateRect
#include "input_host_compact_frame_reply.h"
#include "production-real-pads.inc"

int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    using namespace ymcc::screenpads;
    const std::string hash="40e92f4db7776375ba59bdf4576e638c224fe53973ec73576501947c573c5089";
    const std::string binding(64,'b');
    constexpr auto proof="pair-unproven-safe-zero";
    constexpr int size=200;
    struct Point { int x,y; };
    const Point up{-1,-1};
    bool ownUi=false;
    dryRun=true;feedbackOnlyForegroundFixture=&ownUi;
    core.config=profileDefault(1);core.config.enabled=true;core.config.autoHideSeconds=0;
    core.config.touchFeedback=TouchFeedbackMode::Off;
    enabled=true;nativeMode=true;available=true;activeProfile=appliedProfile=1;
    sizes={SIZE{size,size},SIZE{size,size}};
    kInputHostProtocolConfigHash=hash.c_str();
    NativeTransportObservation transport{29,true};
    unsigned long long sequence=0;
    auto frames=json::array();
    const auto oracle=[](Point p){
        if(p.x<0)return Contact{};
        const auto scale=[](int offset){int n=offset*32767;return short(n>=0?(n+100)/200:(n-100)/200);};
        return Contact{true,false,scale(2*p.x-200),scale(200-2*p.y),0};
    };
    const auto asJson=[](const Contact& c){return json{{"active",c.down},{"click",c.click},{"x",c.x},{"y",c.y},{"pressure",c.pressure}};};
    const auto frame=[&](const char* stage,Point left,Point right,bool click=false){
        fixtureTick+=8;
        FocusFrameContent content{};
        content.touchpads=snapshot(&transport);
        auto l=oracle(left),r=oracle(right);
        if(click){l.click=true;l.pressure=32767;}
        if(content.touchpads.left!=l || content.touchpads.right!=r || content.touchpads.present!=(l.down||r.down))
            throw std::runtime_error(std::string(stage)+": native snapshot disagrees with gesture oracle");
        g_inputHostEpoch=transport.epoch;
        ++sequence;
        const auto id="frame-"+std::to_string(sequence);
        const auto typed=focusIsolationBuildTypedSubmitText(content,id,sequence,sequence,fixtureTick,hash,proof);
        const auto legacy=inputHostBuildSubmitFrameText(focusIsolationAssembleFrame(content),id,sequence,sequence,fixtureTick,hash,proof);
        if(typed!=legacy)throw std::runtime_error(std::string(stage)+": typed and legacy differ");
        auto compact=focusIsolationBuildTypedSubmitText(content,id,sequence,sequence,fixtureTick,hash,proof,true);
        compact.pop_back();
        compact+=",\"frameReplyBinding\":\""+binding+"\"}";
        frames.push_back({{"stage",stage},{"epoch",transport.epoch},{"sequence",sequence},{"tick",fixtureTick},
            {"left",asJson(l)},{"right",asJson(r)},{"legacy",legacy},{"typed",typed},{"compact",compact}});
    };
    const auto pointer=[](UINT message,int side,UINT32 id,Point p){
        handlePointer(nullptr,side,message,id,float(p.x),float(p.y),0,DWORD(fixtureTick),true);
    };
    try {
        frame("initial-neutral",up,up);
        Point l{0,200},r{200,0};
        pointer(WM_POINTERDOWN,0,1,l);frame("left-down",l,up);
        for(int n=1;n<=192;++n){
            l={n,(3*n)%201};pointer(WM_POINTERUPDATE,0,1,l);
            if(n==16)pointer(WM_POINTERDOWN,1,2,r);
            if(n>16 && n<=160){r={200-n,n};pointer(WM_POINTERUPDATE,1,2,r);}
            if(n==161)pointer(WM_POINTERUP,1,2,r);
            frame("swipe",l,n>=16&&n<=160?r:up);
            if(n%16==0)frame("hold-between-pointer-events",l,n>=16&&n<=160?r:up);
        }
        pointer(WM_POINTERUP,0,1,l);frame("swipe-release",up,up);

        l={50,150};pointer(WM_POINTERDOWN,0,3,l);frame("tap-down",l,up);
        pointer(WM_POINTERUP,0,3,l);frame("tap-click",l,up,true);
        fixtureTick+=100;frame("tap-release",up,up);

        l={30,30};pointer(WM_POINTERDOWN,0,4,l);frame("before-cancel",l,up);
        handleCaptureLoss(nullptr,0,4);frame("capture-cancel",up,up);
        pointer(WM_POINTERUPDATE,0,4,{80,80});frame("canceled-update",up,up);

        pointer(WM_POINTERDOWN,0,5,l);frame("before-rejection",l,up);
        // Same native withdrawal call as the rejected SUBMIT_FRAME branch.
        observeNativeTransport(transport.epoch,false,NativeTransportReason::FrameNotAccepted);
        frame("ready-again-after-rejection",up,up);
        pointer(WM_POINTERUPDATE,0,5,{80,80});frame("rejected-update",up,up);
        pointer(WM_POINTERUP,0,5,{80,80});frame("rejected-up",up,up);

        pointer(WM_POINTERDOWN,0,6,l);pointer(WM_POINTERDOWN,1,7,r);frame("before-epoch-change",l,r);
        ++transport.epoch;frame("replacement-neutral",up,up);
        pointer(WM_POINTERUPDATE,0,6,{90,80});pointer(WM_POINTERUPDATE,1,7,{20,20});frame("replacement-old-updates",up,up);
        pointer(WM_POINTERUP,0,6,{90,80});pointer(WM_POINTERUP,1,7,{20,20});frame("replacement-old-releases",up,up);
        r={200,0};pointer(WM_POINTERDOWN,1,8,r);frame("replacement-fresh-down",up,r);
        r={150,75};pointer(WM_POINTERUPDATE,1,8,r);frame("replacement-fresh-move",up,r);
        pointer(WM_POINTERUP,1,8,r);frame("final-neutral",up,up);
        if(systemInputCalls||injectedCount||cursorQueries||nativeTransport.discardedGestures!=3)
            throw std::runtime_error("unexpected input, cursor, or discarded-gesture count");
        std::ofstream out(std::filesystem::path(argv[1]),std::ios::binary);
        out<<json{{"ok",true},{"systemInputCalls",systemInputCalls},{"flow",state(true).at("nativePadFlow")},{"frames",frames}}.dump()<<'\n';
        return out.good()?0:3;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
