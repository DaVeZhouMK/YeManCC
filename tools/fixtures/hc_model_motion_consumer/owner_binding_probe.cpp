// Before bodies are loaded verbatim from the authenticated primary-owner contract.
namespace owner_before {
using namespace candidate;
#define inputCaptureDefaultHcMotionIdentity primaryOwnerBeforeDefaultIdentity
@@BEFORE_METHODS@@
#undef inputCaptureDefaultHcMotionIdentity
@@BASELINE_ACTIVE_PROBE@@
}
namespace owner_proof {
static unsigned long long checks=0;
static void need(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
struct Cost {unsigned long long reviewed,reviewedComparisons,defaults,defaultComparisons,legacy,legacyComparisons,primary;};
static void resetCost(){ymcc::hc_model_motion::ownerProbeCalls=ymcc::hc_model_motion::ownerProbeComparisons=0;ymcc::hc_default_motion::ownerProbeCalls=ymcc::hc_default_motion::ownerProbeComparisons=0;ymcc::hc_legacy_motion::ownerProbeCalls=ymcc::hc_legacy_motion::ownerProbeComparisons=0;ymcc::capabilities::motion::ownerProbeReads=0;}
static Cost cost(){return {ymcc::hc_model_motion::ownerProbeCalls,ymcc::hc_model_motion::ownerProbeComparisons,ymcc::hc_default_motion::ownerProbeCalls,ymcc::hc_default_motion::ownerProbeComparisons,ymcc::hc_legacy_motion::ownerProbeCalls,ymcc::hc_legacy_motion::ownerProbeComparisons,ymcc::capabilities::motion::ownerProbeReads};}
static void add(Cost& a,const Cost& b){a.reviewed+=b.reviewed;a.reviewedComparisons+=b.reviewedComparisons;a.defaults+=b.defaults;a.defaultComparisons+=b.defaultComparisons;a.legacy+=b.legacy;a.legacyComparisons+=b.legacyComparisons;a.primary+=b.primary;}
static json costJson(const Cost& c){return {{"reviewedFindCalls",c.reviewed},{"reviewedFindComparisons",c.reviewedComparisons},{"defaultFindCalls",c.defaults},{"defaultFindComparisons",c.defaultComparisons},{"legacyContainsCalls",c.legacy},{"legacyContainsComparisons",c.legacyComparisons},{"totalCatalogPrimaryReads",c.primary}};}
}
int main(int argc,char** argv){
 using namespace candidate;namespace motion=ymcc::capabilities::motion;using owner_proof::need;
 std::string scenario;unsigned long long boundPairs=0,windows=0,oldEffects=0,newEffects=0,admitted=0,rejected=0,legacySupersession=0;owner_proof::Cost oldTotal{},newTotal{};
 try{
  if(argc!=2)throw std::runtime_error("oracle required");json oracle;std::ifstream(argv[1])>>oracle;
  for(const auto& row:motion::descriptors){
   const auto expected=row.kind==motion::Kind::Generic?motion::Kind::Generic:
    ymcc::hc_default_motion::find(row.deviceClass)?motion::Kind::Default:
    ymcc::hc_model_motion::find(row.deviceClass)?motion::Kind::Reviewed:
    ymcc::hc_legacy_motion::containsClass(row.deviceClass)?motion::Kind::Legacy:motion::Kind::Private;
   need(row.primaryKind==expected,"GENERATED_PRIMARY_OWNER_PRIORITY");
  }
  std::vector<json> rows;for(const auto& row:oracle["identities"])rows.push_back(row);for(const auto& row:oracle["legacy"]["cases"])rows.push_back(row);
  for(const auto& row:json::array({json{{"manufacturer","UNKNOWN"},{"product","UNKNOWN"}},json{{"manufacturer","ASUSTEK COMPUTER INC."},{"product","UNKNOWN"}},json{{"manufacturer","GPD"},{"product","WIN2"}},json{{"manufacturer","GPD"},{"product","G1618-03"}},json{{"manufacturer","VALVE"},{"product","FREMONT"}},json{{"manufacturer","MICRO-STAR INTERNATIONAL CO., LTD."},{"product","MS-1T91"}},json{{"manufacturer","ASUSTEK COMPUTER INC."},{"product","RC73XA"}}}))rows.push_back(row);
  const std::array<std::array<double,6>,2> samples={{{2,-3,5,11,13,-17},{-7.25,2.5,-0.125,19.5,-23.25,0.0625}}};
  for(std::size_t row=0;row<rows.size();++row){setup(rows[row]);inputCaptureResolveHcMotionSelector();const auto identity=g_machineIdentity,initialIdentity=identity;const auto receipt=g_hcMotionSelector;
   for(unsigned state=0;state<35;++state){g_machineIdentity=initialIdentity;g_hcMotionSelector=receipt;g_sensorMeta={};g_sensorMeta.gyroFamily=g_sensorMeta.accelFamily="sensorapi";g_inputCaptureSink=&mockSink;mockSink.activeBindingGeneration=73;g_realStickTest.enabled=true;int substituteRecipe=0;
    switch(state){
     case 1:g_hcMotionSelector.attempted=false;break;case 2:g_hcMotionSelector.resolved=false;break;case 3:g_machineIdentity.resolved=false;break;
     case 4:g_machineIdentity.deviceClass="unknown";break;case 5:g_machineIdentity.manufacturer+="-changed";break;case 6:g_machineIdentity.product+="-changed";break;case 7:g_machineIdentity.systemProduct+="-changed";break;
     case 8:g_machineIdentity.processor+="-changed";break;case 9:g_machineIdentity.systemVersion+="-changed";break;case 10:g_machineIdentity.gyroMatrixId+="-changed";break;case 11:g_machineIdentity.accelMatrixId+="-changed";break;case 12:g_machineIdentity.family=YmccFamily::Unknown;break;
     case 13:g_hcMotionSelector.unifiedBinding.kind=motion::Kind::None;break;case 14:g_hcMotionSelector.unifiedBinding.registration=nullptr;break;case 15:g_hcMotionSelector.unifiedBinding.slot=motion::count(g_hcMotionSelector.unifiedBinding.kind);break;case 16:g_hcMotionSelector.unifiedBinding.recipe=&substituteRecipe;break;
     case 17:{const auto* reviewed=ymcc::hc_model_motion::find(identity.deviceClass);const auto* legacy=ymcc::hc_legacy_motion::find(identity.deviceClass,identity.gyroMatrixId,identity.accelMatrixId);
      if(reviewed&&legacy&&!legacy->controllerPreferred){g_hcMotionSelector.unifiedBinding=motion::bind(motion::Kind::Legacy,legacy,ymcc::hc_legacy_motion::recipes);g_hcMotionSelector.selectorId=legacy->selectorId;g_hcMotionSelector.gyroMatrixId=legacy->gyroMatrixId;g_hcMotionSelector.accelMatrixId=legacy->accelMatrixId;g_hcMotionSelector.matrixSourceSha256=legacy->sourceSha256;
       if(inputCaptureLegacyHcMotionSelectorBound()){++legacySupersession;need(!owner_before::inputCaptureMotionSelectorBound()&&!candidate::inputCaptureMotionSelectorBound(),"REVIEWED_CLASS_CANNOT_FALL_BACK_TO_LEGACY");}}
      break;}
     case 18:g_hcMotionSelector.unifiedBinding.kind=motion::Kind::Default;break;case 19:if(!motion::descriptors.empty())g_hcMotionSelector.unifiedBinding.registration=&motion::descriptors.back();break;
     case 20:g_hcMotionSelector.matrixSourceSha256="wrong-source";break;case 21:g_hcMotionSelector.selectorId+="-changed";break;case 22:g_hcMotionSelector.gyroMatrixId+="-changed";break;case 23:g_hcMotionSelector.accelMatrixId+="-changed";break;case 24:g_hcMotionSelector.dmiProcessor+="-changed";break;case 25:g_hcMotionSelector.dmiSystemVersion+="-changed";break;case 26:g_hcMotionSelector.nativeFamily=99;break;case 27:g_machineIdentity.inputHidKey+="-changed";break;case 28:g_hcMotionSelector.dmiManufacturer+="-changed";break;case 29:mockSink.activeBindingGeneration=74;break;case 30:g_inputCaptureSink=nullptr;break;case 31:g_realStickTest.enabled=false;break;
     case 32:g_machineIdentity.manufacturer="FAKE";g_hcMotionSelector.dmiManufacturer="FAKE";break;
     case 33:g_machineIdentity.product="FAKE";g_hcMotionSelector.dmiProduct="FAKE";break;
     case 34:g_machineIdentity.family=YmccFamily::Unknown;g_hcMotionSelector.nativeFamily=static_cast<int>(YmccFamily::Unknown);break;
    }
    scenario=std::to_string(row)+":"+identity.deviceClass+":state="+std::to_string(state);
    const bool oldBound=owner_before::inputCaptureMotionSelectorBound(),newBound=candidate::inputCaptureMotionSelectorBound();need(oldBound==newBound,"LIVE_BOUND_EQUIVALENCE");++boundPairs;
    for(const auto& sample:samples)for(unsigned control=0;control<8;++control){
     const bool live=control!=1,gyro=control!=2,accel=control!=3;const std::string gp="provider-a",ap=control==4?"provider-b":"provider-a";const UINT64 gg=control==5?0:control==6?72:73,ag=control==7?74:gg;
     owner_proof::resetCost();const auto a=owner_before::probeActive(live,gyro,accel,gp,ap,gg,ag,sample);const auto prior=owner_proof::cost();
     owner_proof::resetCost();const auto b=candidate::probeActive(live,gyro,accel,gp,ap,gg,ag,sample);const auto current=owner_proof::cost();
     need(a.admitted==b.admitted,"PAIR_ADMISSION_EQUIVALENCE");need(a.matrixIdentity==b.matrixIdentity,"MATRIX_RECIPE_EQUIVALENCE");need(a.processCalls==b.processCalls,"ACTUAL_EFFECT_COUNT_EQUIVALENCE");
     for(unsigned axis=0;axis<6;++axis){need(a.active[axis]==b.active[axis],"SIX_AXIS_OUTPUT_EQUIVALENCE");need(a.processAxes[axis]==b.processAxes[axis],"ACTUAL_PROCESS_AXES_EQUIVALENCE");}
     if(current.reviewed||current.defaults||current.legacy)throw std::runtime_error("NO_REPEATED_CLASS_TABLE_SEARCH:"+owner_proof::costJson(current).dump());owner_proof::add(oldTotal,prior);owner_proof::add(newTotal,current);oldEffects+=a.processCalls;newEffects+=b.processCalls;if(b.admitted)++admitted;else ++rejected;++windows;
    }
   }
  }
  need(legacySupersession>0,"SUPERSEDED_LEGACY_NEGATIVE_EXECUTED");need(oldEffects==newEffects&&oldEffects>0&&admitted>0&&rejected>0,"POSITIVE_PROCESSING_REMAINS_ACTIVE");need(oldTotal.reviewed>0&&oldTotal.defaults>0&&oldTotal.legacy>0&&newTotal.primary>0,"REAL_CLASSIFICATION_COST_OBSERVED");
  std::cout<<json{{"schemaVersion",1},{"status","PASS"},{"checks",owner_proof::checks},{"boundPairs",boundPairs},{"windows",windows},{"admitted",admitted},{"rejected",rejected},{"oldProcessCalls",oldEffects},{"newProcessCalls",newEffects},{"supersededLegacyAttempts",legacySupersession},{"oldCost",owner_proof::costJson(oldTotal)},{"newCost",owner_proof::costJson(newTotal)},{"fullBusExecuted",false},{"realDevicesAccessed",false},{"wholeProcessCpuReduction",nullptr}}.dump()<<'\n';return 0;
 }catch(const std::exception& e){std::cout<<json{{"status","FAIL"},{"error",e.what()},{"scenario",scenario},{"checks",owner_proof::checks},{"windows",windows}}.dump()<<'\n';return 1;}
}