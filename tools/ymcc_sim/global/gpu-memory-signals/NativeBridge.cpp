// Include the original complete Windows fixture without editing any method or assertion.
#include <map>
#include <functional>
#define main OriginalGpuWindowsSelftestMain
#include "gpu_dedicated_memory_windows_selftest.cpp"
#undef main
using json=nlohmann::json;
static std::map<std::string,std::function<json(const json&)>> commands;
static std::vector<json> diagnosticEvents;
static struct {std::string deviceClass="HandheldCompanion.Devices.ROGAllyX";} g_machineIdentity;
static std::wstring yemancc_data_dir(){return transport::root.wstring();}
static void ipc_on(const char* name,std::function<json(const json&)> handler){if(!commands.emplace(name,std::move(handler)).second)throw std::runtime_error("duplicate test command");}
static void record_gpu_dedicated_memory_capability(const json& result){if(diagnosticEvents.size()>=256)throw std::runtime_error("diagnostic fixture bound exceeded");diagnosticEvents.push_back(result);}
#include "GpuDispatch.g.h"
static json snapshot(){using namespace transport;return {{"firmwareWrites",state.asusWrites},{"registryWrites",state.writes},{"firmwareReads",state.asusReads},{"model",g_machineIdentity.deviceClass},{"capacityBytes",state.adapters.at(0).bytes},{"boot",state.boot},{"diagnosticEvents",diagnosticEvents.size()},{"hardwareIo",false}};}
static void configure(const json& fixture){
 using namespace transport;
 if(!fixture.is_object())throw std::runtime_error("fixture must be an object");
 static const std::vector<std::string> allowed={"reset","capacityMiB","boot","version","asusReply","asusIgnore","asusOpen","asusIo","clearJournal"};
 for(auto item=fixture.begin();item!=fixture.end();++item)if(std::find(allowed.begin(),allowed.end(),item.key())==allowed.end())throw std::runtime_error("unknown fixture control");
 if(fixture.contains("reset")){
  const auto model=fixture.at("reset").get<std::string>();reset();diagnosticEvents.clear();
  if(model=="rog"){pnp=LR"(PCI\VEN_1002&DEV_OFFLINE\ROG)";state.adapters[0].vendor=0x1002;state.adapters[0].name=L"AMD Radeon (offline ASUS transport)";state.adapters[0].bytes=2048*mib;g_machineIdentity.deviceClass="HandheldCompanion.Devices.ROGAllyX";}
  else if(model=="intel")g_machineIdentity.deviceClass="HandheldCompanion.Devices.ClawCG3EM";
  else throw std::runtime_error("unreviewed fixture model");
 }
 if(fixture.contains("capacityMiB")){const auto value=fixture.at("capacityMiB").get<unsigned>();if(value>32768)throw std::runtime_error("fixture RAM bound");state.adapters[0].bytes=std::uint64_t(value)*mib;}
 if(fixture.contains("boot"))state.boot=fixture.at("boot").get<DWORD>();
 if(fixture.contains("version"))state.version=wide(fixture.at("version").get<std::string>());
 if(fixture.contains("asusReply"))state.asusReply=fixture.at("asusReply").get<DWORD>();
 if(fixture.contains("asusIgnore"))state.asusIgnore=fixture.at("asusIgnore").get<bool>();
 if(fixture.contains("asusOpen"))state.asusOpen=fixture.at("asusOpen").get<bool>();
 if(fixture.contains("asusIo"))state.asusIo=fixture.at("asusIo").get<bool>();
 if(fixture.contains("clearJournal")&&fixture.at("clearJournal").get<bool>())std::filesystem::remove(root/L"gpu-dedicated-memory.json");
}
int main(int argc,char** argv){
 try{
  if(argc!=2)throw std::runtime_error("one explicit owned fixture directory required");
  const auto destination=std::filesystem::absolute(ymcc::dedicatedmemory::wide(argv[1])).lexically_normal();
  if(!std::filesystem::is_directory(destination)||std::filesystem::exists(destination/L"journal"))throw std::runtime_error("fresh existing owned fixture directory required");
  transport::root=destination/L"journal";std::filesystem::create_directory(transport::root);configure(json{{"reset","rog"}});reg_gpu_dedicated_memory();
  std::string line;unsigned count=0;
  while(std::getline(std::cin,line)){
   if(line.size()>131072||++count>256)throw std::runtime_error("request bound exceeded");
   const auto envelope=json::parse(line);const auto sequence=envelope.at("sequence");const auto request=envelope.at("request");json response;
   try{
    if(request.contains("fixture")){configure(request.at("fixture"));response={{"ok",true},{"stats",snapshot()}};}
    else{
     const auto command=request.at("cmd").get<std::string>();auto found=commands.find(command);if(found==commands.end())throw std::runtime_error("unreviewed production command");
     response={{"id",request.at("id")},{"result",found->second(request.value("args",json::object()))}};
    }
    transport::balanced();
   }catch(const std::exception& error){response={{"id",request.value("id",json(nullptr))},{"error",error.what()}};}
   std::cout<<json{{"sequence",sequence},{"response",response},{"stats",snapshot()}}.dump()<<'\n'<<std::flush;
  }
  transport::balanced();return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
