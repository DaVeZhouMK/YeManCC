#define NOMINMAX
#include "touch_feedback_lease.h"
#include "msi_dinput_acquire_recovery.h"
#include "json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
using namespace ymcc::touchhaptic;
struct MockOps {
    struct Spec {unsigned reportLength=32;};
    struct Resource {bool pending=false,cancelled=false;unsigned reportLength=32;};
    enum Fault {None,OpenFailure,ShortWrite,StartFailure,Pending};
    inline static Fault fault=None;inline static bool completable=false,prematureClose=false;
    inline static std::uint64_t tick=0;inline static unsigned opens=0,writes=0,closes=0,cancels=0;
    inline static Resource* quarantined=nullptr;
    static std::uint64_t now()noexcept{return tick;}
    static Resource* open(const Spec& s,std::uint32_t& error){++opens;if(fault==OpenFailure){error=2;return nullptr;}return new Resource{false,false,s.reportLength};}
    static bool pending(const Resource& r)noexcept{return r.pending;}
    static void cancel(Resource& r)noexcept{if(r.pending&&!r.cancelled){r.cancelled=true;++cancels;}}
    static bool reap(Resource& r)noexcept{if(r.pending&&!completable)return false;r.pending=false;return true;}
    static void close(Resource* r)noexcept{prematureClose|=r->pending;++closes;delete r;}
    static void quarantine(Resource* r)noexcept{quarantined=r;}
    static Receipt write(Resource& r,unsigned,unsigned){++writes;Receipt result;result.attempted=true;
        if(fault==Pending){r.pending=true;result.reason=Reason::IoPending;result.error=996;}
        else if(fault==ShortWrite){result.reason=Reason::WriteFailed;result.error=29;result.written=r.reportLength-1;}
        else if(fault==StartFailure){result.reason=Reason::WriteFailed;result.error=5;}
        else{result.ok=true;result.written=r.reportLength;}return result;}
    static void reset(){fault=None;completable=false;prematureClose=false;tick=0;opens=writes=closes=cancels=0;quarantined=nullptr;}
};
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;using Json=nlohmann::json;Json cases=Json::array();bool ok=true;
    auto check=[&](const char* name,bool passed){ok&=passed;cases.push_back({{"name",name},{"ok",passed}});};
    const SourceToken source{Backend::Msi,1,3,7,4};
    MockOps::reset();
    {FeedbackLease<MockOps> lease;bool stable=true;for(int i=0;i<10000;++i){
        stable&=lease.write(source,{32},51,51,[]{return true;}).ok;
        stable&=lease.write(source,{32},0,0,[]{return true;}).ok;}
        check("ten-thousand-pulses-one-resource-no-per-write-open",stable&&MockOps::opens==1&&MockOps::writes==20000&&lease.stats.reuses==19999);
        auto next=source;++next.generation;check("new-source-revalidates-report-capability",lease.write(next,{64},77,77,[]{return true;}).written==64&&MockOps::opens==2&&MockOps::closes==1);
        ++next.power;check("new-power-never-reuses-old-resource",lease.write(next,{48},26,26,[]{return true;}).written==48&&MockOps::opens==3&&MockOps::closes==2);
        ++next.identity;check("new-mode-never-reuses-old-resource",lease.write(next,{32},51,51,[]{return true;}).ok&&MockOps::opens==4);
        const auto writes=MockOps::writes;auto denied=lease.write(next,{32},51,51,[]{return false;});
        check("write-authority-rechecked-after-resource-acquisition",!denied.ok&&denied.reason==Reason::SourceChanged&&MockOps::writes==writes);
        check("repeated-invalidation-is-idempotent",lease.invalidate()&&lease.invalidate()&&!MockOps::prematureClose);
    }
    MockOps::reset();
    {FeedbackLease<MockOps> lease;auto denied=lease.write({}, {32},51,51,[]{return true;});
        check("no-source-never-opens-resource",denied.reason==Reason::SourcePending&&MockOps::opens==0);
        MockOps::fault=MockOps::OpenFailure;auto failed=lease.write(source,{32},51,51,[]{return true;});
        for(int i=0;i<10000;++i)lease.write(source,{32},51,51,[]{return true;});
        check("open-failure-ten-thousand-requests-one-open-attempt",failed.reason==Reason::OpenFailed&&MockOps::opens==1);
        MockOps::tick=250;lease.write(source,{32},51,51,[]{return true;});MockOps::tick=749;lease.write(source,{32},51,51,[]{return true;});
        check("same-source-open-failure-exponential-backoff",MockOps::opens==2);
        MockOps::tick=750;lease.write(source,{32},51,51,[]{return true;});check("bounded-retry-opportunity-at-deadline",MockOps::opens==3);
        auto next=source;++next.generation;MockOps::fault=MockOps::None;
        check("fresh-source-bypasses-previous-generation-backoff",lease.write(next,{32},51,51,[]{return true;}).ok&&MockOps::opens==4);
    }
    for(auto fault:{MockOps::ShortWrite,MockOps::StartFailure}){
        MockOps::reset();FeedbackLease<MockOps> lease;MockOps::fault=fault;
        auto failed=lease.write(source,{32},51,51,[]{return true;});check(fault==MockOps::ShortWrite?"short-write-is-explicit-failure":"start-failure-is-explicit-failure",!failed.ok&&failed.attempted&&failed.reason==Reason::WriteFailed);
        MockOps::fault=MockOps::None;const auto retry=lease.write(source,{32},51,51,[]{return true;});
        check("failed-start-cannot-reopen-on-every-move",!retry.ok&&MockOps::opens==1);
        check("safety-zero-stop-bypasses-backoff-not-authority",lease.write(source,{32},0,0,[]{return true;}).ok&&MockOps::opens==2);
    }
    MockOps::reset();
    {FeedbackLease<MockOps> lease;MockOps::fault=MockOps::Pending;
        const auto failed=lease.write(source,{32},51,51,[]{return true;});auto next=source;++next.generation;
        for(int i=0;i<10000;++i)lease.write(next,{32},77,77,[]{return true;});
        check("inflight-cancellation-retains-one-buffer-and-handle",failed.reason==Reason::IoPending&&lease.resource()!=nullptr&&MockOps::opens==1&&MockOps::closes==0&&MockOps::cancels==1);
        check("retired-io-refuses-new-generation-without-extra-writes",MockOps::writes==1&&!MockOps::prematureClose);
        MockOps::fault=MockOps::None;MockOps::completable=true;MockOps::tick=5000;
        check("completion-harvest-before-fresh-source-open",lease.write(next,{32},77,77,[]{return true;}).ok&&MockOps::closes==1&&MockOps::opens==2);
    }
    MockOps::reset();
    {FeedbackLease<MockOps> lease;MockOps::fault=MockOps::Pending;lease.write(source,{32},51,51,[]{return true;});}
    check("teardown-never-frees-inflight-overlapped-memory",MockOps::quarantined!=nullptr&&!MockOps::prematureClose&&MockOps::closes==0);
    if(MockOps::quarantined){MockOps::quarantined->pending=false;MockOps::close(MockOps::quarantined);MockOps::quarantined=nullptr;}
    FailureLogGate logGate;unsigned logRows=0;
    for(int i=0;i<10000;++i)logRows+=logGate.publish(true,false,source,Reason::OpenFailed,2)?1:0;
    check("ten-thousand-same-failures-one-detailed-log-row",logRows==1);
    check("new-failure-error-is-visible",logGate.publish(true,false,source,Reason::OpenFailed,5));
    auto changed=source;++changed.generation;check("new-source-failure-is-visible",logGate.publish(true,false,changed,Reason::OpenFailed,5));
    check("successful-touch-clears-failure-edge-without-detail-row",!logGate.publish(true,true,changed,Reason::None,0));
    check("failure-after-recovery-is-visible",logGate.publish(true,false,changed,Reason::WriteFailed,29));
    check("manual-feedback-keeps-detailed-receipt",logGate.publish(false,true,changed,Reason::None,0));
    check("manual-success-resets-touch-failure-edge",logGate.publish(true,false,changed,Reason::WriteFailed,29));
    Summary summary;Receipt failed;failed.reason=Reason::WriteFailed;failed.error=29;summary.record(failed);const auto counts=summary.take();
    check("pod-summary-keeps-last-error-with-reason",counts.failed==1&&counts.reasons[(size_t)Reason::WriteFailed]==1&&counts.errors[(size_t)Reason::WriteFailed]==29);
    ymcc::msi::AcquireRecovery recovery;recovery.begin(100,7,11,12);
    check("input-lost-first-reacquire-due-without-reenumeration",recovery.due(100,7,11,12));recovery.attempted(100);
    bool noBusyRetry=true;for(int i=0;i<10000;++i)noBusyRetry&=!recovery.due(100+i%250,7,11,12);
    check("input-lost-no-per-tick-acquire",noBusyRetry&&recovery.attempts==1&&recovery.nextDue==350);
    recovery.begin(200,8,99,99);check("duplicate-loss-cannot-reset-budget-or-generation",recovery.attempts==1&&recovery.power==7&&recovery.topology==11&&recovery.mode==12);
    check("reacquire-power-fence",recovery.stale(8,11,12)&&!recovery.due(350,8,11,12));
    check("reacquire-topology-fence",recovery.stale(7,12,12)&&!recovery.due(350,7,12,12));
    check("reacquire-mode-fence",recovery.stale(7,11,13)&&!recovery.due(350,7,11,13));
    for(unsigned i=1;i<ymcc::msi::AcquireRecovery::budget;++i){const auto now=100+i*250;check("finite-reacquire-deadline",recovery.due(now,7,11,12));recovery.attempted(now);}
    check("eight-attempt-budget-cannot-loop-forever",recovery.exhausted()&&!recovery.due(100000,7,11,12));
    recovery.reset();check("valid-sample-or-release-clears-recovery",!recovery.active&&recovery.attempts==0);
    std::ofstream out(std::filesystem::path(argv[1]),std::ios::binary);out<<Json{{"ok",ok},{"hardwareWrites",0},{"systemInputInjected",false},{"cases",cases}}.dump(2);
    std::cout<<"TOUCH_FEEDBACK_LEASE_ROBUST_OK="<<ok<<" checks="<<cases.size()<<" hardwareWrites=0\n";return ok&&out.good()?0:3;
}
