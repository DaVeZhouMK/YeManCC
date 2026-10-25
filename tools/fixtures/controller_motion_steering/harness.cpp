#include "json.hpp"
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using json = nlohmann::json;
struct XINPUT_GAMEPAD { std::uint16_t wButtons=0; std::uint8_t bLeftTrigger=0,bRightTrigger=0;
    short sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0; };
struct Settings {
    std::string steeringAxis="roll",motionInput="local-space",aimingSightsTrigger,velocityMode="position",outputAxis="both";
    bool motionActionEnabled=true,targetGyroEnabled=true,motionTriggerResolved=true,invertHorizontal=false,invertVertical=false;
    double gyroMultiplier=1,accelerometerMultiplier=1,gyroThreshold=1;
    double steeringMaxAngle=45,steeringPower=1,steeringDeadzone=0,aimingSightsMultiplier=1;
    double motionSensitivityX=0.001,motionSensitivityY=0.001,velocityScale=1;
    double velocityAccumulatedX=0,velocityAccumulatedY=0;
    std::vector<std::pair<double,double>> motionSensitivityArray;
};
static Settings g_realStickTest;
static unsigned long long checks=0,failures=0;
static std::string active;
static void check(bool ok,const char* expression,int line) {
    ++checks;if(!ok){++failures;if(failures<35)std::cerr<<"FAIL "<<active<<" line="<<line<<" expression="<<expression<<'\n';}
}
#define CHECK(v) check(static_cast<bool>(v),#v,__LINE__)
static bool near(double a,double b){return std::abs(a-b)<0.000001;}
static bool same(const Settings& a,const Settings& b){
    return std::tie(a.steeringAxis,a.motionInput,a.aimingSightsTrigger,a.velocityMode,a.outputAxis,
        a.motionActionEnabled,a.targetGyroEnabled,a.motionTriggerResolved,a.invertHorizontal,a.invertVertical,
        a.gyroMultiplier,a.accelerometerMultiplier,a.gyroThreshold,a.steeringMaxAngle,a.steeringPower,a.steeringDeadzone,
        a.aimingSightsMultiplier,a.motionSensitivityX,a.motionSensitivityY,a.velocityScale,
        a.velocityAccumulatedX,a.velocityAccumulatedY,a.motionSensitivityArray)==
      std::tie(b.steeringAxis,b.motionInput,b.aimingSightsTrigger,b.velocityMode,b.outputAxis,
        b.motionActionEnabled,b.targetGyroEnabled,b.motionTriggerResolved,b.invertHorizontal,b.invertVertical,
        b.gyroMultiplier,b.accelerometerMultiplier,b.gyroThreshold,b.steeringMaxAngle,b.steeringPower,b.steeringDeadzone,
        b.aimingSightsMultiplier,b.motionSensitivityX,b.motionSensitivityY,b.velocityScale,
        b.velocityAccumulatedX,b.velocityAccumulatedY,b.motionSensitivityArray);
}
// Aiming is inactive in this bounded LocalSpace proof. Any unexpected request
// to its unrelated trigger subsystem is a test failure, not substitute behavior.
static bool inputCaptureMotionTriggerPressed(const std::string&,const XINPUT_GAMEPAD&) { CHECK(false);return false; }

#include "native-functions.generated.h"

struct Case {std::string id,source,axis;bool controller=false;float gx=0,gy=0,gz=0,am=1,gm=1,cx=11,cy=-22,cz=33;};
struct PipelineResult {bool yaw;MotionSensorFrame sensor;MotionContribution contribution;json diagnostic;};
#include "native-pipeline.generated.h"

