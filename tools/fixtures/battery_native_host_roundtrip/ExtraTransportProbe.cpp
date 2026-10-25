#include "battery_charge_client.h"
#include <iostream>
using Json=nlohmann::json;
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int wmain(int argc,wchar_t** argv){
 if(argc!=4)return 3;memory::ensure();const std::wstring power=argv[1],state=argv[2];Json steps=Json::array();
 auto call=[&](Json input){auto result=ymcc::batterycharge::request(power,state,input);steps.push_back({{"input",input},{"result",result}});return result;};
 const auto name=memory::scenario;
 if(name=="bad-component"||name=="reconnect-timeout"||name=="handshake-eof"){
  auto result=call({{"op","set"},{"target",80},{"fence",std::string(64,'a')}});require(result.at("ok")==false,"untrusted or missing handshake never succeeds");require(memory::exchanges.size()==1&&memory::exchanges[0]["request"]["op"]=="hello","mutation not forwarded after handshake/reconnect failure");
 }else if(name=="host-read-failure"){
  auto result=call({{"op","get"}});require(result.at("ok")==false&&result.at("reason")=="mock read failed","actual Controller read failure reaches native result");
 }else{
  auto before=call({{"op","get"}});require(before.at("ok")==true&&before.at("supported")==true,"actual Controller read must succeed");
  if(name=="stale-fence"){
   auto control=memory::hostControl("fixture.set",{{"raw",1}});(void)control;
  }
  if(name=="partial-operation-write")memory::armWriteFailure=true;
  auto applied=call({{"op","set"},{"target",80},{"fence",before.at("fence")}});
  if(name=="truncated-after-apply"||name=="eof-after-apply"){
   require(applied.at("ok")==false,"lost/truncated actual operation reply must remain unconfirmed");auto after=call({{"op","get"}});require(after.at("ok")==true&&after.at("currentLimit")==80,"explicit reread observes committed actual Controller target");
  }else if(name=="partial-operation-write"){
   require(applied.at("ok")==false,"incomplete operation write cannot succeed");require(memory::exchanges.size()==3,"partial write never dispatches operation to actual Host");
  }else if(name=="stale-fence"){
   require(applied.at("ok")==false,"actual stale fence rejected");
  }else if(name=="software-owned-release"){
   require(applied.at("ok")==true&&applied.at("managed")==true,"actual software policy admitted");auto released=call({{"op","release"}});require(released.at("ok")==true&&released.at("managed")==false,"actual software policy released");
  }else{
   require(applied.at("ok")==true&&applied.at("currentLimit")==80,"actual Controller commit/readback reaches native");auto after=call({{"op","get"}});require(after.at("ok")==true&&after.at("currentLimit")==80,"actual explicit followup read");
  }
 }
 int sets=0;for(const auto& e:memory::exchanges)if(e.at("request").value("op","")=="set")++sets;
 Json r={{"status","PASS"},{"scenario",name},{"steps",steps},{"actualSetDispatches",sets},{"actualHostStateModel","existing Rig/Controller/MemoryStore only"},{"realPipe",false},{"realHostMain",false}};std::ofstream out{std::filesystem::path(argv[3])};out<<r.dump(2);out.close();std::cout<<"NATIVE_ROUNDTRIP_CASE_PASS "<<name<<std::endl;return 0;
}
