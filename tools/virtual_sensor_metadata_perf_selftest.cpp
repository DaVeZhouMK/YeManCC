#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
#include "msi_motion_capabilities.h"
using json=nlohmann::json;using UINT32=unsigned;
static thread_local bool track=false,failNext=false;static thread_local std::uint64_t allocCalls=0,allocBytes=0;
void* operator new(std::size_t n){if(failNext){failNext=false;throw std::bad_alloc();}if(track){++allocCalls;allocBytes+=n;}if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}void operator delete(void* p,std::size_t) noexcept{std::free(p);}void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static json localeCoverage=json::array();
static unsigned checks=0;static void require(bool ok,const char* reason){++checks;if(!ok)throw std::runtime_error(reason);}
struct FakeMutex { unsigned locks=0,unlocks=0;void(*afterUnlock)()=nullptr;void lock(){++locks;}void unlock(){++unlocks;if(afterUnlock)afterUnlock();} };
static std::uint64_t namedMatrixCalls=0;
static bool ymccApplyNamedImuMatrix(const std::string&,double x,double y,double z,double& ox,double& oy,double& oz){++namedMatrixCalls;ox=-x;oy=z;oz=y;return true;}
struct Machine {std::string gyroMatrixId,accelMatrixId,deviceClass,product;};
struct Selector {std::string gyroMatrixId,accelMatrixId;};
#include "metadata-extracted.h"
static bool equalBits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
static auto typicalMeta(){before::InputCaptureSensorMeta m;m.gyroDeviceId="\\\\?\\HID#VID_0DB0&PID_1902#7&12345678&0&0000#{09485f5a-759e-42c2-bd0b-5c86f7a01851}";m.accelDeviceId="\\\\?\\HID#VID_0DB0&PID_1902#7&12345678&0&0000#{c2fb0f5f-e2d2-4c78-bc2c-542b36905e97}";m.gyroFamily=m.accelFamily="sensorapi";m.gyroHcLegacyFields=m.accelHcLegacyFields=true;m.gyroPresent=m.accelPresent=true;return m;}
static void setMeta(const before::InputCaptureSensorMeta& m){before::g_sensorMeta=m;current::g_sensorMeta={m.gyroDeviceId,m.accelDeviceId,m.gyroFamily,m.accelFamily,m.gyroHcLegacyFields,m.accelHcLegacyFields,m.gyroIntervalMs,m.accelIntervalMs,m.gyroPresent,m.accelPresent};}
static void testProject(){
 auto m=typicalMeta();setMeta(m);
 for(unsigned flags=0;flags<256;++flags){m.gyroFamily=(flags&1)?"sensorapi":"winrt";m.accelFamily=(flags&2)?"sensorapi":"SENSORAPI";m.gyroHcLegacyFields=flags&4;m.accelHcLegacyFields=flags&8;m.gyroPresent=flags&16;m.accelPresent=flags&32;setMeta(m);
  const auto old=before::inputCaptureSensorMetaSnapshot();const auto now=current::inputCaptureLegacySensorFieldsSnapshot();
  require(now.gyro== (old.gyroFamily=="sensorapi"&&old.gyroHcLegacyFields),"gyro projection");require(now.accel==(old.accelFamily=="sensorapi"&&old.accelHcLegacyFields),"accel projection");require(current::inputCaptureGyroPresentSnapshot()==old.gyroPresent,"gyro present");
 }
 setMeta(typicalMeta());before::g_sensorMetaMx.afterUnlock=[](){before::g_sensorMeta.gyroPresent=false;before::g_sensorMeta.gyroFamily="winrt";};current::g_sensorMetaMx.afterUnlock=[](){current::g_sensorMeta.gyroPresent=false;current::g_sensorMeta.gyroFamily="winrt";};
 auto old=before::inputCaptureSensorMetaSnapshot();auto now=current::inputCaptureLegacySensorFieldsSnapshot();require(old.gyroFamily=="sensorapi"&&now.gyro,"projection escaped locked snapshot");
 setMeta(typicalMeta());old=before::inputCaptureSensorMetaSnapshot();bool presence=current::inputCaptureGyroPresentSnapshot();require(old.gyroPresent&&presence,"present changed after unlock");before::g_sensorMetaMx.afterUnlock=nullptr;current::g_sensorMetaMx.afterUnlock=nullptr;
 require(before::g_sensorMetaMx.locks==before::g_sensorMetaMx.unlocks&&current::g_sensorMetaMx.locks==current::g_sensorMetaMx.unlocks,"mutex balance");
 setMeta(typicalMeta());failNext=true;bool oldFailed=false;try{(void)before::inputCaptureSensorMetaSnapshot();}catch(const std::bad_alloc&){oldFailed=true;}require(oldFailed,"old unnecessary copy did not allocate");failNext=true;now=current::inputCaptureLegacySensorFieldsSnapshot();require(failNext&&now.gyro&&now.accel,"projection allocated");failNext=false;failNext=true;presence=current::inputCaptureGyroPresentSnapshot();require(failNext&&presence,"present allocated");failNext=false;
}
static unsigned matrixLegacyCases=0;
static void testMatrix(){
 std::mt19937_64 rng(17);auto m=typicalMeta();
 for(unsigned i=0;i<6000;++i){m.gyroFamily=(i&1)?"sensorapi":"winrt";m.accelFamily=(i&2)?"sensorapi":"";m.gyroHcLegacyFields=i&4;m.accelHcLegacyFields=i&8;setMeta(m);
  Machine machine;const auto ci=i%5;if(ci<4){const auto& cap=ymcc::msi_sensor::kMotionCapabilities[ci];machine.deviceClass=cap.deviceClass;machine.product=(i&32)?std::string(cap.boards[0]):"unmatched-board";}else{machine.deviceClass="Unknown";machine.product="unknown";}machine.gyroMatrixId=(i&64)?"fake:gyro:x,y,z":"";machine.accelMatrixId=(i&128)?"fake:accel:x,y,z":"";
  before::g_machineIdentity=current::g_machineIdentity=machine;before::g_hcMotionSelector=current::g_hcMotionSelector={};
  if(i&256)before::g_hcMotionSelector=current::g_hcMotionSelector={"fallback:gyro","fallback:accel"};
  const auto* capability=ymcc::msi_sensor::motionCapability(machine.deviceClass,machine.product);if(capability&&capability->clawHidSensorApiLayout&&((m.gyroFamily=="sensorapi"&&m.gyroHcLegacyFields)||(m.accelFamily=="sensorapi"&&m.accelHcLegacyFields))&&(!machine.gyroMatrixId.empty()||!machine.accelMatrixId.empty()||(i&256)))++matrixLegacyCases;
  std::array<double,6> in,out1{},out2{};for(auto& x:in)x=std::bit_cast<double>(rng());const auto firstCalls=namedMatrixCalls;
  before::inputCaptureApplyHcMatrixCandidate(in[0],in[1],in[2],in[3],in[4],in[5],out1[0],out1[1],out1[2],out1[3],out1[4],out1[5]);const auto beforeCalls=namedMatrixCalls-firstCalls;const auto secondCalls=namedMatrixCalls;
  current::inputCaptureApplyHcMatrixCandidate(in[0],in[1],in[2],in[3],in[4],in[5],out2[0],out2[1],out2[2],out2[3],out2[4],out2[5]);require(beforeCalls==namedMatrixCalls-secondCalls,"matrix call order/count");for(unsigned k=0;k<6;++k)require(equalBits(out1[k],out2[k]),"matrix raw bits");
 }
 require(matrixLegacyCases>100,"MSI legacy capability coverage missing");
}
static void providerCase(const std::string& id){require(before::inputCaptureSensorProviderKey(id)==current::inputCaptureSensorProviderKey(id),"provider value changed");}
static void testProvider(){
 const std::array<std::string,12> bases={"", "ACPI#BOSC0200#1#{09}\\{instance}","prefix-ACPI#ABC#DEF#tail", "prefix-HID#VID_0DB0&PID_1902#INST#{09}", "hid#sensor#no-guid", "sensorapi:gyro", "sensorapi:accel", "acpi#a#{guid}", "acpi#abcdef#tail", "HID#a#{", "mixed-HID#id#instance#{guid}-ACPI#b#1#{later}", "ACPI###{empty}"};
 for(auto& s:bases)providerCase(s);
 for(unsigned i=0;i<256;++i){std::string s="prefix-ACPI#a";s.push_back(static_cast<char>(i));s+="#instance#{g}";providerCase(s);s="prefix-hid#id";s.push_back(static_cast<char>(i));s+="#instance#{g}";providerCase(s);}
 std::mt19937 rng(101);for(unsigned n=0;n<18000;++n){std::string s;const auto len=rng()%512;s.resize(len);for(auto& c:s)c=static_cast<char>(rng()&255);if(n%3==0)s.insert(rng()%(s.size()+1),bases[1]);if(n%5==0)s.insert(rng()%(s.size()+1),bases[3]);providerCase(s);}
 const char* localeNames[]={"C","English_United States.1252","Turkish_Turkey.1254"};for(auto name:localeNames){const auto active=std::setlocale(LC_CTYPE,name);localeCoverage.push_back({{"requested",name},{"available",active!=nullptr},{"active",active?active:""}});if(!active)continue;for(auto& s:bases)providerCase(s);for(unsigned i=0;i<256;++i){std::string s="hid#idi";s.push_back(static_cast<char>(i));s+="#inst#{guid}";providerCase(s);}}require(std::setlocale(LC_CTYPE,"C")!=nullptr,"restore C locale");
}
static void testConcurrent(){
 const auto m=typicalMeta();
 threadedBefore::g_sensorMeta={m.gyroDeviceId,m.accelDeviceId,m.gyroFamily,m.accelFamily,true,false,0,0,true,true};
 threadedCurrent::g_sensorMeta={m.gyroDeviceId,m.accelDeviceId,m.gyroFamily,m.accelFamily,true,false,0,0,true,true};
 std::atomic<bool> go{false};std::atomic<unsigned> invalid{0};std::vector<std::thread> threads;
 threads.emplace_back([&]{while(!go.load()){}for(unsigned i=0;i<10000;++i){
  {std::lock_guard<std::mutex> lock(threadedBefore::g_sensorMetaMx);threadedBefore::g_sensorMeta.gyroHcLegacyFields=(i&1)==0;threadedBefore::g_sensorMeta.accelHcLegacyFields=(i&1)!=0;}
  {std::lock_guard<std::mutex> lock(threadedCurrent::g_sensorMetaMx);threadedCurrent::g_sensorMeta.gyroHcLegacyFields=(i&1)==0;threadedCurrent::g_sensorMeta.accelHcLegacyFields=(i&1)!=0;}
 }});
 for(unsigned t=0;t<4;++t)threads.emplace_back([&]{while(!go.load()){}for(unsigned i=0;i<10000;++i){auto old=threadedBefore::inputCaptureSensorMetaSnapshot();auto now=threadedCurrent::inputCaptureLegacySensorFieldsSnapshot();if(old.gyroHcLegacyFields==old.accelHcLegacyFields||now.gyro==now.accel)invalid.fetch_add(1);}});
 go.store(true);for(auto& t:threads)t.join();require(invalid.load()==0,"mutex projection produced torn fields");
}
static json benchmark(){
 auto m=typicalMeta();setMeta(m);constexpr unsigned count=20000;json rows=json::array();volatile std::size_t sink=0;
 const auto run=[&](const char* name,auto beforeFn,auto currentFn){
  std::vector<double> beforeNs,currentNs;std::uint64_t bCalls=0,bBytes=0,cCalls=0,cBytes=0;
  for(int round=0;round<7;++round){for(int order=0;order<2;++order){bool old=order==(round%2);auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<count;++i){auto result=old?beforeFn():currentFn();sink=sink+result;}auto end=std::chrono::steady_clock::now();(old?beforeNs:currentNs).push_back(std::chrono::duration<double,std::nano>(end-start).count()/count);}}
  track=true;allocCalls=allocBytes=0;for(unsigned i=0;i<count;++i)sink=sink+beforeFn();bCalls=allocCalls;bBytes=allocBytes;track=false;
  track=true;allocCalls=allocBytes=0;for(unsigned i=0;i<count;++i)sink=sink+currentFn();cCalls=allocCalls;cBytes=allocBytes;track=false;
  std::sort(beforeNs.begin(),beforeNs.end());std::sort(currentNs.begin(),currentNs.end());rows.push_back({{"name",name},{"beforeMedianNs",beforeNs[3]},{"currentMedianNs",currentNs[3]},{"beforeAllocCalls",double(bCalls)/count},{"currentAllocCalls",double(cCalls)/count},{"beforeRequestedBytes",double(bBytes)/count},{"currentRequestedBytes",double(cBytes)/count}});
 };
 run("legacy-field-snapshot",[]{auto m=before::inputCaptureSensorMetaSnapshot();return std::size_t(m.gyroFamily=="sensorapi"&&m.gyroHcLegacyFields)+std::size_t(m.accelFamily=="sensorapi"&&m.accelHcLegacyFields);},[]{auto m=current::inputCaptureLegacySensorFieldsSnapshot();return std::size_t(m.gyro)+std::size_t(m.accel);});
 run("gyro-present-snapshot",[]{return std::size_t(before::inputCaptureSensorMetaSnapshot().gyroPresent);},[]{return std::size_t(current::inputCaptureGyroPresentSnapshot());});
 const auto baseline=typicalMeta();
 threadedBefore::g_sensorMeta={baseline.gyroDeviceId,baseline.accelDeviceId,baseline.gyroFamily,baseline.accelFamily,baseline.gyroHcLegacyFields,baseline.accelHcLegacyFields,baseline.gyroIntervalMs,baseline.accelIntervalMs,baseline.gyroPresent,baseline.accelPresent};
 threadedCurrent::g_sensorMeta={baseline.gyroDeviceId,baseline.accelDeviceId,baseline.gyroFamily,baseline.accelFamily,baseline.gyroHcLegacyFields,baseline.accelHcLegacyFields,baseline.gyroIntervalMs,baseline.accelIntervalMs,baseline.gyroPresent,baseline.accelPresent};
 run("legacy-field-snapshot-actual-mutex",[]{auto m=threadedBefore::inputCaptureSensorMetaSnapshot();return std::size_t(m.gyroFamily=="sensorapi"&&m.gyroHcLegacyFields)+std::size_t(m.accelFamily=="sensorapi"&&m.accelHcLegacyFields);},[]{auto m=threadedCurrent::inputCaptureLegacySensorFieldsSnapshot();return std::size_t(m.gyro)+std::size_t(m.accel);});
 run("gyro-present-snapshot-actual-mutex",[]{return std::size_t(threadedBefore::inputCaptureSensorMetaSnapshot().gyroPresent);},[]{return std::size_t(threadedCurrent::inputCaptureGyroPresentSnapshot());});
 run("HID-provider-key",[&]{return before::inputCaptureSensorProviderKey(m.gyroDeviceId).size();},[&]{return current::inputCaptureSensorProviderKey(m.gyroDeviceId).size();});
 m.gyroDeviceId="\\\\?\\ACPI#BOSC0200#1#{09485f5a-759e-42c2-bd0b-5c86f7a01851}\\{sensor-instance}";
 run("ACPI-provider-key",[&]{return before::inputCaptureSensorProviderKey(m.gyroDeviceId).size();},[&]{return current::inputCaptureSensorProviderKey(m.gyroDeviceId).size();});return rows;
}
int main(){try{testProject();testMatrix();testProvider();testConcurrent();std::cout<<json{{"pass",true},{"checks",checks},{"matrixCases",6000},{"matrixLegacyCases",matrixLegacyCases},{"randomProviderCases",18000},{"localeCoverage",localeCoverage},{"actualMutexWriterIterations",10000},{"actualMutexReaderSnapshots",80000},{"benchmarks",benchmark()},{"benchmarkMutexKinds","First two snapshot rows isolate copy/projection with FakeMutex; actual-mutex rows use uncontended std::mutex; provider rows have no lock. Costs include caller-local string destruction; no product hardware or contention cost measured."},{"matrixSeam","Named axis mapper and machine/selector inputs are value-only stubs; actual MSI capability and configuredLegacyAxes are used"},{"productHardwareExecuted",false}}.dump();return 0;}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
