#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
#include "input_host_transaction_mutex.h"
using json=nlohmann::json;
static unsigned checks=0,groups=0;
static void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>static void group(const char* name,F test){test();++groups;std::cerr<<"PASS "<<name<<'\n';}
// Records the old contract violation without ever calling a real std::mutex
// try_lock from its owning thread. It is a test boundary, not a legal baseline.
class StrictProbeMutex {
 std::mutex gate_;std::atomic<const void*> owner_{nullptr};
 static const void* key(){static thread_local char x;return &x;}
public:
 static inline std::atomic<unsigned> sameThreadProbes{0},tryCalls{0},unlockCalls{0};
 void lock(){gate_.lock();owner_.store(key(),std::memory_order_relaxed);}
 bool try_lock(){++tryCalls;if(owner_.load(std::memory_order_relaxed)==key()){++sameThreadProbes;return false;}if(!gate_.try_lock())return false;owner_.store(key(),std::memory_order_relaxed);return true;}
 void unlock(){owner_.store(nullptr,std::memory_order_relaxed);++unlockCalls;gate_.unlock();}
};
namespace current {
 using InputHostMutex=ymcc::inputHost::TransactionMutex<>;
 static InputHostMutex g_inputHostMx;static std::atomic<unsigned> work{0},logs{0};
 static std::function<void()> duringWork;
 static void appendNativeLifecycleLog(const char*,const json&){++logs;}
 static bool request(const char* command){
#include "candidate-entry.inc"
  if(duringWork)duringWork();++work;return true;
 }
}
static void ownerObservation(){
 using M=ymcc::inputHost::TransactionMutex<StrictProbeMutex>;M lock;const auto before=StrictProbeMutex::tryCalls.load();
 check(!lock.ownedByCurrentThread(),"initial owner absent");
 {std::lock_guard<M> held(lock);check(lock.ownedByCurrentThread(),"owning thread recognized");check(!lock.try_lock(),"same-thread try rejects without recursion");check(StrictProbeMutex::tryCalls==before,"self try never reaches underlying mutex");check(lock.ownedByCurrentThread(),"self rejection retains outer owner");}
 check(!lock.ownedByCurrentThread(),"release clears owner");check(lock.try_lock(),"released mutex can be acquired again");check(lock.ownedByCurrentThread(),"try ownership published");lock.unlock();
}
static void heldEntry(){
 current::work=0;
 {std::lock_guard<current::InputHostMutex> outer(current::g_inputHostMx);check(current::request("SUBMIT_FRAME"),"held request accepted");check(current::g_inputHostMx.ownedByCurrentThread(),"inner request never releases outer owner");
  auto foreign=std::async(std::launch::async,[]{return current::g_inputHostMx.try_lock();});check(!foreign.get(),"foreign remains excluded after inner request");}
 check(current::work==1&&!current::g_inputHostMx.ownedByCurrentThread(),"caller scope releases only at end");
}
static void idleEntry(){
 current::work=0;bool observed=false;current::duringWork=[&]{observed=current::g_inputHostMx.ownedByCurrentThread();};
 check(current::request("HIDHIDE_QUERY"),"idle caller adopts owner");current::duringWork={};check(observed,"adopted owner covers work");check(!current::g_inputHostMx.ownedByCurrentThread(),"adopted guard releases at function exit");check(current::work==1,"exactly one idle request work");
}
static void foreignEntry(){
 current::work=0;std::promise<void> acquired,release;auto ready=acquired.get_future();auto permit=release.get_future();
 std::thread holder([&]{std::lock_guard<current::InputHostMutex> owned(current::g_inputHostMx);acquired.set_value();permit.wait();});
 check(ready.wait_for(std::chrono::seconds(2))==std::future_status::ready,"foreign holder ready");
 const auto started=std::chrono::steady_clock::now();const bool accepted=current::request("HIDHIDE_QUERY");const auto elapsed=std::chrono::steady_clock::now()-started;
 const bool noWork=current::work==0;release.set_value();holder.join();
 check(!accepted&&noWork,"foreign owner refuses before sequence or pipe work");check(elapsed<std::chrono::milliseconds(500),"foreign refusal is nonblocking");check(current::g_inputHostMx.try_lock(),"foreign ownership preserved then releases normally");current::g_inputHostMx.unlock();
}
static void distinctThreadIdentity(){
 ymcc::inputHost::TransactionMutex<> lock;std::promise<void> entered,release;auto ready=entered.get_future();auto permit=release.get_future();bool workerOwned=false;
 std::thread worker([&]{std::lock_guard<decltype(lock)> held(lock);workerOwned=lock.ownedByCurrentThread();entered.set_value();permit.wait();});
 const bool readyOk=ready.wait_for(std::chrono::seconds(2))==std::future_status::ready;const bool mine=lock.ownedByCurrentThread();const bool grabbed=lock.try_lock();release.set_value();worker.join();
 check(readyOk&&workerOwned&&!mine&&!grabbed,"TLS identities differ while both threads alive");
}
static void handoffs(){
 using Mutex=ymcc::inputHost::TransactionMutex<>;Mutex lock;std::atomic<unsigned> active{0},overlap{0},commits{0},badOwner{0};std::vector<std::thread> threads;
 for(unsigned t=0;t<4;++t)threads.emplace_back([&]{for(unsigned i=0;i<200;++i){std::lock_guard<Mutex> held(lock);if(!lock.ownedByCurrentThread())++badOwner;if(active.fetch_add(1)!=0)++overlap;++commits;active.fetch_sub(1);}});
 for(auto& thread:threads)thread.join();check(commits==800&&overlap==0&&badOwner==0,"real mutex handoffs preserve single owner and writer");check(!lock.ownedByCurrentThread(),"no owner retained after handoffs");
}
static void exceptionalScope(){
 using Mutex=ymcc::inputHost::TransactionMutex<>;Mutex lock;try{std::unique_lock<Mutex> held(lock);throw std::runtime_error("fixture only");}catch(const std::runtime_error&){}
 check(!lock.ownedByCurrentThread()&&lock.try_lock(),"exceptional RAII release clears identity and mutex");lock.unlock();
 std::unique_lock<Mutex> deferred(lock,std::defer_lock);check(!deferred.owns_lock(),"deferred guard owns nothing");deferred.lock();check(lock.ownedByCurrentThread(),"deferred lock publishes owner");deferred.unlock();check(!lock.ownedByCurrentThread(),"deferred unlock clears owner");
}
struct NoIdentity {static std::uint64_t get() noexcept{return 0;}};
static void identityExhaustion(){
 using namespace ymcc::inputHost::transactionDetail;
 std::atomic<std::uint64_t> nearEnd{std::numeric_limits<std::uint64_t>::max()-1};
 check(reserveIdentity(nearEnd)==std::numeric_limits<std::uint64_t>::max()-1,"last valid identity issued");
 check(reserveIdentity(nearEnd)==0&&reserveIdentity(nearEnd)==0,"exhausted identity stays denied without wrap");
 std::atomic<std::uint64_t> zero{0};check(reserveIdentity(zero)==0,"zero pool never issues zero identity");
 using M=ymcc::inputHost::TransactionMutex<StrictProbeMutex,NoIdentity>;M mutex;const auto tries=StrictProbeMutex::tryCalls.load();
 check(!mutex.ownedByCurrentThread()&&!mutex.try_lock()&&StrictProbeMutex::tryCalls==tries,"unknown identity never owns or enters base try");
 bool threw=false;try{mutex.lock();}catch(const std::system_error&){threw=true;}check(threw,"unknown identity cannot acquire base lock");
}
static void threadExitNonreuse(){
 std::vector<std::uint64_t> identities;
 for(unsigned i=0;i<32;++i){std::uint64_t observed=0;std::thread worker([&]{observed=ymcc::inputHost::transactionDetail::ThreadIdentity::get();});worker.join();identities.push_back(observed);}
 for(std::size_t i=0;i<identities.size();++i){check(identities[i]!=0,"thread identity known");for(std::size_t j=0;j<i;++j)if(identities[i]==identities[j])throw std::runtime_error("thread identity reused");}
 check(std::is_sorted(identities.begin(),identities.end()),"sequential thread exit never reuses identity");
}
int main(){try{
 group("bounded identity allocation saturates rather than wraps",identityExhaustion);
 group("thread exit never reuses another identity",threadExitNonreuse);
 group("same-thread owner observation never reenters base mutex",ownerObservation);
 group("held actual request prefix retains caller owner",heldEntry);
 group("idle actual request prefix adopts exact work scope",idleEntry);
 group("foreign actual request prefix fails before work",foreignEntry);
 group("thread identity is not shared with foreign thread",distinctThreadIdentity);
 group("real standard mutex handoff remains serialized",handoffs);
 group("exception and deferred guards retain RAII semantics",exceptionalScope);
 std::cout<<json({{"pass",true},{"groups",groups},{"checks",checks},{"realStdMutexUsedForCandidate",true},{"unsafeBaselineStdSelfTryExecuted",false},{"actualProductionRequestAdmissionExtracted",true},{"candidateMutexSize",sizeof(current::InputHostMutex)},{"standardMutexSize",sizeof(std::mutex)},{"productsOrInputDevicesExecuted",false}}).dump(2)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
