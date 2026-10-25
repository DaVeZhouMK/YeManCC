extern "C" int legion_wire_emit(const char*,const char*,const char*,unsigned,const float*,const float*,const float*,int,unsigned long long);
static unsigned outputRows=0,sourceRoutes=0,staleDenials=0;
static void emitController(const MotionSample& input,const std::optional<cm::MotionResult>& result,const std::string& name,bool neutral=false){
    const auto controllerMotion=std::optional<MotionSample>{input};const auto controllerProcessed=result;
    const bool controllerMotionRoute=true,live=true,pairProven=false,wakeGyroHold=false;
    const bool g_gmLocked=false,g_gmProcessedThisTick=false,windowsProcessorOwnerReady=false;
    struct WindowsBoundary {ymcc::windows_motion::Stamp submission;} windowsSensorView;
    const auto inputCaptureWindowsProcessingFrameCurrent=[](const WindowsBoundary&){CHECK(false);return false;};
    float cgx=0,cgy=0,cgz=0,gravityX=0,gravityY=0,gravityZ=0,orientationW=0,orientationX=0,orientationY=-1,orientationZ=0,psx=0,psy=0,wsx=0,wsy=0,motionDeltaSeconds=0;
    std::string matrixIdentity;
// @@ACTUAL_CONTROLLER_OUTPUT_FINITE@@
// @@ACTUAL_CONTROLLER_PAIR@@
// @@ACTUAL_MOTION_PAIR@@
// @@ACTUAL_CONTROLLER_RESULT_COPY@@
// @@ACTUAL_PROCESSOR_READY@@
// @@ACTUAL_FINAL_GYRO_GATE@@
    CHECK(gyroSubmitAllowed);if(!result)return;
    const auto raw=axes(input);const float rawAxes[]{raw.gx,raw.gy,raw.gz,raw.ax,raw.ay,raw.az};
    const float q[]{orientationW,orientationX,orientationY,orientationZ},cal[]{cgx,cgy,cgz};
    const auto h=actualHandle();const auto slot=handle(h);CHECK(slot!=nullptr);if(slot){const float seen[]{slot->last.gx,slot->last.gy,slot->last.gz,slot->last.ax,slot->last.ay,slot->last.az};for(unsigned i=0;i<6;++i)CHECK(near(rawAxes[i],seen[i]));}
    double norm=0;for(const auto value:q){CHECK(std::isfinite(value));norm+=double(value)*value;}CHECK(std::fabs(norm-1)<0.0001);
    auto common=commonSnapshot();CommonFrameSourceScope cs(&common);LegionMotionSubmissionScope ls(&input);const auto probe=carrierProbe(true,neutral);CHECK(probe.scoped&&probe.controller&&probe.windows&&probe.issue&&probe.request);
    if(probe.issue&&probe.request){const auto file=(calibrationPath.parent_path()/"owner-host-wire.jsonl").string();const auto n=legion_wire_emit(file.c_str(),name.c_str(),g_machineIdentity.deviceClass.c_str(),input.source.sensorIndex,rawAxes,q,cal,neutral?1:0,currentPowerGeneration());CHECK(n==2);outputRows+=n;}
}
static void controllerHostScenarios(){
 for(const auto& model:familyTopology.at("models"))for(unsigned side=0;side<(model.at("rightAllowed").get<bool>()?2u:1u);++side){
    setupModel(model,side);sameTickRejected();++sourceRoutes;
    for(unsigned step=0;step<32;++step){nextFresh();g_realStickTest.motionInput=step%3==0?"local-space":step%3==1?"player-space":"world-space";const auto input=decodeAtCurrentQueueTime(step%2==0);const auto result=actualConsume(input);finiteResult(result);emitController(input,result,g_machineIdentity.deviceClass+"/bank-"+std::to_string(side)+"/frame-"+std::to_string(step));}
    nextFresh();const auto old=decodeAtCurrentQueueTime();const auto result=actualConsume(old);finiteResult(result);
    ++g_powerGeneration;frameCurrent.power=currentPowerGeneration();legionMotionRevoke();
    auto common=commonSnapshot();{CommonFrameSourceScope cs(&common);LegionMotionSubmissionScope ls(&old);const auto blocked=carrierProbe(true,false);CHECK(!blocked.issue&&!blocked.request);if(!blocked.issue&&!blocked.request)++staleDenials;const auto release=carrierProbe(true,true);CHECK(release.issue&&release.request);
      if(result&&release.issue&&release.request){const auto raw=axes(old);const float rawAxes[]{raw.gx,raw.gy,raw.gz,raw.ax,raw.ay,raw.az},q[]{result->orientation.w,result->orientation.x,result->orientation.y,result->orientation.z},cal[]{result->calibrated.x,result->calibrated.y,result->calibrated.z};const auto file=(calibrationPath.parent_path()/"owner-host-wire.jsonl").string();outputRows+=legion_wire_emit(file.c_str(),"retired-source-neutral-release",g_machineIdentity.deviceClass.c_str(),side,rawAxes,q,cal,1,currentPowerGeneration());}}
    inputCaptureLegionMotionSessionRevoke();CHECK(liveHandles()==0);
 }
}
int main(int argc,char** argv){
 if(argc!=5)return 3;std::cout<<std::unitbuf;calibrationPath=std::filesystem::path(argv[1])/"initial.json";{std::ifstream f(argv[2]);f>>familyTopology;}if(familyTopology.at("schema")!=1)return 3;
 if(!bridge_open(argv[3],argv[4])){std::cerr<<"ABI gap: "<<bridge_error()<<'\n';return 4;}
 try{activeCase="controller-host-codec";controllerHostScenarios();}catch(const std::exception& e){++failures;std::cerr<<"RUNTIME GAP "<<e.what()<<'\n';}
 inputCaptureLegionMotionSessionRevoke();g_legionMotionSession.reset();g_controllerCalibrationTransaction.reset();CHECK(liveHandles()==0);const bool closed=bridge_close()!=0;CHECK(closed);libraryLoaded=false;CHECK(sourceRoutes==6&&staleDenials==6);
 const json report={{"status",failures?"fail":"pass"},{"checks",checks},{"failures",failures},{"sourceRoutes",sourceRoutes},{"staleDenials",staleDenials},{"outputRows",outputRows},{"cleanupComplete",closed&&liveHandles()==0},{"actualNativeDll",true},{"actualRawDecodeAndSourceGuards",true},{"hardwareIo",false},{"pipeTransportExecuted",false},{"physicalAcceptance",false}};
 std::ofstream(std::filesystem::path(argv[1])/"REPORT.json")<<report.dump(2);std::cout<<report.dump()<<'\n';return failures?1:0;
}
