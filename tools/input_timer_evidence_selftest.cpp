// Pure memory fixture: no real timer, input, clock read or device I/O.
#include "input_timer_evidence.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
std::atomic<bool> watchAllocations{false};
std::atomic<std::uint64_t> watchedAllocations{0};
void* allocate(std::size_t size) {
    if (watchAllocations.load(std::memory_order_relaxed)) watchedAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
}
void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace ymcc::inputTimerEvidence {
struct RecorderTestAccess {
    static bool hold(Recorder& r) noexcept { return !r.flag_.test_and_set(std::memory_order_acquire); }
    static void release(Recorder& r) noexcept { r.flag_.clear(std::memory_order_release); }
};
}
using namespace ymcc::inputTimerEvidence;
namespace {
unsigned checks = 0, cases = 0;
nlohmann::json concurrencyResult;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) throw std::runtime_error(label);
}
#define CHECK(condition, label) check(static_cast<bool>(condition), label)
const Trace& recent(const Snapshot& s, unsigned i) { return s.recent[(s.recentStart+i)%recentCapacity]; }
const Boundary& recentBoundary(const Snapshot& s, unsigned i) {
    return s.boundaryRecent[(s.boundaryRecentStart+i)%boundaryRecentCapacity];
}
void verifySnapshot(const Snapshot& s) {
    CHECK(s.summary.known, "snapshot-known");
    const auto& v = s.summary;
    CHECK(v.ordinal == v.ownerBegins+v.createResults+v.killResults+v.entered+v.exited, "accepted-ordinal-conservation");
    CHECK(v.exited == v.matchedLeaves+v.unmatchedLeaves, "leave-match-conservation");
    CHECK(v.entered == v.matchedLeaves+v.inFlight, "unresolved-conservation");
    CHECK(v.inFlight == v.activeCount+v.activeCapacityDrop, "active-capacity-conservation");
    CHECK(v.exited == v.busCalled+v.busySkip+v.stopReturn+v.exceptionOrUnknown, "outcome-conservation");
    CHECK(v.maxInFlight >= v.inFlight, "max-unresolved-bound");
    CHECK(v.ownerCount<=ownerCapacity && v.groupCount<=groupCapacity && v.activeCount<=activeCapacity, "fixed-table-bounds");
    CHECK(s.firstCount<=firstCapacity && s.recentCount<=recentCapacity, "fixed-trace-bounds");
    CHECK(s.boundaryFirstCount<=boundaryFirstCapacity && s.boundaryRecentCount<=boundaryRecentCapacity, "fixed-boundary-bounds");
    CHECK(s.firstCount+v.firstCapacityDrop==v.entered+v.exited, "first-capacity-count");
    CHECK(s.recentCount+v.recentOverwrite==v.entered+v.exited, "recent-overwrite-count");
    CHECK(s.boundaryFirstCount+v.boundaryFirstCapacityDrop==v.ownerBegins+v.createResults+v.killResults, "boundary-first-capacity-count");
    CHECK(s.boundaryRecentCount+v.boundaryRecentOverwrite==v.ownerBegins+v.createResults+v.killResults, "boundary-recent-overwrite-count");
    for (unsigned i=0; i<s.recentCount; ++i) {
        const auto& t=recent(s,i);
        CHECK(t.ordinal>0 && t.ordinal<=v.ordinal, "trace-accepted-ordinal");
        CHECK(!t.ownerBindingKnown, "trace-owner-never-proven");
        if (i) CHECK(recent(s,i-1).ordinal<t.ordinal, "recent-trace-order");
        if (t.type==TraceKind::exit) CHECK(t.entryOrdinal<t.ordinal, "exit-links-original-entry");
    }
    for (unsigned i=1; i<s.boundaryRecentCount; ++i)
        CHECK(recentBoundary(s,i-1).ordinal<recentBoundary(s,i).ordinal, "boundary-recent-order");
}
void disabled() {
    ++cases;
    Recorder r;
    for (unsigned i=0; i<100; ++i) {
        const auto t=r.enter(false,17,0xFEDCBA9876543210ull,100+i,33,99,true);
        r.leave(t,500+i,1,123);
        CHECK(t.ordinal==0, "disabled-token-empty");
    }
    const auto s=r.snapshot();
    CHECK(s.summary.ordinal==0 && s.summary.entered==0 && s.summary.exited==0 && s.firstCount==0,
        "disabled-zero-records");
    CHECK(s.summary.drops.enter==0 && s.summary.drops.leave==0, "disabled-zero-drops");
    CHECK(RecorderTestAccess::hold(r), "disabled-hold-lock");
    const auto t=r.enter(false,1,2,3,4,5,false);
    RecorderTestAccess::release(r);
    CHECK(t.ordinal==0 && r.summary().drops.enter==0, "disabled-no-lock-attempt");
    verifySnapshot(s);
}
void identitiesAndOutcomes() {
    ++cases;
    Recorder r;
    constexpr U64 event=0xFEDCBA9876543210ull, power=0x123456789ABCDEF0ull;
    r.ownerBegin(101,10,event,multimedia,2);
    r.createResult(101,11,event,0xFFFFFFFFu,71);
    const auto t=r.enter(true,0xFFFFFFFFu,event,12,0xFEDCBA98u,power,true);
    r.leave(t,18,1,9876543210ull);
    for (unsigned outcome=2; outcome<=4; ++outcome) r.leave(r.enter(true,22,event+1,20+outcome,23,2,false),30+outcome,outcome);
    r.leave(r.enter(true,22,event+1,40,23,2,false),41,99);
    const auto s=r.snapshot();
    verifySnapshot(s);
    const auto& entry=s.first[0]; const auto& exit=s.first[1];
    CHECK(entry.timerId==t.timerId && entry.eventHandle==event && entry.threadId==t.threadId &&
        entry.powerGeneration==power && entry.nowMs==12 && entry.stopObserved, "exact-entry-identity");
    CHECK(exit.timerId==t.timerId && exit.eventHandle==event && exit.threadId==t.threadId &&
        exit.powerGeneration==power && exit.entryOrdinal==t.ordinal && exit.stopObserved, "exact-exit-identity");
    CHECK(exit.ordinal>entry.ordinal && exit.durationKnown && exit.durationMs==6, "exact-entry-exit-order-duration");
    CHECK(exit.busOrdinal==9876543210ull && !s.summary.busCountProven, "bus-call-does-not-prove-count");
    CHECK(s.summary.busCalled==1 && s.summary.busySkip==1 && s.summary.stopReturn==1 && s.summary.exceptionOrUnknown==2,
        "all-outcome-buckets");
    CHECK(s.summary.inFlight==0 && s.summary.inFlightKnown && !s.summary.callbacksDrainedProven, "zero-accounting-not-drain-proof");
    CHECK(entry.ownerCandidate==101 && entry.ownerCandidateCount==1 && !entry.ownerBindingKnown, "single-receipt-is-only-candidate");
    const auto j=snapshotJson(s);
    CHECK(j["trace"]["first"][0]["eventHandle"].get<U64>()==event &&
        j["trace"]["first"][0]["powerGeneration"].get<U64>()==power, "json-exact-u64-identities");
    CHECK(j["counts"]["busCalled"]==1 && !j["busCountProven"].get<bool>() &&
        !j["steamConsumptionProven"].get<bool>() && !j["callbacksDrainedProven"].get<bool>(), "json-observation-scope");
    CHECK(j["owners"][0]["createErrorOrResult"]==71 && j["owners"][0]["intervalMs"]==2, "json-owner-receipt-fields");
}
void ownerReuseAndSeparateGroups() {
    ++cases;
    Recorder r;
    r.ownerBegin(1,1,900,multimedia,1); r.createResult(1,2,900,7);
    r.ownerBegin(2,3,901,waitable,2); r.createResult(2,4,901,8);
    const auto old=r.enter(true,7,900,5,10,1,false);
    const auto other=r.enter(true,8,901,6,11,2,false);
    r.killResult(1,7,900,7,0);
    r.ownerBegin(3,8,900,sleep,3); r.createResult(3,9,900,7);
    const auto reused=r.enter(true,7,900,10,12,3,false);
    r.leave(old,11,1); r.leave(other,12,2); r.leave(reused,13,3);
    r.leave(r.enter(true,7,901,14,13,4,false),15,4); // same ID, different event
    r.leave(r.enter(true,8,900,16,14,5,false),17,4); // same event, different ID
    const auto s=r.snapshot(); verifySnapshot(s);
    CHECK(s.summary.groupCount==4 && s.summary.maxInFlight==3, "two-dimensional-group-identity");
    CHECK(s.groups[0].timerId==7 && s.groups[0].eventHandle==900 && s.groups[0].entered==2 &&
        s.groups[0].exited==2 && s.groups[0].busCalled==1 && s.groups[0].stopReturn==1, "reused-identity-group-observed-only");
    CHECK(s.first[2].ownerCandidateCount==2 && s.first[2].ownerBindingAmbiguous &&
        s.first[2].ownerCandidate==0 && !s.first[2].ownerBindingKnown, "reused-identity-ambiguous");
    CHECK(s.first[3].entryOrdinal==old.ordinal && s.first[3].powerGeneration==1 && !s.first[3].ownerBindingKnown,
        "late-old-owner-not-rebound");
    CHECK(s.owners[0].kills==1 && s.owners[2].kind==sleep, "lifecycle-kinds-and-reuse-receipts");
}
void entryBeforeCreateAndKill() {
    ++cases;
    Recorder r;
    r.ownerBegin(42,1,1000,multimedia,1);
    const auto t=r.enter(true,9,1000,2,77,1,false);
    auto s=r.snapshot();
    CHECK(s.first[0].ownerCandidateCount==0 && !s.first[0].ownerBindingKnown, "entry-before-create-unknown");
    r.createResult(42,3,1000,9);
    r.killResult(42,4,1000,9,0);
    s=r.snapshot(); verifySnapshot(s);
    CHECK(s.summary.inFlight==1 && s.summary.activeCount==1 && !s.summary.callbacksDrainedProven,
        "kill-api-receipt-not-drain");
    CHECK(recentBoundary(s,2).type==BoundaryKind::killResult && recentBoundary(s,2).result==0, "exact-kill-api-receipt");
    const auto late=r.enter(true,9,1000,5,78,2,true);
    CHECK(late.ordinal>t.ordinal, "callback-after-kill-still-observed");
    r.leave(t,6,1); r.leave(late,7,3);
    s=r.snapshot(); verifySnapshot(s);
    const auto j=snapshotJson(s);
    CHECK(j["boundaries"]["recent"][2]["type"]=="kill-api-receipt" &&
        !j["boundaries"]["recent"][2]["callbacksDrainedProven"].get<bool>(), "json-kill-receipt-not-drain");
    CHECK(!s.first[0].ownerBindingKnown && s.first[0].ownerCandidateCount==0, "no-retroactive-entry-rebinding");
}
void delayedLeaveAndRetainedBoundaries() {
    ++cases;
    Recorder r;
    r.ownerBegin(7,1,33,multimedia,1); r.createResult(7,2,33,4);
    const auto delayed=r.enter(true,4,33,3,999,55,false);
    r.killResult(7,4,33,4,17);
    for (unsigned i=0; i<300; ++i) r.leave(r.enter(true,5,34,10+i*2,2,56,false),11+i*2,2);
    r.leave(delayed,1000,1,77);
    const auto s=r.snapshot(); verifySnapshot(s);
    const auto& last=recent(s,s.recentCount-1);
    CHECK(last.entryOrdinal==delayed.ordinal && last.threadId==999 && last.powerGeneration==55 &&
        last.timerId==4 && last.eventHandle==33 && last.matchedEntry, "delayed-leave-survives-trace-overwrite");
    CHECK(s.summary.inFlight==0 && s.summary.maxInFlight==2 && s.summary.unmatchedLeaves==0, "delayed-leave-actual-matching");
    CHECK(s.boundaryFirstCount==3 && s.boundaryRecentCount==3 && recentBoundary(s,2).result==17,
        "callback-burst-cannot-overwrite-lifecycle");
    CHECK(s.first[0].ordinal==delayed.ordinal && recent(s,0).ordinal>delayed.ordinal, "first-versus-recent-retention");
}
void capacities() {
    ++cases;
    Recorder r;
    for (unsigned i=0; i<ownerCapacity+4; ++i) {
        r.ownerBegin(i,3*i,200+i,multimedia,1); r.createResult(i,3*i+1,200+i,30+i);
        r.killResult(i,3*i+2,200+i,30+i,19+i);
    }
    for (unsigned i=0; i<groupCapacity+4; ++i)
        r.leave(r.enter(true,30+i,200+i,100+i*2,i,1,false),101+i*2,1);
    const auto s=r.snapshot(); verifySnapshot(s);
    CHECK(s.summary.ownerCount==ownerCapacity && s.summary.ownerCapacityDrop==12, "owner-capacity-explicit");
    CHECK(s.summary.entered==groupCapacity+4 && s.summary.exited==groupCapacity+4 &&
        s.summary.groupCapacityDrop==4, "full-owner-group-tables-preserve-callback-observation");
    CHECK(s.summary.groupCount==groupCapacity && s.groups[0].timerId==30 && s.groups[0].exited==1,
        "group-capacity-preserves-existing-groups");
    CHECK(s.summary.boundaryFirstCapacityDrop==28 && s.summary.boundaryRecentOverwrite==20, "boundary-capacity-explicit");
    CHECK(recentBoundary(s,15).owner==11 && !recentBoundary(s,15).ownerSlotKnown, "overflow-owner-receipt-still-retained");
    CHECK(recent(s,s.recentCount-1).timerId==41 && !recent(s,s.recentCount-1).ownerBindingKnown,
        "owner-overflow-no-control-rejection");
    const auto j=snapshotJson(s);
    CHECK(j["caps"]["owners"]==8 && j["counts"]["ownerCapacityDrop"]==12 &&
        j["boundaries"]["recent"].size()==16, "json-capacities-and-loss");
}
void activeCapacityAndInvalidLeaves() {
    ++cases;
    Recorder r;
    Token tokens[activeCapacity+3]{};
    for (unsigned i=0; i<activeCapacity+3; ++i) tokens[i]=r.enter(true,1,2,i,3,4,false);
    for (const auto& token:tokens) r.leave(token,100,1);
    auto s=r.snapshot(); verifySnapshot(s);
    CHECK(s.summary.entered==67 && s.summary.exited==67 && s.summary.matchedLeaves==64 &&
        s.summary.unmatchedLeaves==3 && s.summary.activeCapacityDrop==3, "active-cap-overflow-unproven");
    CHECK(s.summary.inFlight==3 && !s.summary.inFlightKnown && s.summary.maxInFlight==67, "active-overflow-never-fakes-zero");
    r.leave(tokens[0],101,1); // duplicate cannot decrement unresolved entries
    Recorder other;
    const auto foreign=other.enter(true,1,2,102,3,4,false);
    r.leave(foreign,103,2);
    auto forged=tokens[1]; ++forged.threadId; r.leave(forged,104,3);
    s=r.snapshot(); verifySnapshot(s);
    CHECK(s.summary.unmatchedLeaves==6 && s.summary.inFlight==3, "duplicate-foreign-forged-leaves-unmatched");
}
void rollbackAndGeneration() {
    ++cases;
    Recorder r;
    const auto a=r.enter(true,1,2,100,3,90,false); r.leave(a,90,1);
    const auto b=r.enter(true,1,2,80,3,91,true); r.leave(b,85,3);
    const auto s=r.snapshot(); verifySnapshot(s);
    CHECK(s.summary.clockRollbacks==2 && !s.first[1].durationKnown && s.first[1].durationMs==0, "rollback-no-unsigned-duration-wrap");
    CHECK(s.first[3].durationKnown && s.first[3].durationMs==5 && s.first[2].powerGeneration==91,
        "per-entry-duration-after-rollback");
    CHECK(a.ordinal==1 && b.ordinal==3 && s.summary.ordinal==4, "global-ordinal-across-generations");
    CHECK(snapshotJson(s)["trace"]["first"][1]["durationMs"].is_null(), "json-unknown-duration-null");
}
void deterministicContention() {
    ++cases;
    Recorder r;
    const auto t=r.enter(true,1,2,3,4,5,false);
    CHECK(RecorderTestAccess::hold(r), "contention-hold-lock");
    r.ownerBegin(1,4,2,multimedia,1); r.createResult(1,5,2,1); r.killResult(1,6,2,1,0);
    const auto rejected=r.enter(true,1,2,7,4,5,false); r.leave(t,8,1);
    const auto unknown=r.snapshot(); const auto small=r.summary();
    RecorderTestAccess::release(r);
    CHECK(rejected.ordinal==0, "contended-entry-token-empty");
    CHECK(!unknown.summary.known && !unknown.summary.inFlightKnown && !small.known, "snapshot-contention-unknown");
    CHECK(unknown.summary.drops.snapshot==1 && small.drops.summary==1, "inspection-drops-explicit");
    const auto j=snapshotJson(unknown);
    CHECK(!j["known"].get<bool>() && j["counts"].is_null() && j["trace"].is_null() &&
        !j["inFlightKnown"].get<bool>(), "unknown-is-not-zero-json");
    auto s=r.snapshot(); verifySnapshot(s);
    const auto& d=s.summary.drops;
    CHECK(d.ownerBegin==1 && d.createResult==1 && d.killResult==1 && d.enter==1 && d.leave==1, "all-operation-drops-explicit");
    CHECK(s.summary.inFlight==1 && s.summary.activeCount==1 && !s.summary.inFlightKnown && !s.summary.accountingComplete,
        "dropped-leave-keeps-unresolved-entry");
    r.leave(t,9,1);
    s=r.snapshot(); verifySnapshot(s);
    CHECK(s.summary.inFlight==0 && !s.summary.inFlightKnown && !s.summary.callbacksDrainedProven, "later-exit-does-not-erase-drop-history");
    const auto summary=r.summary();
    CHECK(summary.known && summary.ordinal==s.summary.ordinal && summary.entered==s.summary.entered &&
        summary.drops.leave==1, "small-summary-same-accounting");
}
void burstAndNoAllocation() {
    ++cases;
    Recorder r;
    constexpr U64 burst=100000;
    watchedAllocations.store(0);
    watchAllocations.store(true);
    r.ownerBegin(1,0,9,multimedia,1); r.createResult(1,0,9,7);
    for (U64 i=0; i<burst; ++i) {
        r.leave(r.enter(true,7,9,i*2,3,4,false),i*2+1,static_cast<unsigned>(i%4)+1,i);
        r.leave(r.enter(false,7,9,i*2,3,4,false),i*2+1,1);
    }
    r.killResult(1,burst*2,9,7,0);
    const auto small=r.summary();
    const auto s=r.snapshot();
    watchAllocations.store(false);
    CHECK(watchedAllocations.load()==0, "all-recorder-paths-zero-heap-allocation");
    verifySnapshot(s);
    CHECK(s.summary.entered==burst && s.summary.exited==burst && small.inFlight==0, "100k-burst-exact-counts");
    CHECK(s.summary.firstCapacityDrop==burst*2-firstCapacity &&
        s.summary.recentOverwrite==burst*2-recentCapacity, "100k-burst-explicit-retention-loss");
    CHECK(s.first[0].ordinal==3 && recent(s,0).ordinal==burst*2-recentCapacity+3 &&
        recent(s,63).ordinal==burst*2+2, "100k-burst-window-ordinals");
    CHECK(s.summary.busCalled==25000 && s.summary.busySkip==25000 && s.summary.stopReturn==25000 &&
        s.summary.exceptionOrUnknown==25000 && s.summary.activeCapacityDrop==0, "100k-burst-outcomes-and-bounded-slots");
    CHECK(s.boundaryRecentCount==3 && s.summary.boundaryRecentOverwrite==0, "100k-burst-lifecycle-independent");
    const auto j=snapshotJson(s);
    CHECK(j["trace"]["recent"].size()==64 && j["trace"]["first"].size()==16 &&
        j["counts"]["recentOverwrite"]==burst*2-recentCapacity, "100k-burst-cold-json-bounded");
}
void multiThread() {
    ++cases;
    Recorder r;
    constexpr unsigned workers=4, iterations=6000;
    std::atomic<bool> start{false};
    std::atomic<U64> accepted{0}, badSnapshots{0}, knownSnapshots{0}, unknownSnapshots{0};
    std::vector<std::thread> threads;
    threads.reserve(workers+1);
    for (unsigned worker=0; worker<workers; ++worker) threads.emplace_back([&,worker] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (unsigned i=0; i<iterations; ++i) {
            const auto token=r.enter(true,worker+10,worker+100,static_cast<U64>(i)*2,worker+200,i/100,false);
            if (token.ordinal) { accepted.fetch_add(1,std::memory_order_relaxed);
                r.leave(token,static_cast<U64>(i)*2+1,(i%4)+1,i); }
        }
    });
    threads.emplace_back([&] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (unsigned i=0; i<iterations; ++i) {
            const auto s=r.snapshot();
            if (!s.summary.known) { unknownSnapshots.fetch_add(1,std::memory_order_relaxed); continue; }
            knownSnapshots.fetch_add(1,std::memory_order_relaxed);
            const auto& v=s.summary;
            bool good=v.ordinal==v.entered+v.exited && v.entered==v.inFlight+v.matchedLeaves &&
                v.exited==v.matchedLeaves+v.unmatchedLeaves &&
                v.exited==v.busCalled+v.busySkip+v.stopReturn+v.exceptionOrUnknown &&
                s.recentCount<=recentCapacity && v.inFlight==v.activeCount+v.activeCapacityDrop;
            for (unsigned k=1; k<s.recentCount; ++k) good=good && recent(s,k-1).ordinal<recent(s,k).ordinal;
            const auto small=r.summary();
            if (small.known) good=good && small.entered==small.inFlight+small.matchedLeaves;
            if (!good) badSnapshots.fetch_add(1,std::memory_order_relaxed);
        }
    });
    start.store(true,std::memory_order_release);
    for (auto& thread:threads) thread.join();
    const auto s=r.snapshot(); verifySnapshot(s);
    const auto& v=s.summary;
    CHECK(badSnapshots.load()==0, "concurrent-snapshots-never-torn");
    CHECK(knownSnapshots.load()+unknownSnapshots.load()==iterations, "snapshot-known-unknown-conservation");
    CHECK(v.entered+v.drops.enter==static_cast<U64>(workers)*iterations && v.entered==accepted.load(),
        "threaded-accepted-entry-drop-conservation");
    CHECK(v.exited+v.drops.leave==accepted.load(), "threaded-accepted-leave-drop-conservation");
    U64 groupEntries=0, groupExits=0, groupUnresolved=0;
    for (unsigned i=0; i<v.groupCount; ++i) {
        groupEntries+=s.groups[i].entered; groupExits+=s.groups[i].exited; groupUnresolved+=s.groups[i].inFlight;
    }
    CHECK(groupEntries==v.entered && groupExits==v.exited && groupUnresolved==v.inFlight,
        "threaded-per-group-conservation");
    for (unsigned i=0; i<s.recentCount; ++i) {
        const auto& t=recent(s,i);
        CHECK(t.timerId==t.threadId-190 && t.eventHandle==t.threadId-100, "threaded-exact-identity");
    }
    if (v.drops.enter || v.drops.leave || v.activeCapacityDrop || v.unmatchedLeaves)
        CHECK(!v.inFlightKnown && !v.accountingComplete, "threaded-loss-stays-unproven");
    concurrencyResult={{"attemptedEntries",workers*iterations},{"acceptedEntries",v.entered},{"acceptedLeaves",v.exited},
        {"entryDrops",v.drops.enter},{"leaveDrops",v.drops.leave},{"unmatchedLeaves",v.unmatchedLeaves},
        {"activeCapacityDrop",v.activeCapacityDrop},{"unresolved",v.inFlight},
        {"knownSnapshots",knownSnapshots.load()},{"unknownSnapshots",unknownSnapshots.load()},
        {"conservationPassed",true},{"raceDetectorRun",false}};
}
}
int main() {
    try {
        disabled(); identitiesAndOutcomes(); ownerReuseAndSeparateGroups(); entryBeforeCreateAndKill();
        delayedLeaveAndRetainedBoundaries(); capacities(); activeCapacityAndInvalidLeaves();
        rollbackAndGeneration(); deterministicContention(); burstAndNoAllocation(); multiThread();
        const nlohmann::json result={{"pass",true},{"cases",cases},{"checks",checks},{"burstEntries",100000},
            {"watchedAllocations",watchedAllocations.load()},{"pureMemory",true},{"concurrency",concurrencyResult},
            {"bytes",{{"Recorder",sizeof(Recorder)},{"Snapshot",sizeof(Snapshot)},{"Summary",sizeof(Summary)},{"Token",sizeof(Token)}}},
            {"alignment",{{"Snapshot",alignof(Snapshot)},{"Summary",alignof(Summary)},{"Token",alignof(Token)}}},
            {"steamConsumptionProven",false},{"callbacksDrainedProven",false},{"realTimerOrInputIO",false}};
        std::cout<<result.dump()<<'\n';
        return 0;
    } catch (const std::exception& e) {
        watchAllocations.store(false);
        std::cerr<<"CHECK_FAILED "<<e.what()<<" checks="<<checks<<" cases="<<cases<<'\n';
        return 1;
    }
}
