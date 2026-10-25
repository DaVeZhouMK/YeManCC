#include "json.hpp"
#include "legion_motion_source.h"
#include "legion_motion_message_clock.h"
#include "virtual_output_status.h"
#include <atomic>
#include <functional>
#include <iostream>
#include <map>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
using json=nlohmann::json;
using DWORD=std::uint32_t;
using ULONGLONG=std::uint64_t;
using uint64_t=std::uint64_t;
enum class YmccFamily {LenovoLegionGo,Asus,Valve,Other};
#include "production-types.generated.h"
struct MachineIdentity {YmccFamily family=YmccFamily::LenovoLegionGo;std::string deviceClass;};
struct BoundIdentity {std::string stableIdentity;};
using HidHidePhysicalIdentity=BoundIdentity;
static MachineIdentity g_machineIdentity;
static BoundIdentity g_hidHidePhysicalIdentity;
static std::mutex g_hidHidePhysicalIdentityMx,g_legionImuMx;
static std::atomic<std::uint64_t> g_powerGeneration{1},g_hidResolveCacheGeneration{2},g_legionMotionCaptureEpoch{3},g_legionMotionEpochStartedAt{8000};
static std::atomic<unsigned> g_legionMotionSensorIndex{0};
static std::atomic<bool> g_inputCaptureStop{false},g_legionRawInputSubscribed{true};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static bool g_inputCaptureReady=true;
static ymcc::legion_motion::Lease g_legionMotion;
static std::string legionMotionStableIdentity(const BoundIdentity& identity) { return identity.stableIdentity; }
static std::uint64_t currentPowerGeneration() {return g_powerGeneration.load();}
namespace boundary {
    enum class Failure {None,FirstClock,SecondClock,Timestamp,Directory,Writer};
    Failure failure=Failure::None;
    std::uint64_t observedAt=10000,completedAt=10000;
    unsigned clockReads=0,writeCalls=0,timestampCalls=0,directoryCalls=0;
    std::function<void()> afterRead;
    DWORD lastError=73;
    bool writeSucceeds=true;
    std::map<std::wstring,std::string> files;
    std::vector<std::wstring> writes;
    const std::wstring directory=L"C:\\memory-only-controller-evidence";
}
static DWORD GetLastError() {return boundary::lastError;}
static void SetLastError(DWORD value) {boundary::lastError=value;}
static ULONGLONG GetTickCount64() {
    const auto n=++boundary::clockReads;
    SetLastError(901);
    if((n==1&&boundary::failure==boundary::Failure::FirstClock)||(n==2&&boundary::failure==boundary::Failure::SecondClock))throw std::runtime_error("clock boundary");
    if(n==2&&boundary::afterRead)boundary::afterRead();
    return n==1?boundary::observedAt:boundary::completedAt;
}
static std::string nativeLifecycleTimestamp() {
    ++boundary::timestampCalls;SetLastError(902);
    if(boundary::failure==boundary::Failure::Timestamp)throw std::runtime_error("timestamp boundary");
    return "2026-10-17T00:00:00Z-controlled";
}
static std::wstring yemancc_data_dir() {
    ++boundary::directoryCalls;SetLastError(903);
    if(boundary::failure==boundary::Failure::Directory)throw std::runtime_error("directory boundary");
    return boundary::directory;
}
static bool sgWriteFileAtomic(const std::wstring& path,const std::string& text) {
    ++boundary::writeCalls;boundary::writes.push_back(path);SetLastError(904);
    if(boundary::failure==boundary::Failure::Writer)throw std::runtime_error("atomic writer boundary");
    if(!boundary::writeSucceeds)return false;
    boundary::files[path]=text;return true;
}
#include "production-functions.generated.h"

