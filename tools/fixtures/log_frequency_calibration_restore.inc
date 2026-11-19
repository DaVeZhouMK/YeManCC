#include <chrono>
#include <memory>
#include "hc_model_motion_catalog.h"
#include "reviewed_gyro_calibration.h"
struct InputCaptureSensorMeta {bool gyroPresent=true;std::string gyroDeviceId="owned-gyro",accelDeviceId="owned-accel",gyroFamily="winrt",accelFamily="winrt";};
static InputCaptureSensorMeta fixtureMeta;
static struct {std::string deviceClass="owned-calibration-model";} g_machineIdentity;
struct FixtureSensorOwner {std::atomic<std::uint64_t> activeBindingGeneration{1};};
static auto g_inputCaptureSink=std::make_shared<FixtureSensorOwner>();
static int fixtureMotionHandle=0;
static void* g_gm=&fixtureMotionHandle;
static auto g_gmSessionStart=std::chrono::steady_clock::time_point(std::chrono::seconds(1));
static ymcc::reviewed_calibration::Context g_reviewedCalibrationContext;
static std::atomic<bool> g_autoCalibTrusted{false},g_autoCalibFinished{false};
static constexpr float kCalibTrustedConfidence=0.99f;
static bool fixtureExtended=true,fixtureBound=true,fixtureLoaded=false,fixtureRevokeOnLoad=false;
static bool fixtureIdentity=true;
static std::string fixtureKey="owned-key";
static unsigned fixtureLoads=0,fixtureOffsets=0,fixtureModes=0,fixtureStatus=0;
static int fixtureWeight=12;
static float fixtureConfidence=0.995f;
static const ymcc::hc_model_motion::Recipe recipeA{"owned-A","selector-A","matrix-A","accel-A","source-A","0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"};
static const ymcc::hc_model_motion::Recipe recipeB{"owned-B","selector-B","matrix-B","accel-B","source-B","abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"};
static const ymcc::hc_model_motion::Recipe* fixtureRecipe=&recipeA;
struct FixtureProcessingView {bool privateSource=false;};
static FixtureProcessingView fixtureView;
static FixtureProcessingView* t_windowsProcessingView=nullptr;
static ymcc::windows_motion::Stamp fixtureStamp;
static const ymcc::windows_motion::Stamp* t_windowsProcessingStamp=nullptr;
static InputCaptureSensorMeta inputCaptureSensorMetaSnapshot(){return fixtureMeta;}
static const ymcc::hc_model_motion::Recipe* inputCaptureExtendedCalibrationRecipe(){return fixtureExtended?fixtureRecipe:nullptr;}
static const ymcc::hc_model_motion::Recipe* inputCaptureSourceQualifiedCalibrationRecipe(){return fixtureRecipe;}
static bool inputCaptureSourceQualifiedCalibrationBound(){return fixtureBound;}
static unsigned long long currentPowerGeneration(){return g_powerGeneration.load();}
static json inputCaptureExtendedCalibrationSourceIdentity(const std::string&){return fixtureIdentity?json{{"owned",true}}:json(nullptr);}
static std::string inputCaptureCalibrationStorageKey(const std::string& device){return device.empty()?std::string{}:fixtureKey;}
static void g_gmSetMode(void*,int){++fixtureModes;}
static void g_gmSetOffset(void*,float,float,float,int){++fixtureOffsets;}
static void inputCapturePostCalibStatus(int){++fixtureStatus;}
static bool inputCaptureLoadCalibration(const std::string&,float& x,float& y,float& z,int& weight,float& confidence,const std::string* =nullptr,const json* =nullptr,const ymcc::hc_model_motion::Recipe* =nullptr){
 ++fixtureLoads;x=1;y=2;z=3;weight=fixtureWeight;confidence=fixtureConfidence;if(fixtureRevokeOnLoad)g_inputCaptureSink->activeBindingGeneration.fetch_add(1);return fixtureLoaded;
}
@@ACTUAL_FRAME_CURRENT@@
@@ACTUAL_NOTE_APPLIED@@
@@ACTUAL_INVALIDATE@@
@@ACTUAL_RESTORE@@
static bool lastEvent(const fspath::path& root,const char* event){const auto rows=readRecoveryRows(root);return !rows.empty()&&rows.back()["event"]==event;}
static void newSession(){g_gmSessionStart+=std::chrono::seconds(1);}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2&&!(argc==3&&std::wcscmp(argv[2],L"--retired-load")==0))return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();g_domainLogEnabled[1]=false;(void)ymcc::sleepfactlog::writerIdentity();DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
 if(argc==3){
  t_windowsProcessingStamp=&fixtureStamp;fixtureStamp.activeBinding=std::shared_ptr<const std::atomic<std::uint64_t>>(g_inputCaptureSink,&g_inputCaptureSink->activeBindingGeneration);fixtureStamp.bindingGeneration=1;
  fixtureBound=true;fixtureLoaded=true;fixtureRevokeOnLoad=true;
  inputCaptureReviewedCalibrationRestoreOnce(recipeA);const auto retiredRows=readRecoveryRows(root);
  check(fixtureLoads==1&&g_inputCaptureSink->activeBindingGeneration==2&&retiredRows.size()==1&&lastEvent(root,"gyro-calibration-awaiting-current-source"),"actual-binding-retirement-during-load-reaches-original-source-wait");
  check(retiredRows.size()==1&&retiredRows[0]["sourceCurrent"]==false&&!g_autoCalibTrusted.load()&&!g_autoCalibFinished.load()&&g_reviewedCalibrationContext.owner.storageKey.empty(),"retired-load-does-not-report-restored-or-trusted-calibration");
  closeStreams();GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"owned-log-handles-returned");unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failures},{"handlesBefore",before},{"handlesAfter",after},{"completeOriginalFunctions",4},{"actualMotionStampGuard",true},{"deviceCalls",0},{"onlyAffectedRetiredLoad",true}}.dump(2)<<'\n';return failures?2:0;
 }
 check(nativeLifecycleEventIsBase("gyro-calibration-awaiting-current-source")&&nativeLifecycleEventIsBase("gyro-calibration-awaiting-current-recipe"),"two-original-calibration-waiting-names-registered");
 inputCaptureReviewedCalibrationRestoreOnce(recipeA);auto rows=readRecoveryRows(root);
 check(rows.size()==1&&rows[0]["event"]=="gyro-calibration-awaiting-current-recipe"&&rows[0]["loaded"]==false&&rows[0]["weight"]==0&&rows[0]["confidence"]==0&&rows[0]["sourceQualified"]==true&&fixtureLoads==1,"complete-original-restore-off-retains-missing-storage-result");
 fixtureBound=false;inputCaptureReviewedCalibrationRestoreOnce(recipeA);rows=readRecoveryRows(root);
 check(rows.size()==2&&lastEvent(root,"gyro-calibration-awaiting-current-source")&&rows.back()["sourceCurrent"]==false&&rows.back()["calibrationOwner"]=="none"&&!g_autoCalibTrusted.load(),"complete-original-invalidation-off-retains-source-wait-result");
