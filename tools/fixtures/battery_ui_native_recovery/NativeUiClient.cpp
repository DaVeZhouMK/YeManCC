#include "native/battery_charge_client.h"
#include <iostream>
using Json=nlohmann::json;
int main(int argc,char** argv){
 if(argc!=3)return 3;std::string line;while(std::getline(std::cin,line)){
  const auto input=Json::parse(line);const auto id=input.at("id");if(input.value("command","")=="close"){memory::finish();std::cout<<"UI_NATIVE_RESULT "<<Json{{"id",id},{"closed",true}}.dump()<<std::endl;return 0;}
  memory::scenario=input.value("fault","");memory::initialized=true;memory::executablePath=argv[0];memory::connects=0;
  const auto first=memory::exchanges.size();auto request=input.at("request");auto result=ymcc::batterycharge::request(std::filesystem::path(argv[1]).wstring(),std::filesystem::path(argv[2]).wstring(),request);
  Json exchanges=Json::array();for(auto i=first;i<memory::exchanges.size();++i)exchanges.push_back(memory::exchanges[i]);
  std::cout<<"UI_NATIVE_RESULT "<<Json{{"id",id},{"input",request},{"result",result},{"actualHostExchanges",exchanges},{"openPipes",memory::pipes.size()},{"openEvents",memory::events.size()},{"launchAttempts",memory::launchAttempts}}.dump()<<std::endl;
 }
 memory::finish();return 0;
}