namespace {
int checks=0,failures=0;
std::string currentCase;
void expect(bool value,const char* message) {
    ++checks;if(!value){++failures;std::cout<<"FAIL "<<currentCase<<": "<<message<<'\n';}
}
#define EXPECT(expr) expect(static_cast<bool>(expr),#expr)
const std::string tablet="HandheldCompanion.Devices.LegionGoTablet";
const std::wstring file=boundary::directory+L"\\controller-motion-evidence.json";
const json configuration={{"input",{{"gyroMotion",{{"controllerSensorIndex",0},{"enabled",true}}}}},{"windowsCalibration",{{"id","unchanged"}}}};
const std::vector<std::string> legionModels={tablet,"HandheldCompanion.Devices.LegionGoTablet2","HandheldCompanion.Devices.LegionGoSZ1","HandheldCompanion.Devices.LegionGoSZ2"};
void reset(const std::string& model=tablet) {
    g_machineIdentity={YmccFamily::LenovoLegionGo,model};g_hidHidePhysicalIdentity.stableIdentity="controller/lenovo/6127/24962/physical-A";
    g_powerGeneration=1;g_hidResolveCacheGeneration=2;g_legionMotionCaptureEpoch=3;g_legionMotionEpochStartedAt=8000;g_legionMotionSensorIndex=0;
    g_inputCaptureStop=false;g_legionRawInputSubscribed=true;g_inputCaptureReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_legionMotion.revoke();
    boundary::failure=boundary::Failure::None;boundary::observedAt=boundary::completedAt=10000;boundary::clockReads=boundary::writeCalls=boundary::timestampCalls=boundary::directoryCalls=0;
    boundary::afterRead={};boundary::lastError=73;boundary::writeSucceeds=true;boundary::files.clear();boundary::writes.clear();
}
ymcc::legion_motion::Context context() {return {g_powerGeneration.load(),g_hidResolveCacheGeneration.load()};}
ymcc::legion_motion::Source source() {return legionMotionSourceForSelectedLocked();}
ymcc::legion_motion::Sample sample(std::uint64_t receivedAt=9900) {return {{0,0,0},{0,0,0},receivedAt,context(),source()};}
void publish(std::uint64_t receivedAt=9900) {EXPECT(g_legionMotion.publish(sample(receivedAt),context(),source(),true));}
bool same(const std::optional<ymcc::legion_motion::Sample>& a,const std::optional<ymcc::legion_motion::Sample>& b) {
    if(a.has_value()!=b.has_value())return false;
    if(!a)return true;
    return a->gyro==b->gyro&&a->accel==b->accel&&a->capturedAt==b->capturedAt&&a->context.power==b->context.power&&
        a->context.rawGeneration==b->context.rawGeneration&&a->source.physicalIdentity==b->source.physicalIdentity&&
        a->source.captureEpoch==b->source.captureEpoch&&a->source.sensorIndex==b->source.sensorIndex;
}
bool ready() {return g_inputCaptureReady&&!g_inputCaptureStop.load()&&g_legionRawInputSubscribed.load()&&g_powerLifecycle==PowerLifecycle::Ready;}
std::optional<ymcc::legion_motion::Sample> current(std::uint64_t now=10000) {return g_legionMotion.current(context(),source(),ready(),now,1500);}
void readOnlyTruth(const json& result) {
    const auto& r=result.at("runtime");
    EXPECT(result.at("physicalAcceptanceVerified")==false);EXPECT(r.at("readOnly")==true);
    EXPECT(r.at("scope")=="legion-controller-source-only");
    EXPECT(r.at("schemaVersion")==1);EXPECT(r.at("observedAtUptimeMs")==boundary::observedAt);
    EXPECT(r.at("hostSubmission")=="not-observed-by-this-query");EXPECT(r.at("gameConsumption")=="not-observed-by-this-query");
}
void verdict(const json& result,const json& observed,bool fresh,const char* reason) {
    readOnlyTruth(result);const auto& r=result.at("runtime");
    EXPECT(r.at("sampleObserved")==observed);EXPECT(r.at("sampleCurrent")==fresh);EXPECT(r.at("reason")==reason);
    EXPECT(!r.contains("rawGyro"));EXPECT(!r.contains("calibrated"));
}
json status() {boundary::clockReads=0;return inputCaptureControllerSensorStatus();}
void fourModelsAndUnsupported() {
    for(const auto& model:legionModels)for(unsigned index:{0u,1u}) {
        reset(model);g_legionMotionSensorIndex=index;publish();const auto before=current();const auto result=status();
        const bool isTablet=model==legionModels[0]||model==legionModels[1];const unsigned actual=isTablet?index:0;
        EXPECT(result.at("implemented")==true);EXPECT(result.at("deviceClass")==model);EXPECT(result.at("selectionSupported")==isTablet);
        EXPECT(result.at("indices")== (isTablet?json::array({0,1}):json::array({0})));EXPECT(result.at("currentIndex")==actual);
        EXPECT(result.at("settingPath")=="input.gyroMotion.controllerSensorIndex");EXPECT(result.at("writeRoute")=="existing-input-settings-CAS");
        verdict(result,true,true,"fresh-current-sample");EXPECT(same(before,current()));
        EXPECT(result.at("runtime").at("lastAcceptedSample").at("sensorIndex")==actual);
        EXPECT(result.at("runtime").at("powerPhase")=="ready");
        EXPECT(result.at("runtime").at("timestampScope")=="os-queue-receipt-not-firmware-sampling");
        EXPECT(result.at("runtime").at("sampleObservedScope")=="last-admitted-lease-sample;absence-does-not-prove-no-physical-reports");
        EXPECT(boundary::clockReads==2);EXPECT(boundary::writeCalls==0);EXPECT(boundary::directoryCalls==0);EXPECT(boundary::timestampCalls==0);
    }
    for(const auto& model:std::vector<std::string>{"HandheldCompanion.Devices.ROGAlly","HandheldCompanion.Devices.MSIClaw","HandheldCompanion.Devices.GPDWin4","HandheldCompanion.Devices.SteamDeck",tablet+"Suffix",""}) {
        reset(model);publish();const auto before=current();const auto result=status();
        EXPECT(result.at("implemented")==false);EXPECT(result.at("selectionSupported")==false);EXPECT(result.at("indices").empty());
        verdict(result,nullptr,false,"controller-adapter-not-implemented-for-model");EXPECT(!result.at("runtime").contains("lastAcceptedSample"));
        EXPECT(result.at("runtime").at("sampleObservedScope")=="not-observed-by-this-controller-adapter");
        EXPECT(result.at("runtime").at("powerGeneration")==1);EXPECT(result.at("runtime").at("powerPhase")=="ready");
        EXPECT(boundary::clockReads==1);EXPECT(same(before,current()));
    }
    reset();g_machineIdentity.family=YmccFamily::Asus;publish();const auto result=status();EXPECT(result.at("implemented")==false);
    verdict(result,nullptr,false,"controller-adapter-not-implemented-for-model");
}
void lifecycleNames() {
    const std::pair<PowerLifecycle,const char*> phases[]={{PowerLifecycle::Ready,"ready"},{PowerLifecycle::Suspending,"suspending"},
        {PowerLifecycle::Suspended,"suspended"},{PowerLifecycle::Resuming,"resuming"}};
    for(const auto& [phase,name]:phases)for(const bool supported:{true,false}) {
        reset(supported?tablet:"HandheldCompanion.Devices.MSIClaw");g_powerLifecycle=phase;g_powerGeneration=77;
        const auto result=status();EXPECT(result.at("runtime").at("powerPhase")==name);EXPECT(result.at("runtime").at("powerGeneration")==77);
        EXPECT(result.at("runtime").at("sampleObserved")== (supported?json(false):json(nullptr)));
        if(supported)EXPECT(result.at("runtime").at("powerReady")== (phase==PowerLifecycle::Ready));
        EXPECT(boundary::writeCalls==0);readOnlyTruth(result);
    }
}
void noSampleAndReadiness() {
    reset();const auto empty=status();verdict(empty,false,false,"no-admitted-sample");EXPECT(empty.at("runtime").at("lastAcceptedSample").is_null());EXPECT(!current());
    const std::vector<std::pair<const char*,std::function<void()>>> changes={
        {"capture-not-ready",[]{g_inputCaptureReady=false;}},{"capture-stopping",[]{g_inputCaptureStop=true;}},
        {"raw-input-not-subscribed",[]{g_legionRawInputSubscribed=false;}},{"power-not-ready",[]{g_powerLifecycle=PowerLifecycle::Suspended;}},
        {"power-not-ready",[]{g_powerLifecycle=PowerLifecycle::Resuming;}},{"power-not-ready",[]{g_powerLifecycle=PowerLifecycle::Suspending;}}};
    for(const auto& [reason,change]:changes){reset();publish();change();const auto before=current();const auto result=status();verdict(result,true,false,reason);EXPECT(same(before,current()));
        EXPECT(result.at("runtime").at("lastAcceptedSample").at("queuedAtUptimeMs")==9900);}
}
void exactSourceMismatch() {
    const std::vector<std::pair<const char*,std::function<void()>>> changes={
        {"power-generation-unavailable",[]{g_powerGeneration=0;}},{"power-generation-mismatch",[]{g_powerGeneration=9;}},
        {"raw-generation-unavailable",[]{g_hidResolveCacheGeneration=0;}},{"raw-generation-mismatch",[]{g_hidResolveCacheGeneration=9;}},
        {"selected-source-not-bound",[]{g_hidHidePhysicalIdentity.stableIdentity.clear();}},{"physical-identity-mismatch",[]{g_hidHidePhysicalIdentity.stableIdentity="physical-B";}},
        {"selected-source-not-bound",[]{g_legionMotionCaptureEpoch=0;}},{"capture-epoch-mismatch",[]{g_legionMotionCaptureEpoch=9;}},
        {"sensor-index-mismatch",[]{g_legionMotionSensorIndex=1;}},{"selected-source-not-bound",[]{g_legionMotionSensorIndex=2;}}};
    for(const auto& [reason,change]:changes){reset();publish();const auto retained=sample();change();const auto result=status();verdict(result,true,false,reason);EXPECT(!current());
        EXPECT(result.at("runtime").at("lastAcceptedSample").at("physicalIdentity")==retained.source.physicalIdentity);
        EXPECT(result.at("runtime").at("selectedPhysicalIdentity")==source().physicalIdentity);}
}
void ageAndEpochBoundaries() {
    reset();publish(10001);auto result=status();verdict(result,true,false,"sample-from-future");EXPECT(result.at("runtime").at("lastAcceptedSample").at("ageMs").is_null());
    reset();publish(8500);result=status();verdict(result,true,true,"fresh-current-sample");EXPECT(result.at("runtime").at("lastAcceptedSample").at("ageMs")==1500);
    reset();publish(8499);result=status();verdict(result,true,false,"sample-expired");EXPECT(result.at("runtime").at("lastAcceptedSample").at("ageMs")==1501);
    reset();publish(9999);g_legionMotionEpochStartedAt=10000;result=status();verdict(result,true,false,"queue-time-not-after-epoch");EXPECT(!legionMotionSnapshot(10000));
    reset();publish(10000);g_legionMotionEpochStartedAt=10000;const auto equalRetained=current();
    result=status();verdict(result,true,false,"queue-time-not-after-epoch");EXPECT(!legionMotionSnapshot(10000));
    EXPECT(same(equalRetained,current()));EXPECT(result.at("runtime").at("lastAcceptedSample").at("queuedAtUptimeMs")==10000);
    verdict(status(),true,false,"queue-time-not-after-epoch");EXPECT(same(equalRetained,current()));
    boundary::observedAt=boundary::completedAt=10001;publish(10001);result=status();
    verdict(result,true,true,"fresh-current-sample");EXPECT(legionMotionSnapshot(10001).has_value());
    EXPECT(result.at("runtime").at("lastAcceptedSample").at("queuedAtUptimeMs")==10001);
    reset();publish(10001);boundary::completedAt=10002;result=status();verdict(result,true,false,"sample-from-future");EXPECT(result.at("runtime").at("lastAcceptedSample").at("ageMs")==1);
}
void retirementDuringSnapshot() {
    const std::vector<std::function<void()>> changes={
        []{++g_powerGeneration;},[]{++g_hidResolveCacheGeneration;},[]{g_hidHidePhysicalIdentity.stableIdentity="physical-B";++g_legionMotionCaptureEpoch;},
        []{++g_legionMotionCaptureEpoch;},[]{g_legionMotionSensorIndex=1;},[]{++g_legionMotionEpochStartedAt;},
        []{g_inputCaptureReady=false;},[]{g_inputCaptureStop=true;},[]{g_legionRawInputSubscribed=false;},[]{g_powerLifecycle=PowerLifecycle::Suspended;}};
    for(const auto& change:changes){reset();publish();boundary::afterRead=change;const auto result=status();
        verdict(result,true,false,"source-context-changed-during-snapshot");EXPECT(result.at("runtime").at("contextStableAtCheck")==false);
        EXPECT(result.at("runtime").at("lastAcceptedSample").at("powerGeneration")==1);}
    reset();publish();boundary::completedAt=9999;auto result=status();verdict(result,true,false,"source-context-changed-during-snapshot");
    reset();publish(8500);boundary::completedAt=10001;result=status();verdict(result,true,false,"sample-expired-during-snapshot");
    EXPECT(result.at("runtime").at("contextStableAtCheck")==true);EXPECT(result.at("runtime").at("lastAcceptedSample").at("ageMs")==1501);
}
void nonConsumingRealLeaseAndSnapshot() {
    reset();auto raw=sample();raw.gyro={-1.25,2.5,0};raw.accel={0,0,-1};EXPECT(g_legionMotion.publish(raw,context(),source(),true));
    const auto baseline=current();const auto before=legionMotionSnapshot(10000);EXPECT(same(baseline,before));const auto config=configuration.dump();
    for(int i=0;i<3;++i){const auto result=status();verdict(result,true,true,"fresh-current-sample");EXPECT(same(baseline,current()));EXPECT(same(before,legionMotionSnapshot(10000)));}
    EXPECT(configuration.dump()==config);EXPECT(boundary::writeCalls==0);
    auto invalid=raw;invalid.gyro[0]=std::numeric_limits<double>::quiet_NaN();EXPECT(!g_legionMotion.publish(invalid,context(),source(),true));
    EXPECT(same(baseline,current()));verdict(status(),true,true,"fresh-current-sample");
    g_legionMotion.revoke();verdict(status(),false,false,"no-admitted-sample");EXPECT(!current());
    ++g_legionMotionCaptureEpoch;publish();verdict(status(),true,true,"fresh-current-sample");EXPECT(current().has_value());
}
void successfulExport() {
    reset();publish();const auto before=current();const auto config=configuration.dump();SetLastError(1234);
    const auto path=exportControllerMotionEvidence();EXPECT(path==file);EXPECT(GetLastError()==1234);EXPECT(boundary::writeCalls==1);
    EXPECT(boundary::writes.size()==1&&boundary::writes[0]==file);const auto saved=json::parse(boundary::files.at(file));
    EXPECT(saved.at("nativeEvidenceVersion")==1);EXPECT(saved.at("exportEntry")=="logs.exportAll");EXPECT(saved.at("observedAt")=="2026-10-17T00:00:00Z-controlled");
    verdict(saved,true,true,"fresh-current-sample");EXPECT(same(before,current()));EXPECT(configuration.dump()==config);
    reset("HandheldCompanion.Devices.ROGAlly");SetLastError(42);EXPECT(!exportControllerMotionEvidence().empty());EXPECT(GetLastError()==42);
    verdict(json::parse(boundary::files.at(file)),nullptr,false,"controller-adapter-not-implemented-for-model");
}
void exportFailuresNeverReturnOldPath() {
    for(const auto failure:std::vector<boundary::Failure>{boundary::Failure::None,boundary::Failure::FirstClock,boundary::Failure::SecondClock,
        boundary::Failure::Timestamp,boundary::Failure::Directory,boundary::Failure::Writer}) {
        reset();publish();boundary::files[file]="previous-export-must-not-be-reused";
        boundary::failure=failure;boundary::writeSucceeds=false;const auto before=current();const auto config=configuration.dump();SetLastError(818);
        const auto returned=exportControllerMotionEvidence();EXPECT(returned.empty());EXPECT(GetLastError()==818);
        EXPECT(boundary::files.at(file)=="previous-export-must-not-be-reused");EXPECT(same(before,current()));EXPECT(configuration.dump()==config);
        EXPECT(boundary::writeCalls==(failure==boundary::Failure::None||failure==boundary::Failure::Writer?1u:0u));
    }
}
}
int main() {
    const std::pair<const char*,void(*)()> scenarios[]={
        {"four-models-and-unsupported",fourModelsAndUnsupported},{"lifecycle-names-and-scope",lifecycleNames},{"no-sample-and-not-ready",noSampleAndReadiness},
        {"exact-source-mismatch",exactSourceMismatch},{"age-and-epoch-boundaries",ageAndEpochBoundaries},
        {"retirement-and-expiry-during-read",retirementDuringSnapshot},{"non-consuming-lease-and-snapshot",nonConsumingRealLeaseAndSnapshot},
        {"successful-read-only-export",successfulExport},{"failed-export-never-reuses-old-file",exportFailuresNeverReturnOldPath}};
    for(const auto& [name,run]:scenarios){currentCase=name;const int startChecks=checks,startFailures=failures;
        try{run();}catch(const std::exception& e){expect(false,e.what());}
        std::cout<<"CASE "<<name<<" checks="<<checks-startChecks<<" failures="<<failures-startFailures<<'\n';}
    std::cout<<"SUMMARY scenarios="<<std::size(scenarios)<<" checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