#ifndef YMCC_CALIBRATION_WAIT_OLD_ONLY
 const auto loadsBefore=fixtureLoads,offsetsBefore=fixtureOffsets,modesBefore=fixtureModes,statusBefore=fixtureStatus;
 for(unsigned i=0;i<50000;++i)inputCaptureReviewedCalibrationRestoreOnce(recipeA);
 check(readRecoveryRows(root).size()==2&&fixtureLoads==loadsBefore&&fixtureOffsets==offsetsBefore&&fixtureModes==modesBefore&&fixtureStatus==statusBefore,"50000-original-invalid-source-revisits-no-new-record-load-or-application");
 fixtureBound=true;fixtureLoaded=false;newSession();inputCaptureReviewedCalibrationRestoreOnce(recipeA);rows=readRecoveryRows(root);check(rows.size()==3&&lastEvent(root,"gyro-calibration-awaiting-current-recipe"),"new-current-source-session-retains-its-storage-wait");
 const auto stableLoads=fixtureLoads,stableOffsets=fixtureOffsets;for(unsigned i=0;i<50000;++i)inputCaptureReviewedCalibrationRestoreOnce(recipeA);
 check(readRecoveryRows(root).size()==3&&fixtureLoads==stableLoads&&fixtureOffsets==stableOffsets,"50000-original-same-recipe-session-revisits-quiet");
 fixtureLoaded=true;newSession();inputCaptureReviewedCalibrationRestoreOnce(recipeA);rows=readRecoveryRows(root);
 check(rows.size()==4&&lastEvent(root,"gyro-calibration-restored")&&rows.back()["loaded"]==true&&rows.back()["weight"]==12&&g_autoCalibTrusted.load()&&g_autoCalibFinished.load(),"existing-positive-recipe-restore-stays-resident-with-original-flags");
 fixtureRecipe=&recipeB;fixtureKey="owned-key-B";fixtureLoaded=false;inputCaptureReviewedCalibrationRestoreOnce(recipeB);rows=readRecoveryRows(root);
 check(rows.size()==5&&lastEvent(root,"gyro-calibration-awaiting-current-recipe")&&rows.back()["matrixId"]=="matrix-B","recipe-and-key-transition-retains-original-new-attempt");
 t_windowsProcessingStamp=&fixtureStamp;fixtureStamp.activeBinding=std::shared_ptr<const std::atomic<std::uint64_t>>(g_inputCaptureSink,&g_inputCaptureSink->activeBindingGeneration);fixtureStamp.bindingGeneration=1;
 g_inputCaptureSink->activeBindingGeneration=2;inputCaptureReviewedCalibrationRestoreOnce(recipeB);rows=readRecoveryRows(root);
 check(rows.size()==6&&lastEvent(root,"gyro-calibration-awaiting-current-source"),"actual-frame-current-guard-retains-retired-binding-result");
 g_inputCaptureSink->activeBindingGeneration=1;fixtureBound=true;fixtureRevokeOnLoad=true;newSession();inputCaptureReviewedCalibrationRestoreOnce(recipeB);rows=readRecoveryRows(root);
 check(rows.size()==7&&lastEvent(root,"gyro-calibration-awaiting-current-source")&&!g_autoCalibTrusted.load(),"source-revoked-during-load-does-not-emit-restored-success");
 fixtureRevokeOnLoad=false;g_inputCaptureSink->activeBindingGeneration=1;fixtureBound=true;fixtureLoaded=false;g_domainLogEnabled[1]=true;newSession();inputCaptureReviewedCalibrationRestoreOnce(recipeB);g_domainLogEnabled[1]=false;
 check(readRecoveryRows(root).size()==8&&g_nativeDetailWrites==0,"detail-on-keeps-calibration-boundary-resident");
 const auto oldFail=count(Failure::eventWrite);newSession();{ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);inputCaptureReviewedCalibrationRestoreOnce(recipeB);}
 check(count(Failure::eventWrite)==oldFail+1&&readRecoveryRows(root).size()==8&&g_reviewedCalibrationContext.owner.storageKey==fixtureKey,"log-write-failure-counted-without-repeating-calibration-business");
 const auto noRetryLoads=fixtureLoads;inputCaptureReviewedCalibrationRestoreOnce(recipeB);check(readRecoveryRows(root).size()==8&&fixtureLoads==noRetryLoads,"same-session-after-log-failure-keeps-original-no-retry-contract");
 fixtureBound=false;inputCaptureReviewedCalibrationRestoreOnce(recipeB);check(readRecoveryRows(root).size()==9&&lastEvent(root,"gyro-calibration-awaiting-current-source"),"next-real-source-transition-remains-visible-after-file-failure");
 appendNativeLifecycleLog("gyro-calibration-awaiting-current-source-future",{{"sourceCurrent",false}});appendNativeLifecycleLog("gyro-calibration-awaiting-current-recipe-future",{{"loaded",false}});check(readRecoveryRows(root).size()==9,"registry-uses-exact-two-names-without-prefix-permission");
 const json high={{"reason","buttons-change"}};const auto writes=g_nativeLifecycleFrameWrites;ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)appendNativeLifecycleLog("system-xinput-readback",high);const auto arm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);check(arm==0&&writes==g_nativeLifecycleFrameWrites,"unrelated-50000-prebuilt-high-frequency-samples-off-no-allocation-or-write");
#endif
 closeStreams();GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"owned-log-handles-returned");unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failed},{"handlesBefore",before},{"handlesAfter",after},{"completeOriginalFunctions",4},{"actualMotionStampGuard",true},{"deviceCalls",0},{"storageLoadPortIsMemory",true}}.dump(2)<<'\n';return failed?2:0;
}catch(const std::exception& e){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<e.what()<<'\n';return 3;}}
