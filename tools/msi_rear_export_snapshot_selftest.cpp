#include "../native/msi_rear_source_observation_json.h"
#include <iostream>
#include <cstdlib>
using namespace ymcc::msi::rearObservation;
static unsigned checks=0;static void check(bool ok){++checks;if(!ok){std::cerr<<"FAIL "<<checks<<'\n';std::exit(1);}}
int main(){Counters c;c.enable(true);for(unsigned i=0;i<48;++i){c.dinput(1,i&1);c.reserveActivity(c.snapshot().sequence);}
 const auto atCap=c.snapshot();check(atCap.activityReservations==48);c.keyboard(0x7a,0x57,0,true,false);c.keyboard(0x7a,0x57,0x80,false,false);c.keyboard(0x7d,0x59,0x10,true,true);c.dinput(1,1u<<15);
 const auto s=c.snapshot();const auto j=exportSnapshot(s,10000);check(j["activityRowsReserved"]==48&&j["counterTransitionsAfterLastReservation"]==true);check(j["rawKeyboardCountsF11"][0]==1&&j["rawKeyboardCountsF11"][1]==1&&j["rawKeyboardCountsF14"][2]==1);check(j["dinputButtonDownSampleCounts"][15]==1&&j["dinputSamples"]==49);check(j["mappingReadbackVerified"]==false&&j["hardwareWriteIssued"]==false&&j["snapshotAtomicAcrossProducers"]==false);
 const auto encoded=j.dump();check(nlohmann::json::parse(encoded)==j);check(c.snapshot().activityReservations==48);std::cout<<"MSI_REAR_EXPORT_CAP_COVERAGE_OK checks="<<checks<<" hardware_IO=none cap=48 observed_later_events=retained\n";}
