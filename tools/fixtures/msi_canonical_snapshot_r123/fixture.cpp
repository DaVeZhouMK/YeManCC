// R123 offline fixture. Implementations and state declarations are exact source
// includes. Only the XINPUT_GAMEPAD scalar interface and test plumbing are local.
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include "json.hpp"
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
#include "msi_virtual_report_diagnostic.h"
#endif
using json=nlohmann::json;
using BYTE=std::uint8_t;using WORD=std::uint16_t;using SHORT=std::int16_t;using ULONGLONG=std::uint64_t;
struct XINPUT_GAMEPAD {WORD wButtons=0;BYTE bLeftTrigger=0,bRightTrigger=0;SHORT sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0;};
static_assert(sizeof(XINPUT_GAMEPAD)==12);
struct CanonicalFieldSubset {
#include "actual-fields.inc"
};
static CanonicalFieldSubset g_msiDinput;
#include "actual-functions.inc"
template<class T> concept HasReportTick=requires(T& s){s.canonicalReportTick;};
template<class T> concept HasReportSequence=requires(T& s){s.canonicalReportSequence;};
template<class T> concept HasReportGeneration=requires(T& s){s.canonicalReportGeneration;};
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
constexpr bool diagnostic=true;
using SourceStamp=ymcc::msi::virtualReportDiag::SourceStamp;
static_assert(HasReportTick<CanonicalFieldSubset>&&HasReportSequence<CanonicalFieldSubset>&&HasReportGeneration<CanonicalFieldSubset>);
static_assert(std::is_same_v<decltype(&msiDinputPublishCanonicalFrame),void(*)(const XINPUT_GAMEPAD&,ULONGLONG)>);
static_assert(std::is_same_v<decltype(&msiDinputCanonicalSnapshot),bool(*)(XINPUT_GAMEPAD&,SourceStamp*)>);
#else
constexpr bool diagnostic=false;
static_assert(!HasReportTick<CanonicalFieldSubset>&&!HasReportSequence<CanonicalFieldSubset>&&!HasReportGeneration<CanonicalFieldSubset>);
static_assert(std::is_same_v<decltype(&msiDinputPublishCanonicalFrame),void(*)(const XINPUT_GAMEPAD&)>);
static_assert(std::is_same_v<decltype(&msiDinputCanonicalSnapshot),bool(*)(XINPUT_GAMEPAD&)>);
#endif
static unsigned checks=0,publishCalls=0,snapshotCalls=0,invalidateCalls=0;
static ULONGLONG observationTick=10000; // Evidence only; never an input to snapshot.
static json rows=json::array();
static void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
static XINPUT_GAMEPAD payload(){return {0x8123,111,7,-1234,321,-41,999};}
static XINPUT_GAMEPAD sentinel(){return {0x4444,1,2,-3,4,-5,6};}
static json padJson(const XINPUT_GAMEPAD& p){return json::array({p.wButtons,p.bLeftTrigger,p.bRightTrigger,p.sThumbLX,p.sThumbLY,p.sThumbRX,p.sThumbRY});}
static void checkPad(const XINPUT_GAMEPAD& got,const XINPUT_GAMEPAD& expected){
 check(got.wButtons==expected.wButtons,"buttons exact");check(got.bLeftTrigger==expected.bLeftTrigger,"left trigger exact");check(got.bRightTrigger==expected.bRightTrigger,"right trigger exact");
 check(got.sThumbLX==expected.sThumbLX,"LX exact");check(got.sThumbLY==expected.sThumbLY,"LY exact");check(got.sThumbRX==expected.sThumbRX,"RX exact");check(got.sThumbRY==expected.sThumbRY,"RY exact");
}
static json productionState(){return {{"canonicalSeq",g_msiDinput.canonicalSeq.load()},{"canonicalValid",g_msiDinput.canonicalValid.load()},{"canonicalBtnTrig",g_msiDinput.canonicalBtnTrig.load()},{"canonicalThumbs",g_msiDinput.canonicalThumbs.load()},{"bindCount",g_msiDinput.bindCount}};}
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
static json stampJson(const SourceStamp& s){return {{"sequence",s.sequence},{"generation",s.generation},{"capturedTick",s.capturedTick},{"valid",s.valid},{"pad",json::array({s.buttons,s.leftTrigger,s.rightTrigger,s.lx,s.ly,s.rx,s.ry})}};}
static SourceStamp poisonStamp(){return {77,78,79,0xffff,255,254,-1,-2,-3,-4,true};}
static json diagnosticState(){return {{"reportSequence",g_msiDinput.canonicalReportSequence.load()},{"reportGeneration",g_msiDinput.canonicalReportGeneration.load()},{"reportTick",g_msiDinput.canonicalReportTick.load()}};}
#endif
static void publish(const XINPUT_GAMEPAD& p,ULONGLONG captured=10000){
 ++publishCalls;
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
 msiDinputPublishCanonicalFrame(p,captured);
#else
 (void)captured;msiDinputPublishCanonicalFrame(p);
#endif
}
static void invalidate(){++invalidateCalls;msiDinputInvalidateCanonicalFrame();}
struct Read {bool ok=false;XINPUT_GAMEPAD out=sentinel();
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
 SourceStamp stamp=poisonStamp();
#endif
};
static Read snapshot(){Read r;++snapshotCalls;
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
 r.ok=msiDinputCanonicalSnapshot(r.out,&r.stamp);
#else
 r.ok=msiDinputCanonicalSnapshot(r.out);
#endif
 return r;}
