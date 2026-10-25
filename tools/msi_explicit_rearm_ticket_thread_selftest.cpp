#include "../native/msi_explicit_rearm_ticket.h"
#include <atomic>
#include <iostream>
#include <thread>
using namespace ymcc::msi::rearm;
int main(){unsigned cases=0,failed=0;auto check=[&](bool ok){++cases;if(!ok)++failed;};
    Facts f;f.enabled=f.knownBoard=f.restored=f.powerReady=f.wanted=true;
    f.quarantined=f.journalActive=f.hostPresent=f.faultHold=f.transportFault=f.exitFence=false;f.power=7;f.persona="elite";
    auto off=[&](Ticket& t){t.setting(true,"elite",7);t.started(t.snapshot().sequence);t.setting(false,"disabled",7);};
    auto pending=[&](Ticket& t){off(t);t.released(true,true,true,true,true,7);t.setting(true,"elite",7);};
    for(bool releaseFirst:{false,true}){Ticket t;off(t);std::atomic<bool> first{false};
        std::thread a([&](){if(releaseFirst){t.released(true,true,true,true,true,7);first.store(true,std::memory_order_release);}else{while(!first.load(std::memory_order_acquire))std::this_thread::yield();t.released(true,true,true,true,true,7);}});
        std::thread b([&](){if(!releaseFirst){t.setting(true,"elite",7);first.store(true,std::memory_order_release);}else{while(!first.load(std::memory_order_acquire))std::this_thread::yield();t.setting(true,"elite",7);}});
        a.join();b.join();check(t.take(f).preserved);check(!t.take(f).preserved);
    }
    for(unsigned i=0;i<128;i++){Ticket t;pending(t);std::atomic<bool> start{false};std::atomic<unsigned> winners{0};
        auto consume=[&](){while(!start.load(std::memory_order_acquire))std::this_thread::yield();if(t.take(f).preserved)++winners;};
        std::thread a(consume),b(consume);start.store(true,std::memory_order_release);a.join();b.join();check(winners==1);check(!t.take(f).preserved);
    }
    for(unsigned i=0;i<128;i++){Ticket t;pending(t);std::atomic<bool> start{false};Receipt r;
        std::thread a([&](){while(!start.load(std::memory_order_acquire))std::this_thread::yield();t.setting(false,"disabled",7);});
        std::thread b([&](){while(!start.load(std::memory_order_acquire))std::this_thread::yield();r=t.take(f);});
        start.store(true,std::memory_order_release);a.join();b.join();const auto s=t.snapshot();
        check(!s.wanted&&s.persona=="disabled");check(!t.take(f).preserved);
        check(!r.preserved||(s.consumedSequence==r.settingSequence&&r.settingSequence<s.sequence));
    }
    for(unsigned i=0;i<128;i++){Ticket t;t.setting(true,"elite",7);const auto owner=t.snapshot().sequence;
        std::atomic<bool> settingsDone{false};std::thread writer([&](){t.setting(false,"disabled",7);t.setting(true,"elite",7);settingsDone.store(true,std::memory_order_release);});
        while(!settingsDone.load(std::memory_order_acquire))std::this_thread::yield();
        check(t.started(owner));writer.join();t.released(true,true,true,true,true,7);
        check(t.snapshot().startedSequence==owner);check(t.take(f).preserved);check(!t.take(f).preserved);
    }
    std::cout<<"cases="<<cases<<" failed="<<failed<<" scope=concurrent-ticket-only no-controller-IO no-native-Host no-whole-BUS-proof\n";return failed?1:0;
}
