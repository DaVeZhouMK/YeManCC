// Real production sink and readback/admission, explicit in-memory reports only.
#define NOMINMAX
#include <windows.h>
#include <winrt/Windows.Devices.Sensors.h>
#include <atomic>
#include <barrier>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <json.hpp>
using json=nlohmann::json;
// @@CONSTANTS@@
// @@FUNCTIONS@@
// @@SINK@@
static std::shared_ptr<InputCaptureSensorSink> g_inputCaptureSink;
struct Snapshot {
    bool gyro=false,accel=false;
    double gx=0,gy=0,gz=0,ax=0,ay=0,az=0;
    UINT64 gg=0,ag=0,gs=0,as=0;
};
static Snapshot readActualSnapshot() {
    bool gyro=false,accel=false;
    double gx=0,gy=0,gz=0,ax=0,ay=0,az=0;
    UINT64 gyroBindingGeneration=0,accelBindingGeneration=0;
    UINT64 gyroRejectedSamples=0,accelRejectedSamples=0;
    UINT64 gyroTimestamp100ns=0,accelTimestamp100ns=0,gyroSequence=0,accelSequence=0;
    ULONGLONG gyroReceiptTick=0,accelReceiptTick=0;
    double gyroTimeOfDayMs=0,accelTimeOfDayMs=0;
// @@SNAPSHOT@@
    return {gyro,accel,gx,gy,gz,ax,ay,az,gyroBindingGeneration,accelBindingGeneration,gyroSequence,accelSequence};
}
static bool actualAdmitted(const Snapshot& s) {
    return inputCaptureMotionPairAdmitted(true,true,s.gyro,s.accel,true,s.gg,s.ag,true,
        g_inputCaptureSink->activeBindingGeneration.load(std::memory_order_acquire));
}
static unsigned long long checks=0,failed=0,paths=0;
static json failureGroups=json::object(),failures=json::array();
static std::string current;
static void check(bool good,const char* group,const char* detail) {
    ++checks;if(good)return;++failed;failureGroups[group]=failureGroups.value(group,0u)+1;
    if(failures.size()<60)failures.push_back({{"case",current},{"group",group},{"detail",detail}});
}
static void gyro(UINT64 generation,double value=1) {
    g_inputCaptureSink->OnRawGyro(value,2,3,134361240000000000ull+generation,100+generation,generation);
}
static void accel(UINT64 generation,double value=4) {
    g_inputCaptureSink->OnRawAccel(value,5,6,134361240000000000ull+generation,100+generation,generation);
}
static void seed(UINT64 generation) {
    g_inputCaptureSink=std::make_shared<InputCaptureSensorSink>();
    g_inputCaptureSink->SetBindingGeneration(generation);gyro(generation);accel(generation);
    check(actualAdmitted(readActualSnapshot()),"positive","new current gyro and accel form an admitted pair");
}
static void namedCases() {
    current="direct-binding-change";seed(11);
    g_inputCaptureSink->SetBindingGeneration(12);
    auto s=readActualSnapshot();
    check(!s.gyro&&!s.accel,"binding-validity","new binding has no observed channels before its first report");
    check(!actualAdmitted(s),"binding-admission","old six-axis values cannot be relabeled as a new admitted pair");
    gyro(12,12);s=readActualSnapshot();
    check(s.gyro&&!s.accel&&!actualAdmitted(s),"binding-partial","one fresh channel cannot pair with retired other channel");
    accel(12,12);s=readActualSnapshot();check(actualAdmitted(s)&&s.gx==12&&s.ax==12,"positive","both fresh channels resume the same sink");
    const auto gs=s.gs,as=s.as;
    gyro(11,999);accel(11,999);s=readActualSnapshot();
    check(s.gx==12&&s.ax==12&&s.gs==gs&&s.as==as,"late-callback","retired callbacks cannot overwrite or advance current channels");
    current="stop-fallback-transition";seed(21);g_inputCaptureSink->SetBindingGeneration(0);
    s=readActualSnapshot();check(!s.gyro&&!s.accel&&!actualAdmitted(s),"binding-stop","stop retires sample visibility immediately");
    g_inputCaptureSink->SetBindingGeneration(23);s=readActualSnapshot();
    check(!s.gyro&&!s.accel&&!actualAdmitted(s),"binding-fallback","failed-subscription fallback does not resurrect the previous pair");
    accel(23,23);s=readActualSnapshot();check(!s.gyro&&s.accel&&!actualAdmitted(s),"binding-partial","fresh accel alone leaves gyro unavailable");
    gyro(23,23);s=readActualSnapshot();check(actualAdmitted(s),"positive","new fallback pair admits only after both current callbacks");
    current="new-accel-baseline";seed(27);g_inputCaptureSink->SetBindingGeneration(28);
    accel(28,100);
    check(g_inputCaptureSink->accelMotionEpisode==0,"binding-first-accel","first fresh accel value is a baseline, not an old-to-new motion jump");
    current="idempotent-binding";seed(31);const auto before=readActualSnapshot();
    g_inputCaptureSink->SetBindingGeneration(31);s=readActualSnapshot();
    check(actualAdmitted(s)&&s.gs==before.gs&&s.as==before.as&&s.gx==before.gx&&s.ax==before.ax,
        "idempotence","same live generation preserves already observed input");
    current="cross-binding-motion-evidence";seed(41);
    gyro(41,kInputCaptureHighMagDps+1);accel(41,20);
    const auto ge=g_inputCaptureSink->gyroHighMotionEpisode,ae=g_inputCaptureSink->accelMotionEpisode;
    const auto gt=g_inputCaptureSink->gyroHighMotionLastTick,at=g_inputCaptureSink->accelMotionLastTick;
    g_inputCaptureSink->SetBindingGeneration(42);
    check(g_inputCaptureSink->gyroHighMotionEpisode==ge&&g_inputCaptureSink->accelMotionEpisode==ae&&
        g_inputCaptureSink->gyroHighMotionLastTick==gt&&g_inputCaptureSink->accelMotionLastTick==at,
        "motion-evidence","retirement preserves existing cross-binding motion episodes");
    accel(42,1);check(g_inputCaptureSink->accelMotionEpisode==ae,"motion-evidence","first accel report establishes baseline without an artificial motion episode");
    g_inputCaptureSink->ResetSamples();s=readActualSnapshot();
    check(!s.gyro&&!s.accel&&!actualAdmitted(s)&&g_inputCaptureSink->activeBindingGeneration.load()==0,
        "reset","explicit reset still retires binding and samples");
}
static void modelCases() {
    // External contract: only reports delivered in the selected, nonzero binding
    // make a channel observable. Rebinding itself is never an observation.
    for(unsigned encoded=0;encoded<4096;++encoded) {
        current="transition-path-"+std::to_string(encoded);seed(1);
        UINT64 selected=1,next=1,retired=0;bool seenG=true,seenA=true;
        unsigned code=encoded;
        for(unsigned step=0;step<4;++step,code/=8) {
            const unsigned op=code%8;
            if(op==0){if(selected)retired=selected;selected=++next;seenG=seenA=false;g_inputCaptureSink->SetBindingGeneration(selected);}
            if(op==1)g_inputCaptureSink->SetBindingGeneration(selected);
            if(op==2||op==3){if(selected)retired=selected;selected=0;seenG=seenA=false;
                if(op==2)g_inputCaptureSink->SetBindingGeneration(0);else g_inputCaptureSink->ResetSamples();}
            if(op==4&&selected){gyro(selected,static_cast<double>(selected));seenG=true;}
            if(op==5&&selected){accel(selected,static_cast<double>(selected));seenA=true;}
            if(op==6&&retired)gyro(retired,999);
            if(op==7&&retired)accel(retired,999);
            const auto s=readActualSnapshot();
            check(s.gyro==seenG&&s.accel==seenA,"binding-model-validity","observable channels require current-binding reports");
            check(actualAdmitted(s)==(selected!=0&&seenG&&seenA),"binding-model-admission","pair admission agrees with independently tracked observed reports");
            if(selected&&seenG)check(s.gg==selected&&s.gx!=999,"late-callback","visible gyro belongs to selected binding");
            if(selected&&seenA)check(s.ag==selected&&s.ax!=999,"late-callback","visible accel belongs to selected binding");
        }
        ++paths;
    }
}
static void queuedDeliveryCases() {
    for(unsigned i=0;i<16;++i) {
        current="late-two-thread-delivery-"+std::to_string(i);seed(100+i*2);
        const UINT64 old=100+i*2,next=old+1;
        std::barrier ready(3),release(3);
        std::thread g([&]{ready.arrive_and_wait();release.arrive_and_wait();gyro(old,999);});
        std::thread a([&]{ready.arrive_and_wait();release.arrive_and_wait();accel(old,999);});
        ready.arrive_and_wait();g_inputCaptureSink->SetBindingGeneration(next);release.arrive_and_wait();
        g.join();a.join();const auto s=readActualSnapshot();
        check(!s.gyro&&!s.accel&&!actualAdmitted(s),"binding-queued-delivery","late reports from both retired callbacks cannot revive a new binding");
        gyro(next,12);accel(next,12);check(actualAdmitted(readActualSnapshot()),"positive","current reports recover after late delivery");
    }
}
int main() {
    namedCases();modelCases();queuedDeliveryCases();g_inputCaptureSink.reset();
    std::cout<<json({{"status",failed?"fail":"pass"},{"checks",checks},{"failureCount",failed},
        {"failureGroups",failureGroups},{"failures",failures},{"transitionPaths",paths},{"lateDeliverySchedules",16},
        {"actualProductionSink",true},{"actualSnapshotBlock",true},{"actualPairGate",true},
        {"realWinRtCallbacksInvoked",false},{"hardwareIo",false},{"hostExecuted",false}}).dump()<<'\n';
    return failed?1:0;
}
