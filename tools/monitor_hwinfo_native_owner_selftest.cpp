// Exercises the real config parser/merger and the complete demand-driven
// transaction using inert operations. This executable is test-only.
#include <fstream>
#include <iostream>
#include "../native/monitor_hwinfo_manager.h"
using namespace ymcc::hwinfo;
static std::string bytesFromHex(const std::string&s){std::string out;require(s.size()%2==0,"bad hex");for(std::size_t i=0;i<s.size();i+=2)out.push_back(static_cast<char>(std::stoul(s.substr(i,2),nullptr,16)));return out;}
static std::string hex(const std::string&s){constexpr char h[]="0123456789abcdef";std::string out;for(unsigned char c:s){out+=h[c>>4];out+=h[c&15];}return out;}
struct Fake {
 std::vector<std::string> calls;bool lock=true,healthyValue=false,stopped=true,launched=false,die=false,cancel=false;int failAt=0;unsigned waits=0,released=0;std::vector<std::optional<Proof>> proofs;std::size_t index=0;
 void call(const char*n){calls.push_back(n);if(failAt&&calls.size()==failAt)throw Error(5,"fixture-access-denied",true,5);}
 bool acquire(unsigned){call("lock");return lock;}void release(){calls.push_back("release");++released;}
 bool healthy(){call("healthy");return healthyValue;}void applyAffinity(){call("affinity");}
 std::wstring resolve(const std::wstring&){call("resolve");return L"C:\\HWiNFO64\\HWiNFO64.exe";}
 void prepare(const std::wstring&,const Request&){call("preflight");}void stop(){call("stop");stopped=true;}
 void configure(const std::wstring&,const Request&){call("configure");}std::string sensorBudget(const std::wstring&){call("sensor-budget");return "configured";}
 void assertStopped(){call("assert-stopped");if(!stopped)throw Error(4,"fixture-producer-appeared");}void launch(const std::wstring&){call("launch");launched=true;}
 bool exited(){call("exit-check");return die;}
 std::optional<Proof> proof(unsigned){call("proof");if(index<proofs.size())return proofs[index++];return {};}
 bool wait(unsigned){call("wait");++waits;return !cancel;}
};
int main(int argc,char**argv){if(argc!=4)return 2;Json inputs;std::ifstream(argv[1])>>inputs;Json plans=Json::array(),checks=Json::array();auto check=[&](const char*n,bool ok){checks.push_back({{"name",n},{"ok",ok}});if(!ok)std::cerr<<n<<'\n';};
 for(const auto&c:inputs){try{const auto source=c.contains("sourceHex")?decodeIni(bytesFromHex(c["sourceHex"])):managedIni(c.contains("snapshotPolling")?std::optional<bool>{c["snapshotPolling"].get<bool>()}:std::optional<bool>{});const auto target=c.contains("destinationHex")?std::optional<Ini>{decodeIni(bytesFromHex(c["destinationHex"]))}:std::optional<Ini>{};const auto plan=mergeIni(source,target,c.value("period",0U));plans.push_back({{"name",c["name"]},{"code",0},{"hex",hex(plan)}});}catch(const Error&e){plans.push_back({{"name",c["name"]},{"code",e.code},{"error",e.what()}});}}
 Request q;q.powerControl=L"C:\\YMCC\\PowerControl";Proof p{100,{1,2,3,4,5,6,7,8,9,10,11}},next=p;next.poll=101;
 {Fake o;o.healthyValue=true;auto r=transaction(o,q);check("healthy start has no config writes, stop, launch or proof loop",r.code==0&&o.calls==std::vector<std::string>{"lock","healthy","affinity","release"}&&o.released==1);}
 {Fake o;o.healthyValue=true;auto requested=q;requested.period=1500;auto r=transaction(o,requested);check("explicit mismatching period on healthy start is deferred using general health without writes",r.code==12&&o.calls==std::vector<std::string>{"lock","healthy","release"});}
 {Fake o;o.lock=false;auto r=transaction(o,q);check("busy lock performs no process or config work",r.code==11&&o.calls==std::vector<std::string>{"lock"}&&o.released==0);}
 {Fake o;o.proofs={p,next};auto r=transaction(o,q);check("recovery validates before stop, configures after stop, then confirms advancing producer",r.code==0&&o.calls==std::vector<std::string>{"lock","healthy","resolve","preflight","stop","configure","sensor-budget","assert-stopped","launch","exit-check","proof","wait","exit-check","proof","release"}&&o.waits==1&&o.released==1);}
 {Fake o;o.failAt=4;auto r=transaction(o,q);check("bad preflight preserves running upstream and releases recovery lock",r.code==5&&!o.launched&&std::find(o.calls.begin(),o.calls.end(),"stop")==o.calls.end()&&o.released==1&&r.needsElevation);}
 {Fake o;o.failAt=5;auto r=transaction(o,q);check("stop failure never configures or launches",r.code==5&&std::find(o.calls.begin(),o.calls.end(),"configure")==o.calls.end()&&!o.launched&&o.released==1);}
 {Fake o;o.failAt=6;auto r=transaction(o,q);check("configuration failure does not launch and returns elevation need after releasing lock",r.code==5&&!o.launched&&r.needsElevation&&o.released==1);}
 {Fake o;o.stopped=false;auto r=transaction(o,q);check("complete empty proof loop is bounded at 30 observations",r.code==7&&o.waits==30&&o.index==0&&o.released==1);}
 {Fake o;o.proofs.assign(30,p);auto r=transaction(o,q);check("stalled proof cannot be declared recovered",r.code==14&&o.waits==30&&o.released==1);}
 {Fake o;auto requested=q;requested.period=2000;auto r=transaction(o,requested);check("unconfirmed requested period returns its explicit code",r.code==13&&o.waits==30);}
 {Fake o;o.die=true;auto r=transaction(o,q);check("started process exit prevents false confirmation",r.code==6&&o.waits==0&&o.released==1);}
 {Fake o;o.cancel=true;auto r=transaction(o,q);check("application exit interrupts observation without terminating new producer",r.code==15&&o.launched&&o.waits==1&&o.released==1);}
 {Fake o;auto changed=next;changed.layout[3]=55;o.proofs={p,changed};auto r=transaction(o,q);check("different layout cannot count as continuous production",r.code==14&&o.waits==30);}
 {Fake o;o.proofs={p,std::nullopt,next};auto r=transaction(o,q);check("unreadable gap requires fresh consecutive producer proof",r.code==14&&o.waits==30);}
 {Fake o;o.proofs={p,p,next};auto r=transaction(o,q);check("same producer second does not count as progress",r.code==0&&o.waits==2);}
 {Fake o;auto requested=q;requested.restart=true;o.healthyValue=true;o.proofs={p,next};auto r=transaction(o,requested);check("explicit restart actually restarts healthy producer",r.code==0&&o.launched&&std::find(o.calls.begin(),o.calls.end(),"healthy")==o.calls.end());}
 check("only fixed unused groups are allowed",sensorGroups().size()==65&&sensorGroupAllowed(L"F0000101_31")&&sensorGroupAllowed(L"F0000302_0")&&sensorGroupAllowed(L"F00F5000_0")&&!sensorGroupAllowed(L"F0000FF0_0")&&!sensorGroupAllowed(L"F0000101_32")&&!sensorGroupAllowed(L"F0000101_01")&&!sensorGroupAllowed(L"..\\Enabled"));
 {int code=-1;check("ordinary app command line is not captured",!tryCommandLine(L"YMCC.exe --dev http://127.0.0.1",q.powerControl,code));check("malformed private mode exits before normal app startup",tryCommandLine(L"YMCC.exe --hwinfo-manage restart --period 999",q.powerControl,code)&&code==8);check("duplicate private flag is rejected",tryCommandLine(L"YMCC.exe --hwinfo-manage restart --period 1000 --period 2000",q.powerControl,code)&&code==8);}
 {auto request=q;request.executable=L"C:\\portable & edition!\\HWiNFO64.exe";request.powerControl=L"C:\\YMCC with spaces\\PowerControl\\";request.elevated=true;const auto cmd=quoteArgument(L"C:\\YMCC\\YeManCC.exe")+L" "+arguments(request);int n=0;auto args=CommandLineToArgvW(cmd.c_str(),&n);bool ok=args&&n==10&&std::wstring(args[6])==request.powerControl&&std::wstring(args[9])==request.executable;if(args)LocalFree(args);check("same-image elevation arguments roundtrip Windows quoting",ok);}
 bool ok=true;for(const auto&c:checks)ok=ok&&c["ok"].get<bool>();std::ofstream(argv[2])<<plans.dump(2)<<'\n';std::ofstream(argv[3])<<Json({{"ok",ok},{"checks",checks},{"scope","Production native INI and recovery state machine with inert operations; no hardware start/stop or CPU timing."}}).dump(2)<<'\n';return ok?0:1;
}
