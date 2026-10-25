// Actual shortcut wrapper + scalar physical peek; all OS/UI/input APIs are fakes.
#define NOMINMAX
#include <windows.h>
#include <array>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "input_idle_diagnostics.h"
#include "json.hpp"
using json=nlohmann::json;
namespace fake {
unsigned checks=0,reads=0;DWORD error=12345,foregroundPid=444;bool valid=true,hide=false,throwRead=false,throwSink=false;
std::vector<std::string> controls;std::vector<json>rows;std::vector<std::string>domains;
void check(bool okay,const char* label){++checks;if(!okay)throw std::runtime_error(label);}
DWORD getError()noexcept{return error;}void setError(DWORD e)noexcept{error=e;}
ULONGLONG tick()noexcept{error=777;return 1000;}
BOOL isWindow(HWND){controls.push_back("IsWindow");error=222;return valid;}
BOOL visible(HWND){controls.push_back("IsWindowVisible");return TRUE;}
BOOL iconic(HWND){controls.push_back("IsIconic");return FALSE;}
BOOL zoomed(HWND){controls.push_back("IsZoomed");return FALSE;}
}
namespace ymcc::keyboardEvidence {
struct Snapshot {unsigned mask=0;DWORD pid=0;};
Snapshot read(){++fake::reads;fake::error=888;if(fake::throwRead)throw std::runtime_error("fake snapshot failure");return {17,fake::foregroundPid};}
json toJson(const Snapshot& s){return {{"asyncDownMask",s.mask},{"foregroundPidBefore",s.pid},{"foregroundPidAfter",s.pid},{"captureBeginTick",1000},{"captureEndTick",1000},{"sourceAttribution","unobserved"}};}
}
namespace ymcc {static bool summonPressHides(bool,bool,bool,bool,bool){fake::controls.push_back("summon-policy");return fake::hide;}}
struct FocusTargetSnapshot {bool valid=false;};
static HWND g_hwnd=reinterpret_cast<HWND>(std::uintptr_t(42));
static std::atomic<unsigned long long>g_summonGeneration{0};
static std::atomic<bool>g_fullHeight{false},g_keyboardModifierEventGate{false};
static std::atomic<std::uint64_t>g_focusShortcutEvidenceFailures{0};
static FocusTargetSnapshot g_pendingSummonTarget;
static ymcc::idle::Accumulator g_inputIdleActivity;
static void focusBeginReturnToPreviousWindow(ULONGLONG){fake::controls.push_back("return-request");fake::foregroundPid=444;fake::error=333;}
static void bringToFront(HWND,bool){fake::controls.push_back("bringToFront");fake::foregroundPid=9848;fake::error=444;}
static json nativeSummonCandidateSnapshot(const FocusTargetSnapshot&,unsigned long long){return json::object();}
static void ipc_emit(const char*,const json&){fake::controls.push_back("ipc_emit");fake::error=555;}
static void queueSummonGameRegistration(const FocusTargetSnapshot&,unsigned long long){fake::controls.push_back("queue-registration");fake::error=666;}
static void appendNativeLifecycleLog(const char*,const json&){fake::controls.push_back("lifecycle-log");fake::error=999;}
static void appendDomainLog(const char* domain,const char* event,const json& row){if(std::string(event)!="focus-shortcut-input")throw std::runtime_error("wrong event");fake::error=123;if(fake::throwSink)throw std::runtime_error("fake sink failure");fake::domains.emplace_back(domain);fake::rows.push_back(row);}
#define GetLastError fake::getError
#define SetLastError fake::setError
#define GetTickCount64 fake::tick
#define IsWindow fake::isWindow
#define IsWindowVisible fake::visible
#define IsIconic fake::iconic
#define IsZoomed fake::zoomed
#include "actual_focus.inc"
#include "baseline_focus.inc"
#undef GetLastError
#undef SetLastError
#undef GetTickCount64
#undef IsWindow
#undef IsWindowVisible
#undef IsIconic
#undef IsZoomed
namespace ymcc::idle::detail {struct AccumulatorTestAccess {
static void lock(Accumulator& a){a.mx_.lock();}static void unlock(Accumulator& a){a.mx_.unlock();}
};}
static void reset(){fake::controls.clear();fake::rows.clear();fake::domains.clear();fake::reads=0;fake::error=12345;fake::foregroundPid=444;fake::valid=true;fake::hide=false;fake::throwRead=fake::throwSink=false;g_summonGeneration=0;g_focusShortcutEvidenceFailures=0;g_keyboardModifierEventGate=false;g_pendingSummonTarget={};g_inputIdleActivity.enable(false,0);g_inputIdleActivity.enable(true,800);ymcc::idle::Pad p{};p.buttons=0x300;g_inputIdleActivity.physical(true,p,900,true,1,7,"msi-dinput");}
static void peekCases(){
 ymcc::idle::Accumulator a;ymcc::idle::PhysicalSnapshot s{};s.buttons=555;
 fake::check(!a.peekPhysical(s)&&s.buttons==555,"disabled peek changed output");
 a.enable(true,100);ymcc::idle::Pad p{};p.buttons=0x100;a.physical(true,p,110,true,1,7,"one");
 fake::check(a.peekPhysical(s)&&s.sampleTick==110&&s.observationTick==110&&s.latestPhysicalLive&&s.buttons==0x100,"fresh physical snapshot identity");
 auto w=a.take(120);fake::check(w.physical.samples==1&&w.physical.lastSampleTick==110,"peek drained source counter");
 fake::check(a.peekPhysical(s)&&s.sampleTick==110&&s.buttons==0x100&&a.take(121).physical.samples==0,"take erased latest scalar sample");
 a.physical(false,p,130,true,0,7,"one");fake::check(a.peekPhysical(s)&&!s.latestPhysicalLive&&s.sampleTick==110&&s.observationTick==130&&s.haveLastSample,"source absence relabelled old sample fresh");
 a.physical(false,p,140,false,0,8,"two");fake::check(a.peekPhysical(s)&&!s.haveLastSample&&s.powerGeneration==8,"source change retained valid old pad");
 ymcc::idle::detail::AccumulatorTestAccess::lock(a);s.buttons=556;
 const bool got=a.peekPhysical(s);ymcc::idle::detail::AccumulatorTestAccess::unlock(a);
 fake::check(!got&&s.buttons==556,"contended peek blocked or changed output");
 a.enable(false,150);a.enable(true,160);fake::check(a.peekPhysical(s)&&!s.haveLastSample&&!s.latestPhysicalLive&&s.observationTick==0,"reset retained stale source metadata");
}
struct Control {bool result;std::vector<std::string> calls;DWORD error;};
static Control run(bool old,bool enabled,bool hide,bool valid,bool throwRead=false,bool throwSink=false){reset();g_keyboardModifierEventGate=enabled;fake::hide=hide;fake::valid=valid;fake::throwRead=throwRead;fake::throwSink=throwSink;const auto result=old?baselineNativeYmccShortcutSummon({true},true):nativeYmccShortcutSummon({true},true);return {result,fake::controls,fake::error};}
int main(){try{
 peekCases();unsigned scenarios=0;
 for(bool enabled:{false,true})for(bool hide:{false,true})for(bool valid:{false,true})for(unsigned failure:{0u,1u,2u}){
  const auto old=run(true,enabled,hide,valid,failure==1,failure==2),now=run(false,enabled,hide,valid,failure==1,failure==2);++scenarios;
  fake::check(old.result==now.result&&old.calls==now.calls&&old.error==now.error,"diagnostics changed original summon policy/control/error outcome");
  if(!enabled||!valid)fake::check(fake::reads==0&&fake::rows.empty(),"closed/invalid gate queried or logged keyboard");
  if(enabled&&valid&&failure)fake::check(g_focusShortcutEvidenceFailures==2,"snapshot/sink failures escaped or not retained");
 }
 run(false,true,false,true);fake::check(fake::rows.size()==4&&fake::reads==2,"show boundary count");
 fake::check(fake::rows[0]==fake::rows[1]&&fake::rows[2]==fake::rows[3]&&fake::domains==std::vector<std::string>{"focus","virtual","focus","virtual"},"boundary mirror differs");
 fake::check(fake::rows[0]["phase"]=="before-decision"&&fake::rows[2]["phase"]=="show-returned"&&fake::rows[0]["summonGeneration"]==fake::rows[2]["summonGeneration"],"phase/generation correlation lost");
 fake::check(fake::rows[0]["keyboardModifiers"]["foregroundPidBefore"]==444&&fake::rows[2]["keyboardModifiers"]["foregroundPidBefore"]==9848,"sampled foreground order lost");
 const auto pad=fake::rows[0]["physicalAtBoundary"];
 fake::check(pad["leftShoulderInLastSample"]==true&&pad["rightShoulderInLastSample"]==true&&pad["sampleAgeMs"]==100&&pad["sampleIsLatestLiveObservation"]==true,"physical shoulder/sample freshness missing");
 fake::check(fake::rows[0]["crossSourceAtomic"]==false&&fake::rows[0]["keyboardProducerIdentified"]==false&&fake::rows[0]["keyReleaseProven"]==false,"boundary evidence overclaim");
 run(false,true,true,true);fake::check(fake::rows[2]["phase"]=="return-requested","hide boundary mislabeled focus completion");
 reset();g_keyboardModifierEventGate=true;ymcc::idle::detail::AccumulatorTestAccess::lock(g_inputIdleActivity);
 focusShortcutInputEvidence("test",5,false);ymcc::idle::detail::AccumulatorTestAccess::unlock(g_inputIdleActivity);
 fake::check(fake::rows.size()==2&&fake::rows[0]["physicalAtBoundary"]["captured"]==false,"contended capture was not unknown");
 fake::check(fake::error==12345,"diagnostic standalone call changed LastError");
 auto after=g_inputIdleActivity.take(1100);fake::check(after.physical.samples==1,"boundary peek consumed idle evidence");
 std::cout<<json{{"pass",true},{"checks",fake::checks},{"controlScenarios",scenarios},{"actualOsInput",false},{"realOsSnapshot",false},{"fieldProducerProven",false}}.dump()<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
