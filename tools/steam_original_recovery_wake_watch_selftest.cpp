#include "../native/steam_original_recovery_wake_watch.h"
#include <iostream>
#include <stdexcept>
#include <string>

using ymcc::steamOriginal::WakeProof;
using ymcc::steamOriginal::WakeWatch;
using U64=std::uint64_t;
namespace {
constexpr U64 ticksPerMs=10000, eventTime=134370000000000000ULL;
// The former 284-read envelope assumed every fast read cost at least 50ms.
// At most 140 precision reads save 3500ms: allow ceil(3500/1000) extra
// fallback reads, including the old envelope's boundary margin. Hard caps stay.
constexpr unsigned precisionSlots=static_cast<unsigned>((WakeWatch::precisionEndMs-WakeWatch::precisionStartMs)/WakeWatch::precisionPollMs);
constexpr U64 precisionSavingsMs=precisionSlots*(WakeWatch::fastPollMs-WakeWatch::precisionPollMs);
constexpr unsigned scheduledProbeEnvelope=284+static_cast<unsigned>((precisionSavingsMs+WakeWatch::settlePollMs-1)/WakeWatch::settlePollMs);
static_assert(precisionSlots==140&&scheduledProbeEnvelope==288);
unsigned checks=0,precisionLatencyCases=0;
U64 maximumPrecisionLatencyTicks=0;
unsigned checksFor(const char* name,void (*run)()) {
    const auto before=checks;run();
    std::cout<<"{\"case\":\""<<name<<"\",\"passed\":true,\"checks\":"<<checks-before<<"}\n";
    return checks-before;
}
void need(bool value,const char* message) {
    ++checks;
    if(!value)throw std::runtime_error(message);
}
U64 at(U64 ms) { return eventTime+ms*ticksPerMs; }
WakeProof physicalWake() {
    WakeProof p;
    p.generation=p.observedGeneration=p.requestedGeneration=p.admissionObservedGeneration=7;
    p.epoch=p.observedEpoch=9;
    p.eventFileTime=p.admissionFileTime=eventTime;
    p.boundaryFileTime=p.observedBoundaryFileTime=eventTime-ticksPerMs;
    p.latestEntryEpoch=9;p.latestEntryFileTime=p.boundaryFileTime;
    p.eventId=507;
    p.admitted=p.confirmed=p.fresh=p.requested=p.sourceMatched=true;
    return p;
}
void proofChecks() {
    const auto p=physicalWake();
    need(p.valid(7,9,eventTime),"confirmed 507 wake");
    auto q=p;q.eventId=107;need(q.valid(7,9,eventTime),"confirmed 107 wake");
    need(!WakeProof{}.valid(7,9,eventTime),"no kernel proof");
    need(!p.valid(8,9,eventTime),"generation revoked");
    need(!p.valid(7,10,eventTime),"epoch revoked");
    need(!p.valid(7,9,eventTime-1),"future proof");
    need(p.valid(7,9,at(60000)-1),"proof before absolute deadline");
    need(!p.valid(7,9,at(60000)),"proof at absolute deadline");
    need(!p.valid(7,9,UINT64_MAX),"very old proof");
    const auto reject=[&](auto change,const char* message) {
        auto bad=p;change(bad);need(!bad.valid(7,9,eventTime),message);
    };
    reject([](WakeProof& v){v.eventId=1;},"Power Ready is not kernel wake");
    reject([](WakeProof& v){v.eventId=506;},"sleep entry is not wake");
    reject([](WakeProof& v){v.admitted=false;},"admission revoked");
    reject([](WakeProof& v){v.confirmed=false;},"unconfirmed wake");
    reject([](WakeProof& v){v.fresh=false;},"stale wake");
    reject([](WakeProof& v){v.requested=false;},"request withdrawn");
    reject([](WakeProof& v){v.sourceMatched=false;},"unmatched source");
    reject([](WakeProof& v){v.requestedGeneration=6;},"request generation changed");
    reject([](WakeProof& v){v.observedEpoch=8;},"observed epoch changed");
    reject([](WakeProof& v){--v.admissionFileTime;},"event admission mismatch");
    reject([](WakeProof& v){v.boundaryFileTime=eventTime;},"wake not after boundary");
    reject([](WakeProof& v){--v.observedBoundaryFileTime;},"boundary revoked");
    reject([](WakeProof& v){v.latestEntryFileTime=eventTime;},"new sleep entry revokes wake");
    reject([](WakeProof& v){v.eventFileTime=v.admissionFileTime=0;},"missing event time");
    q=p;q.transferred=true;q.observedGeneration=q.admissionObservedGeneration=q.boundaryGeneration=6;
    need(q.valid(7,9,eventTime),"admitted adjacent generation transfer retained");
    q.transferred=false;need(!q.valid(7,9,eventTime),"unadmitted generation transfer");
}
void boundaryChecks() {
    static_assert(WakeWatch::budgetMs==60000&&WakeWatch::observationMs==55000);
    static_assert(WakeWatch::pollMs==10000&&WakeWatch::fastPollMs==50);
    static_assert(WakeWatch::fastWindowMs==2000&&WakeWatch::hintWindowMs==3000);
    static_assert(WakeWatch::maximumFastProbes==240&&WakeWatch::maximumProbes==320);
    static_assert(WakeWatch::precisionStartMs==4000&&WakeWatch::precisionEndMs==7500&&WakeWatch::precisionPollMs==25);
    static_assert(WakeWatch::settleWindowMs==WakeWatch::budgetMs&&WakeWatch::settlePollMs==1000);
    static_assert(WakeWatch::settleFastWindowMs==20000&&WakeWatch::settleFastPollMs==250);
    WakeWatch w;
    need(!w.noteHint(eventTime,true)&&!w.probeDue(eventTime),"hint cannot create a wake");
    need(w.delayMs(eventTime)==10000&&w.probes==0,"inactive safe delay");
    w.eventFileTime=eventTime;
    need(!w.noteHint(eventTime-1,true)&&!w.reserveProbe(eventTime-1,true),"future rejected");
    need(w.probeDue(eventTime)&&w.delayMs(eventTime)==1,"first read due without zero delay");
    need(w.reserveProbe(eventTime,true)&&!w.probeDue(at(50)-1),"first 50ms interval");
    need(w.delayMs(at(50)-1)==1&&w.probeDue(at(50)),"ceil sub-ms remaining delay");
    need(w.fastWindow(at(2000)-1)&&!w.fastWindow(at(2000)),"2s half-open boundary");
    need(w.reserveProbe(at(2000),true)&&w.delayMs(at(2000))==10000,"quiet ten seconds");
    need(!w.probeDue(at(12000)-1)&&w.probeDue(at(12000)),"quiet due boundary");
    need(w.noteHint(at(4000),true)&&w.probeDue(at(4000)),"hint wakes quiet wait early");
    need(w.reserveProbe(at(4000),true),"hint read reserved");
    need(w.noteHint(at(4001),true)&&!w.probeDue(at(4001)),"hint merged against last read");
    need(w.nextProbeFileTime==at(4050)&&w.delayMs(at(4001))==49,"merged hint preserves due");
    need(w.fastWindow(at(7001)-1)&&!w.fastWindow(at(7001)),"3s half-open hint boundary");
    const auto saved=w;
    need(!w.noteHint(at(4002),false)&&w.hintUntilFileTime==saved.hintUntilFileTime&&
        w.nextProbeFileTime==saved.nextProbeFileTime&&w.probes==saved.probes&&
        w.eventFileTime==saved.eventFileTime&&w.lastActivityFileTime==saved.lastActivityFileTime,
        "untrusted or cancelled hint has no state effect");
    need(!w.noteHint(at(4000),true)&&!w.reserveProbe(at(4000),true)&&
        !w.probeDue(at(4000))&&w.delayMs(at(4000))==10000,"backward time rejected");
    need(!w.reserveProbe(at(5000),false)&&!w.allows(at(5000),false),"cancel takes precedence");
    need(w.noteHint(at(54000),true)&&w.hintUntilFileTime==at(55000),"hint clamped to observation");
    need(w.reserveProbe(at(55000)-1,true)&&w.nextProbeFileTime==at(55000)&&
        w.delayMs(at(55000)-1)==1,"next due clamps with ceil");
    need(!w.noteHint(at(55000),true)&&!w.observes(at(55000),true)&&
        !w.reserveProbe(at(55000),true),"55s observation ends");
    need(w.allows(at(55000),true)&&w.allows(at(60000)-1,true)&&
        !w.allows(at(60000),true),"absolute 60s action deadline unchanged");
}
void budgetChecks() {
    WakeWatch w;w.eventFileTime=eventTime;
    for(unsigned i=0;i<WakeWatch::maximumFastProbes;++i)
        need(w.reserveProbe(eventTime,true),"budget-only reserve permits authority fixture");
    need(w.fastProbes==240&&w.probes==240,"fast hard budget");
    need(w.noteHint(eventTime,true),"hint after fast cap");
    // The reservation reaching the cap must itself schedule the 1s floor.
    need(w.nextProbeFileTime>=at(1000),"cap immediately switches to one-second floor");
    for(unsigned i=240;i<WakeWatch::maximumProbes;++i)
        need(w.reserveProbe(eventTime,true),"total budget-only reservations");
    const auto before=w;
    for(const U64 ms:{U64{0},U64{2000},U64{54999},U64{59999}}) {
        need(!w.reserveProbe(at(ms),true)&&!w.noteHint(at(ms),true)&&!w.allows(at(ms),true),
            "total cap never refunded by hints");
        need(w.probes==320&&w.fastProbes==240&&w.nextProbeFileTime==before.nextProbeFileTime&&
            w.hintUntilFileTime==before.hintUntilFileTime,"exhausted state unchanged");
    }
    WakeWatch moderate;moderate.eventFileTime=eventTime;
    need(moderate.beginObservation(eventTime,true),"moderate-only budget fixture starts");
    for(unsigned i=0;i<240;++i)need(moderate.reserveProbe(at(2000),true),"moderate budget-only reservation");
    need(moderate.fastProbes==0&&moderate.settleFastProbes==240&&moderate.periodMs(at(2000))==1000,
        "moderate reads spend the same subsecond pool");
    need(moderate.noteHint(at(2000),true)&&moderate.periodMs(at(2000))==1000,
        "new hint cannot refund moderate read reservations");
    moderate.settleFastProbes=UINT32_MAX;
    need(!moderate.subsecondCapacity()&&moderate.periodMs(at(2000))==1000,
        "subsecond counter overflow cannot reopen fast cadence");
    w.probes=UINT32_MAX;
    need(!w.reserveProbe(eventTime,true)&&w.probes==UINT32_MAX,"oversized counter cannot wrap");
}
unsigned quietSchedule() {
    WakeWatch w;w.eventFileTime=eventTime;need(w.beginObservation(eventTime,true),"quiet observer begins its own clock");
    for(U64 ms=0;ms<55000;) {
        const U64 now=at(ms);
        need(w.probeDue(now)&&w.reserveProbe(now,true),"quiet schedule due and reserve");
        const auto delay=w.delayMs(now);
        need(delay>0&&delay<=10000,"bounded positive CV timeout");
        ms+=delay;
    }
    need(w.probes==273&&w.fastProbes==180&&w.precisionProbes==140&&w.settleFastProbes==58,
        "quiet 40 initial + 8 moderate + 140 precision + 50 moderate + 35 slow = 273, shared subsecond 238");
    return w.probes;
}
unsigned stormSchedule() {
    WakeWatch w;w.eventFileTime=eventTime;need(w.beginObservation(eventTime,true),"storm observer begins its own clock");
    U64 previous=0;
    unsigned afterCap=0;
    // 5,500 hints across the entire observation; no exhaustive phase sweep.
    for(U64 ms=0;ms<55000;ms+=10) {
        const auto now=at(ms);
        need(w.noteHint(now,true),"trusted storm stays within admitted observation");
        const auto due=w.nextProbeFileTime;
        need(w.noteHint(now,true)&&w.nextProbeFileTime==due,"duplicate hint coalesces");
        if(!w.probeDue(now))continue;
        const bool capped=w.fastProbes>=WakeWatch::maximumFastProbes;
        const bool transition=previous&&previous<at(4000)&&now==at(4000);
        const U64 floor=capped?1000:((w.precisionWindow(now)||w.precisionWindow(previous))?25:50);
        if(previous&&!transition)need(now-previous>=floor*ticksPerMs,"storm respects 25ms precision, original 50ms outside, and capped 1s floor");
        need(w.reserveProbe(now,true),"storm reservation");
        if(capped)++afterCap;
        previous=now;
    }
    need(w.fastProbes==240&&w.precisionProbes<=w.fastProbes&&afterCap>0&&w.probes<=scheduledProbeEnvelope,
        "noise cannot retain subsecond cadence after shared cap; precision savings bound");
    need(!w.noteHint(at(55000),true)&&!w.probeDue(at(55000)),"storm cannot extend observation");
    return w.probes;
}
void cadenceBoundaryChecks() {
    WakeWatch w;w.eventFileTime=eventTime;
    need(w.reserveProbe(at(1950),true)&&w.nextProbeFileTime==at(2000),"last physical fast reservation");
    need(w.reserveProbe(at(2000),true)&&w.nextProbeFileTime==at(12000),"2s boundary switches schedule");
    need(w.noteHint(at(4000),true)&&w.reserveProbe(at(4000),true),"hint starts read-only cadence");
    need(w.reserveProbe(at(7000)-1,true)&&w.nextProbeFileTime==at(7050)-1,"last hint fast reservation");
    need(w.reserveProbe(at(7000),true)&&w.nextProbeFileTime==at(17000),"3s boundary switches schedule");
    const auto before=w;
    need(!w.noteHint(at(55000),true)&&!w.noteHint(at(60000),true),"expired hints rejected");
    need(w.eventFileTime==before.eventFileTime&&w.hintUntilFileTime==before.hintUntilFileTime&&
        w.probes==before.probes&&w.fastProbes==before.fastProbes&&
        w.nextProbeFileTime==before.nextProbeFileTime&&w.lastActivityFileTime==before.lastActivityFileTime&&
        w.lastProbeFileTime==before.lastProbeFileTime,"expired hints change no scheduling or authority fields");
    // Preserve the production engine's pure authority-query fixture ordering.
    WakeWatch authority;authority.eventFileTime=1000000;
    need(!authority.allows(999999,true)&&!authority.allows(1000000,false)&&
        authority.allows(1000000,true),"original authority admission fixture");
    need(authority.allows(1000000+59999*10000ULL,true)&&
        !authority.allows(1000000+60000*10000ULL,true),"original authority deadline fixture");
    need(authority.observes(1000000+54999*10000ULL,true)&&
        !authority.observes(1000000+55000*10000ULL,true),"original authority observation fixture");
}

void observationStartChecks() {
    WakeWatch delayed;delayed.eventFileTime=eventTime;
    need(delayed.beginObservation(at(4900),true),"late G3E observer starts one bounded scheduling window");
    need(delayed.eventFileTime==eventTime&&delayed.observationStartFileTime==at(4900),"scheduling clock separate from physical authority");
    need(delayed.probeDue(at(4900))&&delayed.periodMs(at(4900))==25,"event-anchored precision takes priority at late start");
    need(delayed.fastWindow(at(6900)-1)&&!delayed.fastWindow(at(6900)),"late start gets full 2s fast window");
    need(delayed.periodMs(at(6900))==25&&delayed.periodMs(at(7500))==250&&delayed.periodMs(at(24899))==250&&
        delayed.periodMs(at(24900))==1000&&delayed.periodMs(at(54999))==1000,"late observer gets 20s moderate cadence then 1s within the original physical window");
    const auto original=delayed;
    need(!delayed.beginObservation(at(5000),true)&&delayed.observationStartFileTime==original.observationStartFileTime&&
        delayed.nextProbeFileTime==original.nextProbeFileTime&&delayed.eventFileTime==eventTime,"repeated begin cannot restart fast window or physical deadline");
    need(!delayed.observes(at(55000),true)&&!delayed.allows(at(60000),true),"late start never extends physical end");
    WakeWatch cancelled;cancelled.eventFileTime=eventTime;
    need(!cancelled.beginObservation(at(4900),false)&&cancelled.observationStartFileTime==0,"revoked begin changes no clock");
    need(!cancelled.beginObservation(eventTime-1,true)&&cancelled.observationStartFileTime==0,"future physical wake cannot start scheduling");
    need(!cancelled.beginObservation(at(55000),true)&&cancelled.observationStartFileTime==0,"expired begin rejected");
    need(cancelled.reserveProbe(eventTime,true)&&!cancelled.beginObservation(at(5000),true),"cannot restart a watcher already used");
    WakeWatch tail;tail.eventFileTime=eventTime;
    need(tail.beginObservation(at(54999),true)&&tail.reserveProbe(at(54999),true),"late admission limited by remaining physical observation");
    need(tail.delayMs(at(54999))==1&&tail.dueFileTime()==at(55000)&&!tail.probeDue(at(55000)),"fast and settling windows clamp to physical end");
    WakeWatch w;w.eventFileTime=eventTime;need(w.beginObservation(at(4900),true),"start field-shape synthetic schedule");
    U64 previous=0;unsigned settleReads=0;
    for(auto now=at(4900);w.observes(now,true);now=w.dueFileTime()){
        need(w.probeDue(now)&&w.reserveProbe(now,true),"delayed schedule exact production due and reservation");
        if(previous)need(now>previous,"delayed schedule makes progress without spin");
        if(!w.fastWindow(now)&&!w.precisionWindow(now)&&w.settling(now))++settleReads;
        previous=now;
    }
    need(w.probes==204&&w.fastProbes==104&&w.precisionProbes==104&&w.settleFastProbes==70&&settleReads==100,
        "late observer uses remaining 104 precision + 70 moderate + 30 slow = 204 bounded probes");
    WakeWatch hinted;hinted.eventFileTime=eventTime;need(hinted.beginObservation(at(4900),true),"late hinted start");
    for(U64 ms=4900;ms<55000;ms+=10){
        need(hinted.noteHint(at(ms),true),"delayed hint storm stays within physical proof");
        if(hinted.probeDue(at(ms)))need(hinted.reserveProbe(at(ms),true),"delayed storm reserve");
    }
    need(hinted.probes<=320&&hinted.fastProbes==240&&!hinted.noteHint(at(55000),true),"late hint storm caps unchanged and not renewable");
    need(hinted.observationStartFileTime==at(4900)&&hinted.eventFileTime==eventTime,"hints do not change either clock");
}

void lateLossLatencyEnvelopeChecks() {
    // Independent loss times exercise the actual scheduler, including the
    // late-start G3E ordering. They are not observed game transition times.
    for(const U64 start:{U64{0},U64{352},U64{834},U64{4900},U64{8000}})for(const U64 lossOffset:{U64{2101},U64{4001},U64{7001},U64{10001},U64{12833},U64{17567},U64{19999}}){
        WakeWatch w;w.eventFileTime=eventTime;
        need(w.beginObservation(at(start),true),"latency observer admits once");
        U64 detected=UINT64_MAX;
        for(auto now=at(start);w.observes(now,true);now=w.dueFileTime()){
            need(w.probeDue(now)&&w.reserveProbe(now,true),"latency consumes real due and probe budget");
            if(now>=at(start+lossOffset)){detected=now;break;}
        }
        need(detected!=UINT64_MAX&&detected-at(start+lossOffset)<=250*ticksPerMs,
            "no-hint loss in first 20s detected within 250ms");
        need(w.eventFileTime==eventTime&&w.observationStartFileTime==at(start)&&w.probes<=320&&w.fastProbes<=240,
            "faster no-hint detection does not change original proof or counters");
    }
    WakeWatch late;late.eventFileTime=eventTime;
    need(late.beginObservation(at(54000),true),"very late observer admits within original endpoint");
    for(auto now=at(54000);late.observes(now,true);now=late.dueFileTime())
        need(late.probeDue(now)&&late.reserveProbe(now,true)&&now<at(55000),"very late cadence stays inside physical 55s");
    need(!late.observes(at(55000),true)&&!late.allows(at(60000),true),"moderate cadence cannot extend physical lifetime");
}
void lateHintBudgetChecks() {
    // A late burst must not spend the entire total budget before physical55s.
    // This caught the naive 20s250ms extension, despite its quiet/storm passes.
    for(const U64 start:{U64{0},U64{4900}})for(const unsigned pattern:{0u,1u}){
        WakeWatch w;w.eventFileTime=eventTime;
        need(w.beginObservation(at(start),true),"late-burst observer begins");
        for(U64 ms=start;ms<55000;ms+=10){
            need(w.observes(at(ms),true),"late hint bursts cannot exhaust observation before physical endpoint");
            const bool hint=pattern==0?ms>=start+20000:
                ((ms>=start+5000&&ms<start+7000)||(ms>=start+25000&&ms<start+35000));
            if(hint)need(w.noteHint(at(ms),true),"late related hints admitted without refund");
            if(w.probeDue(at(ms)))need(w.reserveProbe(at(ms),true),"late burst actual scheduled read");
        }
        need(w.probes<=scheduledProbeEnvelope&&w.settleFastProbes<=240&&w.fastProbes<=240-w.settleFastProbes&&
            w.precisionProbes<=w.fastProbes,"mixed cadence respects precision-derived envelope and original shared cap");
        need(!w.observes(at(55000),true)&&!w.allows(at(60000),true),"late burst retains physical endpoints");
    }
}
void fullWindowNoHintChecks() {
    struct Case {U64 start,loss;};
    const Case cases[]={{0,21000},{0,35000},{0,52000},{0,54000},
        {352,21000},{352,52000},{834,35000},{834,54000},
        {4900,21000},{4900,35000},{4900,52000},{8000,29000},{8000,52000},{20000,52000},{48000,52000},{52000,54000}};
    for(const auto& item:cases){WakeWatch watch;watch.eventFileTime=eventTime;
        need(watch.beginObservation(at(item.start),true),"no-hint observer admitted");U64 detected=UINT64_MAX,last=0;
        for(auto now=at(item.start);watch.observes(now,true);now=watch.dueFileTime()){
            need(watch.probeDue(now)&&watch.reserveProbe(now,true),"no-hint real scheduler sample");
            if(last)need(now-last<=1000*ticksPerMs,"all admitted no-hint sample gaps at most 1s");
            need(now<at(55000),"no-hint sample never extends physical endpoint");
            if(now>=at(item.loss)){detected=now;break;}last=now;
        }
        need(detected!=UINT64_MAX&&detected-at(item.loss)<=1000*ticksPerMs,"late no-hint loss detected within 1s");
        need(watch.eventFileTime==eventTime&&watch.probes<=320&&watch.fastProbes<=240,"late loss does not change proof or budgets");
        need(!watch.observes(at(55000),true)&&!watch.allows(at(60000),true)&&watch.delayMs(at(55000))==10000,
            "physical 55/60s and inactive 10s boundaries retained");
    }
}
bool sameWatch(const WakeWatch& a,const WakeWatch& b) {
    return a.eventFileTime==b.eventFileTime&&a.observationStartFileTime==b.observationStartFileTime&&
        a.nextProbeFileTime==b.nextProbeFileTime&&a.hintUntilFileTime==b.hintUntilFileTime&&
        a.lastActivityFileTime==b.lastActivityFileTime&&a.lastProbeFileTime==b.lastProbeFileTime&&
        a.probes==b.probes&&a.fastProbes==b.fastProbes&&a.settleFastProbes==b.settleFastProbes&&
        a.precisionProbes==b.precisionProbes;
}
void precisionLatencyChecks() {
    // Loss is independent of hints. A 173ms hint stream is deliberately out of
    // phase with both 25ms and 50ms reads; exact FILETIME due times have no OS cost.
    for(const U64 start:{U64{0},U64{352},U64{834},U64{4900},U64{8000}})
    for(const U64 lossMs:{U64{4000},U64{5140},U64{5600},U64{5870},U64{5920},U64{6900}})
    for(const U64 phase:{U64{0},U64{1},U64{7}*ticksPerMs,U64{24}*ticksPerMs+1})
    for(const bool hinted:{false,true}) {
        const U64 loss=at(lossMs)+phase;
        if(loss<at(start))continue; // An observer cannot detect before admission.
        try {
            WakeWatch w;w.eventFileTime=eventTime;
            need(w.beginObservation(at(start),true),"precision latency observer begins");
            U64 detected=UINT64_MAX,nextHint=at(start),previous=0;
            for(auto now=at(start);w.observes(now,true);) {
                if(hinted&&now==nextHint) {
                    need(w.noteHint(now,true),"independent related hint accepted");
                    nextHint=WakeWatch::addMs(nextHint,173);
                }
                if(w.probeDue(now)) {
                    need(w.reserveProbe(now,true),"precision latency consumes actual due and shared budget");
                    if(previous)need(now>previous,"precision latency scheduler makes progress");
                    previous=now;
                    if(now>=loss){detected=now;break;}
                }
                const auto due=w.dueFileTime();
                const auto next=hinted&&nextHint<due?nextHint:due;
                need(next>now,"hint/read event loop never spins");now=next;
            }
            need(detected!=UINT64_MAX&&detected-loss<=25*ticksPerMs,
                "event+4/5.14/5.6/5.87/5.92/6.9s phased loss must be detected within 25ms");
            const auto latency=detected-loss;
            if(latency>maximumPrecisionLatencyTicks)maximumPrecisionLatencyTicks=latency;
            ++precisionLatencyCases;
            need(w.precisionProbes>0&&w.precisionProbes<=w.fastProbes&&
                w.fastProbes+w.settleFastProbes<=240&&w.probes<=320,"precision latency uses original counters");
            need(w.eventFileTime==eventTime&&w.observationStartFileTime==at(start)&&
                !w.observes(at(55000),true)&&!w.allows(at(60000),true),"latency cannot move physical authority");
        } catch(const std::exception& e) {
            throw std::runtime_error("precision latency startMs="+std::to_string(start)+" lossMs="+
                std::to_string(lossMs)+" phaseTicks="+std::to_string(phase)+" hint="+
                std::to_string(hinted)+": "+e.what());
        }
    }
    need(precisionLatencyCases==184,"bounded precision phase matrix covers every admitted case");
}
void precisionBoundaryChecks() {
    WakeWatch unstarted;unstarted.eventFileTime=eventTime;
    need(!unstarted.precisionWindow(at(4000))&&unstarted.periodMs(at(4000))==10000,
        "physical event alone cannot activate precision");
    need(unstarted.noteHint(at(4000),true)&&!unstarted.precisionWindow(at(4000))&&
        unstarted.periodMs(at(4000))==50,"hint cannot substitute for observer admission");
    for(const U64 start:{U64{0},U64{352},U64{834},U64{4900},U64{8000}}) {
        WakeWatch w;w.eventFileTime=eventTime;
        need(w.beginObservation(at(start),true),"event-anchor delayed observer begins");
        need(w.dueFileTime()==at(start)&&w.probeDue(at(start))&&w.delayMs(at(start))==1,
            "first due is actual observer start, including starts after precision entry");
        for(const auto now:{at(4000)-1,at(4000),at(7500)-1,at(7500)}) {
            const bool expected=now>=at(start)&&now>=at(4000)&&now<at(7500);
            need(w.precisionWindow(now)==expected,"precision [event+4s,event+7.5s) independent of startup delay");
            if(expected)need(w.periodMs(now)==25,"precision wins over initial fast and hint cadence");
        }
        need(!w.precisionWindow(at(start)-1),"precision never precedes admitted observation");
        const auto before=w;
        need(!w.beginObservation(at(start+1),true)&&sameWatch(w,before),"repeated begin cannot refresh any window or budget");
        if(start==8000) {
            need(w.periodMs(at(8000))==50&&w.periodMs(at(10000)-1)==50&&w.periodMs(at(10000))==250,
                "post-window late start retains its original 2s 50ms stage");
            need(w.reserveProbe(at(8000),true)&&w.precisionProbes==0,"late first read is not precision");
        }
    }
    for(const U64 start:{U64{0},U64{352},U64{834}}) {
        WakeWatch w;w.eventFileTime=eventTime;
        need(w.beginObservation(at(start),true)&&w.reserveProbe(at(3999),true),"pending moderate read before precision");
        need(w.nextProbeFileTime==at(4249)&&w.dueFileTime()==at(4000)&&w.delayMs(at(3999))==1,
            "crossing 250ms wait clamps to physical precision entry");
        const auto before=w;
        need(!w.probeDue(at(4000)-1)&&w.delayMs(at(4000)-1)==1&&sameWatch(w,before),
            "clamped entry uses positive ceil without consuming or changing proof");
        need(w.probeDue(at(4000))&&w.reserveProbe(at(4000),true)&&w.nextProbeFileTime==at(4025)&&
            w.precisionProbes==1,"entry read spends one precision and schedules 25ms");
    }
    WakeWatch boundary;boundary.eventFileTime=eventTime;
    need(boundary.beginObservation(eventTime,true)&&boundary.reserveProbe(at(7500)-1,true),"last precision tick is admitted");
    need(boundary.precisionProbes==1&&boundary.periodMs(at(7500))==250,"precision exit restores original moderate period");
    need(boundary.reserveProbe(at(7500),true)&&boundary.precisionProbes==1&&boundary.settleFastProbes==1&&
        boundary.nextProbeFileTime==at(7750),"exit read is moderate and cannot renew precision");
    WakeWatch hinted;hinted.eventFileTime=eventTime;
    need(hinted.beginObservation(eventTime,true)&&hinted.noteHint(at(7499),true),"hint overlaps precision exit");
    need(hinted.periodMs(at(7500)-1)==25&&hinted.periodMs(at(7500))==50&&
        !hinted.precisionWindow(at(7500)),"precision exit restores hint's original 50ms fallback");
}
void precisionBudgetChecks() {
    WakeWatch w;w.eventFileTime=eventTime;
    need(w.beginObservation(eventTime,true),"shared precision budget fixture starts");
    w.fastProbes=w.precisionProbes=139;w.settleFastProbes=100;w.probes=239;
    need(w.subsecondCapacity()&&w.periodMs(at(4500))==25&&w.reserveProbe(at(4500),true),"last shared slot may be precision");
    need(w.precisionProbes==140&&w.fastProbes==140&&w.settleFastProbes==100&&w.probes==240&&
        !w.subsecondCapacity()&&w.periodMs(at(4500))==1000&&w.nextProbeFileTime==at(5500),
        "precision is fast subset, never an extra 140-read pool, and cap applies immediately");
    need(w.noteHint(at(4501),true)&&w.periodMs(at(4501))==1000&&w.nextProbeFileTime==at(5500),
        "hint cannot refund precision or moderate spending");
    need(w.reserveProbe(at(5500),true)&&w.precisionProbes==140&&w.fastProbes==140&&w.probes==241,
        "capped precision read consumes only original total allowance");
    w.probes=320;const auto exhausted=w;
    need(!w.noteHint(at(5501),true)&&!w.reserveProbe(at(5501),true)&&sameWatch(w,exhausted),
        "total exhaustion during precision leaves every field untouched");
    WakeWatch clock;clock.eventFileTime=eventTime;
    need(clock.beginObservation(at(352),true)&&clock.reserveProbe(at(4500),true)&&clock.noteHint(at(4501),true),
        "precision clockback fixture reaches live window");
    const auto before=clock;
    need(!clock.noteHint(at(4500),true)&&!clock.reserveProbe(at(4500),true)&&!clock.probeDue(at(4500))&&
        clock.delayMs(at(4500))==10000&&sameWatch(clock,before),"clockback during precision cannot read, spin or mutate state");
    need(!clock.noteHint(at(4502),false)&&!clock.reserveProbe(at(4502),false)&&!clock.allows(at(4502),false)&&
        sameWatch(clock,before),"cancellation wins over live precision and preserves state");
}
void precisionHintChecks() {
    for(const U64 start:{U64{0},U64{352},U64{834},U64{4900},U64{8000}}) {
        WakeWatch w;w.eventFileTime=eventTime;
        need(w.beginObservation(at(start),true),"fixed-window hint storm starts");
        for(U64 ms=start;ms<11000;ms+=5) {
            const auto now=at(ms);
            const auto counts=w;
            need(w.noteHint(now,true)&&w.probes==counts.probes&&w.fastProbes==counts.fastProbes&&
                w.settleFastProbes==counts.settleFastProbes&&w.precisionProbes==counts.precisionProbes,
                "hint storm cannot consume or refund any read counter");
            const auto hinted=w;
            need(w.noteHint(now,true)&&sameWatch(w,hinted),"duplicate hints preserve all scheduling fields");
            need(w.eventFileTime==eventTime&&w.observationStartFileTime==at(start)&&
                w.precisionWindow(now)==(ms>=4000&&ms<7500),"hint storm cannot move either precision endpoint");
            if(w.probeDue(now)) {
                const auto precision=w.precisionProbes;
                const bool spend=w.precisionWindow(now)&&w.subsecondCapacity();
                need(w.reserveProbe(now,true)&&w.precisionProbes==precision+(spend?1u:0u),
                    "storm counts precision only when original shared slot is available");
            }
            need(w.precisionProbes<=w.fastProbes&&w.fastProbes+w.settleFastProbes<=240&&w.probes<=320,
                "storm preserves original hard budgets throughout");
        }
        const auto precision=w.precisionProbes;
        need(w.noteHint(at(12000),true)&&!w.precisionWindow(at(12000))&&w.periodMs(at(12000))!=25&&
            w.precisionProbes==precision,"late hints cannot reopen expired precision");
        const auto before=w;
        need(!w.beginObservation(at(12001),true)&&sameWatch(w,before),"storm cannot restart observer");
        need(!w.noteHint(at(55000),true)&&!w.reserveProbe(at(55000),true)&&sameWatch(w,before)&&
            !w.observes(at(55000),true)&&!w.allows(at(60000),true),"storm cannot renew physical lifetime");
        if(start==8000)need(w.precisionProbes==0,"post-7.5s observer never invents precision samples");
    }
}

void overflowChecks() {
    WakeWatch w;w.eventFileTime=UINT64_MAX-100;
    need(w.noteHint(UINT64_MAX-99,true)&&w.hintUntilFileTime==UINT64_MAX,"hint addition saturates");
    need(w.reserveProbe(UINT64_MAX-99,true)&&w.nextProbeFileTime==UINT64_MAX,"due addition saturates");
    need(w.delayMs(UINT64_MAX-1)==1&&!w.probeDue(UINT64_MAX-1),"saturated due ceil no spin");
    need(!w.observes(UINT64_MAX,true)&&!w.noteHint(UINT64_MAX,true),"saturated endpoint is exclusive");
    need(!w.noteHint(0,true)&&!w.reserveProbe(0,true),"wrapped or backward timestamp refused");
    need(WakeWatch::addMs(UINT64_MAX-1,UINT64_MAX)==UINT64_MAX,"multiplication overflow avoided");
    WakeWatch late;late.eventFileTime=eventTime;
    need(late.reserveProbe(at(17000),true)&&late.fastProbes==0&&late.delayMs(at(17000))==10000,
        "late watcher does not restart physical fast window");
}
}
int main() {
    try {
        checksFor("proof",proofChecks);checksFor("original-boundaries",boundaryChecks);
        checksFor("original-cadence-boundaries",cadenceBoundaryChecks);checksFor("original-budget",budgetChecks);
        checksFor("overflow",overflowChecks);checksFor("precision-latency",precisionLatencyChecks);
        checksFor("precision-boundaries",precisionBoundaryChecks);checksFor("precision-budget-clockback-cancel",precisionBudgetChecks);
        checksFor("precision-hint-storm",precisionHintChecks);checksFor("original-delayed-observer",observationStartChecks);
        checksFor("first20s-nohint-250ms",lateLossLatencyEnvelopeChecks);checksFor("late-hint-budget",lateHintBudgetChecks);
        checksFor("full-window-nohint-1s",fullWindowNoHintChecks);
        const auto quiet=quietSchedule(),storm=stormSchedule();
        std::cout<<"{\"passed\":true,\"checks\":"<<checks
            <<",\"quietProbes\":"<<quiet<<",\"stormProbes\":"<<storm
            <<",\"precisionLatencyCases\":"<<precisionLatencyCases<<",\"maximumPrecisionLatencyTicks\":"<<maximumPrecisionLatencyTicks
            <<",\"precisionStartMs\":"<<WakeWatch::precisionStartMs<<",\"precisionEndMs\":"<<WakeWatch::precisionEndMs
            <<",\"precisionPollMs\":"<<WakeWatch::precisionPollMs<<",\"scheduledProbeEnvelope\":"<<scheduledProbeEnvelope
            <<",\"fastCap\":"<<WakeWatch::maximumFastProbes<<",\"totalCap\":"<<WakeWatch::maximumProbes
            <<",\"timing\":\"synthetic FILETIME; production header; no field speed claim\",\"processActions\":0,\"sleepActions\":0}\n";
    } catch(const std::exception& e) {
        std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;
    }
}