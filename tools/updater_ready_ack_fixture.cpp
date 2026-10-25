#define NOMINMAX
#include "update_install_recovery.h"
#include <iostream>
static nlohmann::json fixtureJournal;
#include "production-ready-ack.h"
static int checks=0;static void check(bool ok,const char* name){if(!ok)throw std::runtime_error(name);++checks;}
int main(){using namespace ymcc::updaterecovery;try{
 wchar_t image[32768]{};GetModuleFileNameW(nullptr,image,32768);FILETIME c{},e{},k{},u{};GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);
 fixtureJournal={{"operationId","owned-install"},{"phase","applying"},{"helperPid",GetCurrentProcessId()},{"helperCreation",std::to_string(filetime(c))},{"helperImage",utf8(image)}};
 check(actualReadyAck("owned-install"),"live authenticated applying helper acknowledges readiness");check(fixturePauses==0,"ready helper does not incur timeout");
 fixtureJournal["phase"]="repair-required";check(!actualReadyAck("owned-install"),"early helper failure keeps parent alive");check(fixturePauses==0,"early failure returns immediately");
 fixtureJournal["phase"]="preparing";fixtureTicks=0;fixturePauses=0;check(!actualReadyAck("owned-install"),"tools still closing cannot acknowledge parent exit");check(fixtureTicks==90000,"preparing wait remains bounded");
 fixtureJournal["phase"]="authorized";fixtureTicks=0;fixturePauses=0;check(!actualReadyAck("owned-install"),"mere authorization does not permit parent exit");check(fixtureTicks==90000&&fixturePauses==900,"startup wait bounded to ninety seconds");
 fixtureJournal["phase"]="applying";fixtureTicks=0;fixturePauses=0;check(!actualReadyAck("other-operation"),"different transaction cannot acknowledge");check(fixtureTicks==90000,"different transaction wait bounded");
 fixtureJournal["helperCreation"]="0";fixtureTicks=0;fixturePauses=0;check(!actualReadyAck("owned-install"),"reused helper pid cannot acknowledge");check(fixtureTicks==90000,"dead helper wait bounded");
 fixtureJournal["phase"]="committed";fixtureTicks=0;fixturePauses=0;check(!actualReadyAck("owned-install"),"old terminal record cannot authorize exit");check(fixturePauses==0,"terminal record returns immediately");
 std::cout<<"{\"status\":\"PASS\",\"checks\":"<<checks<<",\"scope\":\"actual native launch acknowledgement body; fake journal IO and monotonic clock; real helper process identity\"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
