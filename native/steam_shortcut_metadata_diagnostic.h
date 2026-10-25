#pragma once
#include "steam_shortcut_metadata_identity.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>
namespace ymcc::shortcutmetadata {
namespace detail {
namespace old=ymcc::steamrecord::detail;
inline constexpr std::size_t kMaxReads=2047,kMaxBytes=65536,kMaxSingle=64,kMaxDepth=32;
inline constexpr std::uint32_t kMaxCount=4096;
struct Failure { const char* reason; };
inline void need(bool v,const char* r){if(!v)throw Failure{r};}
inline std::uint32_t u32(const unsigned char* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
inline std::uint64_t u64(const unsigned char* p){std::uint64_t v=0;for(unsigned i=0;i<8;++i)v|=std::uint64_t(p[i])<<(8*i);return v;}
inline std::int32_t index(std::uint32_t v){if(v==0xffffffffU)return -1;need(v<=0x7fffffffU,"shortcut-index-negative");return static_cast<std::int32_t>(v);}
// S89 fixed d9e90 allocator owner: T=manager+20, native capacity=T+38,
// next/highwater=T+24, arena=T+30, live count=T+1c. Native slot bounds
// do not establish malloc success, ABA exclusion, or committed-region capacity.
struct Tree {std::int32_t root=-1;std::uint32_t count=0,highwater=0,capacity=0;std::uint64_t arena=0;bool operator==(const Tree&)const=default;};
inline Tree tree(old::Reader& r,std::uint64_t manager){
 Tree t;t.root=index(r.count(old::offset(manager,0x38),0xffffffffU,"shortcut-root-invalid"));
 t.count=r.count(old::offset(manager,0x3c),kMaxCount,"shortcut-count-limit");
 t.highwater=r.count(old::offset(manager,0x44),kMaxCount,"shortcut-highwater-invalid");
 t.arena=r.u64(old::offset(manager,0x50));
 t.capacity=r.count(old::offset(manager,0x58),kMaxCount,"shortcut-capacity-invalid");
 need(t.count<=t.highwater&&t.highwater<=t.capacity,"shortcut-allocation-header-inconsistent");
 need((t.count==0)==(t.root==-1),"shortcut-tree-header-inconsistent");
 // The full address interval uses the READ native capacity, including an
 // empty tree with retained storage; 4096 is only our resource refusal limit.
 old::vectorRange(t.arena,t.capacity,32);return t;
}
struct Found {std::uint64_t node=0,record=0;bool operator==(const Found&)const=default;};
inline Found find(old::Reader& r,const Tree& t,std::uint32_t wanted){
 std::int32_t current=t.root,parent=-1;std::set<std::int32_t> visited;
 bool hasLower=false,hasUpper=false;std::uint32_t lower=0,upper=0;
 for(std::size_t depth=0;current!=-1;++depth){
  need(depth<kMaxDepth,"shortcut-path-limit");need(visited.insert(current).second,"shortcut-path-cycle");
  // Active nodes may be sparse after frees. The fixed allocator returns a
  // reused free index or the previous highwater; live count is not an index bound.
  need(current>=0&&static_cast<std::uint32_t>(current)<t.highwater,"shortcut-index-out-of-highwater");
  const auto address=old::offset(t.arena,std::uint64_t(current)*32);std::array<unsigned char,32> b{};r.read(address,b.data(),b.size());
  const auto left=index(u32(b.data())),right=index(u32(b.data()+4)),observedParent=index(u32(b.data()+8));const auto key=u32(b.data()+0x10);
  need(observedParent==parent,"shortcut-node-parent-mismatch");need((!hasLower||key>lower)&&(!hasUpper||key<upper),"shortcut-key-order-mismatch");
  if(key==wanted){const auto value=u64(b.data()+0x18);need(old::inRange(value,0x70),"shortcut-record-pointer-invalid");return {address,value};}
  parent=current;if(wanted<key){hasUpper=true;upper=key;current=left;}else{hasLower=true;lower=key;current=right;}
 }
 return {};
}
inline bool complete(const Scope& s){return s.steamPid&&s.gamePid&&s.windowsSession&&s.steamBirth&&s.gameBirth&&s.generation&&s.epoch&&!s.steamPathKey.empty()&&!s.gamePathKey.empty();}
inline bool inputMatches(const Query& q,const Witness& w){return w.valid&&w.scope==q.scope&&w.moduleBase==q.native.moduleBase&&w.moduleSize==q.native.moduleSize&&w.account64==q.native.account64&&w.gameId64==q.native.gameId64&&w.appid==q.appid&&((w.node==0)==(w.record==0))&&(w.node==0||(old::inRange(w.node,32)&&old::inRange(w.record,0x70)));}
inline Json empty(){return {{"source","steam-shortcut-metadata-diagnostic"},{"authority","diagnostic-only-no-admission"},{"status","unavailable"},{"available",false},{"reason","identity-missing"},{"loadedByte",nullptr},{"targetRecordPresent",nullptr},{"recordField68Nonzero",nullptr},{"remote0PredicateSatisfied",nullptr},{"remote1PredicateSatisfied",nullptr},{"nativeOriginalPrimaryMatched",nullptr},{"metadataAllocationMatchesBaseline",nullptr},{"metadataTransition",nullptr},{"atomicSnapshot",false},{"abaExcluded",false},{"loginGenerationLease","unknown"},{"allocatedCapacityVerified",false},{"recordAbsenceIsNotAtomicProof",true},{"accountValuesPersisted",false},{"pointerValuesPersisted",false},{"otherAccountsRecordsRead",false},{"otherShortcutsRecordFieldsRead",false},{"inputVerified",false},{"continueVerified",false},{"actualEventsCollected",false},{"recoveryActionAuthorized",false},{"readCalls",0},{"readBytes",0},{"readTimeLimitIsCheckpointBased",true}};}
}
inline Outcome observe(const Query& input,const Read& read,const Stable& stable,const Clock& suppliedClock){
 using namespace detail;const Query q=input;Outcome out{empty(),{}};
 const char* failure=nullptr;const char* readFailure=nullptr;bool identityStarted=false;std::size_t calls=0,bytes=0;
 const Clock tick=suppliedClock?suppliedClock:Clock([]{return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());});
 std::uint64_t started=0,finished=0;bool startedClock=false;std::uint8_t loaded=0;bool nonzero68=false;Found found;Witness candidate;
 using Span=std::pair<std::uint64_t,std::size_t>;std::map<Span,std::vector<unsigned char>> spans;
 const auto deadline=[&]{const auto now=tick();need(now>=started&&now-started<=100,"read-deadline");finished=now;};
 const auto isStable=[&]{try{return stable&&stable();}catch(...){return false;}};
 Read checkedRead=[&](std::uint64_t a,void* p,std::size_t n){
  try{
   deadline();need(n&&n<=kMaxSingle&&calls<kMaxReads&&n<=kMaxBytes-bytes,"read-budget-limit");++calls;bytes+=n;
   bool ok=false;try{ok=read(a,p,n);}catch(...){readFailure="memory-read-exception";return false;}
   if(!ok){readFailure="memory-read-failed";return false;}deadline();
   const Span key{a,n};std::vector<unsigned char> value(static_cast<unsigned char*>(p),static_cast<unsigned char*>(p)+n);auto it=spans.find(key);
   if(it==spans.end())spans.emplace(key,std::move(value));else need(it->second==value,"observed-span-changed");return true;
  }catch(const Failure& e){readFailure=e.reason;return false;}catch(...){readFailure="memory-read-exception";return false;}
 };
 old::Reader r(checkedRead);
 try{
  need(q.originalTrackerBound,"original-tracker-unbound");need(complete(q.scope)&&q.scope.gamePid==q.native.expectedPid,"original-scope-incomplete");
  need(q.phase==Phase::Baseline||q.phase==Phase::FollowOriginal,"phase-invalid");
  if(q.phase==Phase::FollowOriginal)need(inputMatches(q,q.original),"original-witness-mismatch");else need(!q.original.valid,"baseline-already-bound");
  need(q.native.profileVerified&&q.native.profile==ymcc::steamrecord::ProfileId::Legacy1791918634,"profile-unverified");need(q.native.account64&&q.native.gameId64&&q.native.expectedPid,"identity-missing");
  need(q.appid>=0x80001388U&&(q.native.gameId64>>32)==q.appid&&(q.native.gameId64&0xffffffffULL)==0x02000000ULL,"shortcut-gameid-mismatch");
  need(static_cast<bool>(read)&&static_cast<bool>(stable),"callbacks-unavailable");old::require(old::inRange(q.native.moduleBase,q.native.moduleSize),"module-range-invalid");
  for(const auto anchor:{0x181d008ULL,0x1361f38ULL,0x1361ff0ULL,0x136fa80ULL,0x136fbd0ULL,0x1382c78ULL,0x1382c98ULL,0xa5e6f0ULL})old::moduleAnchor(q.native,anchor,8);
  old::moduleAnchor(q.native,0xcfdc0,14);old::moduleAnchor(q.native,0xa5e790,16);
  need(isStable(),"identity-changed");identityStarted=true;started=tick();finished=started;startedClock=true;
  old::checkGetter(r,q.native);
  need(r.u64(old::moduleAnchor(q.native,0x1382c98,8))==old::moduleAnchor(q.native,0xa5e6f0,8),"shortcut-vtable-slot-mismatch");
  constexpr std::array<unsigned char,16> filter={0x45,0x84,0xe4,0x74,0x0b,0x48,0x83,0x7d,0x68,0x00,0x0f,0x84,0xe6,0x01,0x00,0x00};std::array<unsigned char,16> actual{};r.read(old::moduleAnchor(q.native,0xa5e790,16),actual.data(),actual.size());need(actual==filter,"shortcut-filter-anchor-mismatch");
  const auto selection=old::selectUser(r,q.native);const auto header=old::readHeader(r,selection.user);const auto targets=old::readTargets(r,q.native,header);
  need(targets.size()==1,"native-target-missing-or-ambiguous");need(targets.front().primary==q.native.expectedPid,"native-original-primary-mismatch");
  const auto manager=old::offset(selection.user,0x5a60);
  if(q.phase==Phase::FollowOriginal)need(q.original.user==selection.user&&q.original.manager==manager,"original-owner-witness-changed");
  need(r.u64(manager)==old::moduleAnchor(q.native,0x1382c78,8),"shortcut-manager-type-mismatch");need(r.u64(old::offset(manager,8))==selection.user,"shortcut-manager-owner-mismatch");
  r.read(old::offset(manager,0x10),&loaded,1);const auto beforeTree=tree(r,manager);found=find(r,beforeTree,q.appid);
  // An allocation transition is evidence, not a different original OS game.
  // The original owner/key witness is retained; never dereference old allocations.
  if(found.record){need(r.count(found.record,0xffffffffU,"shortcut-record-id-invalid")==q.appid,"shortcut-record-id-mismatch");nonzero68=r.u64(old::offset(found.record,0x68))!=0;}
  need(tree(r,manager)==beforeTree,"shortcut-tree-header-changed");old::checkSelection(r,q.native,selection);need(isStable(),"identity-changed");deadline();
  need(old::readHeader(r,selection.user)==header,"native-record-header-changed");need(old::readTargets(r,q.native,header)==targets,"native-targets-changed");need(old::selectUser(r,q.native)==selection,"user-selection-changed");
  const auto saved=spans;for(const auto& item:saved){std::array<unsigned char,kMaxSingle> b{};r.read(item.first.first,b.data(),item.first.second);need(std::equal(item.second.begin(),item.second.end(),b.begin()),"observed-span-changed");}
  // Native identity must bracket even the final metadata rereads.
  need(old::selectUser(r,q.native)==selection,"post-metadata-user-changed");
  need(old::readHeader(r,selection.user)==header,"post-metadata-header-changed");
  need(old::readTargets(r,q.native,header)==targets,"post-metadata-targets-changed");
  need(old::readHeader(r,selection.user)==header,"post-metadata-header-changed");
  old::checkSelection(r,q.native,selection);need(isStable(),"identity-changed");deadline();
  candidate={q.scope,q.native.moduleBase,q.native.moduleSize,q.native.account64,q.native.gameId64,q.appid,selection.user,manager,found.node,found.record,true};
 }catch(const Failure& e){failure=e.reason;}catch(const old::Rejected& e){failure=e.reason;}catch(...){failure="observation-unavailable";}
 if(identityStarted&&!isStable())failure="identity-changed";else if(readFailure)failure=readFailure;
 out.diagnostic["readCalls"]=calls;out.diagnostic["readBytes"]=bytes;out.diagnostic["memoryElapsedMs"]=startedClock&&finished>=started?Json(finished-started):Json(nullptr);
 if(failure){out.diagnostic["reason"]=failure;return out;}
 out.diagnostic["status"]="observed";out.diagnostic["available"]=true;out.diagnostic["reason"]=found.record?"shortcut-metadata-observed":"shortcut-target-not-observed";
 out.diagnostic["loadedByte"]=loaded;out.diagnostic["targetRecordPresent"]=found.record!=0;out.diagnostic["nativeOriginalPrimaryMatched"]=true;
 if(found.record){out.diagnostic["recordField68Nonzero"]=nonzero68;out.diagnostic["remote0PredicateSatisfied"]=true;out.diagnostic["remote1PredicateSatisfied"]=nonzero68;}
 if(q.phase==Phase::Baseline){out.diagnostic["metadataTransition"]=found.record?"baseline-present":"baseline-absent";}
 else{
  const bool same=q.original.node==found.node&&q.original.record==found.record;out.diagnostic["metadataAllocationMatchesBaseline"]=same;
  out.diagnostic["metadataTransition"]=same?"same-target-allocation":!q.original.record?"target-record-appeared":!found.record?"target-record-removed":q.original.record!=found.record?"target-record-replaced":"target-node-relocated";
 }
 out.witness=q.phase==Phase::Baseline?candidate:q.original;return out;
}
}


