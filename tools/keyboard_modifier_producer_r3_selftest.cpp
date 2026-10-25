// Source-bound R3 caller/reference comparison. Every input/UI API is synthetic.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
#include "keyboard_modifier_event_evidence.h"
#include "json.hpp"
using json=nlohmann::json;
// Origin cache integration is covered by keyboard_device_origin_export_selftest.
static void keyboardDeviceOriginObserveAtDrain(const ymcc::keyboardEvents::Event&,const wchar_t*,std::size_t) noexcept {}

namespace ke=ymcc::keyboardEvents;
static ke::Recorder g_keyboardModifierEvents;
static std::atomic<bool> g_keyboardModifierEventGate{false};
static std::atomic<std::uint64_t> g_keyboardModifierDeviceGeneration{9};
static std::atomic<bool> g_keyboardModifierHookInstalled{false},g_keyboardModifierRawSubscribed{false};
static std::atomic<ULONGLONG> g_keyboardModifierHookObservedTick{0};
static std::atomic<std::uint64_t> g_keyboardModifierLogFailures{0};
static std::atomic<bool> g_oemHooksWanted{false},g_oemRawPathActive{false};
static HWND g_hwnd=nullptr;
static bool g_webviewReady=false;
// Synthetic COM surface: only the method used by the actual caller is provided.
inline constexpr int COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC=0;
namespace fake {
static HWND window(std::uintptr_t n) noexcept{return reinterpret_cast<HWND>(n);}
struct Plan {
    bool gate=true,hwndPresent=true,valid=true,ready=true,iconic=false,visible=true,controller=true,allowAlt=true;
    UINT inserted=4;DWORD sendError=8765;HRESULT moveResult=S_OK;
    std::vector<HWND> foreground{window(2),window(1)};
    HWND mainRoot=window(10),otherRoot=window(20);
};
static Plan plan{};
static DWORD lastError=13579;
static json trace=json::array(),rows=json::array();
static std::vector<std::vector<INPUT>> sent;
static std::size_t foregroundIndex=0;
static unsigned observationClock=0,observationThread=0,observationPower=0,errorGets=0,errorSets=0,rawCalls=0,conversionCalls=0;
static void ui(const char* name,HWND hwnd=nullptr,int value=0){trace.push_back({{"api",name},{"hwnd",reinterpret_cast<std::uintptr_t>(hwnd)},{"value",value},{"lastErrorAtEntry",lastError}});lastError=4321;}
static DWORD GetLastError() noexcept{++errorGets;return lastError;}
static void SetLastError(DWORD value) noexcept{++errorSets;lastError=value;}
static ULONGLONG GetTickCount64() noexcept{++observationClock;lastError=7101;return 120;}
static DWORD GetCurrentThreadId() noexcept{++observationThread;lastError=7102;return 77;}
static BOOL IsWindow(HWND hwnd){ui("IsWindow",hwnd);return plan.valid?TRUE:FALSE;}
static BOOL IsIconic(HWND hwnd){ui("IsIconic",hwnd);return plan.iconic?TRUE:FALSE;}
static BOOL IsWindowVisible(HWND hwnd){ui("IsWindowVisible",hwnd);return plan.visible?TRUE:FALSE;}
static BOOL ShowWindow(HWND hwnd,int command){ui("ShowWindow",hwnd,command);return FALSE;}
static BOOL BringWindowToTop(HWND hwnd){ui("BringWindowToTop",hwnd);return TRUE;}
static BOOL SetForegroundWindow(HWND hwnd){ui("SetForegroundWindow",hwnd);return TRUE;}
static HWND SetActiveWindow(HWND hwnd){ui("SetActiveWindow",hwnd);return window(99);}
static HWND SetFocus(HWND hwnd){ui("SetFocus",hwnd);return window(99);}
static HWND GetForegroundWindow(){ui("GetForegroundWindow");if(foregroundIndex>=plan.foreground.size())throw std::runtime_error("unplanned foreground observation");return plan.foreground[foregroundIndex++];}
static HWND GetAncestor(HWND hwnd,UINT flags){ui("GetAncestor",hwnd,static_cast<int>(flags));if(flags!=GA_ROOTOWNER)throw std::runtime_error("unexpected ancestor flag");return hwnd==window(1)?plan.mainRoot:plan.otherRoot;}
static UINT SendInput(UINT count,LPINPUT inputs,int inputSize){
    if(!inputs||inputSize!=static_cast<int>(sizeof(INPUT))||!sent.empty())throw std::runtime_error("unplanned SendInput count/size/call");
    trace.push_back({{"api","SendInput"},{"requested",count},{"inputSize",inputSize}});sent.emplace_back(inputs,inputs+count);lastError=plan.sendError;return plan.inserted;
}
static UINT GetRawInputDeviceInfoW(HANDLE,UINT,LPVOID,PUINT){++rawCalls;throw std::runtime_error("no raw device query permitted in producer scenarios");}
}
struct FakeController{HRESULT MoveFocus(int reason){fake::ui("MoveFocus",nullptr,reason);return fake::plan.moveResult;}};
static FakeController controller;
static FakeController* g_ctrl=nullptr;
static std::uint64_t currentPowerGeneration() noexcept{++fake::observationPower;fake::lastError=7103;return 33;}
static void hideWindowAnimated(HWND hwnd){fake::ui("hideWindowAnimated",hwnd);}
static std::string W2U(const wchar_t*,int){++fake::conversionCalls;throw std::runtime_error("no device-name conversion permitted in producer scenarios");}
static void appendDomainLog(const char* domain,const char* name,const json& row){
    if(std::string(domain)!="virtual"||std::string(name)!="keyboard-modifier-events")throw std::runtime_error("unexpected log target");fake::rows.push_back(row);
}
#define GetLastError fake::GetLastError
#define SetLastError fake::SetLastError
#define GetTickCount64 fake::GetTickCount64
#define GetCurrentThreadId fake::GetCurrentThreadId
#define IsWindow fake::IsWindow
#define IsIconic fake::IsIconic
#define IsWindowVisible fake::IsWindowVisible
#define ShowWindow fake::ShowWindow
#define BringWindowToTop fake::BringWindowToTop
#define SetForegroundWindow fake::SetForegroundWindow
#define SetActiveWindow fake::SetActiveWindow
#define SetFocus fake::SetFocus
#define GetForegroundWindow fake::GetForegroundWindow
#define GetAncestor fake::GetAncestor
#define SendInput fake::SendInput
#define GetRawInputDeviceInfoW fake::GetRawInputDeviceInfoW
#include "actual_producer_fragments.inc"
namespace original {
#include "original_producer_fragments.inc"
}
#undef GetLastError
#undef SetLastError
#undef GetTickCount64
#undef GetCurrentThreadId
#undef IsWindow
#undef IsIconic
#undef IsWindowVisible
#undef ShowWindow
#undef BringWindowToTop
#undef SetForegroundWindow
#undef SetActiveWindow
#undef SetFocus
#undef GetForegroundWindow
#undef GetAncestor
#undef SendInput
#undef GetRawInputDeviceInfoW

