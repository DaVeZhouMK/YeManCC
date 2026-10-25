#define NOMINMAX
#include <windows.h>
#include <array>
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "keyboard_owned_pulse.h"
#include "json.hpp"
using json=nlohmann::json;
namespace kp=ymcc::keyboardPulse;
namespace ymcc::keyboardPulse::detail {struct TestAccess {struct Held{Owner& o;~Held(){o.busy_.clear(std::memory_order_release);}};static Held hold(Owner& o){if(o.busy_.test_and_set(std::memory_order_acquire))std::terminate();return {o};}};}
static unsigned checks=0;static json cases=json::array();
static void check(bool b,const char* text){++checks;if(!b)throw std::runtime_error(text);}
struct Model {
 std::array<bool,256> held{};std::vector<UINT> returns;std::size_t call=0;std::vector<std::vector<INPUT>> batches;
 struct Observation {UINT requested,inserted;bool repair;std::uint32_t source;};std::vector<Observation> observations;
 kp::SendReceipt send(const INPUT* p,UINT n)noexcept{
  if(call>=returns.size())std::terminate();const auto inserted=returns[call++];batches.emplace_back(p,p+n);
  for(UINT i=0;i<(std::min)(inserted,n);++i){if(p[i].type!=INPUT_KEYBOARD||p[i].ki.wVk>=256)std::terminate();held[p[i].ki.wVk]=(p[i].ki.dwFlags&KEYEVENTF_KEYUP)==0;}
  return {inserted,static_cast<DWORD>(500+call)};
 }
 void observe(const INPUT*,UINT n,kp::SendReceipt r,bool repair,std::uint32_t source)noexcept{observations.push_back({n,r.inserted,repair,source});}
};
static std::vector<INPUT> pulse(WORD key,bool shift){std::vector<INPUT> v;auto add=[&](WORD vk,bool up){INPUT i{};i.type=INPUT_KEYBOARD;i.ki.wVk=vk;i.ki.dwFlags=up?KEYEVENTF_KEYUP:0;v.push_back(i);};if(shift)add(VK_LSHIFT,false);add(key,false);add(key,true);if(shift)add(VK_LSHIFT,true);return v;}
static auto sender(Model& m){return [&m](const INPUT* p,UINT n)noexcept{return m.send(p,n);};}
static auto observer(Model& m){return [&m](const INPUT* p,UINT n,kp::SendReceipt r,bool repair,std::uint32_t source)noexcept{m.observe(p,n,r,repair,source);};}
static void verifyDebt(kp::Owner& owner,const Model& model){auto s=owner.snapshot();check(s.known,"owner state unavailable in serial test");std::array<bool,256> owned{};for(std::size_t i=0;i<s.releaseCount;++i){check((s.releases[i].ki.dwFlags&KEYEVENTF_KEYUP)!=0,"debt includes DOWN");owned[s.releases[i].ki.wVk]=true;}check(owned==model.held,"owned release set differs from modeled inserted-prefix state");check(s.pending==(s.releaseCount!=0)&&owner.pending()==s.pending,"pending publication wrong");}
static void exhaustive(){unsigned histories=0;
 for(bool shift:{false,true})for(WORD key:{WORD('A'),WORD(VK_LCONTROL),WORD(VK_LMENU),WORD(VK_HOME)}){
  auto inputs=pulse(key,shift);const auto n=static_cast<UINT>(inputs.size());
  for(UINT first=0;first<=n;++first)for(UINT repair=0;repair<=2;++repair){
   kp::Owner owner;Model m;m.returns={first,repair,0,2,2,2};auto r=owner.submit(inputs.data(),n,1000,sender(m),observer(m));++histories;
   check(r.originalIssued&&r.inserted==first&&r.originalError==501,"first request receipt overwritten by repair");check(m.batches.front().size()==n&&!std::memcmp(m.batches.front().data(),inputs.data(),n*sizeof(INPUT)),"first batch changed");
   check(r.apiCalls<=2&&m.observations.size()==m.batches.size(),"submission exceeded bounded repair or lost observation");
   if(first==n)check(r.status==kp::Status::Sent&&m.call==1&&!owner.pending(),"full batch behavior changed");
   else check(r.status==kp::Status::ShortSend,"partial request reclassified as success");
   verifyDebt(owner,m);
   for(unsigned step=0;step<3&&owner.pending();++step){const auto before=m.call;auto cleanup=owner.maintain(10000+step*10000,false,sender(m),observer(m));check(cleanup.apiCalls<=1&&m.call<=before+1,"maintenance emitted multiple batches");verifyDebt(owner,m);}
   check(!owner.pending(),"eventual accepted repair did not clear debt");
   for(std::size_t b=1;b<m.batches.size();++b)for(const auto& i:m.batches[b])check((i.ki.dwFlags&KEYEVENTF_KEYUP)!=0,"compensation sent new DOWN");
  }
 }
 cases.push_back({{"case","partial-counts-and-eventual-owned-release"},{"histories",histories},{"pass",true}});
}
static void delayedAndBlocked(){
 kp::Owner owner;Model m;auto ctrl=pulse(VK_LCONTROL,false),alt=pulse(VK_LMENU,false);m.returns={1,0,0,1,2};
 auto first=owner.submit(ctrl.data(),2,100,sender(m),observer(m),11);check(first.pendingReleases==1&&m.held[VK_LCONTROL],"partial Ctrl setup failed");auto s=owner.snapshot();check(s.retryAt==350,"initial backoff wrong");
 auto noRetry=owner.maintain(349,false,sender(m),observer(m));check(noRetry.apiCalls==0&&m.call==2,"maintenance ignored backoff");
 auto blocked=owner.submit(alt.data(),2,350,sender(m),observer(m));check(blocked.status==kp::Status::DebtBlocked&&!blocked.originalIssued&&m.call==3&&!m.held[VK_LMENU],"unreleased Ctrl leaked into a new Alt pulse");
 check(owner.snapshot().retryAt==850,"continued failure backoff wrong");
 auto accepted=owner.submit(alt.data(),2,351,sender(m),observer(m),22);check(accepted.status==kp::Status::Sent&&accepted.originalIssued&&accepted.apiCalls==2&&m.call==5,"explicit retry did not repair old debt before new pulse");verifyDebt(owner,m);
 check(m.observations[3].source==11&&m.observations[4].source==22,"old debt source mislabeled as new producer");
 check(m.observations[1].repair&&m.observations[2].repair&&m.observations[3].repair&&!m.observations[4].repair,"repair source marking wrong");
 cases.push_back({{"case","persistent-debt-backoff-and-admission"},{"pass",true}});
}
static void noGlobalRelease(){kp::Owner owner;Model m;m.held[VK_LCONTROL]=true;m.held[VK_LMENU]=true;m.returns={};
 auto r=owner.maintain(100,true,sender(m),observer(m));check(r.apiCalls==0&&m.call==0&&m.held[VK_LCONTROL]&&m.held[VK_LMENU],"unowned external modifiers were cleared");
 auto inputs=pulse('A',true);m.held={};m.returns={2,0,1,1};auto first=owner.submit(inputs.data(),4,100,sender(m),observer(m));check(first.pendingReleases==2,"shift+key debt setup");check(m.batches[1][0].ki.wVk=='A'&&m.batches[1][1].ki.wVk==VK_LSHIFT,"repair order differs from intended release order");
 auto partial=owner.maintain(101,true,sender(m),observer(m));check(partial.pendingReleases==1&&!m.held['A']&&m.held[VK_LSHIFT],"partial release dropped wrong ownership");verifyDebt(owner,m);
 owner.maintain(102,true,sender(m),observer(m));verifyDebt(owner,m);cases.push_back({{"case","source-only-release-and-reverse-up-order"},{"pass",true}});}
