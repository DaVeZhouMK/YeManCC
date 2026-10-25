#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../native/msi_key_event_windows.h"
#include "../native/msi_rear_audit.h"
#include <iostream>
struct FakeReader {int32_t beginStatus=0,waitStatus=0;bool object=true;unsigned begins=0,waits=0,budget=0;int32_t begin(){++begins;return beginStatus;}std::pair<int32_t,bool> wait(unsigned ms){++waits;budget=ms;return {waitStatus,object};}};
int main(){unsigned cases=0,failed=0;auto check=[&](const char* n,bool ok){++cases;failed+=!ok;std::cout<<(ok?"PASS ":"FAIL ")<<n<<"\n";};using namespace ymcc::msi;
 FakeReader present;auto p=probeEventClass(present,[]{return true;});check("class requires completed S_OK object, not request acceptance",p.state==EventClassState::Present&&present.waits==1&&present.budget==500);
 FakeReader missing;missing.waitStatus=int32_t(0x80041010u);auto m=probeEventClass(missing,[]{return true;});check("actual invalid-class code never grants source admission",m.state==EventClassState::Absent&&!classObjectAdmitted(m.waitStatus,m.classObject));
 FakeReader notFound;notFound.waitStatus=int32_t(0x80041002u);check("GetObject not-found also means absent",probeEventClass(notFound,[]{return true;}).state==EventClassState::Absent);
 FakeReader timed;timed.waitStatus=0x40004;check("positive WBEM timeout is UNKNOWN, not success",probeEventClass(timed,[]{return true;}).state==EventClassState::Unknown);
 FakeReader falseResult;falseResult.waitStatus=1;check("S_FALSE cannot masquerade as completed class proof",probeEventClass(falseResult,[]{return true;}).state==EventClassState::Unknown);
 FakeReader acceptedOnly;acceptedOnly.object=false;check("accepted call without class object stays unknown",probeEventClass(acceptedOnly,[]{return true;}).state==EventClassState::Unknown);
 FakeReader denied;denied.beginStatus=int32_t(E_ACCESSDENIED);auto d=probeEventClass(denied,[]{return true;});check("permissions failure stays unknown and does not wait",d.state==EventClassState::Unknown&&denied.waits==0);
 FakeReader stale;auto c=probeEventClass(stale,[]{return false;});check("retired generation starts no WMI lookup",c.state==EventClassState::Cancelled&&stale.begins==0);
 FakeReader late;unsigned current=0;check("late class completion does not revive retired generation",probeEventClass(late,[&]{return ++current==1;}).state==EventClassState::Cancelled);
 WindowsEventClassReader noService(nullptr);auto realAdapter=probeEventClass(noService,[]{return true;});check("actual SDK adapter refuses null service without RPC",realAdapter.state==EventClassState::Unknown&&realAdapter.beginStatus==E_POINTER);
 check("definite absence refuses false async authority",!maySubscribeEventClass(EventClassState::Absent));
 check("cancelled source issues no subscription",!maySubscribeEventClass(EventClassState::Cancelled));
 check("unknown metadata does not disable working legacy listener",maySubscribeEventClass(EventClassState::Unknown));
 check("present class still needs actual async subscription",maySubscribeEventClass(EventClassState::Present));
 RearAudit audit;audit.physical(9,1,1,1);audit.submit(1,true);check("disabled diagnostics collect nothing",audit.take().samples==0);
 audit.enable(true);audit.physical(9,0,0,0);audit.physical(9,1,1,1);audit.stage(9,1);audit.submit(0,true);audit.physical(9,0,0,0);audit.physical(9,2,2,2);audit.submit(2,false);audit.physical(9,0,0,0);auto w=audit.take();
 check("one M1/M2 press release survives low-rate aggregation",w.press[0]==1&&w.press[1]==1&&w.release[0]==1&&w.release[1]==1);
 check("unassigned 12/13 changes are observed not mapped",w.extraPress[0]==1&&w.extraPress[1]==1&&w.extraRelease[0]==1&&w.extraRelease[1]==1&&w.extraPress[2]==0);
 check("semantic neutral ACK does not invent rear delivery",w.accepted==1&&w.acceptedRear[0]==0&&w.acceptedRear[1]==0);
 check("rejected rear attempt is not accepted",w.attemptRear[1]==1&&w.acceptedRear[1]==0&&w.rejected==1);
 check("held rear records its actual producer gate",w.heldRearStage[9]==1);
 check("single-generation source is not mixed",w.firstGeneration==9&&w.lastGeneration==9&&!w.mixedGeneration);
 audit.physical(9,1,1,0);audit.physical(10,1,0,4);auto mixed=audit.take();check("generation change stays explicit",mixed.mixedGeneration&&mixed.firstGeneration==9&&mixed.lastGeneration==10);
 check("held-at-bind does not fabricate a fresh press",mixed.press[0]==1&&mixed.extraPress[2]==0&&mixed.physicalDown[0]==2&&mixed.logicalDown[0]==1);
 audit.physical(10,0,0,0);check("release edge remains tracked across windows",audit.take().release[0]==1);
 audit.enable(false);audit.enable(true);audit.physical(11,1,0,0);auto held=audit.take();check("enable during held key is not a press receipt",held.press[0]==0&&held.physicalDown[0]==1&&held.logicalDown[0]==0);
 audit.stage(100,3);check("unknown gate index cannot corrupt counters",audit.take().heldRearStage[0]==0);
 audit.enable(false);check("disabled window reports disabled honestly",!audit.take().enabled);
 std::cout<<"cases="<<cases<<" failed="<<failed<<"\n";return failed?3:0;}
