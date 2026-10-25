// The native observer/JSON functions and actor tail below are inserted verbatim.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
#include "sensor_inventory.h"
#include "sensor_inventory_cache.h"
#include "sensor_thread_mailbox.h"
using json=nlohmann::json;using UINT64=std::uint64_t;using ULONGLONG=std::uint64_t;
namespace inv=ymcc::sensors::inventory;
struct InputCaptureSensorMeta {std::string gyroDeviceId,accelDeviceId,gyroFamily,accelFamily;bool gyroPresent=false,accelPresent=false;};
struct Sink {std::atomic<UINT64> activeBindingGeneration{73};unsigned samples=0;};
static std::shared_ptr<Sink> g_inputCaptureSink;
static inv::ObservationStore<json> g_windowsSensorInventory;
static ymcc::sensors::ThreadMailbox g_sensorThreadMailbox;
static InputCaptureSensorMeta fixtureMeta;
static UINT64 fixtureTick=1000,fixturePower=7;
static std::vector<json> fixtureEvents;
static unsigned fixtureQueries=0,fixtureLogFailures=0;
static std::function<inv::Listing(inv::Kind,ULONGLONG,const std::function<bool()>&)> fixtureEnumerate;
static ULONGLONG GetTickCount64(){return fixtureTick;}
static UINT64 currentPowerGeneration(){return fixturePower;}
static InputCaptureSensorMeta inputCaptureSensorMetaSnapshot(){return fixtureMeta;}
static bool inputCaptureSensorStopRequested(){return g_sensorThreadMailbox.stopRequested();}
static void appendDomainLog(const char* domain,const char* event,const json& body){if(fixtureLogFailures){--fixtureLogFailures;throw std::runtime_error("isolated log failure");}fixtureEvents.push_back({{"domain",domain},{"event",event},{"body",body}});}
static inv::Listing inputCaptureEnumerateWindowsInventory(inv::Kind kind,ULONGLONG deadline,const std::function<bool()>& stop){++fixtureQueries;return fixtureEnumerate(kind,deadline,stop);}
// @@PROVIDER_FUNCTION@@
// @@JSON_FUNCTION@@
// @@OBSERVE_FUNCTION@@
static void inputCaptureRunWindowsSourceProbe(bool = false) {}
static void actorTail(inv::Schedule& inventorySchedule,bool shutdown=false){
// @@ACTOR_TAIL@@
}
static json failures=json::array();static unsigned checks=0;
static void check(bool ok,const std::string& label){++checks;if(!ok)failures.push_back(label);}
static inv::Device device(const std::string& id,bool enabled=true){return{id,"fixture-device",inputCaptureSensorProviderKey(id),enabled};}
static inv::Listing listing(std::initializer_list<inv::Device> devices){return{std::vector<inv::Device>(devices),devices.size(),false};}
static const std::string g1="ACPI#IMUA#1#{GYRO}",a1="ACPI#IMUA#1#{ACCEL}",g2="ACPI#IMUB#2#{GYRO}",a2="ACPI#IMUB#2#{ACCEL}";
static void reset(){g_windowsSensorInventory.clear();g_sensorThreadMailbox.close();g_sensorThreadMailbox.open();g_inputCaptureSink=std::make_shared<Sink>();fixtureTick=1000;fixturePower=7;fixtureEvents.clear();fixtureQueries=0;fixtureLogFailures=0;fixtureMeta={g1,a1,"winrt","winrt",true,true};fixtureEnumerate=[](auto kind,auto,const auto&){return kind==inv::Kind::Gyro?listing({device(g1),device(g2)}):listing({device(a1),device(a2)});};}
static inv::Ticket ticket(){return{ymcc::windows_motion::retain(g_inputCaptureSink,g_inputCaptureSink->activeBindingGeneration.load()),fixturePower};}
static json last(){return fixtureEvents.empty()?json::object():fixtureEvents.back()["body"];}
int main(int argc,char**argv){try{
 reset();const auto initial=fixtureMeta;const auto selected=g_inputCaptureSink;inputCaptureObserveWindowsInventory(ticket());auto data=last();
 check(fixtureQueries==2&&data["status"]=="complete","default success observes both metadata kinds separately");
 check(data["kinds"][0]["enumerated"]==2&&data["kinds"][1]["enumerated"]==2,"two pairs are counted without opening them");
 check(data["selectedAtStart"]["gyro"]["id"]==g1&&data["kinds"][0]["candidates"][0]["matchesSelectedId"]==true,"selected default is identified independently");
 check(data["providers"].size()==2&&data["providers"][0]["uniquePairCandidate"]==true,"same-provider metadata forms two candidate groups");
 check(g_inputCaptureSink==selected&&selected->samples==0&&fixtureMeta.gyroDeviceId==initial.gyroDeviceId&&fixtureMeta.accelDeviceId==initial.accelDeviceId,"observer never replaces selection or publishes a sample");
 check(data["sensorObjectsOpened"]==0&&data["selectionWritten"]==false&&data["samplesPublished"]==false&&data["physicalSensorPresenceProven"]==false,"inventory never claims source or game readiness");
 reset();fixtureEnumerate=[](auto,auto,const auto&){return inv::Listing{};};inputCaptureObserveWindowsInventory(ticket());data=last();
 check(data["status"]=="complete"&&data["kinds"][0]["enumerated"]==0&&data["kinds"][1]["enumerated"]==0,"successful empty metadata enumeration is actual known zero");
 reset();fixtureEnumerate=[](auto,auto,const auto&)->inv::Listing{throw inv::Error{inv::State::Failed,-5};};inputCaptureObserveWindowsInventory(ticket());data=last();
 check(data["status"]=="failed"&&data["kinds"][0]["enumerated"].is_null()&&data["kinds"][1]["enumerated"].is_null(),"failed enumeration is unknown, never zero devices");
 check(data["kinds"][0]["hresult"]==-5,"per-kind failure reason survives JSON");
 reset();fixtureEnumerate=[](auto kind,auto,const auto&){if(kind==inv::Kind::Gyro)throw inv::Error{inv::State::Failed,-6};return inv::Listing{};};inputCaptureObserveWindowsInventory(ticket());data=last();
 check(data["status"]=="partial"&&data["kinds"][0]["enumerated"].is_null()&&data["kinds"][1]["enumerated"]==0,"partial failure preserves unknown and zero independently");
 reset();fixtureEnumerate=[](auto,auto,const auto&)->inv::Listing{throw inv::Error{inv::State::TimedOut,-7};};inputCaptureObserveWindowsInventory(ticket());data=last();
 check(data["status"]=="timed-out"&&fixtureQueries==1&&data["kinds"][1]["status"]=="not-enumerated"&&data["kinds"][1]["enumerated"].is_null(),"timeout leaves the other kind not enumerated");
 reset();fixtureEnumerate=[](auto,auto deadline,const auto&){fixtureTick=deadline+1;return listing({device(g1)});};inputCaptureObserveWindowsInventory(ticket());data=last();
 check(data["status"]=="timed-out"&&data["kinds"][0]["enumerated"].is_null()&&fixtureQueries==1,"even immediately completed data is rejected after the shared budget");
 reset();fixtureEnumerate=[](auto,auto,const auto&)->inv::Listing{throw std::runtime_error("unexpected");};inputCaptureObserveWindowsInventory(ticket());data=last();check(data["status"]=="failed"&&fixtureQueries==2,"unknown enumeration exceptions do not escape observer");
 reset();fixtureEnumerate=[](auto kind,auto,const auto&){if(kind==inv::Kind::Gyro){auto r=listing({device(g1)});r.reportedCount=500;r.truncated=true;return r;}return listing({device(a1)});};inputCaptureObserveWindowsInventory(ticket());data=last();
 check(data["status"]=="truncated"&&data["kinds"][0]["reportedTotal"]==500&&data["kinds"][0]["enumerated"].is_null(),"truncation is not presented as a complete candidate count");
 check(data["providers"][0]["uniquePairCandidate"]==false,"partial inventory cannot become a unique selectable pair");
 reset();fixtureEnumerate=[](auto kind,auto,const auto&){return kind==inv::Kind::Gyro?listing({device(g1),device(g1)}):listing({device(a1)});};inputCaptureObserveWindowsInventory(ticket());data=last();
 check(data["providers"][0]["duplicateIds"]==true&&data["providers"][0]["uniquePairCandidate"]==false,"duplicate device IDs cannot establish a unique pair");
 reset();fixtureEnumerate=[](auto kind,auto,const auto&){return kind==inv::Kind::Gyro?listing({device(g1,false),device(g2)}):listing({device(a2)});};inputCaptureObserveWindowsInventory(ticket());data=last();
 check(data["kinds"][0]["enumerated"]==2&&data["kinds"][0]["enabled"]==1,"disabled candidates remain visible but distinct from enabled candidates");
 reset();fixtureMeta.gyroFamily=fixtureMeta.accelFamily="sensorapi";inputCaptureObserveWindowsInventory(ticket());data=last();
 check(data["selectedAtEnd"]["gyro"]["family"]=="sensorapi"&&data["kinds"][0]["selectedIdListed"].is_null()&&!data["kinds"][0]["candidates"][0]["matchesSelectedId"].get<bool>(),"SDK source is never relabelled as a selected WinRT object");
 reset();fixtureEnumerate=[](auto kind,auto,const auto&){if(kind==inv::Kind::Gyro)fixtureMeta.accelDeviceId="changed-after-first-real-SDK-sample";return kind==inv::Kind::Gyro?listing({device(g1)}):listing({device(a1)});};inputCaptureObserveWindowsInventory(ticket());data=last();
 check(data["selectedMetadataChangedDuringObservation"]==true&&data["selectedAtStart"]["accel"]["id"]==a1&&data["selectedAtEnd"]["accel"]["id"]!=a1,"metadata transition is recorded without rewriting the starting observation");
 for(const std::string change:{"binding","owner-aba","power"}){
  reset();const auto retained=ticket();if(change=="binding")g_inputCaptureSink->activeBindingGeneration=74;if(change=="owner-aba")g_inputCaptureSink=std::make_shared<Sink>();if(change=="power")fixturePower=8;inputCaptureObserveWindowsInventory(retained);data=last();
  check(data["status"]=="source-retired"&&data["sourceCurrent"]==false&&fixtureQueries==0,"retired initial ticket never starts inventory: "+change);
  check(data["bindingGeneration"]==73&&data["powerGenerationAtStart"]==7&&data["kinds"][0]["enumerated"].is_null(),"retired observation retains its original scope instead of signing current counters: "+change);
 }
 for(const std::string change:{"binding","owner-aba","power"}){
  reset();const auto retained=ticket();fixtureEnumerate=[change](auto,auto,const auto&){if(change=="binding")g_inputCaptureSink->activeBindingGeneration=74;if(change=="owner-aba")g_inputCaptureSink=std::make_shared<Sink>();if(change=="power")fixturePower=8;return listing({device(g1)});};inputCaptureObserveWindowsInventory(retained);data=last();
  check(data["status"]=="source-retired"&&data["sourceCurrent"]==false&&fixtureQueries==1,"retirement during enumeration prevents second-kind/current claims: "+change);
 }
 for(const auto request:{ymcc::sensors::ThreadRequest::Resume,ymcc::sensors::ThreadRequest::Stop,ymcc::sensors::ThreadRequest::Shutdown}){
  reset();fixtureEnumerate=[request](auto,auto,const auto&){g_sensorThreadMailbox.submit(request);return inv::Listing{};};inputCaptureObserveWindowsInventory(ticket());data=last();
  check(data["status"]=="cancelled"&&fixtureQueries==1&&data["kinds"][1]["status"]=="not-enumerated","new queued command cancels metadata census rather than delaying command: "+std::to_string(static_cast<int>(request)));
 }
 reset();g_sensorThreadMailbox.submit(ymcc::sensors::ThreadRequest::Stop);inputCaptureObserveWindowsInventory(ticket());data=last();check(data["status"]=="cancelled"&&fixtureQueries==0&&data["kinds"][0]["enumerated"].is_null(),"pending terminal work cancels before any metadata query");
 reset();const auto beforeRetire=fixtureMeta;const auto stale=ticket();inv::Snapshot complete;complete.state=inv::State::Retired;complete.sourceCurrent=false;for(auto& part:complete.parts){part.state=inv::State::Complete;}complete.parts[0].listing=listing({device(g1)});complete.parts[1].listing=listing({device(a1)});data=inputCaptureWindowsInventoryJson(complete,beforeRetire,beforeRetire,stale);check(data["providers"][0]["uniquePairCandidate"]==false,"complete old metadata cannot nominate a current pair after owner retirement");
 reset();fixturePower=0;inputCaptureObserveWindowsInventory(ticket());check(fixtureQueries==0&&last()["status"]=="source-retired","unknown power generation cannot authorize an observation");
 reset();g_inputCaptureSink->activeBindingGeneration=0;inv::Schedule inactive;actorTail(inactive);check(fixtureQueries==0&&fixtureEvents.empty(),"inactive binding never triggers census");
 reset();fixtureLogFailures=1;inputCaptureObserveWindowsInventory(ticket());check(last()["status"]=="failed"&&g_inputCaptureSink->samples==0,"logging failure is isolated from the live source");
 reset();fixtureLogFailures=2;inputCaptureObserveWindowsInventory(ticket());check(fixtureEvents.empty()&&g_inputCaptureSink->activeBindingGeneration==73,"observer remains noexcept even if fallback logging fails");
 reset();inv::Schedule schedule;auto command=g_sensorThreadMailbox.submit(ymcc::sensors::ThreadRequest::Start);auto active=g_sensorThreadMailbox.take();g_sensorThreadMailbox.complete(active,true);
 fixtureEnumerate=[command](auto kind,auto,const auto&){check(command->result==ymcc::sensors::ThreadMailbox::Result::Completed,"actual actor tail sees command acknowledged before metadata enumeration");return kind==inv::Kind::Gyro?listing({device(g1)}):listing({device(a1)});};actorTail(schedule);check(fixtureQueries==2&&last()["status"]=="complete","actual native actor tail runs one census for active binding");actorTail(schedule);check(fixtureQueries==2,"same owner/generation/power does not create repeated metadata work");
 g_inputCaptureSink->activeBindingGeneration=74;actorTail(schedule);check(fixtureQueries==4,"new binding allows one new observation");fixturePower=8;actorTail(schedule);check(fixtureQueries==6,"new power scope is recorded separately");g_inputCaptureSink=std::make_shared<Sink>();g_inputCaptureSink->activeBindingGeneration=74;actorTail(schedule);check(fixtureQueries==8,"same generation on replacement owner cannot inherit observed status");
 auto queued=g_sensorThreadMailbox.submit(ymcc::sensors::ThreadRequest::Resume);g_inputCaptureSink->activeBindingGeneration=75;actorTail(schedule);check(fixtureQueries==8,"actor does not start observation ahead of queued work");active=g_sensorThreadMailbox.take();g_sensorThreadMailbox.complete(active);actorTail(schedule);check(fixtureQueries==10,"latest binding can be observed after queued command completion");actorTail(schedule,true);check(fixtureQueries==10,"shutdown never runs inventory");
 reset();inv::Schedule weak;std::weak_ptr<Sink> old=g_inputCaptureSink;{const auto key=ticket();weak.claim(key.binding,key.power);}g_inputCaptureSink.reset();check(old.expired(),"scheduler weak key does not retain a retired sink");
 Sink reused;std::shared_ptr<Sink> first(&reused,[](auto*){});auto binding1=ymcc::windows_motion::retain(first,73);check(weak.claim(binding1,7),"weak scheduler accepts first control block");binding1={};first.reset();std::shared_ptr<Sink> second(&reused,[](auto*){});check(weak.claim(ymcc::windows_motion::retain(second,73),7),"same counter address with a different shared owner is an independent observation");
 unsigned currentReads=0;auto retired=inv::observe([](auto,auto,const auto&){return inv::Listing{};},[&]{return ++currentReads>1;},[]{return false;},[]{return 1000ull;});check(retired.state==inv::State::Retired&&!retired.sourceCurrent,"a once-observed retirement cannot be undone by a later current probe");
 const auto report=json{{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"actualNativeObserver",true},{"actualNativeJson",true},{"actualNativeActorTail",true},{"wholeNativeThreadProcExecuted",false},{"transport","enumeration/clock/metadata boundary fakes; no WinRT APIs executed"},{"hardwareIo",false},{"normalAppStarted",false},{"newProductDlls",json::array()}};std::ofstream(argv[1])<<report.dump(2);std::cout<<checks<<" checks / "<<failures.size()<<" failures\n";return failures.empty()?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
