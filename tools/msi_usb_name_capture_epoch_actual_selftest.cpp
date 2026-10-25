// Linked only to an extracted NameProvenance class with inert API boundaries.
#include <fstream>
#include <iostream>
#include "NameProvenance.actual.generated.h"
using nlohmann::json;
namespace names=ymcc::msi::usbName;
int main(int argc,char**argv){
 unsigned checks=0,failed=0;json failures=json::array(),scenarios=json::array();
 auto check=[&](bool ok,const char*label){++checks;if(!ok){++failed;failures.push_back(label);}};
 auto source=[](){names::Context c;c.nativePid=17;c.parentCreationTime=1234;c.boardAuthorized=1;c.sourceValid=1;
  c.sourceGeneration=2;c.powerGeneration=3;c.hostEpoch=4;c.observedTick=100;
  names::text(c.board,"Claw 8 AI+ A2VM");names::text(c.usbRoot,"USB\\VID_0DB0&PID_1902\\actual-fixture");
  names::text(c.interfacePath,"\\\\?\\HID#VID_0DB0&PID_1902&MI_00&COL01#actual-fixture");return c;};
 auto run=[&](const char* label,const std::function<void(names::NameProvenance&,std::vector<json>&)>&body){
  names::fixture={};names::fixture.current=source();names::NameProvenance sampler;std::vector<json> rows;
  const bool opened=sampler.open(names::contextMappingName(17,1234),17,1234,reinterpret_cast<HANDLE>(uintptr_t(1)));
  check(opened,"fake mapping opened with exact bound name");body(sampler,rows);
  check(names::fixture.queries==0,"root-unavailable fixture never reaches USB query");
  scenarios.push_back({{"name",label},{"rows",rows},{"rootResolves",names::fixture.rootResolves},{"queries",names::fixture.queries}});
 };
 auto observe=[](names::NameProvenance& p,std::vector<json>& rows){p.observe([&](json r){rows.push_back(std::move(r));});};
 run("same complete tuple is sampled once",[&](auto&p,auto&rows){observe(p,rows);observe(p,rows);check(rows.size()==1,"same source/power/host tuple deduplicates");check(names::fixture.rootResolves==2,"same tuple adds no root reads after first attempt");});
 run("host epoch changes on unchanged source",[&](auto&p,auto&rows){observe(p,rows);++names::fixture.current.hostEpoch;observe(p,rows);check(rows.size()==2,"new Host epoch must retain a separate capture attempt");check(names::fixture.rootResolves==4,"new Host epoch enters actual capture body");});
 run("host changes during failed prior capture",[&](auto&p,auto&rows){names::fixture.changeEpochOnResolve=true;observe(p,rows);check(rows.size()==1&&!rows[0].value("nativeGenerationStable",true),"first capture rejects changed Host epoch");names::fixture.changeEpochOnResolve=false;observe(p,rows);check(rows.size()==2,"changed current Host epoch must not be suppressed by failed old capture");});
 run("pre-Host epoch zero to live Host",[&](auto&p,auto&rows){names::fixture.current.hostEpoch=0;observe(p,rows);names::fixture.current.hostEpoch=1;observe(p,rows);check(rows.size()==2,"first live Host is distinct from pre-Host capture");});
 run("source and power changes keep existing captures",[&](auto&p,auto&rows){observe(p,rows);++names::fixture.current.sourceGeneration;observe(p,rows);++names::fixture.current.powerGeneration;observe(p,rows);check(rows.size()==3,"source/power changes still sampled");});
 run("heartbeat and phase changes stay same tuple",[&](auto&p,auto&rows){observe(p,rows);names::fixture.current.observedTick=200;names::fixture.now=200;names::text(names::fixture.current.phase,"current-source");observe(p,rows);check(rows.size()==1,"heartbeat and phase do not grant another sample");});
 run("fixed eight-sample observer budget",[&](auto&p,auto&rows){for(unsigned i=0;i<12;++i){names::fixture.current.hostEpoch=i;observe(p,rows);}check(rows.size()==9,"eight attempts plus one explicit budget receipt");check(names::fixture.rootResolves==16,"budget caps root resolution attempts");unsigned samples=0,budgets=0;for(const auto&r:rows){samples+=r.contains("sample")?1u:0u;budgets+=r.value("state",std::string{})=="sample-budget-exhausted"?1u:0u;}check(samples==8&&budgets==1,"budget receipt emitted once and no more samples");});
 run("revoked or stale source never samples",[&](auto&p,auto&rows){names::fixture.current.sourceValid=0;observe(p,rows);observe(p,rows);check(rows.size()==1&&names::fixture.rootResolves==0,"invalid source has no root reads");names::fixture.current.sourceValid=1;names::fixture.now=2201;observe(p,rows);check(names::fixture.rootResolves==0,"stale source has no root reads");});
 run("parent exit never samples",[&](auto&p,auto&rows){names::fixture.parentLive=false;observe(p,rows);check(rows.size()==1&&names::fixture.rootResolves==0,"parent exit preserves no-read gate");});
 json report={{"passed",failed==0},{"checks",checks},{"failed",failed},{"failures",failures},{"scenarios",scenarios},
  {"actualClassExtracted",true},{"deviceApiCalls",0},{"mappingApiCalls",0},{"processApiCalls",0},
  {"scope","actual NameProvenance sample admission and cap with fake context/clock/parent/USB root-unavailable boundaries; no live descriptor collection proof"}};
 if(argc==2){std::ofstream out(argv[1]);out<<report.dump(2)<<'\n';if(!out)return 3;}
 std::cout<<"ACTUAL_NAME_CAPTURE_EPOCH checks="<<checks<<" failed="<<failed<<'\n';return failed?1:0;
}
