// Actual GPU evidence collector with allocation/initialization counters only.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <limits>
#include <cstdio>
#include "../deps/json/json.hpp"
using json=nlohmann::json;
static bool tracking=false,failNext=false;static std::size_t allocations=0,defaultEvidenceFields=0;
void* operator new(std::size_t n){if(failNext){failNext=false;throw std::bad_alloc();}if(tracking)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}
#include YMCC_GPU_HEADER
using namespace ymcc;
static monitorgpu::Readings make(unsigned count,unsigned offset=0){monitorgpu::Readings r;const auto identity=[](std::uint32_t index){monitorgpu::Identity key;key.id=100+index;key.instance=index%2;key.anonymousIndex=index;std::snprintf(key.name.data(),key.name.size(),index%2?"dGPU [#0]: NVIDIA fixture %u":"iGPU [#1]: AMD Radeon fixture %u",index);return key;};for(unsigned i=0;i<count;++i){const auto index=offset+i;for(const auto&row:std::array<std::pair<const char*,double>,3>{{{"GPU Power",5.0+i},{"GPU Memory Allocated",1000.0+i},{"GPU Memory Available",7000.0-i}}}){const auto unit=row.first==std::string_view("GPU Power")?"W":"MB";r.add(index,MonitorFoldedText(std::string_view(row.first)),MonitorFoldedText(std::string_view(unit)),row.second,true,identity);}}return r;}
static void observe(monitorgpu::Readings&r,unsigned step,bool invalid=false){r.retainMemoryEvidence([&](std::uint32_t index){if(invalid)return monitormemory::D3DMemory{-1,-1};return monitormemory::D3DMemory{double(step)+index,double(step*2)+index};});}
static json values(const monitorgpu::Readings&r){json all=json::array();for(std::size_t i=0;i<r.size();++i){const auto&d=r.at(i);bool captured=false;const auto raw=r.diagnosticD3D(&d,&captured);const auto memory=r.memoryFor(&d);all.push_back({{"index",d.sensorIndex},{"id",d.key.id},{"name",d.key.name.data()},{"power",d.power},{"captured",captured},{"dedicated",raw.dedicated},{"dynamic",raw.dynamic},{"source",memory.source},{"used",memory.value.used},{"available",memory.value.available},{"total",memory.value.total},{"load",memory.value.load}});}return all;}
int main(int argc,char**argv){if(argc!=2)return 2;json checks=json::array(),work=json::array(),trace=json::array();bool ok=true;const auto check=[&](const char*n,bool v){checks.push_back({{"name",n},{"ok",v}});ok=ok&&v;if(!v)std::cerr<<n<<'\n';};
 for(unsigned count:{0u,1u,2u,64u}){
  auto r=make(count);allocations=defaultEvidenceFields=0;tracking=true;observe(r,1000+count);tracking=false;work.push_back({{"count",count},{"operation","first"},{"allocations",allocations},{"defaultEvidenceFields",defaultEvidenceFields}});check("evidence availability follows admitted GPU count",r.hasMemoryEvidence()==(count>0));trace.push_back(values(r));
  allocations=defaultEvidenceFields=0;tracking=true;for(unsigned i=0;i<300;++i)observe(r,1000+count);tracking=false;work.push_back({{"count",count},{"operation","300-equal"},{"allocations",allocations},{"defaultEvidenceFields",defaultEvidenceFields}});check("unchanged active values allocate nothing",allocations==0);trace.push_back(values(r));
  allocations=defaultEvidenceFields=0;tracking=true;observe(r,2000+count);tracking=false;work.push_back({{"count",count},{"operation","changed"},{"allocations",allocations},{"defaultEvidenceFields",defaultEvidenceFields}});check("changed values keep one bounded immutable allocation",allocations==(count?1u:0u));trace.push_back(values(r));
  monitorgpu::Readings copy=r;check("copy retains immutable evidence by current slot",values(copy)==values(r));auto moved=std::move(copy);check("move retains immutable evidence by current slot",values(moved)==values(r));
  if(count){auto old=values(r);failNext=true;observe(r,3000+count);check("allocation failure withdraws evidence without dropping GPU readings",r.size()==count&&!r.hasMemoryEvidence()&&!failNext);observe(r,4000+count);check("new valid evidence recovers immediately",r.hasMemoryEvidence());trace.push_back(values(r));}
 }
 monitorgpu::Readings destination;for(unsigned i=0;i<1000;++i){const unsigned count=(i*37)%65;auto source=make(count,1000+(i%3)*100);observe(source,10000+i,i%11==0);const auto expected=values(source);destination=source;check("grow/shrink assignment preserves exactly the live GPU evidence",values(destination)==expected);source=monitorgpu::Readings{};check("source reset cannot change another packet's immutable evidence",values(destination)==expected);trace.push_back(expected);}
 {auto r=make(2);observe(r,29);auto retained=r;const auto before=values(retained);unsigned visited=0;r.retainMemoryEvidence([&](std::uint32_t)->monitormemory::D3DMemory{if(++visited==2)throw std::runtime_error("fixture callback failure");return {1,2};});check("partial evidence callback failure withdraws only this packet evidence",visited==2&&r.size()==2&&!r.hasMemoryEvidence());check("partial callback failure cannot mutate a retained immutable packet",values(retained)==before);observe(r,31);check("evidence recovers after a partial callback failure",r.hasMemoryEvidence());}
 {auto r=make(2);observe(r,17);monitorgpu::Device foreign;bool captured=true;const auto missing=r.diagnosticD3D(&foreign,&captured);check("unowned device cannot access another GPU's evidence",!captured&&missing.dedicated==-1&&missing.dynamic==-1);}
 std::ofstream(argv[1])<<json({{"ok",ok},{"checks",checks},{"work",work},{"trace",trace},{"readingsBytes",sizeof(monitorgpu::Readings)},{"realHardwareRead",false},{"cpuTimingOrPercentageMeasured",false}}).dump(2)<<'\n';return ok?0:1;
}
