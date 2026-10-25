#include "../native/msi_target_intent_evidence.h"
#include <iostream>
#include <thread>
#include <vector>
using namespace ymcc::msi::intentEvidence;
int main(){unsigned cases=0;auto check=[&](bool x,const char* n){++cases;if(!x){std::cerr<<"FAIL "<<n<<"\n";std::exit(1);}};
    for(unsigned mask=0;mask<8;++mask)check(enabled(mask&1,mask&2,mask&4)==(mask==7),"family/build/detail three-gate");
    const Json saved={{"revision",20},{"outputTarget",{{"persona","steamdeck"},{"buttonMappingEnabled",true},{"gyroEnabled",true}}}};
    Json off=saved;off["outputTarget"]={{"persona","disabled"},{"buttonMappingEnabled",false},{"gyroEnabled",false}};
    auto on=observation(saved,saved,false,false),runtime=observation(saved,off,true,false),overlay=observation(saved,off,false,true);
    check(on["savedEffectiveTargetEqual"]==true,"saved and effective equal");
    check(runtime["savedEffectiveTargetEqual"]==false&&runtime["temporaryShortcutActive"]==true,"runtime shortcut delta is not a persisted write");
    check(overlay["gameOverrideActive"]==true&&overlay["isSettingsWriteReceipt"]==false,"game overlay is observation not writer");
    for(const auto& r:{on,runtime,overlay})check(r["humanGestureProven"]==false&&r["writesControls"]==false,"no fabricated gesture or control authority");
    check(target(Json())["persona"].is_null(),"missing input does not invent disabled");
    check(target(Json{{"outputTarget",{{"persona",42},{"gyroEnabled","yes"}}}})["persona"].is_null(),"wrong string type stays unknown");
    check(target(Json{{"outputTarget",{{"persona",std::string(9000,'X')}}}})["persona"].is_null(),"unbounded text not logged");
    check(revision(Json{{"revision",-1}}).is_null()&&revision(Json{{"revision",9007199254740992ULL}}).is_null(),"invalid revision unknown");
    const auto committed=documentWrite(off,saved,off,true,false,-1,20);
    check(committed["sourceHandlerProven"]==true&&committed["humanGestureProven"]==false,"IPC handler proof is not human click proof");
    check(committed["requestBaselineIsAuthoritativeBefore"]==false,"request baseline not current-before proof");
    const auto rejected=documentWrite(off,saved,saved,false,true,-1,20);
    check(rejected["writeOk"]==false&&rejected["inputRevisionConflict"]==true&&rejected["committedTarget"]["persona"]=="steamdeck","rejected stale request not fake committed off");
    const auto protectedWrite=documentWrite(off,saved,saved,true,false,19,-1);
    check(protectedWrite["staleInputDropped"]==true&&protectedWrite["committedTarget"]["persona"]=="steamdeck","guarded old input not caller-owned write");
    BoundedObserver b;check(b.take(false,on,true).is_null(),"closed gate emits nothing");
    check(b.take(true,on,true)["receiptSequence"]==1,"first saved/effective receipt");
    check(b.take(true,on,true).is_null(),"unchanged state no loop spam");
    Json onlyRev=on;onlyRev["savedInputRevision"]=21;check(b.take(true,onlyRev,true).is_null(),"unrelated revision/ACK does not spam target evidence");
    check(b.take(true,runtime,true)["receiptSequence"]==2,"runtime target transition observed");
    BoundedObserver cap;for(uint64_t i=0;i<BoundedObserver::maxRows;++i)check(!cap.take(true,Json{{"i",i}},false).is_null(),"bounded receipt delivered");
    check(cap.take(true,on,false).is_null(),"row cap no unbounded spam");
    check(cap.statistics()["capReached"]==true&&cap.statistics()["droppedForCap"]==1&&cap.statistics()["reservedRowsAreDurableLogProof"]==false,"coverage explains cap not silent absence proof");
    BoundedObserver concurrent;std::vector<std::thread> ts;for(unsigned t=0;t<4;++t)ts.emplace_back([&]{for(unsigned i=0;i<100;++i)(void)concurrent.take(true,on,false);});for(auto& t:ts)t.join();
    check(concurrent.take(true,runtime,false).is_null(),"concurrent callers respect per-process cap");
    check(saved["outputTarget"]["persona"]=="steamdeck"&&off["outputTarget"]["persona"]=="disabled","input JSON never mutated");
    std::cout<<"MSI_INTENT_EVIDENCE_PURE_OK cases="<<cases<<" controllerIO=0 settingsWrites=0\n";
}
