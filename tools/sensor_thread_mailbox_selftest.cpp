#include "../native/sensor_thread_mailbox.h"
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
using namespace ymcc::sensors;
using namespace std::chrono_literals;
int main() {
    unsigned cases=0,failed=0;
    auto check=[&](const char* name,bool ok){++cases;if(!ok)++failed;std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    using Result=ThreadMailbox::Result;
    ThreadMailbox mailbox;
    check("unstarted owner rejects work without a wait",!mailbox.submit(ThreadRequest::Start));
    check("open is explicit and cannot replace a live owner",mailbox.open()&&!mailbox.open());
    auto start=mailbox.submit(ThreadRequest::Start);
    check("first ticket is individually taken",mailbox.take()==start);
    ThreadMailbox::Handle stop;
    std::thread poster([&]{stop=mailbox.submit(ThreadRequest::Suspended);});poster.join();
    check("queued suspend remains visible during in-flight discovery",mailbox.stopRequested());
    check("in-flight request cannot be overwritten by another poster",!mailbox.take());
    mailbox.complete(start);
    check("only the completed start is acknowledged",mailbox.wait(start,0ms)==Result::Completed);
    check("suspend still owns a distinct queued ticket",mailbox.take()==stop&&stop->id>start->id);
    mailbox.complete(stop,false);
    check("failure is not a successful acknowledgement",mailbox.wait(stop,0ms)==Result::Failed);
    auto resume=mailbox.submit(ThreadRequest::Resume);
    mailbox.take();
    auto shutdown=mailbox.submit(ThreadRequest::Shutdown);
    check("shutdown closes admission and cancels active discovery",!mailbox.acceptsWork()&&mailbox.stopRequested()&&!mailbox.submit(ThreadRequest::Start));
    mailbox.complete(resume);
    check("previous completion cannot acknowledge shutdown",mailbox.take()==shutdown);
    mailbox.complete(shutdown);mailbox.close();
    check("shutdown owns its own completion even after worker exits",mailbox.wait(shutdown,0ms)==Result::Completed);
    check("startup failure before a later post never strands a waiter",!mailbox.submit(ThreadRequest::Start)&&mailbox.wait({},0ms)==Result::Unavailable);
    mailbox.open();auto a=mailbox.submit(ThreadRequest::Start);mailbox.take();auto b=mailbox.submit(ThreadRequest::Stop);
    mailbox.close();
    check("abnormal pump exit wakes both active and queued owners",mailbox.wait(a,0ms)==Result::Exited&&mailbox.wait(b,0ms)==Result::Exited);
    mailbox.open();auto slow=mailbox.submit(ThreadRequest::Start);mailbox.take();
    check("wait has a bounded deadline",mailbox.wait(slow,5ms)==Result::TimedOut);
    check("timeout quarantines rather than acknowledging live work",!mailbox.acceptsWork()&&!mailbox.submit(ThreadRequest::Resume)&&mailbox.stopRequested());
    auto terminal=mailbox.submit(ThreadRequest::Shutdown);
    check("terminal cleanup can still be queued after timeout",static_cast<bool>(terminal));
    mailbox.complete(slow);
    check("eventual completion stays attached to original ticket",mailbox.wait(slow,0ms)==Result::Completed&&mailbox.take()==terminal);
    mailbox.complete(terminal);mailbox.close();mailbox.open();
    auto fresh=mailbox.submit(ThreadRequest::Start);
    check("new owner cannot reuse an old ticket identity",fresh->id>terminal->id&&mailbox.wait(a,0ms)==Result::Exited);
    mailbox.take();mailbox.complete(fresh);
    std::atomic<unsigned> completed{0};
    std::thread worker([&]{while(completed<200){if(auto t=mailbox.take()){mailbox.complete(t);++completed;}else std::this_thread::yield();}});
    std::vector<std::thread> posters;
    std::atomic<unsigned> successes{0};
    for(unsigned i=0;i<4;++i) posters.emplace_back([&]{for(unsigned n=0;n<50;++n){auto t=mailbox.submit(ThreadRequest::Resubscribe);if(mailbox.wait(t,1000ms)==Result::Completed)++successes;}});
    for(auto& p:posters)p.join();worker.join();mailbox.close();
    check("200 concurrent requests have no lost or cross-acknowledged work",successes==200&&completed==200);
    std::cout<<"cases="<<cases<<" failed="<<failed<<"\n";return failed?3:0;
}
