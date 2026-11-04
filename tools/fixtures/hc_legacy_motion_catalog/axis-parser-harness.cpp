
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <new>
#include <limits>
namespace axisAllocation { thread_local bool active=false; thread_local std::size_t calls=0; }
void* operator new(std::size_t n) {if(axisAllocation::active)++axisAllocation::calls;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
namespace axisLegacy {
// AXIS_LEGACY_BLOCKS
}
namespace axisCandidate {
// AXIS_CANDIDATE_BLOCKS
}
using AxisFunction=bool(*)(const std::string&,double,double,double,double&,double&,double&) noexcept;
static std::uint64_t axisBits(double value){std::uint64_t bits;std::memcpy(&bits,&value,sizeof(bits));return bits;}
static std::array<double,3> axisInvoke(AxisFunction fn,const std::string& id,const std::array<double,3>& in,int alias,bool& accepted){
 std::array<double,3> out{91,92,93};
 accepted=alias==0?fn(id,in[0],in[1],in[2],out[0],out[1],out[2]):alias==1?fn(id,in[0],in[1],in[2],out[0],out[0],out[2]):fn(id,in[0],in[1],in[2],out[0],out[0],out[0]);
 return out;
}
int main(int argc,char** argv){
 json report={{"status","RUNNING"},{"scope","original-axis-parser-only"},{"deviceIo",false},{"hostExecuted",false},{"physicalCpuMeasured",false}};
 try{
  if(argc!=2)throw std::runtime_error("AXIS_INPUT_ARGUMENTS");std::ifstream stream(argv[1]);json inputs;stream>>inputs;
  const double inf=std::numeric_limits<double>::infinity(),nan=std::numeric_limits<double>::quiet_NaN(),tiny=std::numeric_limits<double>::denorm_min();
  const std::array<std::array<double,3>,5> values{{{1,2,3},{-1.5,0,4.5},{0.0,-0.0,tiny},{inf,-inf,nan},{nan,0.0,-0.0}}};
  std::uint64_t checks=0,pairs=0,acceptedPairs=0;report["phase"]="differential";
  for(const auto& value:inputs.at("matrixIds")){
   const auto id=value.get<std::string>();
   for(const auto& v:values)for(int alias=0;alias<3;++alias){bool oldOk=false,newOk=false;
    const auto old=axisInvoke(axisLegacy::ymccApplyNamedImuMatrix,id,v,alias,oldOk),next=axisInvoke(axisCandidate::ymccApplyNamedImuMatrix,id,v,alias,newOk);++pairs;++checks;
    if(oldOk!=newOk)throw std::runtime_error("AXIS_DIFFERENTIAL_ACCEPTANCE");if(oldOk)++acceptedPairs;
    for(int n=0;n<3;++n){++checks;if(axisBits(old[n])!=axisBits(next[n]))throw std::runtime_error("AXIS_DIFFERENTIAL_DOUBLE_BITS");}
   }
  }
  if(acceptedPairs==0)throw std::runtime_error("AXIS_NO_ACTUAL_SUCCESSFUL_TRANSFORM");
  const auto id=inputs.at("allocationMatrixId").get<std::string>();volatile double sink=0;std::size_t counts[2]{};const AxisFunction fns[]={axisLegacy::ymccApplyNamedImuMatrix,axisCandidate::ymccApplyNamedImuMatrix};
  report["phase"]="allocation";
  for(int f=0;f<2;++f){axisAllocation::calls=0;axisAllocation::active=true;bool good=true;
   for(unsigned n=0;n<128;++n){double x=0,y=0,z=0;good=fns[f](id,1,2,3,x,y,z)&&good;sink=sink+x+y+z;}
   axisAllocation::active=false;counts[f]=axisAllocation::calls;if(!good)throw std::runtime_error("AXIS_ALLOCATION_PROBE_INVALID");
  }
  report["differentialPairs"]=pairs;report["checks"]=checks;report["acceptedPairs"]=acceptedPairs;report["matrixIds"]=inputs.at("matrixIds").size();report["callsPerAllocationProbe"]=128;report["legacyAllocations"]=counts[0];report["candidateAllocations"]=counts[1];report["checksum"]=sink;
  if(counts[0]==0)throw std::runtime_error("AXIS_LEGACY_ALLOCATION_CONTROL_MISSING");
  if(counts[1]!=0)throw std::runtime_error("AXIS_REPEATED_RECIPE_MUST_NOT_ALLOCATE");
  report["status"]="PASS";report["phase"]="complete";std::cout<<report.dump()<<'\n';return 0;
 }catch(const std::exception& e){axisAllocation::active=false;report["status"]="FAIL";report["error"]=e.what();std::cout<<report.dump()<<'\n';return 1;}
}
