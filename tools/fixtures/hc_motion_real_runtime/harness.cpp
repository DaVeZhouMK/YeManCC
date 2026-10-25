// Actual CPU DLL only; no controller, sensor, Host or OEM operation.
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <json.hpp>
#include "controller_motion_session.h"
using json=nlohmann::json;
namespace cm=ymcc::controller_motion;

// These declarations supply the exact production block's local environment.
// Every function pointer below is resolved from the existing pinned real DLL.
static void* g_gm=nullptr;
static decltype(cm::Ops::process) g_gmProcess=nullptr;
static decltype(cm::ReadOps::getCal) g_gmGetCal=nullptr,g_gmGetGravity=nullptr;
static decltype(cm::ReadOps::getPlayer) g_gmPlayer=nullptr,g_gmWorld=nullptr;
static bool g_gmProcessedThisTick=false;
static struct { std::string motionInput; } g_realStickTest;
static float g_ymMadgQ[4]={0,0,-1,0};
// @@VERBATIM_FUNCTIONS@@

struct WindowsResult {
    cm::MotionVector3 calibrated,gravity;
    cm::MotionQuaternion orientation;
    cm::MotionVector2 player,world;
    bool processed=false;
};
static WindowsResult windowsTick(const cm::RawSixAxes& axes,float motionDeltaSeconds,
    bool live=true,bool pairProven=true,bool hcMatrixAdmitted=true,bool calibrationPairProven=false) {
    const bool gyro=true,accel=true;
    const double mgx=axes.gx,mgy=axes.gy,mgz=axes.gz,maxa=axes.ax,may=axes.ay,maz=axes.az;
    const double hcCandidateGx=mgx,hcCandidateGy=mgy,hcCandidateGz=mgz;
    const double hcCandidateAx=maxa,hcCandidateAy=may,hcCandidateAz=maz;
    float cgx=0,cgy=0,cgz=0,gravityX=0,gravityY=0,gravityZ=0;
    float orientationW=0,orientationX=0,orientationY=-1,orientationZ=0;
    float psx=0,psy=0,wsx=0,wsy=0;
// @@VERBATIM_WINDOWS_CONSUMER@@
    return {{cgx,cgy,cgz},{gravityX,gravityY,gravityZ},
        {orientationW,orientationX,orientationY,orientationZ},{psx,psy},{wsx,wsy},g_gmProcessedThisTick};
}
struct Library {
    HMODULE module=nullptr;
    cm::Ops ops;
    cm::ReadOps reads;
    template<class F> F resolve(const char* name) {
        auto pointer=GetProcAddress(module,name);
        if(!pointer)throw std::runtime_error(std::string("missing real DLL export: ")+name);
        return reinterpret_cast<F>(pointer);
    }
    explicit Library(const wchar_t* path) {
        module=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module)throw std::runtime_error("cannot load pinned real GamepadMotion DLL");
        ops={resolve<decltype(ops.create)>("CreateGamepadMotion"),resolve<decltype(ops.destroy)>("DeleteGamepadMotion"),
             resolve<decltype(ops.setMode)>("SetCalibrationMode"),resolve<decltype(ops.setOffset)>("SetCalibrationOffset"),
             resolve<decltype(ops.process)>("ProcessMotion")};
        reads={resolve<decltype(reads.getCal)>("GetCalibratedGyro"),resolve<decltype(reads.getGravity)>("GetGravity"),
               resolve<decltype(reads.getPlayer)>("GetPlayerSpaceGyro"),resolve<decltype(reads.getWorld)>("GetWorldSpaceGyro")};
        g_gmProcess=ops.process;g_gmGetCal=reads.getCal;g_gmGetGravity=reads.getGravity;
        g_gmPlayer=reads.getPlayer;g_gmWorld=reads.getWorld;
    }
    ~Library(){if(module)FreeLibrary(module);}
    Library(const Library&)=delete;
    Library& operator=(const Library&)=delete;
};
struct Handle {
    Library& lib;void* value=nullptr;
    Handle(Library& library,const cm::Calibration& offset):lib(library),value(library.ops.create()) {
        if(!value)throw std::runtime_error("CreateGamepadMotion returned null");
        lib.ops.setMode(value,0);
        lib.ops.setOffset(value,offset.x,offset.y,offset.z,offset.weight);
    }
    ~Handle(){if(value)lib.ops.destroy(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
static json failures=json::array(),coverage=json::array(),groupCounts=json::object();
static std::uint64_t checks=0,totalFrames=0,failureCount=0;
static json failureGroups=json::object();
static std::string currentCase;
static void check(bool success,const char* group,const std::string& detail) {
    ++checks;
    groupCounts[group]=groupCounts.value(group,0u)+1;
    if(!success){++failureCount;failureGroups[group]=failureGroups.value(group,0u)+1;
        if(failures.size()<80)failures.push_back({{"case",currentCase},{"group",group},{"detail",detail}});}
}
static bool closeEnough(double a,double b,double tolerance=0.00002) {
    return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=tolerance*(1+std::abs(b));
}
static bool same3(const cm::MotionVector3& a,const cm::MotionVector3& b) {
    return closeEnough(a.x,b.x)&&closeEnough(a.y,b.y)&&closeEnough(a.z,b.z);
}
static bool same2(const cm::MotionVector2& a,const cm::MotionVector2& b) {
    return closeEnough(a.x,b.x)&&closeEnough(a.y,b.y);
}
static bool finiteQ(const cm::MotionQuaternion& q) {
    return std::isfinite(q.w)&&std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z);
}
static double qnorm(const cm::MotionQuaternion& q) {return q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z;}
static std::array<double,3> oracleMatrix(const json& m,const std::array<double,3>& input) {
    std::array<double,3> output{};
    for(std::size_t i=0;i<3;++i)output.at(m.at("swap").at(i).get<unsigned>())=input.at(i);
    for(std::size_t i=0;i<3;++i)output[i]*=m.at("axis").at(i).get<double>();
    return output;
}
static std::array<double,3> inverseOracle(const json& m,const std::array<double,3>& canonical) {
    std::array<double,3> raw{};
    for(std::size_t i=0;i<3;++i){const auto dst=m.at("swap").at(i).get<unsigned>();raw[i]=canonical[dst]/m.at("axis").at(dst).get<double>();}
    return raw;
}
static cm::RawSixAxes mappedAxes(const json& g,const json& a,const std::array<double,3>& rawG,const std::array<double,3>& rawA) {
    double gx=0,gy=0,gz=0,ax=0,ay=0,az=0;
    const bool gyro=ymccApplyNamedImuMatrix(g.at("id"),rawG[0],rawG[1],rawG[2],gx,gy,gz);
    const bool accel=ymccApplyNamedImuMatrix(a.at("id"),rawA[0],rawA[1],rawA[2],ax,ay,az);
    check(gyro&&accel,"matrices","actual named matrices accepted");
    const auto eg=oracleMatrix(g,rawG),ea=oracleMatrix(a,rawA);
    check(closeEnough(gx,eg[0])&&closeEnough(gy,eg[1])&&closeEnough(gz,eg[2])&&closeEnough(ax,ea[0])&&closeEnough(ay,ea[1])&&closeEnough(az,ea[2]),
        "matrices","actual matrices match independently read HC Axis and AxisSwap");
    return {static_cast<float>(gx),static_cast<float>(gy),static_cast<float>(gz),
            static_cast<float>(ax),static_cast<float>(ay),static_cast<float>(az)};
}
static cm::RawSixAxes oracleAxes(const json& g,const json& a,const std::array<double,3>& rawG,const std::array<double,3>& rawA) {
    const auto gyro=oracleMatrix(g,rawG),accel=oracleMatrix(a,rawA);
    return {static_cast<float>(gyro[0]),static_cast<float>(gyro[1]),static_cast<float>(gyro[2]),
            static_cast<float>(accel[0]),static_cast<float>(accel[1]),static_cast<float>(accel[2])};
}
static cm::MotionVector3 read3(decltype(cm::ReadOps::getCal) fn,void* handle) {
    cm::MotionVector3 v;fn(handle,&v.x,&v.y,&v.z);return v;
}
static cm::MotionVector2 read2(decltype(cm::ReadOps::getPlayer) fn,void* handle,float parameter) {
    cm::MotionVector2 v;fn(handle,&v.x,&v.y,parameter);return v;
}
struct Sequence {
    Library& lib;cm::Calibration offset;Handle reference,windows;
    cm::Session controller;cm::Key key;double previous=10.0;
    Sequence(Library& library,std::string id,cm::Calibration calibration):lib(library),offset(calibration),
        reference(library,calibration),windows(library,calibration),key{std::move(id),1,2,3,0} {
        g_gm=windows.value;ymccMadgwickResetOrientation();
        check(controller.select(key,lib.ops,lib.reads,offset,previous),"session","real controller handle selection");
    }
    void frame(const cm::RawSixAxes& actual,const cm::RawSixAxes& expected,double delta,unsigned plane,bool stationary=false) {
        const double now=previous+delta;
        const float dt=static_cast<float>(now-previous);previous=now;
        g_realStickTest.motionInput=plane==1?"player-space":plane==2?"world-space":"local-space";
        const auto w=windowsTick(actual,dt);
        const auto c=controller.process(key,actual,now,{plane==1,plane==2});
        lib.ops.process(reference.value,expected.gx,expected.gy,expected.gz,expected.ax,expected.ay,expected.az,dt);
        const auto cal=read3(lib.reads.getCal,reference.value),gravity=read3(lib.reads.getGravity,reference.value);
        check(w.processed&&c.has_value(),"native-call","both real production paths processed admitted input");
        check(same3(w.calibrated,cal),"windows-calibration","real C ABI calibrated read agrees with independent handle");
        check(same3(w.gravity,gravity),"windows-gravity","real C ABI gravity agrees with independent handle");
        check(c&&same3(c->calibrated,cal),"controller-calibration","real Controller Session calibrated read");
        check(c&&same3(c->gravity,gravity),"controller-gravity","real Controller Session gravity read");
        check(closeEnough(cal.x,expected.gx-offset.x)&&closeEnough(cal.y,expected.gy-offset.y)&&closeEnough(cal.z,expected.gz-offset.z),
            "calibration-units","Manual offset is subtracted once in degrees per second");
        check(finiteQ(w.orientation)&&closeEnough(qnorm(w.orientation),1),"orientation-runtime","Windows quaternion stays finite and normalized");
        check(c&&c->orientationFinite&&closeEnough(qnorm(c->orientation),1),"controller-orientation","Controller quaternion stays finite and normalized");
        if(c)check(closeEnough(w.orientation.w,c->orientation.w)&&closeEnough(w.orientation.x,c->orientation.x)&&
            closeEnough(w.orientation.y,c->orientation.y)&&closeEnough(w.orientation.z,c->orientation.z),
            "orientation-runtime","Windows and Controller overlays agree with identical real DLL output");
        if(stationary)check(finiteQ(w.orientation)&&closeEnough(w.orientation.w,0)&&closeEnough(w.orientation.x,0)&&
            closeEnough(w.orientation.y,-1)&&closeEnough(w.orientation.z,0),"orientation-stationary","zero correction and zero calibrated rate retain the initial quaternion");
        if(plane==1){const auto p=read2(lib.reads.getPlayer,reference.value,1.41f);
            check(same2(w.player,p)&&c&&c->player&&same2(*c->player,p),"player-space","actual player-space output at HC 1.41 parameter");}
        if(plane==2){const auto p=read2(lib.reads.getWorld,reference.value,.125f);
            check(same2(w.world,p)&&c&&c->world&&same2(*c->world,p),"world-space","actual world-space output at HC 0.125 parameter");}
        if(c)check(c->player.has_value()==(plane==1)&&c->world.has_value()==(plane==2),"planes","only requested planes are read");
        ++totalFrames;
    }
    void retirement() {
        const cm::RawSixAxes sample{2,3,4,0,0,-1};
        controller.revoke();
        check(!controller.process(key,sample,previous+.008),"retirement","revoked source cannot process through freed real DLL handle");
        check(!controller.select(key,lib.ops,lib.reads,offset,previous),"retirement","same retired source token cannot restart");
        auto fresh=key;++fresh.captureEpoch;
        check(controller.select(fresh,lib.ops,lib.reads,offset,previous),"retirement","new capture epoch creates a fresh real DLL handle");
        const auto next=controller.process(fresh,sample,previous+.008);
        check(next&&same3(next->calibrated,{sample.gx-offset.x,sample.gy-offset.y,sample.gz-offset.z}),
            "retirement","new session restores its own calibration");
    }
};
static void recipeSequence(Library& lib,const json& recipe) {
    currentCase=recipe.at("id");const auto& g=recipe.at("gyro");const auto& a=recipe.at("accel");
    Sequence seq(lib,currentCase,{.125f,-.25f,.5f,10});
    for(unsigned i=0;i<384;++i) {
        const bool still=i<32;
        const double t=static_cast<double>(i-std::min(i,32u))*.008;
        const std::array<double,3> canonicalG=still?std::array<double,3>{.125,-.25,.5}:
            std::array<double,3>{.125+std::sin(t)*120.,-.25+std::cos(t*1.3)*70.,.5+std::sin(t*.7)*90.};
        const std::array<double,3> canonicalA=still?std::array<double,3>{0,0,-1}:
            std::array<double,3>{std::sin(t*.4)*.3,std::sin(t*.7)*.3,-std::sqrt(1-.09*std::pow(std::sin(t*.4),2)-.09*std::pow(std::sin(t*.7),2))};
        const auto rawG=inverseOracle(g,canonicalG),rawA=inverseOracle(a,canonicalA);
        seq.frame(mappedAxes(g,a,rawG,rawA),oracleAxes(g,a,rawG,rawA),i%3==0?.004:i%3==1?.008:.012,i%3,still);
    }
    seq.retirement();
    coverage.push_back({{"id",currentCase},{"frames",384},{"kind",recipe.at("kind")}});
}
static void logSequence(Library& lib,const json& row) {
    currentCase=row.at("family");Sequence seq(lib,"log-replay:"+currentCase,{});
    unsigned frames=0;
    for(const auto& f:row.at("frames")) {
        const auto& raw=f.at("raw");
        const std::array<double,3> g={raw.at("gx"),raw.at("gy"),raw.at("gz")};
        const std::array<double,3> a={raw.at("ax"),raw.at("ay"),raw.at("az")};
        double gx=0,gy=0,gz=0,ax=0,ay=0,az=0;
        const bool gm=ymccApplyNamedImuMatrix(f.at("matrixIds").at("gyro"),g[0],g[1],g[2],gx,gy,gz);
        const bool am=ymccApplyNamedImuMatrix(f.at("matrixIds").at("accel"),a[0],a[1],a[2],ax,ay,az);
        const auto& values=f.at("candidate");
        const cm::RawSixAxes actual{static_cast<float>(gx),static_cast<float>(gy),static_cast<float>(gz),
            static_cast<float>(ax),static_cast<float>(ay),static_cast<float>(az)};
        const cm::RawSixAxes expected{values[0],values[1],values[2],values[3],values[4],values[5]};
        check(gm&&am&&closeEnough(gx,expected.gx)&&closeEnough(gy,expected.gy)&&closeEnough(gz,expected.gz)&&
            closeEnough(ax,expected.ax)&&closeEnough(ay,expected.ay)&&closeEnough(az,expected.az),
            "recorded-matrices","actual current mapping matches numeric candidate recorded beside the same raw sample");
        seq.frame(actual,expected,f.at("deltaSeconds").get<double>(),frames%3);++frames;
    }
    check(frames>0,"recorded-inputs","actual supplied log contributes raw six-axis vectors");
    coverage.push_back({{"id",currentCase},{"frames",frames},{"kind","recorded-vectors-only"}});
}
static void rejectionCases(Library& lib) {
    currentCase="Windows admission and Session independence";
    const cm::Calibration offset{};Handle win(lib,offset),other(lib,offset);g_gm=win.value;
    const cm::RawSixAxes axes{10,20,30,.1f,.2f,-.9f};
    for(unsigned flags=0;flags<4;++flags) {
        g_realStickTest.motionInput="local-space";
        const bool live=(flags&1)!=0,pair=(flags&2)!=0;
        auto r=windowsTick(axes,.008f,live,pair,pair);
        check(r.processed==(live&&pair),"windows-admission","verbatim Windows live/pair gate");
    }
    cm::Session first,second;const cm::Key key1{"cpu-independent-a",7,8,9,0},key2{"cpu-independent-b",7,8,9,0};
    check(first.select(key1,lib.ops,lib.reads,{1,2,3,10},20)&&second.select(key2,lib.ops,lib.reads,{4,5,6,10},20),
        "independence","two independent real controller handles");
    const auto a=first.process(key1,axes,20.008),b=second.process(key2,axes,20.008);
    check(a&&b&&same3(a->calibrated,{9,18,27})&&same3(b->calibrated,{6,15,24}),"independence","per-source offsets do not cross sessions");
    check(!first.process(key2,axes,20.016),"independence","foreign key cannot invoke another session");
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=3)throw std::runtime_error("expected pinned DLL and fixture JSON paths");
        const json data=json::parse(std::ifstream(argv[2]));
        { Library lib(argv[1]);
          rejectionCases(lib);
          for(const auto& r:data.at("recipes"))recipeSequence(lib,r);
          for(const auto& r:data.at("logs"))logSequence(lib,r);
          g_gm=nullptr;
        }
        json result={{"status",failureCount==0?"pass":"fail"},{"checks",checks},{"failures",failures},
            {"frames",totalFrames},{"coverage",coverage},{"groups",groupCounts},{"failureCount",failureCount},{"failureGroups",failureGroups},
            {"dllLoaded",true},{"realLibraryCallbacks",true},{"sensorIo",false},{"hostExecuted",false},
            {"hardwareAcceptance",false},{"recordedInputBoundary","raw-vector replay from exported logs; not source admission, complete original session state, or original Host/game consumption"}};
        std::cout<<result.dump()<<'\n';return failureCount==0?0:1;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