struct Stats {unsigned rows=0;unsigned long long checks=0,failures=0;};
int main(int argc,char** argv){
    if(argc!=2)return 2;
    std::ifstream file(argv[1]);if(!file)return 3;
    std::map<std::string,Stats> groups;
    unsigned rows=0;std::string line;
    while(std::getline(file,line)){
        std::istringstream in(line);Case c;int expectedYaw=0,legacyYaw=0;
        double dg[3]{},da[3]{};int sx=0,sy=0,lx=0,ly=0;
        in>>c.id>>c.source>>c.axis>>c.gx>>c.gy>>c.gz>>c.am>>c.gm>>c.cx>>c.cy>>c.cz>>expectedYaw;
        for(auto& v:dg)in>>v;for(auto& v:da)in>>v;
        in>>sx>>sy>>legacyYaw>>lx>>ly;if(!in)return 4;
        c.controller=c.source=="Controller";active=c.id;
        const auto startChecks=checks,startFailures=failures;
        g_realStickTest={};g_realStickTest.steeringAxis=c.axis;
        g_realStickTest.accelerometerMultiplier=c.am;g_realStickTest.gyroMultiplier=c.gm;
        const auto before=g_realStickTest;
        const float scaledY=c.gy*c.am,scaledZ=c.gz*c.am;
        CHECK(inputCaptureHcSteeringAxisResolvesYaw(c.controller,scaledY,scaledZ)==(expectedYaw!=0));
        CHECK(same(g_realStickTest,before));
        const auto output=actualPipeline(c);
        CHECK(output.yaw==(expectedYaw!=0));
        CHECK(same(g_realStickTest,before));
        CHECK(near(output.sensor.defaultAccelX,da[0]));
        CHECK(near(output.sensor.defaultAccelY,da[1]));
        CHECK(near(output.sensor.defaultAccelZ,da[2]));
        CHECK(output.sensor.calibratedGyroX==c.cx&&output.sensor.calibratedGyroY==c.cy&&output.sensor.calibratedGyroZ==c.cz);
        CHECK(output.contribution.mapped&&output.contribution.triggered&&!output.contribution.velocityMode);
        CHECK(output.contribution.shortX==sx&&output.contribution.shortY==sy);
        const auto& diag=output.diagnostic;
        CHECK(diag.at("steeringAxis")==((expectedYaw!=0)?"yaw":"roll"));
        CHECK(diag.at("processProven")==true&&diag.at("pairProven")==true);
        const char* names[]={"x","y","z"};
        for(unsigned i=0;i<3;++i){
            CHECK(near(diag.at("defaultGyro").at(names[i]).get<double>(),dg[i]));
            CHECK(near(diag.at("defaultAccel").at(names[i]).get<double>(),da[i]));
        }
        // Omitted argument retains the real Windows default, even immediately
        // after a Controller Auto row. No temporary global-axis override is used.
        CHECK(inputCaptureHcSteeringAxisResolvesYaw()==(legacyYaw!=0));
        const auto legacy=inputCaptureProcessMotionContribution(output.sensor,1.0/60.0,true,true,true,XINPUT_GAMEPAD{});
        CHECK(legacy.shortX==lx&&legacy.shortY==ly);
        CHECK(same(g_realStickTest,before));
        // Interleaving two isolated frames must not cache the previous source's
        // steering choice or contaminate the shared profile axis.
        const auto again=actualPipeline(c);
        CHECK(again.yaw==output.yaw&&again.contribution.shortX==output.contribution.shortX&&
              again.contribution.shortY==output.contribution.shortY);
        CHECK(same(g_realStickTest,before));
        auto& group=groups[c.source+"-"+c.axis];++group.rows;
        group.checks+=checks-startChecks;group.failures+=failures-startFailures;++rows;
    }
    for(const auto& [name,s]:groups)std::cout<<"CASE "<<name<<" rows="<<s.rows<<" checks="<<s.checks<<" failures="<<s.failures<<'\n';
    std::cout<<"SUMMARY groups="<<groups.size()<<" rows="<<rows<<" checks="<<checks<<" failures="<<failures<<'\n';
    return failures==0?0:1;
}
