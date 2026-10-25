#include "../native/early_start_service.h"
#include "../native/boot_mode_platform.h"
#include <iostream>
#include <stdexcept>
using namespace ymcc::earlystart;
struct MemoryPorts {
 State state{true,true,SERVICE_AUTO_START,SERVICE_RUNNING,0};
 bool enableOk=true,startOk=true,confirmEnable=true,startFinishes=true;
 int reads=0,enables=0,starts=0,sleeps=0;unsigned long long tick=0;
 State read(){++reads;return state;}
 bool enableAutomatic(){++enables;if(!enableOk){state.error=5;return false;}if(confirmEnable)state.startType=SERVICE_AUTO_START;return true;}
 bool start(){++starts;if(!startOk){state.error=5;return false;}state.status=SERVICE_START_PENDING;return true;}
 unsigned long long now()const{return tick;}
 void pause(){++sleeps;tick+=50;if(startFinishes&&state.status==SERVICE_START_PENDING)state.status=SERVICE_RUNNING;}
};
static int passed=0;
void check(bool value,const char* name){if(!value)throw std::runtime_error(name);++passed;std::cout<<"PASS "<<name<<"\n";}
int main(){try{
 {MemoryPorts p;const auto r=ensure(p);check(r.ok&&!r.changed&&!p.enables&&!p.starts,"running automatic service has no writes");}
 {MemoryPorts p;p.state.startType=SERVICE_DISABLED;p.state.status=SERVICE_STOPPED;const auto r=ensure(p);check(r.ok&&r.changed&&p.enables==1&&p.starts==1,"disabled stopped service becomes automatic and running");const auto again=ensure(p);check(again.ok&&p.enables==1&&p.starts==1,"repair is idempotent");}
 {MemoryPorts p;p.state.startType=SERVICE_DEMAND_START;const auto r=ensure(p);check(r.ok&&p.enables==1&&p.starts==0,"manual running service only changes automatic intent");}
 {MemoryPorts p;p.state.known=false;p.state.error=5;const auto r=ensure(p);check(!r.ok&&!p.enables&&!p.starts,"unknown query cannot mutate service");}
 {MemoryPorts p;p.state.installed=false;const auto r=ensure(p);check(!r.ok&&r.reason=="service-missing"&&!p.enables&&!p.starts,"missing service is reported and never installed");}
 {MemoryPorts p;p.state.startType=SERVICE_DISABLED;p.enableOk=false;const auto r=ensure(p);check(!r.ok&&r.reason=="service-enable-failed"&&!p.starts&&r.state.error==5,"access denied cannot start after failed enable");}
 {MemoryPorts p;p.state.startType=SERVICE_DISABLED;p.confirmEnable=false;const auto r=ensure(p);check(!r.ok&&r.reason=="service-enable-unconfirmed"&&!p.starts,"write acknowledgement requires config readback");}
 {MemoryPorts p;p.state.status=SERVICE_STOPPED;p.startOk=false;const auto r=ensure(p);check(!r.ok&&p.starts==1&&r.reason=="service-start-failed","failed start is reported without retries");}
 {MemoryPorts p;p.state.status=SERVICE_START_PENDING;const auto r=ensure(p);check(r.ok&&p.starts==0&&p.sleeps==1,"existing pending start is observed not restarted");}
 {MemoryPorts p;p.state.status=SERVICE_STOP_PENDING;const auto r=ensure(p);check(!r.ok&&!p.starts&&!p.sleeps,"stop pending is left alone");}
 {MemoryPorts p;p.state.status=SERVICE_START_PENDING;p.startFinishes=false;const auto r=ensure(p);check(!r.ok&&r.reason=="service-start-pending"&&p.tick<=2500&&p.reads<=53,"pending observation is strictly bounded");}
 {const auto state=ymcc::bootmode::platformObservation(true,true,true,false);check(state.known&&state.supported&&!state.active&&state.activeKnown,"supported desktop remains available");}
 {const auto state=ymcc::bootmode::platformObservation(true,true,false,true);check(state.known&&state.supported&&state.active,"active FSE is not masked by support hint");}
 {const auto state=ymcc::bootmode::platformObservation(false,true,false,true);check(state.known&&state.supported&&state.active,"active export can prove availability independently");}
 {const auto state=ymcc::bootmode::platformObservation(true,false,true,false);check(state.known&&state.supported&&!state.activeKnown&&!ymcc::bootmode::mayRequest(state,false,true,true),"missing active export cannot authorize a synthetic mode change");}
 {const auto state=ymcc::bootmode::platformObservation(false,false,false,false);check(!state.known&&!state.supported&&!state.active,"missing APIs remain unknown not fake supported");}
 std::cout<<"{\"pass\":true,\"passed\":"<<passed<<"}\n";return 0;
 }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}}
