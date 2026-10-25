// Verbatim model matrices, frame assembly, fingerprint and final submit text.
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <sstream>
#include <vector>
#include "json.hpp"
#include "canonical_frame_fingerprint_memo.h"
#include "hc_model_motion_catalog.h"
#include "hc_legacy_motion_catalog.h"
#include "msi_motion_capabilities.h"
using json=nlohmann::json;
static std::string persona,profile;
static const char* inputHostBoundOrCurrentPersona(){return persona.c_str();}
static const char* inputHostBoundOrCurrentProfileIdentity(){return profile.c_str();}
static unsigned long long inputHostProtocolPowerGenerationLocked(){return 5;}
static std::string g_inputHostRunId="numeric-wire-run",g_inputHostStartupNonce="numeric-wire-nonce";
static unsigned long long g_inputHostEpoch=3,g_inputHostConfigRevision=1;
// @@CONSTANTS@@
// @@PAD_TYPES@@
// @@FRAME_TYPE@@
// @@FUNCTIONS@@
int main(int argc,char** argv){try{
 if(argc!=3)return 2;const auto rows=json::parse(std::ifstream(argv[1]));std::ofstream out(argv[2]);uint64_t index=0;
 for(const auto& row:rows){++index;const auto raw=row.at("raw");double gx=raw[0],gy=raw[1],gz=raw[2],ax=raw[3],ay=raw[4],az=raw[5];
 if(row.value("controllerPostDecode",false)){}else{
  if(!ymccApplyNamedImuMatrix(row.at("gyroId"),raw[0],raw[1],raw[2],gx,gy,gz)||!ymccApplyNamedImuMatrix(row.at("accelId"),raw[3],raw[4],raw[5],ax,ay,az))throw std::runtime_error("actual model matrix missing");}
 FocusFrameContent content;content.buttons=0x1500;content.backButtons=3;content.lxShort=-16384;content.lyShort=8192;content.rxShort=4096;content.ryShort=-2048;
 content.lt=0.25f;content.rt=0.75f;content.ltRaw=0.5f;content.rtRaw=1.0f;content.rawLtByte=128;content.rawRtByte=255;
 content.imuAdmitted=row.at("admitted");content.gyroX=gx;content.gyroY=gy;content.gyroZ=gz;content.accelX=ax;content.accelY=ay;content.accelZ=az;
 const auto q=row.at("quaternion");content.qw=q[0];content.qx=q[1];content.qy=q[2];content.qz=q[3];
 const auto effective=row.at("neutral").get<bool>()?focusIsolationNeutralContent():content;
 auto frame=focusIsolationAssembleFrame(effective);
 if(row.value("quaternionAbsent",false))frame["imu"].erase("quaternion");
 if(row.contains("quaternionPayload"))frame["imu"]["quaternion"]=row["quaternionPayload"];
 persona=row.at("persona");profile=row.at("profile");
 const auto fingerprint=inputHostCanonicalFrameHash(effective.buttons,inputHostNormalizeAxis(effective.lxShort),inputHostNormalizeAxis(effective.lyShort),inputHostNormalizeAxis(effective.rxShort),inputHostNormalizeAxis(effective.ryShort),effective.lt,effective.rt);
 const auto wire=inputHostBuildSubmitFrameText(frame,"wire-"+std::to_string(index),index,index,10000+index,fingerprint,"hc-same-provider-selector-v2");
 auto result=row;result["actualMapped"]={gx,gy,gz,ax,ay,az};result["wire"]=wire;out<<result.dump()<<'\n';
 }
 std::cout<<json({{"status","pass"},{"rows",index},{"actualNativeMatrixAndWire",true},{"sourceAdmission","fixture-supplied, historical flag preserved"},{"hardwareIo",false},{"hostExecuted",false}}).dump()<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
