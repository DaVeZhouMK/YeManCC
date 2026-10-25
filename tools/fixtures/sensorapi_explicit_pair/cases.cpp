template<class T>constexpr bool hasExactPair=requires(T value){value.explicitPair;};
static unsigned checks=0;static std::vector<std::string> failures;
static void expect(bool ok,const std::string& message){++checks;if(!ok)failures.push_back(message);}
static GUID sensorId(unsigned value){GUID out{};out.Data1=value;return out;}
static std::array<unsigned,2> receivedKinds(const std::shared_ptr<Shared>& state){std::lock_guard<std::mutex> lock(state->mx);std::array<unsigned,2> counts{};for(const auto& s:state->received)++counts[s.kind==Kind::Gyro?0:1];return counts;}
static Options standardOptions(const std::shared_ptr<Shared>& state,UINT64 generation){auto out=options(state,generation);out.clawHidCompatible=false;out.accelPollFloorMs=8;return out;}
static void balanced(const std::shared_ptr<Shared>& state,const std::string& name){expect(state->writes==0&&state->permissions==0,name+": no device writes or permissions");expect(state->managers==0&&state->sensors==0&&state->reports==0,name+": all COM references released");expect(state->createdThread!=GetCurrentThreadId()&&state->createdThread==state->destroyedThread,name+": worker owns COM lifetime");}
template<class T>static void explicitCases(){
 if constexpr(!requires(T value){value.explicitPair;}){expect(false,"missing explicit SensorAPI pair contract: available gyro 3 cannot be requested");}
 else {
  for(const unsigned gyro:{1u,3u}){auto state=std::make_shared<Shared>();state->ambiguous=true;Provider provider;T request=standardOptions(state,40+gyro);request.explicitPair=std::array<GUID,2>{sensorId(gyro),sensorId(2)};
   expect(provider.start(request),"exact pair starts worker");expect(await([&]{const auto c=receivedKinds(state);return c[0]>=2&&c[1]>=2;}),"both explicitly requested sources deliver actual SDK samples");expect(provider.stop(1500),"exact pair drains worker");
   bool ids=true,raw=true,provenance=true;for(const auto& sample:state->received){const bool g=sample.kind==Kind::Gyro;ids&=sample.descriptor.id.Data1==(g?gyro:2);
    raw&=sample.axes==(g?std::array<double,3>{double(gyro*10+1),double(gyro*10+2),double(gyro*10+3)}:std::array<double,3>{.25,-.5,1});
    provenance&=sample.timestamp100ns>0&&sample.receiptTick>0&&sample.generation==40+gyro&&sample.layout==(g?"sdk-motion-gyro-dps":"sdk-motion-accel-g");}
   expect(ids,"only requested sensor IDs are sampled; no ordinal fallback");expect(raw,"independent selected-device numbers survive actual field decoder");expect(provenance,"original timestamp, receive tick, generation and standard layout retained");balanced(state,"exact pair");
  }
  // One uniquely identified SDK object may expose both documented triples.
  {auto state=std::make_shared<Shared>();state->combined=true;Provider provider;T request=standardOptions(state,100);request.explicitPair=std::array<GUID,2>{sensorId(5),sensorId(5)};
   expect(provider.start(request),"one SDK object starts explicit dual-kind capture");expect(await([&]{const auto c=receivedKinds(state);return c[0]>=2&&c[1]>=2;}),"one SDK object exposes both independently decoded triples");expect(provider.stop(1500),"shared SDK object drains");
   bool raw=true;for(const auto& sample:state->received)raw&=sample.descriptor.id.Data1==5&&sample.axes==(sample.kind==Kind::Gyro?std::array<double,3>{51,52,53}:std::array<double,3>{.25,-.5,1});
   expect(raw,"same ID is accepted by actual per-kind data fields, not by positional guessing");balanced(state,"shared SDK object");
  }
  for(unsigned fault=0;fault<9;++fault){auto state=std::make_shared<Shared>();state->ambiguous=true;Provider provider;T request=standardOptions(state,70+fault);
   std::array<GUID,2> pair{sensorId(3),sensorId(2)};
   if(fault==0)pair[0]=sensorId(999);
   if(fault==1)pair[1]=sensorId(999);
   if(fault==2)pair[0]=GUID{};
   if(fault==3)pair[1]=pair[0];
   if(fault==4)pair={sensorId(2),sensorId(1)};
   if(fault==5){state->duplicateIds=true;pair[0]=sensorId(1);}
   if(fault==6)request.wantGyro=false;
   if(fault==7)request.wantAccel=false;
   if(fault==8)request.clawHidCompatible=true;
   request.explicitPair=pair;std::atomic<bool> inventory{false};std::atomic<int> g{-2},a{-2};request.inventory=[&](const auto&,int gyro,int accel){g=gyro;a=accel;inventory=true;};
   expect(provider.start(request),"invalid exact request still has bounded worker lifetime");expect(await([&]{return inventory.load()||!provider.running();}),"invalid request settles");expect(provider.stop(1500),"invalid request drains");
   expect(state->received.empty(),"invalid exact pair cannot leak any available fallback sample: "+std::to_string(fault));expect(g<0&&a<0,"invalid pair publishes no partial exact selection: "+std::to_string(fault));balanced(state,"invalid pair");
  }
  for(const std::string issue:{"missing-axis","no-timestamp","denied"}){auto state=std::make_shared<Shared>();state->ambiguous=true;state->missingAxis=issue=="missing-axis";state->noTimestamp=issue=="no-timestamp";state->denied=issue=="denied";
   Provider provider;T request=standardOptions(state,90);request.explicitPair=std::array<GUID,2>{sensorId(3),sensorId(2)};expect(provider.start(request),"selected bad-report source starts read-only worker");
   expect(await([&]{return state->gyroReads>=2&&state->accelReads>=2;}),"selected source actually attempted both SDK reports");expect(provider.stop(1500),"selected bad reports drain");expect(state->received.empty(),"selection alone cannot fabricate usable numeric/timestamped samples: "+issue);balanced(state,"bad report");
  }
 }
}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 // Both gyro 1 and gyro 3 really expose different standard reports, with no typed WinRT devices in this transport.
 {auto state=std::make_shared<Shared>();ComPtr<ISensor> source;source.Attach(new Sensor(state,Kind::Gyro,3));ComPtr<ISensorDataReport> report;source->GetData(&report);PROPVARIANT value{};const PROPERTYKEY key{kSdkMotion,10};expect(SUCCEEDED(report->GetSensorValue(key,&value))&&value.dblVal==31,"SDK-only gyro 3 is independently readable and numerically distinct");PropVariantClear(&value);}
 {auto state=std::make_shared<Shared>();state->ambiguous=true;Provider provider;auto request=standardOptions(state,21);std::atomic<int> gyro{-2},accel{-2};std::atomic<unsigned> inventoryRows{0};request.inventory=[&](const auto& rows,int g,int a){inventoryRows=static_cast<unsigned>(rows.size());gyro=g;accel=a;};
  expect(provider.start(request),"default standard SDK worker starts");expect(await([&]{return receivedKinds(state)[1]>=2;}),"default retains its existing usable accel behavior");expect(provider.stop(1500),"default drains");
  expect(inventoryRows==3,"controlled inventory contains two gyro candidates and one accel");expect(gyro==-1&&accel>=0,"default continues to reject equal gyro candidates");expect(receivedKinds(state)[0]==0,"default cannot guess the first gyro");balanced(state,"default pair");}
 {auto state=std::make_shared<Shared>();state->combined=true;Provider provider;auto request=standardOptions(state,101);
  expect(provider.start(request),"existing default route opens one SDK object exposing both kinds");expect(await([&]{const auto c=receivedKinds(state);return c[0]>=2&&c[1]>=2;}),"existing default route already supports both documented triples from one ID");expect(provider.stop(1500),"default shared SDK object drains");
  expect(receivedKinds(state)[0]>=2&&receivedKinds(state)[1]>=2,"shared object provides real gyro and accel samples without explicit selection");balanced(state,"default shared object");}
 explicitCases<Options>();
 nlohmann::json report={{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"exactPairContractPresent",hasExactPair<Options>},{"realSdkDecoder",true},{"sdkTransport","controlled COM reports; no typed WinRT sources"},{"hardwareIo",false},{"newProductionDlls",nlohmann::json::array()}};
 std::ofstream(argv[1])<<report.dump(2);std::cout<<report["status"]<<" "<<checks<<" checks / "<<failures.size()<<" failures\n";return failures.empty()?0:1;
}