// Private synthetic executable: actual extracted R2 adapters, no real input APIs.
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
using json = nlohmann::json;
// Origin cache integration is covered by keyboard_device_origin_export_selftest.
static void keyboardDeviceOriginObserveAtDrain(const ymcc::keyboardEvents::Event&,const wchar_t*,std::size_t) noexcept {}

namespace ke = ymcc::keyboardEvents;

static ke::Recorder g_keyboardModifierEvents;
static std::atomic<bool> g_keyboardModifierEventGate{false};
static std::atomic<std::uint64_t> g_keyboardModifierDeviceGeneration{1};
static std::atomic<bool> g_keyboardModifierHookInstalled{false}, g_keyboardModifierRawSubscribed{false};
static std::atomic<ULONGLONG> g_keyboardModifierHookObservedTick{0};
static std::atomic<std::uint64_t> g_keyboardModifierLogFailures{0};
static std::atomic<bool> g_oemHooksWanted{false}, g_oemRawPathActive{false};
namespace fake {
struct Calls { unsigned errorGet=0,errorSet=0,tick=0,thread=0,power=0,size=0,name=0,convert=0,send=0,append=0; };
static Calls calls{};
static DWORD lastError=0;
static ULONGLONG tick=120;
static DWORD thread=77;
static std::uint64_t power=33;
struct NamePlan {
    UINT sizeResult=0,required=12,nameResult=11,returned=12;
    DWORD sizeError=5001,nameError=5002;
    bool terminate=true,empty=false,throwQuery=false,throwConvert=false,changeGeneration=false;
    std::wstring path=L"fake-device";
};
static NamePlan plan{};
struct Reply { UINT inserted; DWORD error; };
static std::vector<Reply> replies;
static std::vector<std::vector<INPUT>> sent;
static std::vector<json> rows;
static bool throwLog=false;
static DWORD GetLastError() noexcept { ++calls.errorGet;return lastError; }
static void SetLastError(DWORD value) noexcept { ++calls.errorSet;lastError=value; }
static ULONGLONG GetTickCount64() noexcept { ++calls.tick;lastError=7001;return tick; }
static DWORD GetCurrentThreadId() noexcept { ++calls.thread;lastError=7002;return thread; }
static UINT GetRawInputDeviceInfoW(HANDLE device,UINT command,LPVOID data,PUINT chars) {
    if(!device||command!=RIDI_DEVICENAME||!chars)throw std::runtime_error("invalid fake raw query");
    if(!data){++calls.size;lastError=plan.sizeError;*chars=plan.required;return plan.sizeResult;}
    ++calls.name;lastError=plan.nameError;
    if(plan.throwQuery)throw std::runtime_error("synthetic raw API exception");
    const UINT capacity=*chars;
    auto* out=static_cast<wchar_t*>(data);
    if(plan.nameResult!=UINT(-1)){
        if(plan.empty&&capacity)out[0]=0;
        else {
            const std::size_t n=(std::min)(plan.path.size(),static_cast<std::size_t>(capacity));
            std::copy_n(plan.path.data(),n,out);
            if(plan.terminate&&n<capacity)out[n]=0;
        }
    }
    *chars=plan.returned;
    if(plan.changeGeneration)g_keyboardModifierDeviceGeneration.fetch_add(1);
    return plan.nameResult;
}
static UINT SendInput(UINT count,LPINPUT inputs,int inputSize) {
    if(!inputs||inputSize!=sizeof(INPUT)||sent.size()>=replies.size())throw std::runtime_error("unplanned fake SendInput");
    ++calls.send;sent.emplace_back(inputs,inputs+count);
    const auto reply=replies[sent.size()-1];lastError=reply.error;return reply.inserted;
}
}
static std::uint64_t currentPowerGeneration() noexcept { ++fake::calls.power;fake::lastError=7003;return fake::power; }
static std::string W2U(const wchar_t* text,int length) {
    ++fake::calls.convert;fake::lastError=7004;
    if(fake::plan.throwConvert)throw std::runtime_error("synthetic conversion exception");
    std::string result;for(int i=0;i<length;++i){if(text[i]>127)throw std::runtime_error("fake conversion is ASCII-only");result.push_back(static_cast<char>(text[i]));}return result;
}
static void appendDomainLog(const char* domain,const char* name,const json& row) {
    ++fake::calls.append;
    if(std::string(domain)!="virtual"||std::string(name)!="keyboard-modifier-events")throw std::runtime_error("unexpected log target");
    if(fake::throwLog)throw std::runtime_error("synthetic sink failure");
    fake::rows.push_back(row);
}
// Redirect ALL directly called Win32/input APIs in the unchanged fragments.
#define GetLastError fake::GetLastError
#define SetLastError fake::SetLastError
#define GetTickCount64 fake::GetTickCount64
#define GetCurrentThreadId fake::GetCurrentThreadId
#define GetRawInputDeviceInfoW fake::GetRawInputDeviceInfoW
#define SendInput fake::SendInput
#include "actual_adapter_fragments.inc"
#undef GetLastError
#undef SetLastError
#undef GetTickCount64
#undef GetCurrentThreadId
#undef GetRawInputDeviceInfoW
#undef SendInput

