// R28 actual source-extracted ACK parser vs its existing DOM truth.
#define NOMINMAX
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
static std::string g_inputHostRunId="fixture-run",g_inputHostStartupNonce="fixture-nonce",g_inputHostHostInstanceId="fixture-instance";
static unsigned long long g_inputHostEpoch=2,g_inputHostConfigRevision=1;
static const char* kInputHostTargetId="fixture-target";
static const char* kInputHostOwnerIdentity="fixture-owner";
static const char* kInputHostProtocolConfigHash="fixture-config";
static const char* kInputHostLockedCoreSha256="fixture-core-sha";
static const char* kInputHostLockedCoreMvid="fixture-mvid";
static unsigned long long inputHostProtocolPowerGenerationLocked(){return 3;}
static std::string inputHostBoundOrCurrentPersona(){return "steamdeck";}
static std::string inputHostBoundOrCurrentProfileIdentity(){return "fixture-profile";}
#include "extracted.hpp"
static uint64_t checks=0,cases=0,oldFalseAccepts=0,newConservativeFallbacks=0,sink=0;
static void check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
static json baseAck(){return {{"protocolVersion",1},{"command","SUBMIT_FRAME"},{"requestId","r"},{"sequence",5},{"runId","fixture-run"},{"epoch",2},{"powerGeneration",3},{"targetId","fixture-target"},{"ownerIdentity","fixture-owner"},{"persona","steamdeck"},{"profileIdentity","fixture-profile"},{"startupNonce","fixture-nonce"},{"configRevision",1},{"configHash","fixture-config"},{"assetVerified",true},{"lockedCoreSha256","fixture-core-sha"},{"lockedCoreMvid","fixture-mvid"},{"hostInstanceId","fixture-instance"},{"ok",true},{"status","frame-accepted"},{"firstFrame",false},{"submitElapsedUs",27},{"targetPnpAdmitted",true},{"targetPnpRootCount",1},{"targetPnpHidChildCount",4},{"detail","canonical-frame-submitted"},{"releaseDisposition",nullptr}};}
static std::string extra(const std::string& raw){auto s=baseAck().dump();s.insert(s.size()-1,raw);return s;}
static bool same(const InputHostFastReceipt&a,const InputHostFastReceipt&b){return a.used==b.used&&a.firstFrame==b.firstFrame&&a.submitElapsedUs==b.submitElapsedUs&&a.hostInstanceId==b.hostInstanceId;}
static bool pair(const std::string& wire,int expected=-1){
 ++cases;InputHostFastReceipt oldOut,newOut;oldOut.used=newOut.used=true;oldOut.firstFrame=newOut.firstFrame=true;oldOut.submitElapsedUs=newOut.submitElapsedUs=777;oldOut.hostInstanceId=newOut.hostInstanceId="sentinel";const auto sentinel=newOut;
 bool old=baselineAck::scanSubmitFrameReceipt(wire.data(),wire.data()+wire.size(),"frame-accepted","r",5,oldOut);
 bool now=candidateAck::scanSubmitFrameReceipt(wire.data(),wire.data()+wire.size(),"frame-accepted","r",5,newOut);
 auto dom=domGolden(wire,"frame-accepted","r",5);
 check(!now||old,"candidate expanded fast acceptance");check(!now||dom.accepted,"candidate accepted DOM-invalid ACK");
 if(now){check(newOut.firstFrame==dom.firstFrame&&newOut.submitElapsedUs==dom.submitUs&&newOut.hostInstanceId==dom.instance,"candidate/DOM result values differ");check(same(oldOut,newOut),"valid receipt values differ");}
 else check(same(newOut,sentinel),"failed fast path mutated out receipt");
 if(old&&!dom.accepted)++oldFalseAccepts;if(old&&dom.accepted&&!now)++newConservativeFallbacks;
 if(expected>=0)check(now==(expected!=0),"unexpected fast eligibility");return now;
}
static std::string uescape(unsigned value){char out[7];std::snprintf(out,sizeof(out),"\\u%04X",value);return out;}
static void correctness(){
 const auto base=baseAck().dump();pair(base,1);
 for(unsigned u=0;u<=0xffff;++u){auto token=uescape(u);bool surrogate=u>=0xd800&&u<=0xdfff;pair(extra(",\"detail\":\""+token+"\""),surrogate?0:1);}
 for(unsigned hi=0xd800;hi<=0xdbff;++hi)for(unsigned lo:{0xdc00u,0xdfffu})pair(extra(",\"detail\":\""+uescape(hi)+uescape(lo)+"\""),1);
 for(const auto& raw:std::vector<std::string>{"\\uD800","\\uDC00","\\uD800\\u0041","\\uD800\\uD800","\\uDC00\\uD800"}){
  pair(extra(",\"detail\":{\"nested\":[\""+raw+"\"]}"),0);pair(extra(",\"detail\":{\""+raw+"\":true}"),0);
 }
 for(const auto& num:std::vector<std::string>{"1e9999","-1e9999","1e309","-1e309","1.7976931348623159e308","18446744073709551616","-1","0.0","1e-9999","-0","1.25","9.3e+20"}){
  pair(extra(",\"detail\":"+num),0);pair(extra(",\"detail\":[{\"value\":"+num+"}]"),0);
 }
 for(unsigned e=0;e<10000;e+=97){pair(extra(",\"detail\":1e"+std::to_string(e)),0);pair(extra(",\"detail\":1e-"+std::to_string(e)),0);}
 for(const auto& token:std::vector<std::string>{"0","1","18446744073709551615"})pair(extra(",\"detail\":"+token),1);
 for(const auto& invalid:std::vector<std::string>{"1.","+1","01",".5","-","NaN","Infinity","[1,]","{\"x\":}","\"\\xAA\""})pair(extra(",\"detail\":"+invalid),0);
 for(unsigned n=0;n<base.size();++n)pair(base.substr(0,n),0);
 pair(base+std::string(1,'\0'),0);pair(base+"\r\n",1);
 for(const auto& bad:std::vector<std::string>{"\"tail\""," trailing","[]","\0"})if(!bad.empty())pair(base+bad,0);
 for(const auto& value:std::vector<json>{nullptr,false,0,"wrong",json::object(),json::array()})for(const auto& key:{"protocolVersion","requestId","sequence","runId","epoch","persona","hostInstanceId","ok","status","firstFrame","submitElapsedUs"}){auto j=baseAck();j[key]=value;pair(j.dump());}
 pair(extra(",\"requestId\":\"r\""),0);pair(extra(",\"requestId\":\"wrong\""),0);
 for(const auto& bytes:std::vector<std::string>{std::string("\xc0\xaf",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\xe2\x82",2)})pair(extra(",\"detail\":\""+bytes+"\""),0);
 std::mt19937_64 rng(20261015);
 for(unsigned n=0;n<4000;++n){auto j=baseAck();switch(n%4){case 0:j["detail"]=rng();break;case 1:j["detail"]=static_cast<int64_t>(rng());break;case 2:j["detail"]="valid-微星-fixture-"+std::to_string(rng());break;default:j["detail"]={{"nested",json::array({rng(),true,nullptr,"😀"})}};break;}pair(j.dump());}
 // Every required or optional known field mutation still delegates to the original rules.
 check(oldFalseAccepts>=2048,"original malformed-string defect not reproduced");check(newConservativeFallbacks>0,"valid nonuint fallback not exercised");
}
using Scan=bool(*)(const char*,const char*,const char*,const std::string&,unsigned long long,InputHostFastReceipt&);
static double measure(const std::string& wire,bool after,unsigned n){Scan fn=after?candidateAck::scanSubmitFrameReceipt:baselineAck::scanSubmitFrameReceipt;InputHostFastReceipt result;
 auto begin=std::chrono::steady_clock::now();for(unsigned i=0;i<n;++i){sink+=fn(wire.data(),wire.data()+wire.size(),"frame-accepted","r",5,result);sink+=result.submitElapsedUs;}return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count()/n;
}
static double median(std::vector<double>x){std::sort(x.begin(),x.end());return x[x.size()/2];}
static json costs(int campaign){json rows=json::array();std::vector<std::pair<std::string,std::string>> scenarios{{"ASCII-full-ACK",baseAck().dump()},{"ASCII-unknown-nested",extra(",\"extension\":{\"a\":[0,1,2],\"b\":true}")},{"valid-Unicode-escape",extra(",\"extension\":\"\\u5FAE\\u661F\\uD83D\\uDE00\"")}};
 for(const auto& entry:scenarios){measure(entry.second,false,3000);measure(entry.second,true,3000);std::vector<double>b,a;for(unsigned r=0;r<11;++r){double x,y;if(((r+campaign)&1)==0){x=measure(entry.second,false,16000);y=measure(entry.second,true,16000);}else{y=measure(entry.second,true,16000);x=measure(entry.second,false,16000);}b.push_back(x);a.push_back(y);}double ratio=median(a)/median(b);bool hot=entry.first!="valid-Unicode-escape";rows.push_back({{"name",entry.first},{"beforeMedianNs",median(b)},{"afterMedianNs",median(a)},{"candidateToBaselineRatio",ratio},{"hotAsciiControl",hot},{"within5PercentRegression",!hot||ratio<=1.05},{"beforeSamplesNs",b},{"afterSamplesNs",a}});}
 return rows;
}
int main(int argc,char**argv){try{correctness();if(argc>1&&std::string(argv[1])=="cost"){int c=argc>2?std::stoi(argv[2]):1;auto rows=costs(c);bool pass=true;for(const auto&r:rows)pass=pass&&r.at("within5PercentRegression").get<bool>();std::cout<<json({{"pass",pass},{"campaign",c},{"rows",rows},{"correctnessChecks",checks},{"scope","source-extracted ACK validation only; no actual pipe/Host or CPU claim"}}).dump(2)<<'\n';return pass?0:2;}
 std::cout<<json({{"pass",true},{"cases",cases},{"checks",checks},{"originalFalseAccepts",oldFalseAccepts},{"candidateFalseAccepts",0},{"conservativeValidFallbacks",newConservativeFallbacks},{"hardwareOrProductExecuted",false}}).dump(2)<<'\n';return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
