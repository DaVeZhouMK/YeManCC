#include "../native/msi_qs_keyboard_mirror.h"
#include <iostream>
#include <cstdlib>
using namespace ymcc::msi::qsMirror;
static unsigned checks=0;
static void check(bool ok){++checks;if(!ok){std::cerr<<"FAIL "<<checks<<'\n';std::exit(1);}}
int main(){
 constexpr uint64_t qs=1ull<<26,claw=1ull<<25,m1=1ull<<20;
 constexpr uint32_t win=1u<<1,g=1u<<2,tab=1u<<3,ctrl=1u<<4;
 for(unsigned flags=0;flags<32;++flags)for(const auto required:{uint64_t(0),qs,claw,m1,qs|m1,qs|claw}){
  const bool known=flags&1,armed=flags&2,noDesktop=flags&4,click=flags&8,wmi=flags&16;
  const auto mirror=ruleMask(known,armed,noDesktop,required,click,qs);
  check(mirror==((known&&armed&&noDesktop&&click&&required==qs)?qs:0));
  for(const auto mask:{uint64_t(0),qs,claw,claw|qs,m1,m1|qs,m1|qs|claw}){
   const auto result=suppressionMask(mask,wmi,mirror,qs|claw);
   check(result==(wmi?(mask&~(qs|claw))|(mask&mirror):mask));
   check((result&~mask)==0&&(result&m1)==(mask&m1));
  }
 }
 for(unsigned flags=0;flags<4;++flags)for(const auto actions:{uint64_t(0),qs,claw,m1,qs|m1})for(const auto raw:{uint64_t(0),qs,claw,m1,qs|m1})for(const auto mirror:{uint64_t(0),qs})for(const auto keys:{uint32_t(0),win,win|g,win|tab,win|g|tab,win|g|ctrl}){
  const bool wmi=flags&1,msi=flags&2,exact=keys==(win|g)||keys==(win|tab);
  check(suppressionCanonical(actions,raw,mirror,wmi,msi,keys,win|g,win|tab)==(actions|((wmi&&msi&&exact)?raw&mirror:0)));
 }
 std::cout<<"MSI_QS_MIRROR_PURE_OK checks="<<checks<<" APIs=none action_dispatch=none writes=none\n";
}
