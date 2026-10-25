// R36 actual-source elapsed scope; pure clock/last-error stand-ins, no OS input APIs.
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <array>
using DWORD = unsigned long;
static std::atomic<bool> g_domainLogEnabled[2];
static thread_local DWORD lastError=0;
static thread_local unsigned long long clockReads=0,errorReads=0,errorWrites=0;
static thread_local long long clockUs=0;
static DWORD GetLastError() noexcept {++errorReads;return lastError;}
static void SetLastError(DWORD value) noexcept {++errorWrites;lastError=value;}
struct FixtureClock {
    using time_point=std::chrono::steady_clock::time_point;
    static time_point now() noexcept {
        ++clockReads;clockUs+=10;lastError=0xDEAD;
        return time_point{}+std::chrono::microseconds(clockUs);
    }
};
#include "actual-scope.hpp"
static std::atomic<unsigned long long> checks{0};
static void check(bool ok,const char* detail){checks.fetch_add(1);if(!ok)throw std::runtime_error(detail);}
static void reset(bool gate){g_domainLogEnabled[1]=gate;clockReads=errorReads=errorWrites=0;clockUs=0;lastError=99;for(int i=0;i<kTickCostStageCount;++i){g_tickCostSumUs[i]=0;g_tickCostCount[i]=0;g_tickCostMaxUs[i]=0;}}
static void ordinary(){
    reset(false);
    {TickPreambleElapsedScope s(kTickCostPreamblePhysicalRead);s.finish();s.finish();}
    check(clockReads==0&&errorReads==0&&errorWrites==0,"disabled scope performed observation");
    check(lastError==99&&g_tickCostCount[kTickCostPreamblePhysicalRead]==0,"disabled scope changed state");
    reset(true);
    {
        TickPreambleElapsedScope s(kTickCostPreamblePhysicalRead);
        check(clockReads==1&&lastError==99,"constructor must preserve last-error");
        lastError=77;s.finish();
        check(clockReads==2&&lastError==77,"finish must preserve current last-error");
        check(g_tickCostCount[kTickCostPreamblePhysicalRead]==1&&g_tickCostSumUs[kTickCostPreamblePhysicalRead]==10&&g_tickCostMaxUs[kTickCostPreamblePhysicalRead]==10,"one completed span expected");
        s.finish();check(clockReads==2,"repeated finish observed twice");lastError=88;
    }
    check(lastError==88&&clockReads==2&&errorReads==2&&errorWrites==2,"destructor after finish changed newer state");
    reset(true);
    try{TickPreambleElapsedScope s(kTickCostPreambleOemPumps);lastError=71;throw std::runtime_error("original-action");}
    catch(const std::runtime_error& e){check(std::string(e.what())=="original-action","diagnostic changed action exception");}
    check(lastError==71&&g_tickCostCount[kTickCostPreambleOemPumps]==1,"unwind did not retain action last-error/count");
    reset(true);
    {TickPreambleElapsedScope s(kTickCostPreamblePowerGuard);g_domainLogEnabled[1]=false;s.finish();}
    check(clockReads==2&&g_tickCostCount[kTickCostPreamblePowerGuard]==0,"gate-off completion must not accumulate");
    reset(false);
    {TickPreambleElapsedScope s(kTickCostPreamblePowerGuard);g_domainLogEnabled[1]=true;s.finish();}
    check(clockReads==0&&g_tickCostCount[kTickCostPreamblePowerGuard]==0,"gate-on must not invent earlier start");
    reset(true);
    {TickPreambleElapsedScope outer(kTickCostPreamblePowerGuard);{TickPreambleElapsedScope inner(kTickCostPreambleOemPumps);}}
    check(g_tickCostSumUs[kTickCostPreambleOemPumps]==10&&g_tickCostSumUs[kTickCostPreamblePowerGuard]==30,"nested scopes shared a start");
    reset(true);
    for(int i=18;i<24;++i){TickPreambleElapsedScope s(static_cast<TickCostStage>(i));}
    for(int i=0;i<24;++i)check(g_tickCostCount[i]==(i<18?0:1),"old stage identity or new stage dispatch changed");
    const std::array<const char*,24> expected={"frameBuildUs","receiptValidateUs","sampleLogUs","uiTelemetryUs","tickTotalUs","tickPreambleUs","preHostUs","pipeWriteUs","pipeReadUs","hostSubmitUs","preHostMetaUs","preHostDsuUs","preHostSampleQueueUs","preHostMotionPlaneUs","preHostMiscUs","preHostCpuUs","sampleLogCpuUs","preambleCpuUs","preamblePhysicalReadUs","preambleWindowUs","preamblePowerGuardUs","preambleOemPumpsUs","preambleHostReconcileUs","preambleFrameSetupUs"};
    check(kTickCostStageCount==24,"stage count mismatch");
    for(int i=0;i<24;++i)check(kTickCostStageNames[i]&&std::string(kTickCostStageNames[i])==expected[i],"stage name/index mismatch");
}
static void concurrency(){
    reset(true);std::array<std::exception_ptr,4> errors{};std::array<std::thread,4> threads;
    for(unsigned i=0;i<threads.size();++i)threads[i]=std::thread([&,i]{try{
        clockUs=0;clockReads=0;lastError=100+i;
        for(int n=0;n<10000;++n){TickPreambleElapsedScope s(kTickCostPreambleHostReconcile);check(lastError==100+i,"cross-thread last-error");s.finish();check(lastError==100+i,"cross-thread finish last-error");}
        check(clockReads==20000,"thread-local scope clock count");
    }catch(...){errors[i]=std::current_exception();}});
    for(auto& thread:threads)thread.join();for(auto e:errors)if(e)std::rethrow_exception(e);
    check(g_tickCostCount[kTickCostPreambleHostReconcile]==40000,"lost concurrent samples");
    check(g_tickCostSumUs[kTickCostPreambleHostReconcile]==400000&&g_tickCostMaxUs[kTickCostPreambleHostReconcile]==10,"concurrent elapsed attribution changed");
}
int main(){try{
    static_assert(!std::is_copy_constructible_v<TickPreambleElapsedScope>);
    static_assert(!std::is_move_constructible_v<TickPreambleElapsedScope>);
    static_assert(std::is_nothrow_constructible_v<TickPreambleElapsedScope,TickCostStage>);
    static_assert(std::is_nothrow_destructible_v<TickPreambleElapsedScope>);
    ordinary();concurrency();
    std::cout<<"{\"pass\":true,\"checks\":"<<checks.load()<<",\"sourceScopeExtracted\":true,\"stageCount\":24,\"concurrentSamples\":40000,\"realInputOrProductCalls\":false,\"clockAndLastErrorAreModels\":true}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