static void synchronousReentry(){
 kp::Owner owner;Model m;auto inputs=pulse(VK_LCONTROL,false);m.returns={2};
 bool callbackRan=false;auto reentrantObserver=[&](const INPUT* data,UINT count,kp::SendReceipt r,bool repair,std::uint32_t source)noexcept{
  m.observe(data,count,r,repair,source);callbackRan=true;
  auto nested=owner.submit(inputs.data(),2,101,sender(m),observer(m));
  if(nested.status!=kp::Status::Busy||nested.originalIssued||owner.snapshot().known)std::terminate();
 };
 auto r=owner.submit(inputs.data(),2,100,sender(m),reentrantObserver);
 check(callbackRan&&r.status==kp::Status::Sent&&m.call==1,"synchronous callback reentry changed outer pulse or issued nested input");
 verifyDebt(owner,m);cases.push_back({{"case","synchronous-observer-reentry-defined-busy"},{"pass",true}});
}
static void invalidAndBusy(){
 Model m;kp::Owner owner;auto good=pulse('A',false);auto bad=good;
 for(unsigned variant=0;variant<7;++variant){bad=good;switch(variant){case 0:bad[0].type=INPUT_MOUSE;break;case 1:bad[0].ki.dwFlags=KEYEVENTF_SCANCODE;break;case 2:bad[0].ki.dwFlags=KEYEVENTF_UNICODE;break;case 3:bad[0].ki.wVk=0;break;case 4:bad[1].ki.wVk='B';break;case 5:bad[0].ki.dwFlags=KEYEVENTF_KEYUP;break;default:bad[1].ki.dwFlags=0;break;}
  check(owner.submit(bad.data(),2,100,sender(m),observer(m)).status==kp::Status::InvalidBatch,"invalid pulse admitted");}
 check(owner.submit(nullptr,2,100,sender(m),observer(m)).status==kp::Status::InvalidBatch&&m.call==0,"null/invalid batch caused I/O");
 {auto lock=kp::detail::TestAccess::hold(owner);auto r=owner.submit(good.data(),2,100,sender(m),observer(m));check(r.status==kp::Status::Busy&&m.call==0,"busy owner issued or blocked");check(!owner.snapshot().known,"snapshot fabricated coherent state while locked");}
 m.returns={1,0,1};owner.submit(good.data(),2,100,sender(m),observer(m));{auto lock=kp::detail::TestAccess::hold(owner);auto r=owner.maintain(1000,true,sender(m),observer(m));check(r.status==kp::Status::Busy&&m.call==2&&owner.pending(),"busy maintenance lost debt");}
 owner.maintain(1001,true,sender(m),observer(m));verifyDebt(owner,m);cases.push_back({{"case","invalid-and-trylock-busy"},{"pass",true}});
}
int main(){try{exhaustive();delayedAndBlocked();noGlobalRelease();invalidAndBusy();synchronousReentry();std::cout<<json({{"pass",true},{"checks",checks},{"cases",cases},{"actualOSInput",false},{"bookkeeping","count-based serial prefix, not consumer receipt"}}).dump(2)<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
