// Actual FxSound consumers, using the shared deterministic inventory OS ports.
#include "tools/fixtures/process_inventory_ports.h"
#include "consumer-slices.inc"

namespace checks {
using P=ymcc::fxsound::ProcessState;
inline unsigned assertions=0;
void require(bool value,const char* message){++assertions;if(!value)throw std::runtime_error(message);}
void returned(){require(osfacts::s.handles.empty(),"owned fake HANDLE not returned");require(osfacts::s.opens+osfacts::s.snapshots==osfacts::s.closes+(osfacts::s.snapshotFail?osfacts::s.snapshots:0),"HANDLE accounting differs");require(osfacts::s.violations==0,"fake API contract violated");}
void reset(std::vector<osfacts::Row> rows={{900,L"unrelated.exe"}}){returned();osfacts::s=osfacts::State{};osfacts::s.rows=std::move(rows);ymcc::processInventory::invalidate();}
void after(){
    reset();require(fx_after::processState(250)==P::stopped,"initial passive stopped");
    osfacts::s.tick+=249;require(fx_after::processState(250)==P::stopped&&osfacts::s.queryCalls==1,"passive250 coalesces before age250");
    osfacts::s.rows={{100}};++osfacts::s.tick;require(fx_after::processState(250)==P::running&&osfacts::s.queryCalls==2,"age250 boundary refreshes");
    reset();fx_after::processState(250);osfacts::s.rows={{100}};require(fx_after::processState()==P::running&&osfacts::s.queryCalls==2,"fresh0 after cached stopped sees launch");
    reset();fx_after::processState(250);osfacts::s.mode=osfacts::QueryMode::failed;require(fx_after::processState()==P::unknown&&osfacts::s.queryCalls==2,"fresh0 failure never borrows stopped");
    reset({{100}});require(fx_after::processState(250)==P::running,"old success running");osfacts::s.mode=osfacts::QueryMode::failed;require(fx_after::processState()==P::unknown,"fresh failure never borrows running");
    auto receipt=ymcc::processInventory::read(ymcc::processInventory::resolveQuery(),ymcc::processInventory::currentTime,250);require(receipt->status==ymcc::processInventory::Status::Failed&&receipt->rows.empty(),"failure owner receipt has no old rows");
    for(auto mode:{osfacts::QueryMode::malformed,osfacts::QueryMode::allocationFailure}){reset({{100}});fx_after::processState(250);osfacts::s.mode=mode;require(fx_after::processState()==P::unknown,"malformed/allocation owner failure unknown");}
    reset({{100}});fx_after::processState(250);osfacts::s.mode=osfacts::QueryMode::unsupported;osfacts::s.rows={{900,L"unrelated.exe"},{101}};osfacts::s.partial=true;
    require(fx_after::processState()==P::unknown,"partial Toolhelp unknown without old success");returned();
    reset();osfacts::s.mode=osfacts::QueryMode::unsupported;osfacts::s.snapshotFail=true;require(fx_after::processState()==P::unknown,"full Toolhelp failure unknown");returned();
    reset({{100}});osfacts::s.mode=osfacts::QueryMode::unsupported;require(fx_after::processState()==P::running&&osfacts::s.snapshots==1,"complete Toolhelp supported fallback");returned();
    reset({{100}});osfacts::s.rows[0].session=8;require(fx_after::processState()==P::stopped,"foreign session not running");require(!fx_after::identity().pid,"foreign identity rejected");
    reset({{100}});osfacts::s.rows[0].sessionFail=true;require(fx_after::processState()==P::unknown,"target session lookup unknown");require(!fx_after::identity().pid,"target session identity rejected");
    reset({{100}});osfacts::s.ownSessionFail=true;require(fx_after::processState()==P::unknown&&!fx_after::identity().pid&&osfacts::s.queryCalls==0,"own session failure gates owner reads");
    reset({{100},{101}});osfacts::s.rows[0].path=L"Q:\\Wrong\\FxSound.exe";osfacts::s.rows[1].birth=0x100000123ULL;
    auto selected=fx_after::identity();require(selected.pid==101&&selected.birth==0x100000123ULL,"same name wrong path skipped; exact path birth selected");returned();
    reset({{100},{101}});osfacts::s.rows[0].birth=11;osfacts::s.rows[1].birth=22;selected=fx_after::identity();require(selected.pid==100&&selected.birth==11,"multiple exact: first table row wins");
    require(!fx_after::current({101,22}),"current(second exact) false");require(fx_after::current({100,11}),"current(first exact) true");
    osfacts::s.rows[0].birth=12;require(!fx_after::current({100,11}),"same pid new birth rejects old identity");require(fx_after::current({100,12}),"same pid new birth accepts current identity");
    reset();fx_after::processState(250);osfacts::s.rows={{100}};require(fx_after::identity().pid==100&&osfacts::s.queryCalls==2,"identity is fresh0 after passive stopped");
    osfacts::s.mode=osfacts::QueryMode::failed;require(!fx_after::identity().pid&&!fx_after::current({100,100}),"identity/current failure never borrow old success");
    for(int guard=0;guard!=7;++guard){reset({{100}});auto& row=osfacts::s.rows[0];if(guard==0)row.heldPid=777;if(guard==1)row.heldSession=8;if(guard==2)row.heldSessionFail=true;if(guard==3)row.alive=false;if(guard==4)row.pathFail=true;if(guard==5)row.birthFail=true;if(guard==6)row.openFail=true;require(!fx_after::identity().pid,"held pid/session/liveness/path/birth/open guard rejects");returned();}
    reset({{100}});osfacts::s.mode=osfacts::QueryMode::unsupported;osfacts::s.rows={{100},{101}};osfacts::s.partial=true;require(!fx_after::identity().pid,"partial Toolhelp identity rejects even first matching row");returned();
    std::cout<<"PASS after actual owner + verbatim processState/identity/current assertions="<<assertions<<"\n";
}
void before(){reset();require(fx_before::processState()==P::stopped,"before stopped first");require(fx_before::processState()==P::stopped,"before stopped second");returned();std::cout<<"BEFORE_DUPLICATE_ENUMERATION snapshots="<<osfacts::s.snapshots<<" expected=1\n";require(osfacts::s.snapshots==1,"BEFORE_DUPLICATE_ENUMERATION: original repeated Toolhelp fails passive coalescing");}
} // namespace checks
int main(int argc,char** argv){try{if(argc!=2)throw std::runtime_error("select --before or --after");if(!std::strcmp(argv[1],"--before"))checks::before();else if(!std::strcmp(argv[1],"--after"))checks::after();else throw std::runtime_error("unknown selection");return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}