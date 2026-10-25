// Real native request-lease parser and original decoder oracle; no network/device/time.
#include <iostream>
#include <fstream>
#include <new>
#include <string>
#include <vector>
#include "fan_response_contract.h"
using json=nlohmann::json;
unsigned parserCalls=0,checks=0;std::vector<std::string>failures;bool failLeaseSlice=false,failGenericParse=false;
json FixtureLeaseParse(const std::string& text){++parserCalls;if(failGenericParse)throw std::bad_alloc();return json::parse(text);}
std::string FixtureLeaseSlice(const std::string& text,size_t first,size_t length){if(failLeaseSlice)throw std::bad_alloc();return text.substr(first,length);}
#include "LeaseParserExtract.inc"
void check(bool value,const std::string& message){++checks;if(!value)failures.push_back(message);}
int main(int argc,char** argv){
 const std::string id="fixture-lease-0001";
 struct Case{std::string name,text;bool canonical;};std::vector<Case>cases;
 for(const auto& token:{std::string("12345678"),id,std::string(80,'A')}){
  cases.push_back({"canonical plain","{\"leaseId\":\""+token+"\"}",true});
  for(const auto* revision:{"0","1","18446744073709551615"})cases.push_back({"canonical mode","{\"leaseId\":\""+token+"\",\"msiFirmwareModeRevision\":"+revision+"}",true});
 }
 for(const auto& text:std::vector<std::string>{"", "null","[]","{}","true","123", "{\"leaseId\":\"short\"}","{\"leaseId\":\"fixture lease 0001\"}","{\"leaseId\":5}","{\"leaseId\":null}","{\"leaseId\":false}","{\"leaseId\":\""+std::string(81,'x')+"\"}","{\"leaseId\":\"\"}","{\"leaseId\":\"fixture-\\u006cease-0001\"}"," {\"leaseId\":\""+id+"\"}","{\"leaseId\": \""+id+"\"}","{\"leaseId\":\""+id+"\" }","{\"leaseId\":\""+id+"\"}\n","{\"LeaseId\":\""+id+"\"}","{\"lease\":\""+id+"\"}","{\"lease\":{\"LeaseId\":\""+id+"\"}}","{\"state\":{\"leaseId\":\""+id+"\"}}","{\"leaseId\":\""+id+"\",\"extra\":true}","{\"leaseId\":\""+id+"\",\"leaseId\":\"fixture-lease-0002\"}","{\"lease\":\"fixture-lease-0002\",\"leaseId\":\""+id+"\"}","{\"lease\":\"bad\",\"leaseId\":\""+id+"\"}","{\"lease\":12,\"leaseId\":\""+id+"\"}","{\"leaseId\":\""+id+"\"}garbage","{\"leaseId\":\""+id+"\"]","{\"leaseId\":\""+id+"\",}","{\"leaseId\":\""+id+"\",\"extra\":\""+std::string(2048,'x')+"\"}"})cases.push_back({"legacy/malformed",text,false});
 for(const auto* number:{"00","01","-1","+1","1.5","1e2","1e99999","18446744073709551616","9999999999999999999999999","null","true","\"1\"","","1 "," 1","1,\"extra\":true"})cases.push_back({"noncanonical mode","{\"leaseId\":\""+id+"\",\"msiFirmwareModeRevision\":"+number+"}",false});
 cases.push_back({"unterminated mode","{\"leaseId\":\""+id+"\",\"msiFirmwareModeRevision\":1]",false});
 cases.push_back({"mode before token","{\"msiFirmwareModeRevision\":1,\"leaseId\":\""+id+"\"}",false});
 json records=json::array();
 for(size_t index=0;index<cases.size();++index){const auto& c=cases[index];std::string oracle;try{oracle=extractFanLeaseIdFromJson(json::parse(c.text));}catch(...){}
  parserCalls=0;const auto actual=parseFanLeaseIdText(c.text);check(actual==oracle,"decoder result differs at case "+std::to_string(index));
  // Empty input already returns before JSON. All other noncanonical shapes keep the decoder.
  const unsigned expected=c.text.empty()?0:c.canonical?0:1;check(parserCalls==expected,"unexpected generic parse count at case "+std::to_string(index));
  records.push_back(json{{"case",index},{"name",c.name},{"canonical",c.canonical},{"result",actual},{"oracle",oracle},{"genericParses",parserCalls}});
 }
 json mutationCases=json::array();std::vector<std::string> corpus;
 for(const auto& seed:std::vector<std::string>{"{\"leaseId\":\""+id+"\"}","{\"leaseId\":\""+id+"\",\"msiFirmwareModeRevision\":7}"})for(size_t i=0;i<seed.size();++i){
   auto changed=seed;changed.erase(i,1);corpus.push_back(changed);
   if(std::string("{}\":,").find(seed[i])!=std::string::npos){changed=seed;changed.insert(i,1,' ');corpus.push_back(changed);changed=seed;changed[i]='\0';corpus.push_back(changed);}
 }
 unsigned emittedBodies=0;if(argc==2){std::ifstream input(argv[1]);if(!input)return 2;json values;input>>values;for(const auto& value:values){corpus.push_back(value.get<std::string>());++emittedBodies;}}
 for(size_t index=0;index<corpus.size();++index){std::string oracle;try{oracle=extractFanLeaseIdFromJson(json::parse(corpus[index]));}catch(...){}const auto actual=parseFanLeaseIdText(corpus[index]);check(actual==oracle,"edited/emitted body differs at "+std::to_string(index));mutationCases.push_back(json{{"case",index},{"result",actual},{"oracle",oracle}});}
 failLeaseSlice=failGenericParse=true;bool escaped=false;std::string allocationResult;try{allocationResult=parseFanLeaseIdText("{\"leaseId\":\""+id+"\"}");}catch(...){escaped=true;}failLeaseSlice=failGenericParse=false;check(!escaped,"lease extraction allocation error escaped its passive observer boundary");check(allocationResult.empty(),"failed extraction invented a lease");
 parserCalls=0;for(int i=0;i<100;++i){check(parseFanLeaseIdText("{\"leaseId\":\""+id+"\"}")==id,"plain heartbeat lost its token");check(parseFanLeaseIdText("{\"leaseId\":\""+id+"\",\"msiFirmwareModeRevision\":7}")==id,"MSI mode heartbeat lost its token");}
 check(parserCalls==0,"normal heartbeat still performs a full request JSON parse");
 std::cout<<json{{"ok",failures.empty()},{"checks",checks},{"failures",failures},{"cases",records},{"mutationCases",mutationCases},{"emittedBodies",emittedBodies},{"normalRequestGenericParses",parserCalls},{"hardwareCalls",0},{"networkCalls",0},{"cpuBenchmark",false}}.dump()<<'\n';return failures.empty()?0:1;
}
