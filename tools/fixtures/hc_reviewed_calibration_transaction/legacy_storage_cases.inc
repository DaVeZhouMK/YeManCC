 // Configuration storms for the actual unqualified/legacy reader and writer.
 candidate::g_machineIdentity.family=candidate::YmccFamily::Unknown;candidate::g_machineIdentity.deviceClass="fixture-unqualified";
 const std::string legacyId="fixture-legacy-device",legacyKey=candidate::inputCaptureCalibrationStorageKey(legacyId);
 txCheck(candidate::inputCaptureSourceQualifiedCalibrationRecipe()==nullptr&&legacyKey=="FIXTURE-LEGACY-DEVICE","legacy storage storm cannot enter a reviewed source");
 const json legacyRecord={{"xOffset",1.0},{"yOffset",2.0},{"zOffset",3.0},{"weight",10},{"confidence",1.0},{"thresholdG",2000.0},{"thresholdA",4.0}};
 const auto legacyLoad=[&](float& x,float& y,float& z,int& weight,float& confidence){return candidate::inputCaptureLoadCalibration(legacyId,x,y,z,weight,confidence);};
 for(const auto* field:{"xOffset","yOffset","zOffset","weight","confidence"})for(const auto& invalid:std::vector<json>{"bad",true,nullptr,json::array(),json::object()}){
  auto bad=legacyRecord;bad[field]=invalid;writeText(json{{legacyKey,bad},{"another-device",legacyRecord}}.dump());float x=17,y=18,z=19,c=0.6f;int weight=6;bool loaded=false,threw=false;
  try{loaded=legacyLoad(x,y,z,weight,c);}catch(...){threw=true;}
  txCheck(!threw&&!loaded&&x==17&&y==18&&z==19&&weight==6&&c==0.6f,"legacy wrong scalar returns uncalibrated without exception or partial outputs");++scenarios;
 }
 for(const auto* field:{"xOffset","yOffset","zOffset","weight","confidence"}){
  auto bad=legacyRecord;bad[field]=1e300;writeText(json{{legacyKey,bad}}.dump());float x=17,y=18,z=19,c=0.6f;int weight=6;bool loaded=false,threw=false;try{loaded=legacyLoad(x,y,z,weight,c);}catch(...){threw=true;}
  txCheck(!threw&&!loaded&&x==17&&y==18&&z==19&&weight==6&&c==0.6f,"legacy numeric overflow cannot become an active calibration");++scenarios;
 }
 writeText(json{{legacyKey,legacyRecord},{"another-device",legacyRecord},{"future-sentinel",{{"keep",true}}}}.dump());
 candidate::inputCaptureStoreCalibration(legacyId,0.25f,-0.5f,0.75f,10,1.0f);
 txCheck(read()[legacyKey]["xOffset"]==0.25f&&read()["another-device"]==legacyRecord&&read()["future-sentinel"]["keep"]==true,"legacy healthy write preserves other device and unknown records");++scenarios;
 auto beforeInvalid=bytes();candidate::inputCaptureStoreCalibration(legacyId,std::numeric_limits<float>::quiet_NaN(),0,0,10,1.0f);
 txCheck(bytes()==beforeInvalid,"legacy nonfinite measurement does not persist JSON null as calibration");++scenarios;
 writeText("{ corrupt-calibration");const auto corruptReceipt=candidate::inputCaptureWriteCalibrationRecord(legacyKey,nullptr,0.25f,-0.5f,0.75f,10,1.0f,[]{return true;});
 if(!corruptReceipt.saved)throw std::runtime_error("Corrupt recovery failed: "+corruptReceipt.reason+" win32="+std::to_string(corruptReceipt.error));
 bool archived=false;for(const auto& entry:std::filesystem::directory_iterator(file.parent_path()))if(entry.path().filename().wstring().starts_with(L"calibration.json.before-calibration-recovery-")){std::ifstream saved(entry.path(),std::ios::binary);const std::string raw(std::istreambuf_iterator<char>(saved),{});if(raw=="{ corrupt-calibration")archived=true;}
 txCheck(archived&&read()[legacyKey]["xOffset"]==0.25f,"legacy corrupt source is archived before learned record self-heals storage");++scenarios;
 writeText(json{{"schemaVersion",99},{legacyKey,legacyRecord}}.dump());beforeInvalid=bytes();float x=17,y=18,z=19,c=0.6f;int weight=6;bool futureLoaded=false;try{futureLoaded=legacyLoad(x,y,z,weight,c);}catch(...){}
 candidate::inputCaptureStoreCalibration(legacyId,0.25f,-0.5f,0.75f,10,1.0f);txCheck(!futureLoaded&&bytes()==beforeInvalid,"future calibration container remains inactive and byte-identical");++scenarios;
 writeText(json{{legacyKey,legacyRecord},{"another-device",legacyRecord}}.dump());const auto lockedBeforeLegacy=bytes();HANDLE legacyHeld=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(legacyHeld==INVALID_HANDLE_VALUE)throw std::runtime_error("Legacy target lock failed");
 candidate::g_sensorMeta.gyroDeviceId=legacyId;candidate::g_sensorMeta.gyroPresent=true;candidate::g_gyroCalibStageAtomic=candidate::GyroCalibStage::ApplyOffset;candidate::g_gyroCalibMsiSession=false;
 candidate::g_gyroCalibConfidence=1;candidate::g_gyroCalibOx=0.25f;candidate::g_gyroCalibOy=-0.5f;candidate::g_gyroCalibOz=0.75f;candidate::g_calibSourceAuto=false;candidate::fixtureEvents.clear();candidate::fixtureUiEvents.clear();
 candidate::inputCaptureGyroCalibrationTick();CloseHandle(legacyHeld);const auto legacyResult=lastResult();
 txCheck(legacyResult["saved"]==false&&bytes()==lockedBeforeLegacy,"actual legacy tick cannot claim saved when file replacement is blocked");++scenarios;


 // Missing required axes are corrupt records, not trusted zero offsets.
 for(const auto* missing:{"xOffset","yOffset","zOffset","weight"}){
  auto partial=legacyRecord;partial.erase(missing);writeText(json{{legacyKey,partial}}.dump());float px=17,py=18,pz=19,pc=0.6f;int pw=6;
  txCheck(!legacyLoad(px,py,pz,pw,pc)&&px==17&&py==18&&pz==19&&pw==6,"missing core calibration field cannot fabricate an offset or weight");++scenarios;
 }
 // Legacy confidence was optional; valid historical records still round trip.
 auto historical=legacyRecord;historical.erase("confidence");writeText(json{{legacyKey,historical}}.dump());
 {float px=0,py=0,pz=0,pc=1;int pw=0;txCheck(legacyLoad(px,py,pz,pw,pc)&&px==1&&py==2&&pz==3&&pw==10&&pc==0,"historical confidence omission remains readable");++scenarios;}
 const auto healthy=json{{legacyKey,legacyRecord},{"another-device",legacyRecord},{"unknown",{{"keep",true}}}}.dump();
 writeText(healthy);candidate::fixtureFailFlush=true;candidate::inputCaptureStoreCalibration(legacyId,0.25f,-0.5f,0.75f,10,1);candidate::fixtureFailFlush=false;
 txCheck(bytes()==healthy,"flush failure leaves the original file intact");++scenarios;
 // Archive flush must succeed before a damaged source can be replaced.
 writeText("{ broken-archive-test");candidate::fixtureFlushCalls=0;candidate::fixtureFailFlushAt=2;candidate::inputCaptureStoreCalibration(legacyId,0.25f,-0.5f,0.75f,10,1);candidate::fixtureFailFlushAt=0;
 txCheck(bytes()=="{ broken-archive-test","archive flush failure does not activate rebuilt file");++scenarios;
 candidate::inputCaptureStoreCalibration(legacyId,0.25f,-0.5f,0.75f,10,1);txCheck(read()[legacyKey]["xOffset"]==0.25f,"explicit retry after archive fault self-heals");++scenarios;
 for(const auto& damaged:std::vector<std::string>{"","[]","null",std::string("\xff\xfe{\0",4)}){
  writeText(damaged);const bool saved=candidate::inputCaptureStoreCalibration(legacyId,0.25f,-0.5f,0.75f,10,1);bool found=false;
  for(const auto& entry:std::filesystem::directory_iterator(file.parent_path()))if(entry.path().filename().wstring().starts_with(L"calibration.json.before-calibration-recovery-")){std::ifstream input(entry.path(),std::ios::binary);if(std::string(std::istreambuf_iterator<char>(input),{})==damaged)found=true;}
  txCheck(saved&&found&&read()[legacyKey]["confidence"]==1,"empty/root/encoding damaged original is exact-byte archived and self-healed");++scenarios;
 }
 writeText(healthy);const auto lockPath=file.wstring()+L".reviewed.lock";HANDLE writerLock=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);if(writerLock==INVALID_HANDLE_VALUE)throw std::runtime_error("Legacy sidecar lock failed");
 txCheck(!candidate::inputCaptureStoreCalibration(legacyId,0.25f,-0.5f,0.75f,10,1)&&bytes()==healthy,"second writer cannot ignore the shared calibration lock");CloseHandle(writerLock);++scenarios;
 txCheck(candidate::inputCaptureStoreCalibration(legacyId,0.25f,-0.5f,0.75f,10,1),"lock release permits a later explicit save");++scenarios;
 writeText(healthy);HANDLE unreadable=CreateFileW(file.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);if(unreadable==INVALID_HANDLE_VALUE)throw std::runtime_error("Legacy unreadable fixture failed");
 const bool unreadableSaved=candidate::inputCaptureStoreCalibration(legacyId,0.25f,-0.5f,0.75f,10,1);CloseHandle(unreadable);txCheck(!unreadableSaved&&bytes()==healthy,"unreadable original cannot be reinterpreted as absent/corrupt");++scenarios;
 std::filesystem::remove(file);txCheck(candidate::inputCaptureStoreCalibration(legacyId,0.25f,-0.5f,0.75f,10,1)&&read()[legacyKey]["xOffset"]==0.25f,"genuinely missing calibration initializes atomically");++scenarios;
 writeText(std::string((1u<<20)+1,'x'));const auto tooLarge=bytes();{float px=17,py=18,pz=19,pc=0.6f;int pw=6;txCheck(!legacyLoad(px,py,pz,pw,pc)&&bytes()==tooLarge,"oversized calibration read is bounded and preserves original before an explicit save");++scenarios;}
 writeText(std::string((1u<<20)+1,'x'));const auto oversizedCalibration=bytes();
 txCheck(candidate::inputCaptureStoreCalibration(legacyId,0.25f,-0.5f,0.75f,10,1)&&read()[legacyKey]["xOffset"]==0.25f,"over-limit calibration source can rebuild after complete streamed archive");++scenarios;
 bool oversizedCalibrationArchived=false;for(const auto& entry:std::filesystem::directory_iterator(file.parent_path()))if(entry.path().filename().wstring().starts_with(L"calibration.json.before-calibration-recovery-")){std::ifstream input(entry.path(),std::ios::binary);if(std::string(std::istreambuf_iterator<char>(input),{})==oversizedCalibration)oversizedCalibrationArchived=true;}
 txCheck(oversizedCalibrationArchived,"over-limit calibration original remains available byte-for-byte");++scenarios;
 // Reviewed provenance wrapper uses the same transaction, with source-current gates.
 const ymcc::hc_model_motion::Recipe reviewed{"fixture-class","fixture-selector","fixture-gyro-matrix","fixture-accel-matrix","fixture-source","fixture-sha"};
 writeText(healthy);const auto declined=candidate::inputCaptureWriteReviewedCalibration("reviewed-key",reviewed,0.25f,-0.5f,0.75f,10,1,[]{return false;});
 txCheck(!declined.saved&&bytes()==healthy,"reviewed source retirement before write preserves file");++scenarios;
 int sourceChecks=0;const auto retired=candidate::inputCaptureWriteReviewedCalibration("reviewed-key",reviewed,0.25f,-0.5f,0.75f,10,1,[&]{return ++sourceChecks<2;});
 txCheck(!retired.saved&&bytes()==healthy,"reviewed source retirement during transaction cannot publish");++scenarios;
 const auto reviewedSaved=candidate::inputCaptureWriteReviewedCalibration("reviewed-key",reviewed,0.25f,-0.5f,0.75f,10,1,[]{return true;});
 txCheck(reviewedSaved.saved&&reviewedSaved.verified&&read()["reviewed-key"]["matrixId"]=="fixture-gyro-matrix"&&read()["another-device"]==legacyRecord,"reviewed wrapper preserves provenance and unrelated devices");++scenarios;
 writeText("{ invalid-reviewed");const auto reviewedRecovered=candidate::inputCaptureWriteReviewedCalibration("reviewed-key",reviewed,0.25f,-0.5f,0.75f,10,1,[]{return true;});
 txCheck(reviewedRecovered.saved&&read()["reviewed-key"]["matrixSourceSha256"]=="fixture-sha","reviewed record can self-heal after preserving corrupt source");++scenarios;
 for(const auto& entry:std::filesystem::directory_iterator(file.parent_path()))txCheck(!entry.path().filename().wstring().ends_with(L".tmp"),"calibration transaction leaves no owned temporary behind");

 writeText(healthy);const auto external=json{{"external-device",legacyRecord},{"external-sentinel",true}}.dump();
 candidate::fixtureAfterFlush=[&]{writeText(external);};const bool raced=candidate::inputCaptureStoreCalibration(legacyId,0.25f,-0.5f,0.75f,10,1);
 txCheck(!raced&&bytes()==external,"noncooperating writer changes detected before calibration publication");++scenarios;
 txCheck(candidate::inputCaptureStoreCalibration(legacyId,0.25f,-0.5f,0.75f,10,1)&&read()["external-sentinel"]==true&&read()["external-device"]==legacyRecord,"retry reads and preserves external changes");++scenarios;

 // A selected valid record cannot make an unbounded unknown subtree safe to consume.
 for(bool insideRecord:{false,true}){
  const auto nested=std::string("");std::string value="0";for(int i=0;i<2000;++i)value="{\"next\":"+value+"}";
  const auto deepRecord=insideRecord?legacyRecord.dump().substr(0,legacyRecord.dump().size()-1)+",\"metadata\":"+value+"}":legacyRecord.dump();
  const auto deepRaw="{\""+legacyKey+"\":"+deepRecord+(insideRecord?"":",\"unknown\":"+value)+"}";
  writeText(deepRaw);float px=17,py=18,pz=19,pc=0.6f;int pw=6;bool loaded=false,threw=false;try{loaded=legacyLoad(px,py,pz,pw,pc);}catch(...){threw=true;}
  txCheck(!threw&&!loaded&&px==17&&py==18&&pz==19&&pw==6,"deep calibration trees remain uncalibrated with unchanged outputs");++scenarios;
  if(!threw&&!loaded){
   const bool saved=candidate::inputCaptureStoreCalibration(legacyId,0.25f,-0.5f,0.75f,10,1);bool archived=false;
   for(const auto& entry:std::filesystem::directory_iterator(file.parent_path()))if(entry.path().filename().wstring().starts_with(L"calibration.json.before-calibration-recovery-")){std::ifstream input(entry.path(),std::ios::binary);if(std::string(std::istreambuf_iterator<char>(input),{})==deepRaw)archived=true;}
   txCheck(saved&&archived&&read()[legacyKey]["xOffset"]==0.25f,"deep calibration source is preserved and an explicit learned record can heal it");++scenarios;
  }
 }
