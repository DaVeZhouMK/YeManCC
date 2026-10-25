// The sink below captures values from the actual native BUS motion slices.
// SDK scheduling and physical-pad availability are controlled test inputs.
// No pipe, HMContext, virtual target, device or normal product is started.
// @@ACTUAL_SUBMISSION_FRAME@@
// @@ACTUAL_SOURCE_GUARD@@
static std::vector<json> rows;
static unsigned guardBlocks=0,sourceOnly=0;
static bool near(double a,double b){return std::isfinite(a)&&std::isfinite(b)&&std::fabs(a-b)<=0.00002*(1+std::fabs(b));}
static void expect(bool ok,const std::string& name){::check(ok,"native-owner-host-wire",name);}
static int quantize(double value,double scale,bool round=false,double limit=0){
 if(!std::isfinite(value))return 0;if(limit>0)value=std::clamp(value,-limit,limit);
 double counts=value*scale;if(round)counts=std::nearbyint(counts);return static_cast<int>(std::clamp(counts,-32768.0,32767.0));
}
static bool sourceCurrent(const candidate::NativeBusEvidence& e,bool neutral){InputFrameSubmission frame;frame.ticket.neutral=neutral;frame.windowsMotion=&e.view.submission;return inputHostWindowsMotionSubmissionCurrent(frame);}
static void rejectRetired(const candidate::NativeBusEvidence& e,const std::string& name){
 expect(!sourceCurrent(e,false),name+" final actual source guard rejects retired live frame");if(!sourceCurrent(e,false))++guardBlocks;
 expect(sourceCurrent(e,true),name+" same-target neutral remains source independent");
}
static void record(const candidate::NativeBusEvidence& e,const std::string& name,bool expectedAllowed,bool neutral){
 expect(e.processed&&e.calls>0&&e.processedHandle==candidate::g_gm,name+" binds actual processed handle and frame");
 if(!neutral)expect(e.output==expectedAllowed,name+" actual physical/motion output gate matches scenario");
 if(!expectedAllowed&&!neutral){++sourceOnly;return;}
 expect(sourceCurrent(e,neutral),name+" actual final source permission");
 if(!neutral){
  for(unsigned i=0;i<6;++i)expect(near(e.raw[i],e.processInput[i]),name+" Host raw axis derives from same actual DLL input "+std::to_string(i));
  std::array<float,3> actualCal{};candidate::g_gmGetCal(e.processedHandle,&actualCal[0],&actualCal[1],&actualCal[2]);
  for(unsigned i=0;i<3;++i)expect(near(e.calibrated[i],actualCal[i]),name+" calibrated readback from same actual DLL handle "+std::to_string(i));
  double norm=0;for(const auto value:e.quaternion){expect(std::isfinite(value),name+" finite actual overlay quaternion");norm+=double(value)*value;}expect(std::fabs(norm-1)<0.0001,name+" unit actual overlay quaternion");
 }
 for(const auto& target:std::array<std::pair<const char*,const char*>,2>{{{"steamdeck","steam-deck-composite"},{"dualsense-edge","dualsense-edge"}}}){
  FocusFrameContent content;content.buttons=0x1500;content.backButtons=3;content.lxShort=-16384;content.lyShort=8192;content.rxShort=4096;content.ryShort=-2048;
  content.lt=.25f;content.rt=.75f;content.ltRaw=.5f;content.rtRaw=1;content.rawLtByte=128;content.rawRtByte=255;
  content.imuAdmitted=e.output;
  content.gyroX=e.raw[0];content.gyroY=e.raw[1];content.gyroZ=e.raw[2];content.accelX=e.raw[3];content.accelY=e.raw[4];content.accelZ=e.raw[5];
  content.qw=e.quaternion[0];content.qx=e.quaternion[1];content.qy=e.quaternion[2];content.qz=e.quaternion[3];
  const auto effective=neutral?focusIsolationNeutralContent():content;const auto frame=focusIsolationAssembleFrame(effective);
  persona=target.first;profile=target.second;const auto index=rows.size()+1;
  const auto fingerprint=inputHostCanonicalFrameHash(effective.buttons,inputHostNormalizeAxis(effective.lxShort),inputHostNormalizeAxis(effective.lyShort),inputHostNormalizeAxis(effective.rxShort),inputHostNormalizeAxis(effective.ryShort),effective.lt,effective.rt);
  const auto wire=inputHostBuildSubmitFrameText(frame,"owner-wire-"+std::to_string(index),index,index,10000+index,fingerprint,"hc-same-provider-selector-v2");
  const bool admitted=e.output&&!neutral;std::array<double,6> wanted=admitted?e.raw:std::array<double,6>{};std::array<double,4> q=admitted?std::array<double,4>{e.quaternion[0],e.quaternion[1],e.quaternion[2],e.quaternion[3]}:std::array<double,4>{1,0,0,0};
  std::array<int,6> words{};
  if(persona=="steamdeck")words={quantize(wanted[3],16384),quantize(-wanted[5],16384),quantize(wanted[4],16384),quantize(wanted[0],16),quantize(-wanted[2],16),quantize(wanted[1],16)};
  else words={quantize(wanted[0],16,true,2048),quantize(wanted[1],16,true,2048),quantize(wanted[2],16,true,2048),quantize(wanted[3],8192,true,4),quantize(wanted[4],8192,true,4),quantize(wanted[5],8192,true,4)};
  std::array<int,4> qw{};if(admitted)for(unsigned i=0;i<4;++i)qw[i]=quantize(q[i],32767);
  rows.push_back({{"name",name+"/"+persona},{"model",candidate::g_machineIdentity.deviceClass},{"persona",persona},{"neutral",neutral},{"expectedMapped",e.raw},{"actualMapped",e.raw},{"expectedAxes",wanted},{"expectedQuaternion",q},{"expectedQuaternionPresent",true},{"expectedWords",words},{"expectedQuaternionWords",qw},{"wire",wire},{"sourceBinding",e.view.submission.bindingGeneration},{"choiceGeneration",e.view.submission.choiceGeneration},{"processedCalls",e.calls},{"privateSource",e.view.privateSource},{"scope","actual-owner-processing-and-frame/controlled-sdk-pad/no-pipe-or-hardware"}});
 }
}
