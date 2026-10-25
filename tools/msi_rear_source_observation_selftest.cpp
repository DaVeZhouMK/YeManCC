#include "../native/msi_rear_source_observation.h"
#include "../native/msi_native_rear_keyboard.h"
#include <iostream>
#include <cstdlib>
using ymcc::msi::rearObservation::Counters;
static unsigned checks=0;
static void check(bool ok,const char* name){++checks;if(!ok){std::cerr<<"FAIL "<<name<<"\n";std::exit(1);}}
int main(){
 using namespace ymcc::msi::nativeRear;
 check(!buildWritesEnabled,"default production build cannot automatically persist unverified native mapping");
 for(unsigned bits=0;bits<1024;++bits){Facts f{bool(bits&1),bool(bits&2),bool(bits&4),bool(bits&8),bool(bits&16),bool(bits&32),bool(bits&64),bool(bits&128),bool(bits&256),bool(bits&512),2};
  check(!writeAdmissionAllowed(f),"all default native write admissions are held");
  check(writeAdmissionAllowed(f,true)==allowed(f),"explicit offline experiment does not relax family or virtual gates");}
 Plan p;Token t;check(p.request(true,1,100,t),"test plan starts");p.complete(t,Result::Prepared,200);check(p.stamp().written,"old episode holds transport receipt");
 check(p.request(true,2,300,t)&&!p.stamp().written,"new power never inherits old written receipt");
 Counters c;c.keyboard(0x7a,1,0,true,false);c.dinput(1,1);check(c.snapshot().sequence==0&&c.snapshot().samples==0,"disabled observation ignores all families");c.enable(true);
 for(unsigned vk=0;vk<256;++vk)c.keyboard(vk,vk,0,true,false);
 auto s=c.snapshot();check(s.sequence==2&&s.keyboard[0]==1&&s.keyboard[4]==1,"only F11/F14 are recorded; arbitrary typing is not retained");
 c.keyboard(0x7a,87,0x80,false,false);c.keyboard(0x7a,87,0x10,true,true);c.keyboard(0x7a,87,0x90,false,true);
 s=c.snapshot();check(s.keyboard[0]==1&&s.keyboard[1]==1&&s.keyboard[2]==1&&s.keyboard[3]==1,"injected and non-injected edges are distinguished before policy rejection");
 c.dinput(0,0xffffffff);check(c.snapshot().samples==0,"unqualified generation not counted");
 const auto sequence=s.sequence;c.dinput(4,0);check(c.snapshot().sequence==sequence+1,"initial neutral source generation observed");
 for(unsigned i=0;i<2000;++i)c.dinput(4,0);check(c.snapshot().sequence==sequence+1,"neutral sampling cannot create periodic dirty logs");
 c.dinput(4,(1u<<14)|(1u<<15)|(1u<<16)|(1u<<31));s=c.snapshot();check(s.rawMask==0x8001c000&&s.buttonDown[14]==1&&s.buttonDown[15]==1&&s.buttonDown[16]==1&&s.buttonDown[31]==1,"raw indexes seen before rear policy with bounded width");
 c.dinput(5,0);s=c.snapshot();check(s.generation==5&&s.buttonDown[15]==1,"lifetime counts retained but latest generation explicit");
 c.enable(false);const auto old=s;c.keyboard(0x7d,88,0,true,false);c.dinput(6,1);s=c.snapshot();check(s.sequence==old.sequence&&s.samples==old.samples,"turning gate off freezes counters without owning input");
 std::cout<<"MSI_REAR_ROLLBACK_OBSERVATION_OK checks="<<checks<<" hardware_io=none firmware_writes=none\n";
}
