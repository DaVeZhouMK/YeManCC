namespace identity_contract {
static const ss::PairChoice sdkPair{
 "sensorapi:{00000001-0000-0000-0000-000000000000}",
 "sensorapi:{00000002-0000-0000-0000-000000000000}"};
static std::string providerId="HID#VID_1234&PID_5678#SOURCE-1";
static bool prepare(void* pointer,const ss::Proposal& r,const ss::ActiveSource& original,const std::string& key,const ss::Authority& authority,ss::PreparedSource& out) noexcept {
 auto& b=*static_cast<Backend*>(pointer);try{
  ++b.prepares;out.resource=++b.nextResource;b.resources[out.resource]="caller";out.requestId=r.requestId;out.parentLifetime=original.lifetime;out.parentScope=original.scope;out.pair=r.pair;out.profile=ss::fingerprint(original.model);out.calibrationKey=key;
  b.captureCells[out.resource]=std::make_shared<std::atomic<bool>>(true);out.captureLease=b.captureCells[out.resource];out.authority=out.captureAuthority=authority;out.captureScope=inv::Ticket{ymcc::windows_motion::retain(b.captureSink,100),1};b.clock+=10;out.evidence=evidence(r.requestId,r.pair,b.clock);
  for(auto& source:out.evidence.sources){source.provider=providerId;}
  return true;
 }catch(...){return false;}
}
namespace stream=ymcc::sensors::stream;
struct StreamBackend : Backend {
 std::map<std::uint64_t,std::shared_ptr<stream::PrivateSource>> captured;
 stream::CaptureParent parent()const{return{{ymcc::windows_motion::retain(captureSink,100),1},1,1,1,ss::fingerprint(active.model),true};}
 void advance(const ss::Authority& authority,const std::shared_ptr<stream::PrivateSource>& source,const ss::PairChoice& ids){
  for(unsigned sample=0;sample<2;++sample){++clock;for(unsigned kind=0;kind<2;++kind){const auto id=kind?ids.accelId:ids.gyroId;
   const pr::Axes raw{double(kind+1),2,3,clock*100,clock};check(source->observe(kind,id,authority,raw,parent()),"actual private cache accepts fresh qualified sample");}}
 }
 static StreamBackend& self(void* p){return *static_cast<StreamBackend*>(static_cast<Backend*>(p));}
 static bool prepareStream(void* p,const ss::Proposal& proposal,const ss::ActiveSource& original,const std::string& key,const ss::Authority& authority,ss::PreparedSource& out) noexcept {
  auto& b=self(p);try{
   ++b.prepares;const auto resource=++b.nextResource;const auto api=ss::sourceApis(proposal.pair);
   auto source=std::make_shared<stream::PrivateSource>(proposal,original,key,authority,b.parent(),std::array<std::string,2>{providerId,providerId},b.clock);
   check(source->contextCurrent(b.parent()),"API-qualified immutable authority initializes private capture");
   for(unsigned kind=0;kind<2;++kind){const auto id=kind?proposal.pair.accelId:proposal.pair.gyroId;
    check(source->opened(kind,id,b.parent()),"exact requested API identity opens private cache");
    if(api[kind]=="sensorapi"){
     ++b.clock;check(!source->observe(kind,id,authority,{1,2,3,b.clock*100,b.clock},b.parent()),"SDK ID and sample alone cannot invent native layout evidence");
     check(!source->readingFrame(kind,0,0,b.parent()),"SensorAPI cannot impersonate a WinRT ReadingTransform read");
     check(!source->standardSdkFrame(kind,id,"claw-compat-layout",b.parent()),"legacy Claw layout cannot qualify a standard SDK source");
     check(source->standardSdkFrame(kind,id,ss::standardSdkLayout(kind),b.parent()),"actual standard per-kind layout qualifies SDK coordinates");
    }else{check(source->readingFrame(kind,0,0,b.parent()),"WinRT still requires actual native ReadingTransform");check(!source->standardSdkFrame(kind,id,ss::standardSdkLayout(kind),b.parent()),"WinRT capture does not acquire a SensorAPI coordinate receipt");}
   }
   b.advance(authority,source,proposal.pair);out=source->prepared(resource,b.clock,b.parent());
   check(out.resource==resource,"complete fresh actual private cache prepares current evidence");
   if(!out.resource)return false;b.resources[resource]="caller";b.captured[resource]=source;return true;
  }catch(...){return false;}
 }
 static ss::Adoption adoptStream(void* p,const ss::Proposal& proposal,const ss::ActiveSource&,const ss::PreparedSource& prepared)noexcept{
  auto& b=self(p);try{
   ++b.adopts;const auto gen=++b.epoch;b.sink->activeBindingGeneration=gen;b.active.scope.binding=ymcc::windows_motion::retain(b.sink,gen);
   b.active.resource=prepared.resource;b.active.selectionId=proposal.requestId;b.active.pair=proposal.pair;b.active.api=prepared.authority->api;
   b.active.profile=prepared.profile;b.active.calibrationKey=prepared.calibrationKey;b.active.policy=proposal.policy;b.active.authority=prepared.authority;b.active.captureAuthority=prepared.captureAuthority;b.active.captureLease=prepared.captureLease;
   const auto source=b.captured.at(prepared.resource);b.advance(prepared.captureAuthority,source,proposal.pair);b.active.frame=source->frame(b.active,b.parent(),b.clock);
   return {true,true,gen};
  }catch(...){return {};}
 }
 static void releaseStream(void* p,std::uint64_t resource)noexcept{auto& b=self(p);const auto found=b.captured.find(resource);if(found!=b.captured.end()){found->second->retire();b.captured.erase(found);}Backend::release(p,resource);}
 ss::Operations streamOps(){auto result=ops();result.prepare=&prepareStream;result.adopt=&adoptStream;result.release=&releaseStream;return result;}
 void finish(){for(auto& [id,source]:captured)source->retire();captured.clear();cleanup();}
};
static void run(){

 {StreamBackend b;ss::SelectionTransaction txn;const auto old=b.active;
  const auto first=txn.apply(request(1,sdkPair),b.streamOps());check(first.outcome==ss::Outcome::Committed,"actual source contract commits a qualified SensorAPI pair");
  check(b.active.api==std::array<std::string,2>{"sensorapi","sensorapi"},"committed authority records actual API for both kinds");
  check(b.active.calibrationKey.ends_with("SENSORAPI-DPS-G-NATIVE"),"SensorAPI commit owns its API-specific calibration key");
  check(b.active.frame&&ss::frameCurrent(b.active,b.clock),"actual SDK private-cache frame is current under its separate capture lease");
  if(b.active.frame){for(unsigned kind=0;kind<2;++kind){const auto& value=b.active.frame->values[kind];check(!value.readingTransform&&value.standardSdkLayout==ss::standardSdkLayout(kind),"SDK frame retains standard layout and unknown WinRT transform");}
   auto forged=b.active;forged.frame->values[0].readingTransform=0;check(!ss::frameCurrent(forged,b.clock),"invented WinRT transform cannot strengthen SDK frame evidence");
   forged=b.active;forged.frame->values[0].standardSdkLayout=ss::standardSdkLayout(1);check(!ss::frameCurrent(forged,b.clock),"wrong per-kind SDK layout cannot publish");
   forged=b.active;forged.frame->values[0].api="winrt";check(!ss::frameCurrent(forged,b.clock),"a current source ID cannot be relabelled to another API");}
  const auto a=b.active;const auto noop=txn.apply(request(2,sdkPair),b.streamOps());check(noop.outcome==ss::Outcome::NoOp&&b.prepares==1,"current exact SDK pair avoids unnecessary rebind");
  const auto typed=txn.apply(request(3,pair("B")),b.streamOps());check(typed.outcome==ss::Outcome::Committed&&b.active.api==std::array<std::string,2>{"winrt","winrt"},"actual contract moves from SensorAPI back to typed WinRT capture");
  check(!ymcc::windows_motion::current(a.scope.binding)&&!ss::frameCurrent(a,b.clock),"old SDK choice is retired independently from retained capture");
  const auto back=txn.apply(request(4,sdkPair),b.streamOps());check(back.outcome==ss::Outcome::Committed&&b.active.calibrationKey==a.calibrationKey,"returning to same SDK IDs restores original API-qualified key");
  check(b.active.calibrationKey!=old.calibrationKey,"explicit SDK source never borrows original default calibration key");
  if(b.active.frame){const auto source=b.captured.at(b.active.resource);source->retire();check(!ss::frameCurrent(b.active,b.clock)&&!source->frame(b.active,b.parent(),b.clock),"capture retirement rejects retained samples even while choice epoch is unchanged");}
  b.finish();}
 {auto e=evidence(1,sdkPair,1000);for(unsigned kind=0;kind<2;++kind){e.sources[kind].api="sensorapi";e.sources[kind].provider=providerId;}
  check(!ss::evaluate(e,sdkPair).configurable,"SensorAPI evidence cannot rely on inherited WinRT transform");
  for(unsigned kind=0;kind<2;++kind){auto& series=e.sources[kind].observations;series.readingTransform.reset();series.standardSdkLayout=ss::standardSdkLayout(kind);}
  check(ss::evaluate(e,sdkPair).configurable,"two real progressing SDK triples qualify without a WinRT getter");
  e.sources[0].observations.standardSdkLayoutError=-1;check(!ss::evaluate(e,sdkPair).configurable,"failed SDK layout read is not a native coordinate proof");}

 {check(ss::validSensorApiId(sdkPair.gyroId),"independent canonical SDK identity is accepted");
  for(unsigned position=0;position<sdkPair.gyroId.size();++position){auto corrupted=sdkPair.gyroId;corrupted[position]='*';check(!ss::validSensorApiId(corrupted),"every reserved-prefix/GUID/delimiter position is validated: "+std::to_string(position));}
  for(const auto& suffix:std::array<std::string,3>{" ","/other",std::string(1,'\0')})check(!ss::validSensorApiId(sdkPair.gyroId+suffix),"trailing identity data cannot alias a sensor ID");
  auto uppercase=sdkPair;for(auto* id:{&uppercase.gyroId,&uppercase.accelId})std::transform(id->begin(),id->end(),id->begin(),[](unsigned char c){return c>='a'&&c<='z'?char(c-'a'+'A'):char(c);});
  check(ss::calibrationKey(model(),sdkPair,ss::TransformPolicy::ExplicitCurrentModel)==ss::calibrationKey(model(),uppercase,ss::TransformPolicy::ExplicitCurrentModel),"API-qualified calibration remains case insensitive like source IDs");}
 for(const auto& id:std::array<std::string,8>{sdkPair.gyroId,"SENSORAPI:{abcdef01-2345-6789-abcd-0123456789ab}","sensorapi:{00000000-0000-0000-0000-000000000000}","sensorapi:{00000001-0000-0000-0000-00000000000Z}","sensorapi:00000001-0000-0000-0000-000000000000","sensorapi:{00000001-0000-0000-0000-000000000000}/extra","sensorapi:","ACPI#SOURCE#{GYRO}"}){
  const bool expected=id==sdkPair.gyroId||id.starts_with("SENSORAPI:")||id.starts_with("ACPI#");
  const auto proposal=request(1,{id,id});const auto* reason=ss::authorizeIntent(model(),proposal);check((reason==nullptr)==expected,"strict API-qualified identity admission: "+id);
 }
 {const auto p=request(1,{sdkPair.gyroId,pair("B").accelId});check(ss::authorizeIntent(model(),p)!=nullptr,"mixed API request cannot silently open typed WinRT fallback");}
 {const auto sdkKey=ss::calibrationKey(model(),sdkPair,ss::TransformPolicy::ExplicitCurrentModel),typedKey=ss::calibrationKey(model(),pair("B"),ss::TransformPolicy::ExplicitCurrentModel);
  check(sdkKey.ends_with("SENSORAPI-DPS-G-NATIVE")&&typedKey.ends_with("WINRT-DPS-G-NATIVE"),"calibration namespace includes actual requested API");
  check(sdkKey!=typedKey,"API-specific calibration cannot alias existing typed source keys");}
 {Backend b;auto ops=b.ops();ops.prepare=&prepare;ss::SelectionTransaction txn;const auto result=txn.apply(request(1,sdkPair),ops);
  check(result.outcome!=ss::Outcome::Committed,"declared SDK IDs without standard SDK observation cannot manufacture a commit");
  check(b.active.api==std::array<std::string,2>{"winrt","winrt"},"rejected SDK preparation retains actual original API");b.cleanup();}
}
}
int main(int argc,char** argv){
 try{if(argc!=2)return 2;identity_contract::run();
 const json report={{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"productionContractExecuted",true},{"platformOperationsControlled",true},{"productionPrivateRouteIntegrated",false},{"hardwareIo",false},{"newProductDlls",json::array()}};
 std::ofstream(argv[1])<<report.dump(2);std::cout<<checks<<" checks / "<<failures.size()<<" failures\n";return failures.empty()?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}