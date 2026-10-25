// Actual sink/snapshot/Windows ProcessMotion+Madgwick block. Explicit memory reports.
#define NOMINMAX
#include <windows.h>
#include <winrt/Windows.Devices.Sensors.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <json.hpp>
using json=nlohmann::json;
// @@CONSTANTS@@
// @@FUNCTIONS@@
// @@SINK@@
static std::shared_ptr<InputCaptureSensorSink> g_inputCaptureSink;
static void* g_gm=nullptr;
static void (*g_gmProcess)(void*,float,float,float,float,float,float,float)=nullptr;
static void (*g_gmGetCal)(void*,float*,float*,float*)=nullptr;
static void (*g_gmGetGravity)(void*,float*,float*,float*)=nullptr;
static void (*g_gmPlayer)(void*,float*,float*,float)=nullptr;
static void (*g_gmWorld)(void*,float*,float*,float)=nullptr;
static bool g_gmProcessedThisTick=false;
static struct {std::string motionInput;double gyroThreshold=2000;}g_realStickTest;
static float g_ymMadgQ[4]={0,0,-1,0};
// @@ORIENTATION@@
struct Snapshot {
 bool gyro=false,accel=false;std::array<double,6> axes{};
 uint64_t gg=0,ag=0,gs=0,as=0,gt=0,at=0,gr=0,ar=0;
};
static Snapshot snapshot(){
 bool gyro=false,accel=false;double gx=0,gy=0,gz=0,ax=0,ay=0,az=0;
 UINT64 gyroBindingGeneration=0,accelBindingGeneration=0,gyroSequence=0,accelSequence=0;
 UINT64 gyroTimestamp100ns=0,accelTimestamp100ns=0,gyroRejectedSamples=0,accelRejectedSamples=0;
 ULONGLONG gyroReceiptTick=0,accelReceiptTick=0;double gyroTimeOfDayMs=0,accelTimeOfDayMs=0;
// @@SNAPSHOT@@
 return {gyro,accel,{gx,gy,gz,ax,ay,az},gyroBindingGeneration,accelBindingGeneration,gyroSequence,accelSequence,gyroReceiptTick,accelReceiptTick,gyroRejectedSamples,accelRejectedSamples};
}
static json diagnostic(const Snapshot& s){
 const auto gyroSequence=s.gs,accelSequence=s.as,gyroRejectedSamples=s.gr,accelRejectedSamples=s.ar;
 json eventObj_gyro=json::object(),eventObj_accel=json::object();
// @@DIAGNOSTIC@@
 return {{"gyro",eventObj_gyro},{"accel",eventObj_accel}};
}
static bool admitted(const Snapshot& s){return inputCaptureMotionPairAdmitted(true,true,s.gyro,s.accel,true,s.gg,s.ag,true,g_inputCaptureSink->activeBindingGeneration.load());}
static bool windowsTick(const Snapshot& s){
 const bool live=true,gyro=s.gyro,accel=s.accel,controllerMotionRoute=false;
 const bool pairProven=admitted(s),hcMatrixAdmitted=pairProven,calibrationPairProven=false;
 const float motionDeltaSeconds=0.008f;
 double gx=s.axes[0],gy=s.axes[1],gz=s.axes[2];
// @@CLIP@@
 double mgx=gx,mgy=gy,mgz=gz,maxa=s.axes[3],may=s.axes[4],maz=s.axes[5];
 const double hcCandidateGx=mgx,hcCandidateGy=mgy,hcCandidateGz=mgz,hcCandidateAx=maxa,hcCandidateAy=may,hcCandidateAz=maz;
 double cgx=0,cgy=0,cgz=0;float gravityX=0,gravityY=0,gravityZ=0;
 float orientationW=0,orientationX=0,orientationY=0,orientationZ=0,psx=0,psy=0,wsx=0,wsy=0;
// @@WINDOWS@@
 return g_gmProcessedThisTick;
}
struct Library {
 HMODULE module=nullptr;void* (*create)()=nullptr;void (*destroy)(void*)=nullptr;
 void (*setMode)(void*,int)=nullptr;void (*setOffset)(void*,float,float,float,int)=nullptr;
 template<class F>F load(const char* name){auto p=GetProcAddress(module,name);if(!p)throw std::runtime_error(name);return reinterpret_cast<F>(p);}
 explicit Library(const wchar_t* path){module=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)throw std::runtime_error("load pinned native library");
 create=load<decltype(create)>("CreateGamepadMotion");destroy=load<decltype(destroy)>("DeleteGamepadMotion");setMode=load<decltype(setMode)>("SetCalibrationMode");setOffset=load<decltype(setOffset)>("SetCalibrationOffset");
 g_gmProcess=load<decltype(g_gmProcess)>("ProcessMotion");g_gmGetCal=load<decltype(g_gmGetCal)>("GetCalibratedGyro");g_gmGetGravity=load<decltype(g_gmGetGravity)>("GetGravity");g_gmPlayer=load<decltype(g_gmPlayer)>("GetPlayerSpaceGyro");g_gmWorld=load<decltype(g_gmWorld)>("GetWorldSpaceGyro");}
 ~Library(){if(g_gm)destroy(g_gm);if(module)FreeLibrary(module);}
 void reset(){if(g_gm)destroy(g_gm);g_gm=create();if(!g_gm)throw std::runtime_error("create");setMode(g_gm,0);setOffset(g_gm,0,0,0,0);ymccMadgwickResetOrientation();}
};
static unsigned checks=0,failures=0,invalidCases=0;static json errors=json::array(),groups=json::object();static std::string active;
static void check(bool ok,const char* group){++checks;if(ok)return;++failures;groups[group]=groups.value(group,0u)+1;if(errors.size()<70)errors.push_back({{"case",active},{"group",group}});}
static bool orientationFinite(){for(auto v:g_ymMadgQ)if(!std::isfinite(v))return false;return true;}
template<class S>static uint64_t rejected(const S& sink,bool gyro){if constexpr(requires{sink.gyroRejectedSamples;sink.accelRejectedSamples;})return gyro?sink.gyroRejectedSamples:sink.accelRejectedSamples;else return 0;}
static void send(bool gyro,const std::array<double,3>& v,uint64_t generation=7,uint64_t tick=100){
 const UINT64 ts=134361240000000000ull+tick*10000;
 if(gyro)g_inputCaptureSink->OnRawGyro(v[0],v[1],v[2],ts,tick,generation);else g_inputCaptureSink->OnRawAccel(v[0],v[1],v[2],ts,tick,generation);
}
static void seed(Library& lib){lib.reset();g_inputCaptureSink=std::make_shared<InputCaptureSensorSink>();g_inputCaptureSink->SetBindingGeneration(7);send(true,{0.1,-0.2,0.3});send(false,{0.05,-0.1,0.99});for(int i=0;i<32;++i)check(windowsTick(snapshot())&&orientationFinite(),"positive-real-runtime");}
static void invalid(Library& lib){
 const std::array<double,5> bad={std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),1e300,-1e300};
 for(unsigned b=0;b<bad.size();++b)for(unsigned axis=0;axis<6;++axis){active="invalid-"+std::to_string(b)+"-axis-"+std::to_string(axis);seed(lib);const auto before=snapshot();const bool gyro=axis<3;std::array<double,3> v=gyro?std::array<double,3>{0.1,-0.2,0.3}:std::array<double,3>{0.05,-0.1,0.99};v[axis%3]=bad[b];
 send(gyro,v,7,200);const auto after=snapshot();
 check(!(gyro?after.gyro:after.accel),"numeric-channel-unavailable");check(!admitted(after),"numeric-pair-rejected");
 check((gyro?g_inputCaptureSink->gyroSequence:g_inputCaptureSink->accelSequence)==(gyro?before.gs:before.as)&&(gyro?g_inputCaptureSink->gyroReceiptTick:g_inputCaptureSink->accelReceiptTick)==100,"numeric-invalid-not-an-observation");
 check(rejected(*g_inputCaptureSink,gyro)==1,"numeric-rejection-count");
 check(diagnostic(after).at(gyro?"gyro":"accel").value("rejectedNumericSamples",uint64_t(0))==1,"numeric-logged-rejection-count");
 check(!windowsTick(after),"numeric-invalid-skips-real-process");check(orientationFinite(),"numeric-orientation-not-contaminated");
 send(gyro,gyro?std::array<double,3>{0.1,-0.2,0.3}:std::array<double,3>{0.05,-0.1,0.99},7,300);
 for(int i=0;i<16;++i)check(windowsTick(snapshot())&&orientationFinite(),"numeric-good-report-recovers");++invalidCases;}
}
static void sourceOnly(Library& lib){
 const double maxFloat=static_cast<double>((std::numeric_limits<float>::max)());
 const std::array<double,9> valid={0,-0.0,1,-1,1e-300,-1e-300,maxFloat,-maxFloat,std::numeric_limits<double>::denorm_min()};
 for(double value:valid)for(bool gyro:{false,true}){active="finite-float-domain";seed(lib);send(gyro,{value,value,value},7,200);check(admitted(snapshot()),"numeric-valid-domain-preserved");check(rejected(*g_inputCaptureSink,gyro)==0,"numeric-valid-not-rejected");}
 for(bool gyro:{false,true}){active="overflow-boundary";seed(lib);send(gyro,{std::nextafter(maxFloat,std::numeric_limits<double>::infinity()),0,0},7,200);check(!admitted(snapshot()),"numeric-nextafter-float-max-rejected");}
 active="retired-invalid-callback";seed(lib);g_inputCaptureSink->SetBindingGeneration(8);send(true,{1,2,3},8);send(false,{0,0,1},8);send(true,{NAN,0,0},7);send(false,{NAN,0,0},7);check(admitted(snapshot()),"numeric-old-invalid-cannot-revoke-current");check(rejected(*g_inputCaptureSink,true)==0&&rejected(*g_inputCaptureSink,false)==0,"numeric-old-invalid-not-counted");
 active="binding-counter-lifetime";seed(lib);send(true,{NAN,0,0});send(false,{0,NAN,0});check(rejected(*g_inputCaptureSink,true)==1&&rejected(*g_inputCaptureSink,false)==1,"numeric-both-counted");
 g_inputCaptureSink->SetBindingGeneration(7);check(rejected(*g_inputCaptureSink,true)==1,"numeric-same-binding-keeps-diagnostic");g_inputCaptureSink->SetBindingGeneration(8);check(rejected(*g_inputCaptureSink,true)==0&&rejected(*g_inputCaptureSink,false)==0,"numeric-new-binding-clears-diagnostic");
 send(true,{NAN,0,0},8);g_inputCaptureSink->ResetSamples();check(rejected(*g_inputCaptureSink,true)==0,"numeric-reset-clears-diagnostic");
}
int wmain(int argc,wchar_t** argv){try{if(argc!=2)return 2;Library lib(argv[1]);invalid(lib);sourceOnly(lib);g_inputCaptureSink.reset();std::cout<<json({{"status",failures?"fail":"pass"},{"checks",checks},{"failureCount",failures},{"failureGroups",groups},{"failures",errors},{"invalidCases",invalidCases},{"actualNativeLibraryLoaded",true},{"actualSinkAndSnapshot",true},{"actualWindowsConsumer",true},{"winRtCallbackInvoked",false},{"hardwareIo",false},{"hostExecuted",false}}).dump()<<'\n';return failures?1:0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
