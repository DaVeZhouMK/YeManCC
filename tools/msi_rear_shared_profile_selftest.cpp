#include "../native/msi_rear_shared_profile.h"
#include "../native/msi_rear_protocol.h"
#include <iostream>
#include <cstdlib>
using namespace ymcc::msi::rearProfile;
static unsigned checks=0;static void check(bool ok){++checks;if(!ok){std::cerr<<"FAIL "<<checks<<'\n';std::exit(1);}}
static void modelWrite(std::array<uint8_t,1024>& profile,const std::array<uint8_t,64>& p){
 check(p[0]==0x0f&&p[3]==0x3c&&p[4]==0x21&&p[5]==1);const auto offset=(size_t(p[6])<<8)|p[7];
 check(p[8]<=55&&offset+p[8]<=profile.size());for(unsigned i=0;i<p[8];++i)profile[offset+i]=p[9+i];
}
int main(){
 check(m1Index==0xba&&m1Flag==0xbb&&m2Index==0x163&&m2Flag==0x164);
 for(const uint16_t fw:std::array<uint16_t,6>{0x166,0x167,0x217,0x219,0x308,0x411}){
  check(modernLayoutCandidate(fw));std::array<uint8_t,1024> profile{};profile[m1Index]=17;profile[m2Index]=18;
  auto x=ymcc::msi::rearProtocol::reports(fw,ymcc::msi::rearProtocol::Route::XInputKeyboard);modelWrite(profile,x.m1);modelWrite(profile,x.m2);
  const auto a=basicKey(profile.data()+m1Index),b=basicKey(profile.data()+m2Index);
  check(a.index==17&&b.index==18&&matchesKeyboardCandidateFields(a,true)&&matchesKeyboardCandidateFields(b,false));
  check(mapOrMacroEnabled(a)&&mapOrMacroEnabled(b));
  auto d=ymcc::msi::rearProtocol::reports(fw,ymcc::msi::rearProtocol::Route::DInputButtons);modelWrite(profile,d.m1);modelWrite(profile,d.m2);
  check(!mapOrMacroEnabled(basicKey(profile.data()+m1Index))&&!mapOrMacroEnabled(basicKey(profile.data()+m2Index)));
  check(profile[m1Index]==1&&profile[m2Index]==1); // Don't blindly restore these over a user's original index.
 }
 check(!modernLayoutCandidate(0x163)&&!modernLayoutCandidate(0x211)&&!modernLayoutCandidate(0x419));
 for(const bool m1:{false,true}){
  auto q=readCurrentBasicKey(m1);check(q[4]==4&&q[5]==0&&q[8]==8);auto ack=q;ack[4]=5;ack[5]=1;ack[9]=m1?17:18;ack[10]=4;ack[11]=0;ack[12]=m1?0x7a:0x7d;for(unsigned i=13;i<17;++i)ack[i]=0xff;
  auto r=decodeBasicKeyAck(ack.data(),ack.size(),m1);check(r.valid&&r.profileIndex==1&&matchesKeyboardCandidateFields(r.key,m1));
  for(unsigned i=0;i<9;++i){auto bad=ack;bad[i]^=0x80;if(i==5){check(decodeBasicKeyAck(bad.data(),64,m1).valid);continue;}check(!decodeBasicKeyAck(bad.data(),64,m1).valid);}
  check(!decodeBasicKeyAck(ack.data(),63,m1).valid&&!decodeBasicKeyAck(nullptr,64,m1).valid&&!decodeBasicKeyAck(ack.data(),64,!m1).valid);
 }
 std::cout<<"MSI_SHARED_PROFILE_STATIC_PROOF_OK checks="<<checks<<" actual_device_io=none ROM_writes=none simulation_only=true\n";
}