static void checkFailed(const Read& r){check(!r.ok,"snapshot rejects unavailable/incomplete cache");checkPad(r.out,sentinel());
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
 check(stampJson(r.stamp)==stampJson(SourceStamp{}),"failure clears every poisoned source field");
#endif
}
static void checkStamp(const Read& r,std::uint64_t seq,std::uint64_t generation,ULONGLONG tick){
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
 check(r.stamp.sequence==seq,"diagnostic publish ordinal");check(r.stamp.generation==generation,"published bindCount");check(r.stamp.capturedTick==tick,"published capture tick");
 check(r.stamp.valid==(seq!=0&&generation!=0&&tick!=0),"stamp validity is nonzero provenance only");
 check(json::array({r.stamp.buttons,r.stamp.leftTrigger,r.stamp.rightTrigger,r.stamp.lx,r.stamp.ly,r.stamp.rx,r.stamp.ry})==padJson(r.out),"source fields from same payload");
#else
 (void)r;(void)seq;(void)generation;(void)tick;
#endif
}
static void reset(){
 observationTick=10000;g_msiDinput.bindCount=17;g_msiDinput.canonicalSeq=0;g_msiDinput.canonicalBtnTrig=0;g_msiDinput.canonicalThumbs=0;g_msiDinput.canonicalValid=false;
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
 g_msiDinput.canonicalReportSequence=0;g_msiDinput.canonicalReportGeneration=0;g_msiDinput.canonicalReportTick=0;
#endif
}
template<class F>static void run(const char* name,bool common,F body){
 reset();const auto startChecks=checks,startPublish=publishCalls,startSnapshot=snapshotCalls,startInvalidate=invalidateCalls;
 json row={{"name",name},{"common",common}};
 try{body(row);row["passed"]=true;}catch(const std::exception& e){row["passed"]=false;row["error"]=e.what();}
 row["checks"]=checks-startChecks;row["publishes"]=publishCalls-startPublish;row["snapshots"]=snapshotCalls-startSnapshot;row["invalidations"]=invalidateCalls-startInvalidate;row["productionState"]=productionState();
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
 row["diagnosticState"]=diagnosticState();
#endif
 rows.push_back(row);
}
int main(){
 run("never-published-invalid-cache",true,[](json& row){const auto r=snapshot();checkFailed(r);check(g_msiDinput.canonicalSeq==0,"read cannot advance production seqlock");row["snapshotOk"]=r.ok;});
 run("published-static-all-zero-is-valid",true,[](json& row){publish({});const auto r=snapshot();check(r.ok,"zero payload is a successful publication");checkPad(r.out,{});check(g_msiDinput.canonicalValid&&g_msiDinput.canonicalSeq==2,"zero publication valid stable seqlock");checkStamp(r,1,17,10000);row["pad"]=padJson(r.out);});
 run("identical-payload-and-tick-still-publish",true,[](json& row){
  const auto p=payload();publish(p,10000);const auto first=snapshot();publish(p,10000);const auto second=snapshot();check(first.ok&&second.ok,"identical payload accepted twice");checkPad(second.out,p);
  check(g_msiDinput.canonicalSeq==4,"each actual publish advances production seq by two");checkStamp(first,1,17,10000);checkStamp(second,2,17,10000);row["samePayload"]=padJson(first.out)==padJson(second.out);
 });
 run("changed-payload-same-binding-roundtrip",true,[](json& row){auto p=payload();publish(p);p.wButtons=0x2040;p.bLeftTrigger=255;p.sThumbLX=-32768;publish(p,10001);const auto r=snapshot();check(r.ok,"changed payload read");checkPad(r.out,p);checkStamp(r,2,17,10001);row["pad"]=padJson(r.out);});
 run("even-valid-cache-stops-writing-snapshot-does-not-renew",true,[](json& row){
  const auto p=payload();publish(p,10000);const auto before=productionState();
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
  const auto sourceBefore=diagnosticState();
#endif
  json observations=json::array();
  for(ULONGLONG now:{10001ULL,100000ULL,86410000ULL}){observationTick=now;const auto r=snapshot();check(r.ok,"stopped even valid cache remains readable");checkPad(r.out,p);checkStamp(r,1,17,10000);check(productionState()==before,"snapshot must not increment seq or rewrite payload/valid");observations.push_back({{"observationTick",observationTick},{"ok",r.ok}});}
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
  check(diagnosticState()==sourceBefore,"snapshot cannot renew diagnostic ordinal generation or capture tick");row["source"]=stampJson(snapshot().stamp);
#endif
  row["observations"]=observations;
 });
 run("stable-even-valid-false-stops-reading",true,[](json& row){publish(payload());const auto seq=g_msiDinput.canonicalSeq.load();g_msiDinput.canonicalValid=false;const auto r=snapshot();checkFailed(r);check(g_msiDinput.canonicalSeq==seq,"valid flag control leaves seq stopped");row["snapshotOk"]=r.ok;});
 run("actual-invalidate-retains-bytes-but-snapshot-fails",true,[](json& row){
  publish(payload());const auto bt=g_msiDinput.canonicalBtnTrig.load(),seq=g_msiDinput.canonicalSeq.load();const auto th=g_msiDinput.canonicalThumbs.load();invalidate();
  check(!g_msiDinput.canonicalValid&&g_msiDinput.canonicalSeq==seq+2,"actual invalidation commits invalid state");check(g_msiDinput.canonicalBtnTrig==bt&&g_msiDinput.canonicalThumbs==th,"invalidator retains stored payload bytes");checkFailed(snapshot());row["retainedBytesAreAuthorized"]=false;
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
  check(g_msiDinput.canonicalReportSequence==1&&g_msiDinput.canonicalReportGeneration==17&&g_msiDinput.canonicalReportTick==10000,"invalidation is not a new diagnostic publish");
#endif
 });
 run("stopped-odd-seqlock-rejects-until-stable",true,[](json& row){publish(payload());++g_msiDinput.canonicalSeq;const auto seq=g_msiDinput.canonicalSeq.load();checkFailed(snapshot());check(g_msiDinput.canonicalSeq==seq,"bounded snapshot retries never finish writer");++g_msiDinput.canonicalSeq;const auto r=snapshot();check(r.ok,"stable control recovers read");checkPad(r.out,payload());checkStamp(r,1,17,10000);row["oddRejectedThenEvenReadable"]=true;});
 run("republish-zero-after-invalidation",true,[](json& row){publish({});invalidate();checkFailed(snapshot());publish({},10001);const auto r=snapshot();check(r.ok,"new zero publish restores validity");checkPad(r.out,{});check(g_msiDinput.canonicalSeq==6,"publish invalidate publish are three commits");checkStamp(r,2,17,10001);row["pad"]=padJson(r.out);});
 const std::array<XINPUT_GAMEPAD,2> edges={XINPUT_GAMEPAD{0xffff,0,255,-32768,32767,-1,0},XINPUT_GAMEPAD{0x8000,255,0,32767,-32768,1,-1}};
 for(unsigned i=0;i<edges.size();++i)run(i?"signed-axis-and-trigger-boundary-B":"signed-axis-and-trigger-boundary-A",true,[=](json& row){publish(edges[i]);const auto r=snapshot();check(r.ok,"boundary scalar roundtrip");checkPad(r.out,edges[i]);checkStamp(r,1,17,10000);row["pad"]=padJson(r.out);});
 run("production-seqlock-uint32-wrap-is-not-missing-source",true,[](json& row){g_msiDinput.canonicalSeq=std::numeric_limits<std::uint32_t>::max()-1;publish(payload());check(g_msiDinput.canonicalSeq==0&&g_msiDinput.canonicalValid,"stable seq may wrap to zero while valid");const auto r=snapshot();check(r.ok,"production seq zero not a no-source token");checkPad(r.out,payload());checkStamp(r,1,17,10000);row["snapshotOk"]=r.ok;});
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
 run("default-capture-tick-zero-does-not-fabricate-time",false,[](json& row){++publishCalls;msiDinputPublishCanonicalFrame(payload());const auto r=snapshot();check(r.ok,"payload validity independent of diagnostic tick");checkStamp(r,1,17,0);check(!r.stamp.valid,"missing tick only invalidates provenance");row["source"]=stampJson(r.stamp);});
 run("binding-generation-zero-does-not-invalidate-payload",false,[](json& row){g_msiDinput.bindCount=0;publish(payload());const auto r=snapshot();check(r.ok,"payload accepted even if source generation zero");checkStamp(r,1,0,10000);check(!r.stamp.valid,"missing generation yields invalid provenance");row["source"]=stampJson(r.stamp);});
 run("diagnostic-publish-sequence-zero-control",false,[](json& row){publish(payload());g_msiDinput.canonicalReportSequence=0;const auto r=snapshot();check(r.ok,"stable payload accepted with diagnostic ordinal zero");checkStamp(r,0,17,10000);check(!r.stamp.valid,"zero diagnostic ordinal cannot produce valid provenance");row["source"]=stampJson(r.stamp);});
 run("production-seq-diagnostic-ordinal-and-binding-distinct",false,[](json& row){g_msiDinput.canonicalSeq=40;g_msiDinput.canonicalReportSequence=(1ULL<<40);g_msiDinput.bindCount=73;publish(payload(),22222);const auto r=snapshot();check(r.ok,"separate counters valid");check(g_msiDinput.canonicalSeq==42,"32-bit seqlock advances independently");checkStamp(r,(1ULL<<40)+1,73,22222);check(r.stamp.sequence!=g_msiDinput.canonicalSeq&&r.stamp.generation!=g_msiDinput.canonicalSeq,"diagnostic values are not seqlock aliases");row["source"]=stampJson(r.stamp);});
 run("bindcount-change-without-publish-cannot-relabel-old-stamp",false,[](json& row){
  publish(payload(),10000);const auto old=snapshot();g_msiDinput.bindCount=99;const auto retained=snapshot();check(retained.ok,"snapshot alone does not enforce current bindCount");checkStamp(retained,1,17,10000);
  publish(payload(),10001);const auto newer=snapshot();checkStamp(newer,2,99,10001);checkStamp(old,1,17,10000);row["oldSource"]=stampJson(old.stamp);row["newSource"]=stampJson(newer.stamp);
 });
 run("optional-source-pointer-does-not-change-payload-or-counters",false,[](json& row){publish(payload());const auto p=productionState(),d=diagnosticState();XINPUT_GAMEPAD a=sentinel(),b=sentinel();snapshotCalls+=2;check(msiDinputCanonicalSnapshot(a),"default diagnosticSource=nullptr");check(msiDinputCanonicalSnapshot(b,nullptr),"explicit diagnosticSource=nullptr");checkPad(a,payload());checkPad(b,payload());check(productionState()==p&&diagnosticState()==d,"read path is observer only");row["pad"]=padJson(a);});
 run("source-stamp-validity-is-not-a-clock-age-or-future-gate",false,[](json& row){observationTick=10000;publish(payload(),999999999);const auto r=snapshot();check(r.ok,"snapshot does not reject supplied future capture tick");checkStamp(r,1,17,999999999);check(r.stamp.valid,"nonzero supplied tick marks provenance present only");row["observationTick"]=observationTick;row["source"]=stampJson(r.stamp);});
#endif
 unsigned failed=0,common=0;for(const auto& row:rows){failed+=!row.at("passed").get<bool>();common+=row.at("common").get<bool>();}
 const json abi={{"diagnostic",diagnostic},{"reportTickFieldPresent",HasReportTick<CanonicalFieldSubset>},{"reportSequenceFieldPresent",HasReportSequence<CanonicalFieldSubset>},{"reportGenerationFieldPresent",HasReportGeneration<CanonicalFieldSubset>},{"publishParameterCount",diagnostic?2:1},{"snapshotParameterCount",diagnostic?2:1}};
 std::cout<<json({{"passed",failed==0},{"caseCount",rows.size()},{"commonCases",common},{"checks",checks},{"failedCases",failed},{"publishCalls",publishCalls},{"snapshotCalls",snapshotCalls},{"invalidateCalls",invalidateCalls},{"abi",abi},{"cases",rows}}).dump(2)<<'\n';
 return failed?1:0;
}