static json scenarios=json::array();
static unsigned checks=0,failures=0,referenceRuns=0,candidateRuns=0;
static std::vector<std::string> failureMessages;
static void check(bool condition,const char* description){++checks;if(!condition){++failures;failureMessages.emplace_back(description);}}
static void scenario(const std::string& name,const std::function<void()>& body){
    const auto c=checks,f=failures;failureMessages.clear();
    try{body();}catch(const std::exception& e){++failures;failureMessages.emplace_back(std::string("unexpected exception: ")+e.what());}catch(...){++failures;failureMessages.emplace_back("unexpected nonstandard exception");}
    scenarios.push_back({{"name",name},{"checks",checks-c},{"failed",failures-f},{"failures",failureMessages}});
}
static void reset(const fake::Plan& plan){
    g_keyboardModifierEventGate=false;g_keyboardModifierEvents.enable(false,100);g_keyboardModifierEvents.enable(true,100);(void)g_keyboardModifierEvents.take(100);
    if(!plan.gate)g_keyboardModifierEvents.enable(false,100);g_keyboardModifierEventGate=plan.gate;
    g_keyboardModifierDeviceGeneration=9;g_keyboardModifierHookInstalled=false;g_keyboardModifierRawSubscribed=false;g_keyboardModifierHookObservedTick=0;g_oemHooksWanted=false;g_oemRawPathActive=false;g_keyboardModifierLogFailures=0;
    fake::plan=plan;g_hwnd=plan.hwndPresent?fake::window(1):nullptr;g_webviewReady=plan.ready;g_ctrl=plan.controller?&controller:nullptr;
    fake::lastError=13579;fake::trace=json::array();fake::rows=json::array();fake::sent.clear();fake::foregroundIndex=0;
    fake::observationClock=0;fake::observationThread=0;fake::observationPower=0;fake::errorGets=0;fake::errorSets=0;fake::rawCalls=0;fake::conversionCalls=0;
}
static json inputArrays(){
    auto arrays=json::array();for(const auto& call:fake::sent){auto batch=json::array();for(const auto& i:call)batch.push_back({{"type",i.type},{"vk",i.ki.wVk},{"scan",i.ki.wScan},{"flags",i.ki.dwFlags},{"time",i.ki.time},{"extra",i.ki.dwExtraInfo}});arrays.push_back(batch);}return arrays;
}
static unsigned countApi(const json& trace,const char* name){unsigned count=0;for(const auto& i:trace)if(i.at("api")==name)++count;return count;}
enum class Producer{Focus,Desktop,TaskView};
struct Outcome{json trace,inputs,events,rows;bool result=false,threw=false;std::string exception;DWORD error=0;unsigned clock=0,thread=0,power=0,errorGets=0,errorSets=0,raw=0,conversion=0;};
static Outcome run(Producer producer,const fake::Plan& plan,bool before){
    reset(plan);if(before)++referenceRuns;else ++candidateRuns;Outcome out;
    try{
        switch(producer){
        case Producer::Focus:out.result=before?original::refocusWebView(plan.allowAlt):refocusWebView(plan.allowAlt);break;
        case Producer::Desktop:if(before)original::returnToDesktop();else returnToDesktop();break;
        case Producer::TaskView:if(before)original::nativeSendTaskView();else nativeSendTaskView();break;
        }
    }catch(const std::runtime_error& e){out.threw=true;out.exception=e.what();}
    out.trace=fake::trace;out.inputs=inputArrays();out.error=fake::lastError;out.clock=fake::observationClock;out.thread=fake::observationThread;out.power=fake::observationPower;out.errorGets=fake::errorGets;out.errorSets=fake::errorSets;
    if(!before)keyboardModifierFlushEvidence(json::object(),200);
    out.rows=fake::rows;out.events=json::array();for(const auto& row:out.rows)for(const auto& e:row.at("events"))out.events.push_back(e);
    out.raw=fake::rawCalls;out.conversion=fake::conversionCalls;return out;
}
static void sameControl(const Outcome& before,const Outcome& after){
    check(before.trace==after.trace,"actual source-before/after UI/input call order and arguments equal");
    check(before.inputs==after.inputs,"all actual keyboard fields and array order equal to source-before");
    check(before.result==after.result&&before.threw==after.threw&&before.exception==after.exception,"original return and exception behavior preserved");
    check(before.error==after.error,"caller LastError preserved against actual reference");
    check(before.clock==0&&before.thread==0&&before.power==0,"source-before contains no diagnostics");
    check(after.raw==0&&after.conversion==0,"no raw query or name conversion in self producer scenarios");
}
static void exactBatch(const Outcome& out,const std::vector<WORD>& keys,const std::vector<DWORD>& flags){
    check(out.inputs.size()==1,"exactly one original SendInput batch, no extra release");if(out.inputs.empty())return;
    const auto& b=out.inputs.at(0);check(b.size()==keys.size(),"original batch size");
    for(std::size_t n=0;n<keys.size();++n){const auto& i=b.at(n);check(i.at("type")==INPUT_KEYBOARD&&i.at("vk")==keys[n]&&i.at("flags")==flags[n],"exact original input type/key/down-up order");check(i.at("scan")==0&&i.at("time")==0&&i.at("extra")==0,"all other original keyboard fields zero");}
}
static void receipt(const Outcome& out,const fake::Plan& plan,ke::SourceId source,UINT requested,WORD modifier){
    if(!plan.gate){check(out.events.empty()&&out.clock==0&&out.thread==0&&out.power==0&&out.errorSets==0,"gate off records nothing and calls no diagnostic observation API");return;}
    check(out.events.size()==1&&out.clock==1&&out.thread==1&&out.power==1,"one receipt for one existing SendInput return");if(out.events.empty())return;
    const auto& e=out.events.at(0);const auto mask=ke::bit(modifier);
    check(e.at("kind")=="YMCC-SendInput-call"&&e.at("source")==ke::sourceName(source),"actual serializer sourceName and kind");
    check(e.at("requestedInputs")==requested&&e.at("insertedInputs")==plan.inserted,"original requested and inserted counts");
    check(e.at("fullBatchInserted")==(plan.inserted==requested)&&e.at("win32ErrorHint")==((plan.inserted==requested)?0:plan.sendError),"count gate and nondefinitive error hint");
    check(e.at("attemptedModifierDownMask")==mask&&e.at("attemptedModifierUpMask")==mask,"modifier-only request masks");
    check(e.at("countBasedPrefixModifierDownMask")==(plan.inserted?mask:0)&&e.at("countBasedPrefixModifierUpMask")==((plan.inserted>=requested)?mask:0),"0/partial/full count-based modifier prefix");
    check(e.at("ordinaryKeysRetained")==false&&!e.contains("virtualKey")&&!e.contains("scanCode")&&!e.contains("message")&&!e.contains("extraInfo"),"SelfEvent row excludes ordinary typed key data");
    check(e.at("errorHintIsDefinitive")==false&&e.at("prefixInterpretation")=="Count-based prefix assumption, not a per-key or consumer receipt","no UIPI or key-consumer receipt claim");
    check(out.rows.at(0).at("completeInputCoverage")==false&&out.rows.at(0).at("emptyIsAbsenceProof")==false,"no complete coverage/absence proof");
}
static void producerMatrix(){
    scenario("default-off-before-fixture-reset",[]{check(!g_keyboardModifierEventGate.load()&&!g_keyboardModifierEvents.take(1).enabled,"actual recorder and synthetic integration gate start off");});
    for(const bool gate:std::array<bool,2>{false,true}){
        for(UINT inserted=0;inserted<=2;++inserted){
            const std::string name="focus-alt-gate-"+std::string(gate?"on":"off")+"-inserted-"+std::to_string(inserted);
            scenario(name,[=]{fake::Plan plan;plan.gate=gate;plan.inserted=inserted;
                const auto before=run(Producer::Focus,plan,true),after=run(Producer::Focus,plan,false);sameControl(before,after);
                exactBatch(after,{VK_MENU,VK_MENU},{0,KEYEVENTF_KEYUP});receipt(after,plan,ke::SourceId::FocusAltFallback,2,VK_MENU);
                check(after.result&&!after.threw&&countApi(after.trace,"SetForegroundWindow")==2&&countApi(after.trace,"MoveFocus")==1,"fallback always second attempt; successful focus moves WebView once independently of inserted count");
                check(countApi(after.trace,"ShowWindow")==0,"visible noniconic window has no restore");
            });
        }
        for(UINT inserted=0;inserted<=4;++inserted){
            scenario("desktop-gate-"+std::string(gate?"on":"off")+"-inserted-"+std::to_string(inserted),[=]{fake::Plan plan;plan.gate=gate;plan.inserted=inserted;
                const auto before=run(Producer::Desktop,plan,true),after=run(Producer::Desktop,plan,false);sameControl(before,after);
                exactBatch(after,{VK_LWIN,'D','D',VK_LWIN},{0,0,KEYEVENTF_KEYUP,KEYEVENTF_KEYUP});receipt(after,plan,ke::SourceId::ReturnToDesktop,4,VK_LWIN);
                check(!after.threw&&countApi(after.trace,"hideWindowAnimated")==1,"existing valid-window desktop hide retained regardless of count");
                check(after.trace.at(1).at("api")=="hideWindowAnimated"&&after.trace.at(2).at("api")=="SendInput","original hide precedes original WinD batch");
                check(after.error==plan.sendError,"WinD SendInput LastError survives observer");
            });
            scenario("taskview-gate-"+std::string(gate?"on":"off")+"-inserted-"+std::to_string(inserted),[=]{fake::Plan plan;plan.gate=gate;plan.inserted=inserted;
                const auto before=run(Producer::TaskView,plan,true),after=run(Producer::TaskView,plan,false);sameControl(before,after);
                exactBatch(after,{VK_LWIN,VK_TAB,VK_TAB,VK_LWIN},{0,0,KEYEVENTF_KEYUP,KEYEVENTF_KEYUP});receipt(after,plan,ke::SourceId::TaskView,4,VK_LWIN);
                check(after.threw==(inserted!=4),"TaskView original exception gate follows original inserted count");
                check(after.exception==(inserted==4?std::string():"failed to send Win+Tab (Win32 "+std::to_string(plan.sendError)+")"),"exact original TaskView throw message");
                check(after.error==plan.sendError&&countApi(after.trace,"hideWindowAnimated")==0&&countApi(after.trace,"SetForegroundWindow")==0,"TaskView preserves LastError and has no new UI side effect");
            });
        }
    }
}
static void focusBoundary(const char* name,const std::function<void(fake::Plan&)>& setup,bool expectedFocus,unsigned attempts,unsigned sends,unsigned move,unsigned restores){
    scenario(name,[=]{for(const bool gate:std::array<bool,2>{false,true}){
        fake::Plan plan;plan.gate=gate;plan.inserted=1;setup(plan);
        const auto before=run(Producer::Focus,plan,true),after=run(Producer::Focus,plan,false);sameControl(before,after);
        check(after.result==expectedFocus&&!after.threw,"focus boundary result equals expected original outcome");
        check(countApi(after.trace,"SetForegroundWindow")==attempts&&countApi(after.trace,"SendInput")==sends&&countApi(after.trace,"MoveFocus")==move&&countApi(after.trace,"ShowWindow")==restores,"exact original focus attempt/send/MoveFocus/restore counts");
        if(sends)receipt(after,plan,ke::SourceId::FocusAltFallback,2,VK_MENU);
        else check(after.events.empty()&&after.clock==0&&after.thread==0&&after.power==0,"no fallback means no receipt or observation APIs");
        if(restores)for(const auto& entry:after.trace)if(entry.at("api")=="ShowWindow")check(entry.at("value")==SW_RESTORE,"restore uses existing SW_RESTORE argument");
    }});
}
static void boundaryTests(){
    focusBoundary("focus-null-hwnd-gate",[](fake::Plan& p){p.hwndPresent=false;},false,0,0,0,0);
    focusBoundary("focus-invalid-window-gate",[](fake::Plan& p){p.valid=false;},false,0,0,0,0);
    focusBoundary("focus-webview-not-ready-gate",[](fake::Plan& p){p.ready=false;},false,0,0,0,0);
    focusBoundary("focus-first-attempt-success-no-Alt",[](fake::Plan& p){p.foreground={fake::window(1)};},true,1,0,1,0);
    focusBoundary("focus-Alt-disallowed-first-attempt-fails",[](fake::Plan& p){p.allowAlt=false;p.foreground={fake::window(2)};},false,1,0,0,0);
    focusBoundary("focus-second-attempt-fails",[](fake::Plan& p){p.foreground={fake::window(2),fake::window(2)};},false,2,1,0,0);
    focusBoundary("focus-null-foreground-then-success",[](fake::Plan& p){p.foreground={nullptr,fake::window(1)};},true,2,1,1,0);
    focusBoundary("focus-controller-absent",[](fake::Plan& p){p.controller=false;},true,2,1,0,0);
    focusBoundary("focus-MoveFocus-E_FAIL-return-preserved",[](fake::Plan& p){p.moveResult=E_FAIL;},true,2,1,1,0);
    focusBoundary("focus-iconic-restores-before-first-attempt",[](fake::Plan& p){p.iconic=true;p.foreground={fake::window(1)};},true,1,0,1,1);
    focusBoundary("focus-invisible-restores-before-first-attempt",[](fake::Plan& p){p.visible=false;p.foreground={fake::window(1)};},true,1,0,1,1);
    focusBoundary("focus-shared-root-owner-recognized",[](fake::Plan& p){p.otherRoot=p.mainRoot;p.foreground={fake::window(2)};},true,1,0,1,0);
    focusBoundary("focus-no-root-owner-match-still-tries-Alt",[](fake::Plan& p){p.mainRoot=nullptr;p.otherRoot=nullptr;},true,2,1,1,0);
    for(const bool hwnd:std::array<bool,2>{false,true})scenario(hwnd?"desktop-invalid-window-skips-hide":"desktop-null-window-skips-hide",[=]{
        for(const bool gate:std::array<bool,2>{false,true}){fake::Plan p;p.gate=gate;p.hwndPresent=hwnd;p.valid=false;p.inserted=2;
            const auto before=run(Producer::Desktop,p,true),after=run(Producer::Desktop,p,false);sameControl(before,after);
            check(countApi(after.trace,"hideWindowAnimated")==0&&countApi(after.trace,"SendInput")==1,"invalid/null HWND skips hide but still sends original WinD");receipt(after,p,ke::SourceId::ReturnToDesktop,4,VK_LWIN);
        }
    });
    scenario("TaskView-zero-LastError-does-not-become-UIPI-proof",[]{fake::Plan p;p.inserted=0;p.sendError=0;const auto before=run(Producer::TaskView,p,true),after=run(Producer::TaskView,p,false);sameControl(before,after);
        check(after.exception=="failed to send Win+Tab (Win32 0)"&&after.events.at(0).at("win32ErrorHint")==0&&after.events.at(0).at("errorHintIsDefinitive")==false,"original zero-error throw retained; receipt does not invent error attribution");
    });
}
static INPUT key(WORD vk,bool up=false){INPUT i{};i.type=INPUT_KEYBOARD;i.ki.wVk=vk;i.ki.wScan=0xdead;i.ki.time=0xbeef;i.ki.dwExtraInfo=0xfeed;i.ki.dwFlags=up?KEYEVENTF_KEYUP:0;return i;}
static void mappingPrivacyTests(){
    scenario("sourceName-seven-IDs-numeric-compatibility-and-unknown",[]{
        check(static_cast<unsigned>(ke::SourceId::Other)==static_cast<unsigned>(original::SourceId::Other)&&static_cast<unsigned>(ke::SourceId::NativeKeyboardShortcut)==static_cast<unsigned>(original::SourceId::NativeKeyboardShortcut)&&static_cast<unsigned>(ke::SourceId::OemKeyboardReplay)==static_cast<unsigned>(original::SourceId::OemKeyboardReplay)&&static_cast<unsigned>(ke::SourceId::KeyboardGamebar)==static_cast<unsigned>(original::SourceId::KeyboardGamebar),"0..3 numeric values match actual source-before enum");
        const std::array<const char*,7> labels{"other-self-send","nativeKeyboardShortcutSend","oemKeyboardConsumeReplay","nativeFrontendButtonSendGamebar","refocusWebView-alt-fallback","returnToDesktop","nativeSendTaskView"};
        for(unsigned id=0;id<labels.size();++id)check(std::string(ke::sourceName(static_cast<ke::SourceId>(id)))==labels[id],"pure mapping for every SourceId");
        check(static_cast<unsigned>(ke::SourceId::FocusAltFallback)==4&&static_cast<unsigned>(ke::SourceId::ReturnToDesktop)==5&&static_cast<unsigned>(ke::SourceId::TaskView)==6,"new source IDs appended at 4..6");
        check(std::string(ke::sourceName(static_cast<ke::SourceId>(7)))=="other-self-send"&&std::string(ke::sourceName(static_cast<ke::SourceId>(255)))=="other-self-send","unknown source IDs have explicit fallback");
        fake::Plan p;reset(p);fake::lastError=9900;for(unsigned id=0;id<7;++id)(void)ke::sourceName(static_cast<ke::SourceId>(id));check(fake::lastError==9900&&fake::trace.empty()&&fake::observationClock==0,"sourceName is pure without UI/API/error side effects");
    });
    scenario("actual-serializer-sourceName-all-seven-and-unknown-private-rows",[]{
        fake::Plan p;reset(p);const std::array<INPUT,4> inputs{key(VK_LSHIFT),key('A'),key('A',true),key(VK_LSHIFT,true)};
        for(unsigned id=0;id<7;++id)keyboardModifierObserveSelfSend(static_cast<ke::SourceId>(id),inputs.data(),4,2);
        keyboardModifierObserveSelfSend(static_cast<ke::SourceId>(255),inputs.data(),4,2);keyboardModifierFlushEvidence(json::object(),200);
        check(fake::rows.size()==1&&fake::rows.at(0).at("events").size()==8,"actual serializer handles all seven IDs and unknown");
        const auto& row=fake::rows.at(0);for(unsigned id=0;id<8;++id){const auto& e=row.at("events").at(id);
            check(e.at("source")==ke::sourceName(static_cast<ke::SourceId>(id==7?255:id)),"serializer uses actual sourceName including unknown");
            check(e.at("attemptedModifierDownMask")==ke::bit(VK_LSHIFT)&&e.at("attemptedModifierUpMask")==ke::bit(VK_LSHIFT)&&e.at("countBasedPrefixModifierDownMask")==ke::bit(VK_LSHIFT)&&e.at("countBasedPrefixModifierUpMask")==0,"shifted ordinary request stores only modifiers and count-based prefix");
            check(!e.contains("virtualKey")&&!e.contains("scanCode")&&!e.contains("message")&&!e.contains("extraInfo")&&e.at("ordinaryKeysRetained")==false,"ordinary key/scan/time/extra not serialized");
            check(e.at("maskInspectionLimit")==64&&e.at("maskInspectionComplete")==true&&e.at("errorHintIsDefinitive")==false,"serializer explicit inspection/error limits preserved");
        }
        check(json::parse(row.dump())==row&&fake::rawCalls==0&&fake::conversionCalls==0,"JSON round trip with no raw-device metadata query");
    });
    scenario("actual-three-producer-Event-POD-privacy",[]{
        for(const Producer producer:std::array<Producer,3>{Producer::Focus,Producer::Desktop,Producer::TaskView}){
            fake::Plan p;p.inserted=producer==Producer::Focus?2:4;reset(p);
            if(producer==Producer::Focus)(void)refocusWebView(true);else if(producer==Producer::Desktop)returnToDesktop();else nativeSendTaskView();
            const auto s=g_keyboardModifierEvents.take(200);check(s.count==1,"one actual POD receipt retained");if(s.count){const auto& e=s.events[0];
                check(e.vk==0&&e.scan==0&&e.flags==0&&e.message==0&&e.extraInfo==0&&e.device==0&&e.deviceGeneration==0,"producer Event retains no ordinary VK/scan/message/extra/device content");
                check(e.tick==120&&e.threadId==77&&e.powerGeneration==33,"actual adapter observation context retained");
                const auto expected=producer==Producer::Focus?ke::SourceId::FocusAltFallback:(producer==Producer::Desktop?ke::SourceId::ReturnToDesktop:ke::SourceId::TaskView);check(e.sourceId==expected,"actual producer source ID retained in POD");
            }
        }
    });
}
int main(){
    producerMatrix();boundaryTests();mappingPrivacyTests();
    const json result={{"passed",failures==0},{"checks",checks},{"failed",failures},{"scenarios",scenarios},{"referenceCallerRuns",referenceRuns},{"candidateCallerRuns",candidateRuns},
        {"actualBeforeAfterFragmentsExecuted",true},{"allFragmentWin32UiInputCallsFaked",true},{"realInputDeviceHookHostProductProbeServiceExecuted",false},{"fixtureOrigin","private-synthetic-R3-validation"},
        {"limits",json::array({"Source-before versus source-after extracted caller behavior only; not full production translation units","Win32 focus and COM MoveFocus outcomes are controlled fakes, not OS/WebView2 validation","hideWindowAnimated invocation/gate/order compared; actual hide lifecycle implementation not executed","SendInput counts and modifier prefix masks are observations/assumptions, not per-key/consumer receipts or UIPI proof","Actual raw-name function is included only as serializer dependency; producer scenarios assert no raw/name queries","No lifecycle timer wiring, logging configuration, actual hooks/devices/Host/product or mainline incident attribution verified"})}};
    std::puts(result.dump().c_str());return failures?1:0;
}