static json scenarios=json::array();
static unsigned checks=0,failures=0;
static std::vector<std::string> messages;
static void check(bool ok,const char* description) {
    ++checks;if(!ok){++failures;messages.emplace_back(description);}
}
static void scenario(const char* name,const std::function<void()>& body) {
    const auto before=checks,failed=failures;messages.clear();
    try{body();}catch(const std::exception& e){++failures;messages.emplace_back(std::string("unexpected exception: ")+e.what());}
    catch(...){++failures;messages.emplace_back("unexpected non-standard exception");}
    scenarios.push_back({{"name",name},{"checks",checks-before},{"failed",failures-failed},{"failures",messages}});
}
static void reset(bool enabled=true) {
    g_keyboardModifierEventGate=false;g_keyboardModifierEvents.enable(false,100);
    g_keyboardModifierEvents.enable(true,100);(void)g_keyboardModifierEvents.take(100);
    if(!enabled)g_keyboardModifierEvents.enable(false,100);
    g_keyboardModifierEventGate=enabled;g_keyboardModifierDeviceGeneration=9;
    g_keyboardModifierHookInstalled=false;g_keyboardModifierRawSubscribed=false;
    g_keyboardModifierHookObservedTick=0;g_oemHooksWanted=false;g_oemRawPathActive=false;
    g_keyboardModifierLogFailures=0;fake::calls={};fake::lastError=12345;
    fake::plan={};fake::replies.clear();fake::sent.clear();fake::rows.clear();fake::throwLog=false;
}
static INPUT key(WORD vk,bool up=false) {
    INPUT input{};input.type=INPUT_KEYBOARD;input.ki.wVk=vk;
    input.ki.wScan=0xdead;input.ki.time=0xbeef;input.ki.dwExtraInfo=0x12345678;
    input.ki.dwFlags=up?KEYEVENTF_KEYUP:0;return input;
}
static KBDLLHOOKSTRUCT ll(DWORD vk=VK_LCONTROL,DWORD flags=0) {
    KBDLLHOOKSTRUCT kb{};kb.vkCode=vk;kb.scanCode=0x1d;kb.flags=flags;kb.time=0xabcdef;kb.dwExtraInfo=0x87654321;return kb;
}
static RAWINPUT raw(WORD vk=VK_RCONTROL,WORD flags=RI_KEY_E0,std::uintptr_t device=0x1122334455667788ULL) {
    RAWINPUT r{};r.header.dwType=RIM_TYPEKEYBOARD;r.header.hDevice=reinterpret_cast<HANDLE>(device);
    r.data.keyboard.VKey=vk;r.data.keyboard.MakeCode=0x1d;r.data.keyboard.Flags=flags;
    r.data.keyboard.Message=(flags&RI_KEY_BREAK)?WM_SYSKEYUP:WM_SYSKEYDOWN;r.data.keyboard.ExtraInformation=0xabcdef12;return r;
}
static ke::Snapshot take() { return g_keyboardModifierEvents.take(150); }
static ke::Event nameEvent(std::uintptr_t device=0x1234,std::uint64_t generation=9) {
    ke::Event e{};e.kind=ke::Kind::RawInput;e.device=device;e.deviceGeneration=generation;e.vk=VK_LCONTROL;return e;
}
static json flush() {
    keyboardModifierFlushEvidence(json::object(),200);
    check(fake::rows.size()==1,"one serialized row");return fake::rows.empty()?json::object():fake::rows.back();
}
static void noTyped(const ke::Event& e) {
    check(e.vk==0&&e.scan==0&&e.flags==0&&e.message==0&&e.extraInfo==0&&e.device==0,"SelfEvent contains no ordinary key/scan/flags/message/extra/device");
}
static void expectName(const char* status,unsigned expectedQueries,const std::function<void()>& configure) {
    reset();configure();unsigned budget=4;auto r=keyboardModifierRawNameAtDrain(nameEvent(),budget);
    check(r.at("status")==status,"raw-name status");check(fake::calls.size+fake::calls.name==expectedQueries,"raw-name API call bound");
    check(budget==3,"one name observation consumes one budget unit");check(fake::lastError==12345,"raw-name LastError restored");
    check(r.at("eventPathIdentityProven")==false&&r.at("queriedAfterEvent")==true,"raw-name provenance limits");
}
static void adapterTests() {
    scenario("gate-off-all-observers-no-APIs",[]{
        reset(false);auto l=ll();auto r=raw();auto i=key(VK_LCONTROL);
        keyboardModifierObserveLowLevel(l,WM_KEYDOWN);keyboardModifierObserveRaw(r);
        keyboardModifierObserveSelfSend(ke::SourceId::Other,&i,1,0);
        const auto s=take();check(s.count==0&&!s.enabled,"disabled recorder empty");
        check(fake::calls.errorGet==0&&fake::calls.tick==0&&fake::calls.thread==0&&fake::calls.power==0,"disabled adapters call no observation APIs");
        check(fake::lastError==12345,"gate-off LastError untouched");
    });
    scenario("modifier-filter-and-null-input",[]{
        reset();keyboardModifierObserveLowLevel(ll('A'),WM_KEYDOWN);
        keyboardModifierObserveRaw(raw('Z'));auto r=raw();r.header.dwType=RIM_TYPEMOUSE;keyboardModifierObserveRaw(r);
        keyboardModifierObserveSelfSend(ke::SourceId::Other,nullptr,3,1);
        auto s=take();check(s.count==0,"ordinary LL/raw and nonkeyboard raw ignored");
        check(fake::calls.tick==0&&fake::calls.errorGet==0,"early filter calls no APIs");
        auto ordinary=key('Q');keyboardModifierObserveSelfSend(ke::SourceId::Other,&ordinary,1,1);
        s=take();check(s.count==0&&s.ignoredNonModifiers==1,"ordinary-only self batch has no retained event");
        check(fake::lastError==12345,"ordinary self LastError preserved");
    });
    scenario("all-eleven-modifiers-and-LL-flags",[]{
        reset();for(const auto vk:ke::modifierKeys)keyboardModifierObserveLowLevel(ll(vk,LLKHF_INJECTED|LLKHF_LOWER_IL_INJECTED|LLKHF_UP|LLKHF_EXTENDED),WM_SYSKEYUP);
        const auto s=take();check(s.count==11,"all left/right and generic modifiers accepted");
        for(std::size_t n=0;n<s.count;++n){const auto& e=s.events[n];
            check(e.kind==ke::Kind::LowLevel&&e.vk==ke::modifierKeys[n],"LL kind/key retained");
            check(e.scan==0x1d&&e.message==WM_SYSKEYUP&&e.extraInfo==0x87654321,"LL payload retained");
            check(e.flags==(LLKHF_INJECTED|LLKHF_LOWER_IL_INJECTED|LLKHF_UP|LLKHF_EXTENDED),"LL injected/lowerIL/up/ext flags retained");
            check(e.tick==120&&e.threadId==77&&e.powerGeneration==33,"LL synthetic observation context retained");
        }
        check(fake::lastError==12345&&fake::calls.errorSet==11,"LL restores LastError even when fake context APIs clobber it");
    });
    scenario("RAW-device-generation-e0-e1-down-up",[]{
        reset();const std::array<WORD,4> flags{RI_KEY_E0,RI_KEY_E1,RI_KEY_E0|RI_KEY_BREAK,RI_KEY_E1|RI_KEY_BREAK};
        for(std::size_t n=0;n<flags.size();++n){g_keyboardModifierDeviceGeneration=100+n;keyboardModifierObserveRaw(raw(VK_CONTROL,flags[n]));}
        const auto s=take();check(s.count==4,"all RAW variants retained");
        for(std::size_t n=0;n<s.count;++n){const auto& e=s.events[n];
            check(e.kind==ke::Kind::RawInput&&e.vk==VK_CONTROL&&e.scan==0x1d,"RAW kind/key/scan");
            check(e.flags==flags[n]&&e.message==DWORD((flags[n]&RI_KEY_BREAK)?WM_SYSKEYUP:WM_SYSKEYDOWN),"RAW e0/e1/down/up/message");
            check(e.device==0x1122334455667788ULL&&e.deviceGeneration==100+n&&e.extraInfo==0xabcdef12,"RAW full-width hDevice/generation/extra");
            check(e.tick==120&&e.threadId==77&&e.powerGeneration==33,"RAW observation context");
        }
        check(fake::lastError==12345&&fake::calls.size==0&&fake::calls.name==0,"RAW callback preserves error and never queries names");
    });
    scenario("self-partial-prefix-source-and-no-typed-content",[]{
        const std::array<ke::SourceId,5> sources{ke::SourceId::NativeKeyboardShortcut,ke::SourceId::OemKeyboardReplay,ke::SourceId::KeyboardGamebar,ke::SourceId::Other,static_cast<ke::SourceId>(255)};
        for(const auto source:sources){reset();std::array<INPUT,6> inputs{key(VK_LCONTROL),key('A'),key(VK_RMENU),key(VK_LCONTROL,true),key('A',true),key(VK_RMENU,true)};
            keyboardModifierObserveSelfSend(source,inputs.data(),6,3);const auto s=take();check(s.count==1,"partial batch retained");
            if(s.count){const auto& e=s.events[0];check(e.sourceId==source&&e.kind==ke::Kind::SelfSendInput,"source and self kind unchanged");
                check(e.requestedInputs==6&&e.insertedInputs==3&&e.win32ErrorHint==12345,"original requested/inserted/error counts");
                const auto mask=ke::bit(VK_LCONTROL)|ke::bit(VK_RMENU);
                check(e.attemptedModifierDownMask==mask&&e.attemptedModifierUpMask==mask,"attempted modifier masks");
                check(e.insertedModifierDownMask==mask&&e.insertedModifierUpMask==0,"prefix includes ordinary positions, no imaginary up receipt");noTyped(e);
            }check(fake::lastError==12345,"self LastError restored");
        }
    });
    scenario("self-zero-full-counts-nonkeyboard-and-generic",[]{
        for(const UINT inserted:std::array<UINT,3>{0,2,4}){
            reset();auto mouse=key(VK_LCONTROL);mouse.type=INPUT_MOUSE;
            std::array<INPUT,4> inputs{mouse,key(VK_CONTROL),key(VK_SHIFT,true),key(VK_MENU,true)};
            keyboardModifierObserveSelfSend(ke::SourceId::Other,inputs.data(),4,inserted);const auto s=take();check(s.count==1,"generic self modifiers retained");
            if(s.count){const auto& e=s.events[0];check(e.attemptedModifierDownMask==ke::bit(VK_CONTROL),"nonkeyboard union key ignored");
                check(e.attemptedModifierUpMask==(ke::bit(VK_SHIFT)|ke::bit(VK_MENU)),"generic SHIFT/ALT up retained");
                check(e.insertedModifierDownMask==(inserted>=2?ke::bit(VK_CONTROL):0),"zero/partial/full down prefix");
                check(e.insertedModifierUpMask==(inserted==4?(ke::bit(VK_SHIFT)|ke::bit(VK_MENU)):0),"zero/partial/full up prefix");
                check(e.win32ErrorHint==(inserted==4?0u:12345u),"full batch suppresses stale error hint");noTyped(e);
            }
        }
    });
    scenario("self-inspection-limit-64-and-large-UINT",[]{
        for(const UINT requested:std::array<UINT,3>{64,65,UINT(-1)}){
            reset();std::array<INPUT,64> inputs{};for(auto& i:inputs)i=key('A');inputs[63]=key(VK_RSHIFT);
            keyboardModifierObserveSelfSend(ke::SourceId::Other,inputs.data(),requested,64);
            const auto row=flush();const auto& e=row.at("events").at(0);
            check(e.at("requestedInputs")==requested&&e.at("insertedInputs")==64,"large original counts retained");
            check(e.at("attemptedModifierDownMask")==ke::bit(VK_RSHIFT)&&e.at("countBasedPrefixModifierDownMask")==ke::bit(VK_RSHIFT),"64th input observed with no read past 64-input allocation");
            check(e.at("maskInspectionLimit")==64&&e.at("maskInspectionComplete")==(requested<=64),"serializer publishes incomplete inspection above 64");
        }
        reset();std::array<INPUT,65> inputs{};for(auto& i:inputs)i=key('A');inputs[64]=key(VK_LCONTROL);
        keyboardModifierObserveSelfSend(ke::SourceId::Other,inputs.data(),65,65);const auto s=take();
        check(s.count==0&&s.ignoredNonModifiers==1,"modifier solely beyond inspection cap yields no self event; this is a known limit");
    });
    scenario("actual-shortcut-caller-gate-behavior-and-shift",[]{
        reset(false);fake::replies={{2,501}};nativeKeyboardShortcutSend(VK_HOME,false);
        check(fake::calls.send==1&&fake::sent[0].size()==2,"gate off preserves original two-input SendInput call");
        check(fake::sent[0][0].ki.wVk==VK_HOME&&fake::sent[0][0].ki.dwFlags==0&&fake::sent[0][1].ki.dwFlags==(KEYEVENTF_EXTENDEDKEY|KEYEVENTF_KEYUP),"exact current shortcut input flags observed");
        check(take().count==0&&fake::lastError==501,"gate off does not alter return LastError");
        reset();fake::replies={{2,502}};nativeKeyboardShortcutSend('A',true);const auto s=take();
        check(fake::sent.size()==1&&fake::sent[0].size()==4,"requiresShift preserves four inputs");
        check(fake::sent[0][0].ki.wVk==VK_LSHIFT&&fake::sent[0][1].ki.wVk=='A'&&fake::sent[0][2].ki.dwFlags==KEYEVENTF_KEYUP&&fake::sent[0][3].ki.wVk==VK_LSHIFT,"actual caller order retained");
        check(s.count==1,"requiresShift ordinary key produces modifier-only evidence");if(s.count){const auto& e=s.events[0];
            check(e.sourceId==ke::SourceId::NativeKeyboardShortcut&&e.requestedInputs==4&&e.insertedInputs==2,"shortcut source/counts");
            check(e.attemptedModifierDownMask==ke::bit(VK_LSHIFT)&&e.attemptedModifierUpMask==ke::bit(VK_LSHIFT)&&e.insertedModifierDownMask==ke::bit(VK_LSHIFT)&&e.insertedModifierUpMask==0,"requiresShift partial modifier prefix");noTyped(e);}
        check(fake::lastError==502,"caller return error preserved after observer");
        check(nativeKeyboardShortcutInputFlags(VK_NEXT)==KEYEVENTF_EXTENDEDKEY&&nativeKeyboardShortcutInputFlags('A')==0,"actual shortcut flag helper");
    });
    scenario("actual-gamebar-original-fallback-and-return",[]{
        for(const UINT sent:std::array<UINT,3>{0,2,4}){
            reset();fake::replies={{sent,600+sent},{1,610}};const bool ok=nativeFrontendButtonSendGamebar();const auto s=take();
            check(ok==(sent==4),"gamebar result depends on original four-input count");
            check(fake::sent.size()==(sent==2?2u:1u),"existing fallback invoked only for partial nonzero return");
            check(fake::sent[0].size()==4&&fake::sent[0][0].ki.wVk==VK_LWIN&&fake::sent[0][1].ki.wVk=='G'&&fake::sent[0][3].ki.dwFlags==KEYEVENTF_KEYUP,"actual gamebar request unchanged");
            check(s.count==(sent==2?2u:1u),"each actual SendInput return has own modifier event");
            if(s.count){check(s.events[0].sourceId==ke::SourceId::KeyboardGamebar&&s.events[0].requestedInputs==4&&s.events[0].insertedInputs==sent,"gamebar original evidence source/count");noTyped(s.events[0]);}
            if(sent==2){check(fake::sent[1].size()==2&&fake::sent[1][0].ki.wVk=='G'&&fake::sent[1][1].ki.wVk==VK_LWIN,"actual two-up fallback unchanged");
                check(s.events[1].attemptedModifierUpMask==ke::bit(VK_LWIN)&&s.events[1].insertedModifierUpMask==0&&s.events[1].insertedInputs==1,"fallback ordinary key prefix is not Win-up receipt");}
            check(fake::lastError==(sent==2?610:600+sent),"final fake SendInput error survives diagnostics");
        }
    });
}
static void rawNameTests() {
    scenario("raw-name-null-generation-budget-before-API",[]{
        reset();unsigned budget=4;auto r=keyboardModifierRawNameAtDrain(nameEvent(0),budget);
        check(r.at("status")=="null-device-handle"&&budget==4,"null handle avoids budget/API");
        r=keyboardModifierRawNameAtDrain(nameEvent(0x1234,8),budget);
        check(r.at("status")=="device-generation-changed"&&budget==4,"stale generation avoids query");
        budget=0;r=keyboardModifierRawNameAtDrain(nameEvent(),budget);
        check(r.at("status")=="query-budget-exhausted"&&budget==0,"zero budget avoids query");
        check(fake::calls.size==0&&fake::calls.name==0&&fake::lastError==12345,"early name branches restore error and query no device");
    });
    scenario("raw-name-size-query-failure",[]{expectName("size-query-failed",1,[]{fake::plan.sizeResult=UINT(-1);});});
    scenario("raw-name-zero-size",[]{expectName("empty-size",1,[]{fake::plan.required=0;});});
    scenario("raw-name-size-overlimit",[]{expectName("name-over-limit",1,[]{fake::plan.required=1025;});});
    scenario("raw-name-data-query-failure",[]{expectName("name-query-failed",2,[]{fake::plan.nameResult=UINT(-1);});});
    scenario("raw-name-returned-capacity-overlimit",[]{expectName("invalid-or-unterminated-name",2,[]{fake::plan.returned=1025;});});
    scenario("raw-name-no-terminator",[]{expectName("invalid-or-unterminated-name",2,[]{fake::plan.terminate=false;});});
    scenario("raw-name-empty-buffer",[]{expectName("invalid-or-unterminated-name",2,[]{fake::plan.empty=true;});});
    scenario("raw-name-generation-changed-during-query",[]{expectName("device-generation-changed-during-query",2,[]{fake::plan.changeGeneration=true;});});
    scenario("raw-name-success-and-maximum-terminated-length",[]{
        expectName("name-observed-at-drain",2,[]{});
        reset();fake::plan.required=1024;fake::plan.returned=1024;fake::plan.nameResult=1023;fake::plan.path=std::wstring(1023,L'x');
        unsigned budget=1;const auto r=keyboardModifierRawNameAtDrain(nameEvent(),budget);
        check(r.at("status")=="name-observed-at-drain"&&r.at("interfacePath").get<std::string>().size()==1023,"1023 characters plus terminator within 1024 buffer");
        check(budget==0&&fake::lastError==12345,"success bounded budget/error");
    });
    scenario("raw-name-exception-restores-LastError",[]{
        for(const bool conversion:std::array<bool,2>{false,true}){
            reset();fake::plan.throwQuery=!conversion;fake::plan.throwConvert=conversion;unsigned budget=4;bool threw=false;
            try{(void)keyboardModifierRawNameAtDrain(nameEvent(),budget);}catch(const std::runtime_error&){threw=true;}
            check(threw&&fake::lastError==12345&&budget==3,"query/conversion exception propagates with RAII LastError restoration");
        }
    });
    scenario("raw-name-query-failure-captures-hint-before-restoration",[]{
        reset();fake::plan.sizeResult=UINT(-1);unsigned budget=4;auto r=keyboardModifierRawNameAtDrain(nameEvent(),budget);
        check(r.at("win32Error")==5001&&fake::lastError==12345,"size failure hint separate from restored error");
        reset();fake::plan.nameResult=UINT(-1);budget=4;r=keyboardModifierRawNameAtDrain(nameEvent(),budget);
        check(r.at("win32Error")==5002&&fake::lastError==12345,"name failure hint separate from restored error");
    });
    scenario("raw-name-inconsistent-fake-result-observed-limit",[]{
        reset();fake::plan.nameResult=4096;unsigned budget=4;const auto r=keyboardModifierRawNameAtDrain(nameEvent(),budget);
        check(r.at("status")=="name-observed-at-drain"&&r.at("nameResult")==4096,"actual code checks returned capacity and terminator, not independent successful nameResult range; inconsistent synthetic API result accepted");
        check(fake::lastError==12345,"inconsistent fake result still restores error");
    });
}
static void serializerTests() {
    scenario("serializer-LL-injection-flags-and-explicit-limits",[]{
        reset();keyboardModifierObserveLowLevel(ll(VK_LCONTROL,LLKHF_INJECTED|LLKHF_LOWER_IL_INJECTED|LLKHF_UP),WM_KEYUP);
        keyboardModifierObserveLowLevel(ll(VK_RCONTROL),WM_KEYDOWN);const auto row=flush();const auto& e=row.at("events").at(0);const auto& e2=row.at("events").at(1);
        check(row.at("schemaVersion")==2&&row.at("capacity")==64&&row.at("eventCount")==2,"schema/capacity/count");
        check(row.at("recorderWindowComplete")==true&&row.at("completeInputCoverage")==false&&row.at("emptyIsAbsenceProof")==false,"queue complete is not coverage/absence proof");
        check(row.at("writerProcessFromInjectedFlag")=="unobserved","injected flag cannot identify writer process");
        check(e.at("kind")=="existing-low-level-hook"&&e.at("injected")==true&&e.at("lowerIntegrityInjected")==true&&e.at("down")==false,"LL injection/down serialization");
        check(e2.at("injected")==false&&e2.at("lowerIntegrityInjected")==false&&e2.at("down")==true,"LL unflagged down serialization");
        check(e.at("lowLevelEventTimeCaptured")==false&&e.at("tick")==120,"callback tick is not original hook time");
        check(e.at("virtualKey")==VK_LCONTROL&&e.at("scanCode")==0x1d&&e.at("extraInfo")==0x87654321,"LL serializes retained modifier payload");
        check(!e.contains("deviceHandle")&&!e.contains("source"),"LL serializer keeps event-specific scope");
        check(json::parse(row.dump())==row,"serializer JSON round trip");
    });
    scenario("serializer-all-source-IDs-and-unknown-fallback",[]{
        reset();const std::array<ke::SourceId,5> sources{ke::SourceId::NativeKeyboardShortcut,ke::SourceId::OemKeyboardReplay,ke::SourceId::KeyboardGamebar,ke::SourceId::Other,static_cast<ke::SourceId>(255)};
        const std::array<const char*,5> expected{"nativeKeyboardShortcutSend","oemKeyboardConsumeReplay","nativeFrontendButtonSendGamebar","other-self-send","other-self-send"};
        std::array<INPUT,4> inputs{key(VK_LSHIFT),key('A'),key('A',true),key(VK_LSHIFT,true)};
        for(const auto source:sources)keyboardModifierObserveSelfSend(source,inputs.data(),4,2);
        const auto row=flush();check(row.at("events").size()==sources.size(),"all self source rows retained");
        for(std::size_t n=0;n<sources.size();++n){const auto& e=row.at("events").at(n);
            check(e.at("source")==expected[n],"source label and unknown fallback");
            check(e.at("kind")=="YMCC-SendInput-call"&&e.at("fullBatchInserted")==false&&e.at("win32ErrorHint")==12345,"self count/error serialization");
            check(e.at("errorHintIsDefinitive")==false&&e.at("ordinaryKeysRetained")==false,"explicit error/privacy limits");
            check(e.at("prefixInterpretation")=="Count-based prefix assumption, not a per-key or consumer receipt","prefix explanation explicit");
            check(!e.contains("virtualKey")&&!e.contains("scanCode")&&!e.contains("extraInfo")&&!e.contains("message"),"typed fields absent in SelfEvent serializer");
            check(e.at("countBasedPrefixModifierDownMask")==ke::bit(VK_LSHIFT)&&e.at("countBasedPrefixModifierUpMask")==0,"serialized partial prefix masks");
        }
    });
    scenario("serializer-RAW-cache-budget-and-generation",[]{
        reset();for(std::uintptr_t handle=1;handle<=6;++handle)keyboardModifierObserveRaw(raw(VK_LCONTROL,RI_KEY_E0,handle));
        keyboardModifierObserveRaw(raw(VK_LCONTROL,RI_KEY_BREAK,1));const auto row=flush();
        check(fake::calls.size==4&&fake::calls.name==4&&row.at("rawNameQueryBudgetRemaining")==0,"at most four names/eight raw API calls per drain");
        check(row.at("events").at(4).at("deviceNameObservation").at("status")=="query-budget-exhausted"&&row.at("events").at(5).at("deviceNameObservation").at("status")=="query-budget-exhausted","budget exhaustion visible");
        check(row.at("events").at(6).at("deviceNameObservation").at("status")=="name-observed-at-drain","same handle/generation reuses cache without budget");
        check(row.at("events").at(0).at("flags")==RI_KEY_E0&&row.at("events").at(0).at("down")==true&&row.at("events").at(6).at("down")==false,"raw flags/down serializer");
        reset();g_keyboardModifierDeviceGeneration=8;keyboardModifierObserveRaw(raw(VK_LCONTROL,0,123));g_keyboardModifierDeviceGeneration=9;keyboardModifierObserveRaw(raw(VK_LCONTROL,0,123));
        const auto mixed=flush();check(mixed.at("events").at(0).at("deviceNameObservation").at("status")=="device-generation-changed","old generation not queried");
        check(mixed.at("events").at(1).at("deviceNameObservation").at("status")=="name-observed-at-drain"&&fake::calls.size==1&&fake::calls.name==1,"cache keyed by generation, not just reused hDevice");
    });
    scenario("serializer-drain-context-allowlist-and-coverage",[]{
        reset();g_keyboardModifierHookInstalled=true;g_keyboardModifierHookObservedTick=222;g_keyboardModifierRawSubscribed=true;g_oemHooksWanted=true;g_oemRawPathActive=true;
        keyboardModifierObserveLowLevel(ll(),WM_KEYDOWN);
        const json idle={{"hostSnapshotKnown",true},{"hostProcessPresent",true},{"hostPrepared",true},{"hostActive",false},{"hostEpoch",42},{"hostRunId","private-synthetic-run"},{"hostPersona","synthetic-SD"},{"hostLifecycle","private"},{"samplePowerGeneration",55},{"ordinaryTypedText","must-not-copy"},{"unrelated",9}};
        keyboardModifierFlushEvidence(idle,200);check(fake::rows.size()==1,"context row emitted");const auto& row=fake::rows.at(0);
        for(const char* name:{"hostSnapshotKnown","hostProcessPresent","hostPrepared","hostActive","hostEpoch","hostRunId","hostPersona","hostLifecycle","samplePowerGeneration"})check(row.at(name)==idle.at(name),"allowlisted context copied");
        check(!row.contains("ordinaryTypedText")&&!row.contains("unrelated"),"unrelated/typed context not copied");
        check(row.at("hookInstalledCached")==true&&row.at("hookCoverageKnown")==true&&row.at("hookCoverageObservedTick")==222&&row.at("rawKeyboardSubscribedCached")==true&&row.at("rawOemExclusiveAtDrain")==true,"atomic drain coverage caches serialized");
        check(row.at("contextScope")=="Host and hook coverage are drain-time observations, not an atomic event-time binding","drain-time binding limitation explicit");
        check(row.at("events").at(0).at("powerGeneration")==33&&row.at("samplePowerGeneration")==55,"event and drain context not conflated");
    });
    scenario("serializer-empty-coverage-transition-and-disabled",[]{
        reset();keyboardModifierObserveLowLevel(ll(),WM_KEYDOWN);(void)flush();fake::rows.clear();
        keyboardModifierFlushEvidence(json::object(),300);check(fake::rows.empty(),"empty unchanged coverage suppresses log");
        g_keyboardModifierHookInstalled=true;keyboardModifierFlushEvidence(json::object(),400);
        check(fake::rows.size()==1&&fake::rows[0].at("eventCount")==0&&fake::rows[0].at("emptyIsAbsenceProof")==false,"empty coverage transition row does not prove no input");
        reset(false);keyboardModifierFlushEvidence(json::object(),500);check(fake::rows.empty(),"disabled recorder does not log");
    });
    scenario("serializer-overflow-window-and-clear",[]{
        reset();for(unsigned n=0;n<100;++n)keyboardModifierObserveLowLevel(ll(),WM_KEYDOWN);const auto row=flush();
        check(row.at("eventCount")==64&&row.at("droppedOverflow")==36&&row.at("recorderWindowComplete")==false,"capacity/loss serialized explicitly");
        keyboardModifierObserveLowLevel(ll(VK_RMENU),WM_KEYDOWN);fake::rows.clear();const auto next=flush();
        check(next.at("eventCount")==1&&next.at("droppedOverflow")==0&&next.at("windowBeginTick")==200,"drain clears event/loss window");
    });
    scenario("serializer-sink-and-name-exceptions-known-loss",[]{
        reset();keyboardModifierObserveLowLevel(ll(),WM_KEYDOWN);fake::throwLog=true;keyboardModifierFlushEvidence(json::object(),200);
        check(fake::rows.empty()&&g_keyboardModifierLogFailures==1,"sink exception caught and recorded");
        check(take().count==0,"already drained events are not replayed after sink exception");
        fake::throwLog=false;keyboardModifierObserveLowLevel(ll(),WM_KEYDOWN);const auto row=flush();check(row.at("logFailures")==1,"next row exposes prior serialization failure");
        reset();fake::plan.throwConvert=true;keyboardModifierObserveRaw(raw());keyboardModifierFlushEvidence(json::object(),200);
        check(fake::rows.empty()&&g_keyboardModifierLogFailures==1&&fake::lastError==12345,"raw conversion exception caught with error restoration");
    });
}
int main() {
    adapterTests();rawNameTests();serializerTests();
    const json report={{"passed",failures==0},{"checks",checks},{"failed",failures},{"scenarios",scenarios},
        {"actualSourceFragmentsExecuted",true},{"allFragmentWin32InputCallsFaked",true},{"realInputApisExecuted",false},
        {"realDeviceHookProductProbeServiceExecuted",false},{"fixtureOrigin","private-synthetic-candidate-validation"},
        {"scope","Bound frozen R10 actual adapter/caller/serializer fragments with actual recorder and JSON; synthetic dependencies only"},
        {"limits",json::array({"Not OS hook/raw/SendInput validation or historical mainline attribution","Count-based prefixes are assumptions, not per-key or consumer receipts","Fake W2U is ASCII-only; actual Unicode converter not exercised","Successful raw-name result range is not checked independently of returned capacity/terminator; inconsistent synthetic result accepted","SelfSend modifier only after index 63 produces no retained event; inspection bound is 64","A serializer exception increments logFailures after take; drained events are not replayed","No full callbacks, lifecycle, timer wiring, OEM replay caller, Steam mapping, keyboard-IN reports, or OEM behavior exercised"})}};
    const auto output=report.dump();std::puts(output.c_str());return failures?1:0;
}