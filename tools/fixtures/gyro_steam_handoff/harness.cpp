#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <stdexcept>
#include <vector>
#include "json.hpp"
#include "input_shortcut_toggle.h"
#include "input_startup_policy.h"
using json = nlohmann::json;
// Touchpad output is outside this gyro test; an attempted touch branch is fatal.
namespace ymcc::screenpads { struct Snapshot {bool present=false;};
static json snapshotJson(const Snapshot&) {throw std::runtime_error("unexpected touchpad branch");} }
// PRODUCTION_CONFIG
static RealStickTestConfig g_realStickTest;
static bool g_realStickTestAdmissionLogged=false;
static std::string inputHostConfiguredPersona(){return "steamdeck";}
static void appendNativeLifecycleLog(const char*,const json&){}
// PRODUCTION_FUNCTIONS
// PRODUCTION_BLEND
static void configure(const json& motion) {
    const bool targetGyroEnabled=true;
    const bool triggerResolved=motion.value("fixtureTriggerResolved",true);
    const bool enabled=motion.value("enabled",true);
    const std::string configuredOutputMode=motion.value("outputMode",std::string("virtual-stick"));
    // PRODUCTION_READ_STRING
    // PRODUCTION_ACTION
    const bool motionActionChanged=g_realStickTest.motionActionEnabled!=nextMotionActionEnabled;
    const bool motionEnabledChanged=g_realStickTest.motionEnabled!=nextMotionEnabled;
    const auto configuredMode=motion.value("motionMode",std::string("on"));
    const auto configuredTrigger=motion.value("motionTrigger",json()).is_string()?motion["motionTrigger"].get<std::string>():"";
    const bool modeChanged=g_realStickTest.motionMode!=configuredMode;
    const bool triggerChanged=g_realStickTest.motionTrigger!=configuredTrigger;
    const bool targetChanged=false,virtualTargetChanged=false,targetGyroChanged=false;
    g_realStickTest.steamControlled=steamControlled;
    g_realStickTest.motionActionEnabled=nextMotionActionEnabled;
    g_realStickTest.motionEnabled=nextMotionEnabled;
    g_realStickTest.targetGyroEnabled=targetGyroEnabled;
    g_realStickTest.motionTriggerResolved=triggerResolved;
    g_realStickTest.motionMode=configuredMode;
    g_realStickTest.motionTrigger=configuredTrigger;
    // PRODUCTION_RESET
}
static bool rawAdmission(const std::string& persona,bool gyroSubmitAllowed,bool controllerMotionRoute=false) {
    const auto inputHostPersona=[&](){return persona;};
    const double orientationW=1,orientationX=0,orientationY=0,orientationZ=0;
    return // PRODUCTION_IMU_ADMISSION
    ;
}
static bool sensorAck(const MotionContribution& motionContribution,bool imuAdmitted) {
    const auto now=0;
    // PRODUCTION_ACK
    return sensorCurrent;
}
static json wire(const MotionContribution& contribution,const XINPUT_GAMEPAD& pad,bool imuAdmitted) {
    const auto blend=focusIsolationBlendStick(pad,true,contribution,"right",1.3);
    FocusFrameContent frameContent;
    frameContent.lxShort=blend.lxShort;frameContent.lyShort=blend.lyShort;
    frameContent.rxShort=blend.rxShort;frameContent.ryShort=blend.ryShort;
    frameContent.rawLtByte=pad.bLeftTrigger;frameContent.rawRtByte=pad.bRightTrigger;
    const double imuRawGx=17,imuRawGy=-23,imuRawGz=41,maxa=0.1,may=0.2,maz=0.97;
    const double orientationW=0.9,orientationX=0.1,orientationY=0.2,orientationZ=0.3;
    // PRODUCTION_IMU_FIELDS
    return focusIsolationAssembleFrame(frameContent);
}
static unsigned checks=0;
static void check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
int main(){try {
    g_realStickTest.gyroThreshold=2000;
    MotionSensorFrame sensor;sensor.calibratedGyroX=20;sensor.calibratedGyroZ=40;
    XINPUT_GAMEPAD pad;ZeroMemory(&pad,sizeof(pad));
    check(!inputCaptureSteamControlsMotion(json(nullptr)),"null config is not Steam");
    for(const auto mode:{"on","off","toggle","suppress"}){
        const json bank={{"steam",{{"motionMode",mode},{"motionTrigger","LT"},{"aimingSightsTrigger","RB"},{"gyroMultiplier",1.7}}}};
        const json remembered={{"activePreset","steam"},{"motionMode",mode},{"motionTrigger","LT"},{"presets",bank}};
        const auto shortcut=ymcc::inputShortcutGyroParams(remembered,"steam");
        const auto startup=ymcc::startupGyroPresetParams("steam",bank);
        const auto linked=ymcc::inputShortcutLinkedGyroMotion(remembered);
        for(const auto& p:{shortcut,startup,linked})check(p["motionMode"]=="off"&&p["motionTrigger"].is_null()&&p["aimingSightsTrigger"].is_null()&&p["gyroMultiplier"]==1.7,"native restore entries cancel Steam local triggers without dropping sensor tuning");
    }
    check(inputCaptureSteamControlsMotion(json{{"preset","steam"}}),"legacy Steam selection");
    check(inputCaptureSteamControlsMotion(json{{"activePreset","invalid"},{"preset","steam"}}),"invalid active falls back to Steam");
    check(!inputCaptureSteamControlsMotion(json{{"activePreset","fps"},{"preset","steam"}}),"valid active FPS wins");
    check(inputCaptureSteamControlsMotion(json{{"activePreset","steam"},{"preset","fps"}}),"active Steam wins");
    for(const auto mode:{"on","off","toggle"})for(const bool pressed:{false,true}){
        configure(json{{"activePreset","steam"},{"motionMode",mode},{"motionTrigger","LT"}});
        pad.bLeftTrigger=pressed?255:0;pad.sThumbRX=3123;pad.sThumbRY=-4321;
        check(g_realStickTest.motionEnabled&&g_realStickTest.motionActionEnabled,"Steam retains sensor lifecycle demand");
        check(!inputCaptureMotionTriggered(pad),"Steam bypasses all local trigger modes");
        g_realStickTest.velocityAccumulatedX=999;g_realStickTest.velocityAccumulatedY=-456;
        // Even a stale caller claiming triggered=true must not map Steam locally.
        const auto c=inputCaptureProcessMotionContribution(sensor,1.0/120.0,true,true,true,pad);
        check(!c.mapped&&c.shortX==0&&c.shortY==0,"Steam cannot produce a local motion contribution");
        check(g_realStickTest.velocityAccumulatedX==0&&g_realStickTest.velocityAccumulatedY==0,"Steam clears velocity displacement");
        const auto frame=wire(c,pad,rawAdmission("steamdeck",true));
        check(frame["raw"]["sThumbRX"]==3123&&frame["raw"]["sThumbRY"]==-4321,"Steam preserves physical stick");
        check(frame["raw"]["bLeftTrigger"]==(pressed?255:0),"Steam preserves physical LT for Steam Input");
        check(frame["imu"]["admitted"]==true&&frame["imu"]["gyroDps"]["x"]==17&&frame["imu"]["gyroDps"]["y"]==-23&&frame["imu"]["accelG"]["z"]==0.97,"Steam receives raw IMU with local mapping off");
        check(sensorAck(c,true)&&!sensorAck(c,false),"Steam applied ACK requires admitted raw IMU");
        const auto unavailable=wire(c,pad,rawAdmission("steamdeck",false));
        check(unavailable["imu"]["admitted"]==false&&unavailable["imu"]["gyroDps"]["z"]==0,"missing source stays safe-zero");
    }
    check(rawAdmission("dualsense-edge",true)&&!rawAdmission("xbox360",true),"only IMU-capable personas admit raw data");
    configure(json{{"activePreset","steam"},{"motionTrigger","stale-unknown-trigger"},{"fixtureTriggerResolved",false},{"outputMode","disabled"}});
    check(g_realStickTest.motionEnabled&&g_realStickTest.motionActionEnabled&&!inputCaptureMotionTriggered(pad),"stale local trigger and output cannot block Steam sensor lifecycle");
    configure(json{{"activePreset","fps"},{"motionMode","off"},{"motionTrigger","LT"}});
    pad.sThumbRX=pad.sThumbRY=0;pad.bLeftTrigger=0;
    check(!inputCaptureMotionTriggered(pad),"FPS remains LT-held after Steam");
    pad.bLeftTrigger=255;
    const auto fps=inputCaptureProcessMotionContribution(sensor,1.0/120.0,true,inputCaptureMotionTriggered(pad),true,pad);
    check(fps.mapped&&fps.shortX!=0&&fps.shortY!=0,"FPS resumes local motion on LT");
    check(sensorAck(fps,false)&&!sensorAck(MotionContribution{},true),"local ACK still requires mapped motion");
    configure(json{{"activePreset","custom"},{"motionMode","toggle"},{"motionTrigger","LT"}});
    check(inputCaptureMotionTriggered(pad),"custom toggle turns on");
    configure(json{{"activePreset","steam"},{"motionMode","toggle"},{"motionTrigger","LT"}});
    check(!g_realStickTest.motionToggleStatus&&!g_realStickTest.motionTogglePressed,"owner switch clears toggle");
    configure(json{{"activePreset","custom"},{"motionMode","toggle"},{"motionTrigger","LT"}});
    check(!g_realStickTest.motionToggleStatus&&!g_realStickTest.motionTogglePressed,"returning local starts without stale toggle");
    configure(json{{"activePreset","racing"},{"motionMode","on"},{"motionTrigger",""}});
    check(inputCaptureMotionTriggered(pad),"racing always-on remains local");
    configure(json{{"activePreset","steam"},{"enabled",false}});
    check(!g_realStickTest.motionEnabled&&!inputCaptureMotionTriggered(pad),"explicit gyro OFF remains off");
    std::cout<<json{{"status","pass"},{"checks",checks},{"deviceIo",false},{"physicalAcceptance",false}}.dump()<<std::endl;return 0;
}catch(const std::exception& e){std::cout<<json{{"status","fail"},{"checks",checks},{"failure",e.what()}}.dump()<<std::endl;return 1;}}
