// Original pre-D14 production matrix function; immutable comparison, not a runtime fallback.
// Baseline function SHA256: ed60e75583da1838482d823c70784a52a0cd81ef35100e3c7ffe8df4b8090443
namespace matrix_before {
using namespace candidate;
static void inputCaptureApplyHcMatrixCandidate(
    double gx, double gy, double gz, double ax, double ay, double az,
    double& outGx, double& outGy, double& outGz,
    double& outAx, double& outAy, double& outAz) noexcept {
    outGx = gx; outGy = gy; outGz = gz;
    outAx = ax; outAy = ay; outAz = az;
    // The machine identity retains its canonical source tuple. Only a current,
    // exact reviewed binding selects the newer motion-only resource recipe.
    const bool reviewedFamily = g_machineIdentity.family == YmccFamily::Ayanero ||
        g_machineIdentity.family == YmccFamily::OneXPlayer || g_machineIdentity.family == YmccFamily::Minisforum;
    const auto* resource = reviewedFamily ? ymcc::hc_model_motion::find(g_machineIdentity.deviceClass) : nullptr;
    const bool selectedResource = resource && !resource->identityGyroMatrixId.empty() && inputCaptureNewHcMotionSelectorBound();
    const std::string& gyroId = selectedResource ? g_hcMotionSelector.gyroMatrixId : !g_machineIdentity.gyroMatrixId.empty()
        ? g_machineIdentity.gyroMatrixId : g_hcMotionSelector.gyroMatrixId;
    const std::string& accelId = selectedResource ? g_hcMotionSelector.accelMatrixId : !g_machineIdentity.accelMatrixId.empty()
        ? g_machineIdentity.accelMatrixId : g_hcMotionSelector.accelMatrixId;
    if (gyroId.empty() && accelId.empty()) return;
    if (!gyroId.empty())
        (void)ymccApplyNamedImuMatrix(gyroId, gx, gy, gz, outGx, outGy, outGz);
    if (!accelId.empty())
        (void)ymccApplyNamedImuMatrix(accelId, ax, ay, az, outAx, outAy, outAz);
    const auto legacyFields=inputCaptureLegacySensorFieldsSnapshot();
    const auto* msi=ymcc::msi_sensor::motionCapability(g_machineIdentity.deviceClass,g_machineIdentity.product);
    if(msi && msi->clawHidSensorApiLayout) {
        if(legacyFields.gyro) {
            const auto mapped=ymcc::msi_sensor::configuredLegacyAxes({gx,gy,gz});
            outGx=mapped[0];outGy=mapped[1];outGz=mapped[2];
        }
        if(legacyFields.accel) {
            const auto mapped=ymcc::msi_sensor::configuredLegacyAxes({ax,ay,az});
            outAx=mapped[0];outAy=mapped[1];outAz=mapped[2];
        }
    }
}
@@BASELINE_ACTIVE_PROBE@@
}
namespace matrix_proof {
static unsigned long long checks=0;
static void need(bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(label);}
static std::array<double,6> apply(bool old,const std::array<double,6>& raw){std::array<double,6> v{};if(old)matrix_before::inputCaptureApplyHcMatrixCandidate(raw[0],raw[1],raw[2],raw[3],raw[4],raw[5],v[0],v[1],v[2],v[3],v[4],v[5]);else candidate::inputCaptureApplyHcMatrixCandidate(raw[0],raw[1],raw[2],raw[3],raw[4],raw[5],v[0],v[1],v[2],v[3],v[4],v[5]);return v;}
static unsigned long long findCalls(){return ymcc::hc_model_motion::matrixProbeFindCalls;}
static unsigned long long comparisons(){return ymcc::hc_model_motion::matrixProbeComparisons;}
static void resetCost(){ymcc::hc_model_motion::matrixProbeFindCalls=0;ymcc::hc_model_motion::matrixProbeComparisons=0;}
static bool reviewedFamily(){using namespace candidate;return g_machineIdentity.family==YmccFamily::Ayanero||g_machineIdentity.family==YmccFamily::OneXPlayer||g_machineIdentity.family==YmccFamily::Minisforum;}
}
int main(int argc,char** argv){
 using namespace candidate;using matrix_proof::need;std::string scenario;json categories=json::object();unsigned long long transforms=0,windows=0,admitted=0,rejected=0,oldEffects=0,newEffects=0,oldFind=0,newFind=0,oldComparisons=0,newComparisons=0,oldBusFind=0,newBusFind=0;std::set<std::string> classes,boundClasses;
 try{
  if(argc!=2)throw std::runtime_error("oracle-path-required");json oracle;std::ifstream(argv[1])>>oracle;std::vector<json> rows;
  for(const auto& row:oracle["identities"])rows.push_back(row);
  for(const auto& row:oracle["legacy"]["cases"])rows.push_back(row);
  for(const auto& row:json::array({json{{"manufacturer","UNKNOWN"},{"product","UNKNOWN"}},json{{"manufacturer","ASUSTEK COMPUTER INC."},{"product","UNKNOWN"}},json{{"manufacturer","GPD"},{"product","WIN2"}},json{{"manufacturer","VALVE"},{"product","FREMONT"}}}))rows.push_back(row);
  const std::array<std::array<double,6>,3> samples={{{2,-3,5,11,13,-17},{-7.25,2.5,-0.125,19.5,-23.25,0.0625},{1e-9,-1e-8,1e-7,-1e-6,2e-7,3e-8}}};
  for(std::size_t rowIndex=0;rowIndex<rows.size();++rowIndex){setup(rows[rowIndex]);inputCaptureResolveHcMotionSelector();const auto identity=g_machineIdentity;const auto receipt=g_hcMotionSelector;classes.insert(identity.deviceClass);if(inputCaptureMotionSelectorBound())boundClasses.insert(identity.deviceClass);
   for(unsigned state=0;state<17;++state)for(unsigned legacyFields=0;legacyFields<2;++legacyFields){
    g_machineIdentity=identity;g_hcMotionSelector=receipt;g_sensorMeta={};g_sensorMeta.gyroHcLegacyFields=legacyFields!=0;g_sensorMeta.accelHcLegacyFields=legacyFields!=0;g_realStickTest.enabled=true;g_inputCaptureSink=&mockSink;mockSink.activeBindingGeneration.store(73);g_gm=&mockGamepadHandle;g_gmProcess=&recordMotionEffect;g_gmGetCal=true;auto copied=ymcc::hc_model_motion::recipes[0];
    auto& binding=g_hcMotionSelector.unifiedBinding;
    switch(state){case 1:binding={};break;case 2:binding.registration=nullptr;break;case 3:binding.slot=9999;break;case 4:binding.recipe=&copied;break;case 5:binding.kind=ymcc::capabilities::motion::Kind::None;break;case 6:binding=ymcc::capabilities::motion::bind(ymcc::capabilities::motion::Kind::Reviewed,&ymcc::hc_model_motion::recipes[(rowIndex+1)%std::size(ymcc::hc_model_motion::recipes)],ymcc::hc_model_motion::recipes);break;case 7:g_hcMotionSelector.gyroMatrixId+="-changed";break;case 8:g_hcMotionSelector.matrixSourceSha256+="-changed";break;case 9:g_machineIdentity.deviceClass+="-changed";break;case 10:g_machineIdentity.family=static_cast<YmccFamily>(0);break;case 11:g_machineIdentity.product+="-changed";break;case 12:g_hcMotionSelector.resolved=false;break;case 13:g_machineIdentity.resolved=false;break;case 14:g_inputCaptureSink=nullptr;break;case 15:g_machineIdentity.gyroMatrixId+="-changed";break;case 16:g_realStickTest.enabled=false;break;default:break;}
    const bool family=matrix_proof::reviewedFamily();
    for(std::size_t sample=0;sample<samples.size();++sample){scenario="row="+std::to_string(rowIndex)+",class="+g_machineIdentity.deviceClass+",state="+std::to_string(state)+",legacy="+std::to_string(legacyFields)+",sample="+std::to_string(sample);
     matrix_proof::resetCost();const auto old=matrix_proof::apply(true,samples[sample]);const auto priorFind=matrix_proof::findCalls(),priorComparisons=matrix_proof::comparisons();matrix_proof::resetCost();const auto now=matrix_proof::apply(false,samples[sample]);const auto currentFind=matrix_proof::findCalls(),currentComparisons=matrix_proof::comparisons();
     for(unsigned axis=0;axis<6;++axis)need(old[axis]==now[axis],"MATRIX_OUTPUT_EQUIVALENCE");need(priorFind==(family?1u:0u),"BASELINE_LOOKUP_CONTROL");need(currentFind==0,"MATRIX_LOOKUP_REMOVAL");need((currentComparisons==0)&&(!family||priorComparisons>0),"MATRIX_COMPARISON_REMOVAL");oldFind+=priorFind;newFind+=currentFind;oldComparisons+=priorComparisons;newComparisons+=currentComparisons;++transforms;
     for(unsigned control=0;control<8;++control){const bool live=control!=1,gyro=control!=2,accel=control!=3;const std::string gp="provider-a",ap=control==4?"provider-b":"provider-a";const UINT64 gg=control==5?0:control==6?72:73,ag=control==7?74:gg;
      (void)inputCaptureMotionSelectorBound();matrix_proof::resetCost();const auto a=matrix_before::probeActive(live,gyro,accel,gp,ap,gg,ag,samples[sample]);const auto priorBus=matrix_proof::findCalls();(void)inputCaptureMotionSelectorBound();matrix_proof::resetCost();const auto b=candidate::probeActive(live,gyro,accel,gp,ap,gg,ag,samples[sample]);const auto currentBus=matrix_proof::findCalls();
      need(a.admitted==b.admitted,"BUS_ADMISSION_EQUIVALENCE");need(a.matrixIdentity==b.matrixIdentity,"BUS_RECIPE_EQUIVALENCE");need(a.processCalls==b.processCalls,"GAMEPAD_MOTION_EFFECT_COUNT_EQUIVALENCE");for(unsigned axis=0;axis<6;++axis){need(a.active[axis]==b.active[axis],"BUS_OUTPUT_EQUIVALENCE");need(a.processAxes[axis]==b.processAxes[axis],"GAMEPAD_MOTION_AXES_EQUIVALENCE");}need(priorBus==currentBus+(family?1u:0u),"BUS_LOOKUP_DELTA");oldBusFind+=priorBus;newBusFind+=currentBus;oldEffects+=a.processCalls;newEffects+=b.processCalls;++windows;if(b.admitted)++admitted;else ++rejected;
     }
    }
    categories[std::to_string(state)]=categories.value(std::to_string(state),0ULL)+samples.size();
   }
  }
  need(oldEffects==newEffects&&oldEffects>0&&admitted>0&&rejected>0,"REAL_EFFECTS_PRESERVED");need(oldFind>0&&newFind==0&&oldComparisons>0&&newComparisons==0,"PERFORMANCE_NOT_FROM_DISABLED_PATH");
  json result={{"status","PASS"},{"checks",matrix_proof::checks},{"matrixTransformPairs",transforms},{"busWindowPairs",windows},{"admitted",admitted},{"rejected",rejected},{"oldProcessCalls",oldEffects},{"newProcessCalls",newEffects},{"oldMatrixReviewedFindCalls",oldFind},{"newMatrixReviewedFindCalls",newFind},{"oldMatrixStringComparisons",oldComparisons},{"newMatrixStringComparisons",newComparisons},{"oldBusReviewedFindCalls",oldBusFind},{"newBusReviewedFindCalls",newBusFind},{"distinctResolvedClasses",classes.size()},{"distinctBoundClasses",boundClasses.size()},{"states",categories},{"wholeProcessCpuReduction",nullptr},{"fullBusExecuted",false},{"realDevicesAccessed",false},{"physicalAcceptance",false}};std::cout<<result.dump()<<'\n';return 0;
 }catch(const std::exception& e){std::cout<<json{{"status","FAIL"},{"error",e.what()},{"scenario",scenario},{"checks",matrix_proof::checks},{"transforms",transforms},{"windows",windows}}.dump()<<'\n';return 1;}
}
