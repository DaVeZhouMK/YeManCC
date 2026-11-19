#pragma once
inline std::atomic<unsigned> readDiscoveries{0};
inline bool discoverAbsent=false;
inline RuntimeReceipt fixtureDiscoverRuntime(){
    ++readDiscoveries;
    if(discoverAbsent)return selectRuntime({},false);
    FILETIME birth{},exit{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user);
    return selectRuntime({Identity{GetCurrentProcessId(),(std::uint64_t(birth.dwHighDateTime)<<32)|birth.dwLowDateTime,ymcc::losslessUiaFixture::image}},false);
}
#include "read-inspect.inc"
inline bool captureSharedPowerLifecycle(ymcc::sleeppower::SharedPowerV1& out);
inline bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1& out);
inline ymcc::sleeppower::RevocationMirrors::Registration observeSharedPowerRevocation(const ymcc::sleeppower::SharedPowerV1& captured){
    auto registration=g_sharedPowerRevocations.observe(&readSharedSystemPower,captured);
    borrowedCancellation=registration.event();return registration;
}
inline std::wstring U2W(const std::string& value){return std::wstring(value.begin(),value.end());}
inline void installRead(){
#include "read-entry.inc"
}
inline json readArgs(const std::string& command){
    if(command=="lossless.detect")return json::object();
    FILETIME birth{},exit{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user);
    return {{"exe","C:/Fixture/LosslessScaling.exe"},{"pid",GetCurrentProcessId()},{"processCreated",std::to_string((std::uint64_t(birth.dwHighDateTime)<<32)|birth.dwLowDateTime)}};
}
inline json callRead(const std::string& command){return readEntries.at(command)(readArgs(command));}
inline bool unavailableRead(const std::string& command,const json& value){return command=="lossless.detect"?!value.at("known").get<bool>()&&!value.at("running").get<bool>():!value.at("ok").get<bool>()&&value.at("button")==""&&value.at("status")=="";}
inline bool readableRead(const std::string& command,const json& value){return command=="lossless.detect"?value.at("known").get<bool>()&&value.at("running").get<bool>()&&value.at("readable").get<bool>():value.at("ok").get<bool>()&&value.at("button")=="Scale";}
inline void beginRead(){ymcc::losslessUiaFixture::reset();readDiscoveries=0;discoverAbsent=false;g_exitRequested=false;readEntries.clear();borrowedCancellation=nullptr;installRead();}
inline void expectReadResources(){namespace leaf=ymcc::losslessUiaFixture;
    expect(leaf::clientsLive==0&&leaf::elementsLive==0&&leaf::conditionsLive==0,"read-returned-all-controlled-uia-references");
    expect(leaf::initCalls==leaf::uninitCalls&&leaf::processOpened==leaf::processClosed&&leaf::unexpected==0,"read-returned-com-apartment-and-owned-process-handles");
}
