#include "ps_endpoint_ownership.h"
#include <iostream>
using State=ymcc::psEndpointOwnership::State;
using Expected=ymcc::psEndpointOwnership::Expected;
using Json=nlohmann::json;
static int checks=0,failures=0;
static void check(bool ok,const char* name){++checks;if(!ok){++failures;std::cout<<"FAIL "<<name<<"\n";}}
static Expected expected(){return {"host-a","run-a","target-a","dualsense-edge",7};}
static Json receipt(unsigned long long generation=1){Json r={{"hostInstanceId","host-a"},{"runId","run-a"},{"epoch",7},{"targetId","target-a"}};r["psEndpointOwnership"]={{"schemaVersion",1},{"persona","dualsense-edge"},{"hostInstanceId","host-a"},{"runId","run-a"},{"epoch",7},{"targetId","target-a"},{"bindingGeneration",generation},{"source","sdk-instance-id-and-cm-subtree"},{"sdkRootInstanceId","SWD\\HIDMAESTRO_VID_054C_PID_0DF2\\A"},{"hidInstanceIds",Json::array({"HID\\HIDMAESTRO_VID_054C_PID_0DF2\\A"})},{"complete",true},{"reason","ready"}};return r;}
int main(){
 const Json hello={{"psEndpointOwnershipVersion",1}};
 {State s;s.hello(Json::object(),expected());check(!s.capable()&&!s.accept(receipt()),"old Host cannot manufacture ownership capability");}
 {State s;s.hello(hello,expected());check(s.accept(receipt())&&s.snapshot().valid,"current exact SDK-root receipt admitted");auto a=s.snapshot();check(s.accept(receipt())&&s.snapshot().revision==a.revision,"identical refresh does not retire a valid observation");s.invalidate("release-start");check(!s.snapshot().valid&&s.revision()>a.revision,"release revokes prior query revision");}
 {State s;s.hello(hello,expected());auto r=receipt();r["psEndpointOwnership"]["hidInstanceIds"].push_back("HID\\HIDMAESTRO_VID_054C_PID_0DF2\\B");check(s.accept(r),"multi-child target admitted through complete receipt");const auto rev=s.revision();auto& ids=r["psEndpointOwnership"]["hidInstanceIds"];std::swap(ids[0],ids[1]);ids[1]="hid\\hidmaestro_vid_054c_pid_0df2\\a";check(s.accept(r)&&s.revision()==rev,"same case-insensitive child set in another order is an unchanged binding");}
 for(const auto* field:{"hostInstanceId","runId","targetId","persona"}){State s;s.hello(hello,expected());auto r=receipt();r["psEndpointOwnership"][field]="foreign";check(!s.accept(r),"nested foreign tuple cannot bind target");}
 {State s;s.hello(hello,expected());auto r=receipt();r["runId"]="foreign";check(!s.accept(r),"outer foreign tuple cannot bind target");}
 {State s;s.hello(hello,expected());auto r=receipt();r["psEndpointOwnership"]["epoch"]=8;check(!s.accept(r),"foreign epoch cannot bind target");}
 {State s;s.hello(hello,expected());check(s.accept(receipt()),"initial target available before replacement");const auto first=s.snapshot();s.replacementRequested();check(!s.snapshot().valid&&s.revision()>first.revision,"replacement invalidates old target before command");check(!s.accept(receipt()),"same-generation acknowledgement cannot revive replaced target");auto newer=receipt(2);check(s.accept(newer),"new generation may reuse OS instance spelling");const auto second=s.snapshot();check(second.generation==2&&second.revision>first.revision,"replacement publishes its new binding generation");check(!s.accept(receipt()),"retired target acknowledgement cannot overwrite new target");}
 {State s;s.hello(hello,expected());check(s.accept(receipt()),"current target before SDK internal generation change");const auto before=s.revision();check(s.accept(receipt(2))&&s.revision()>before,"generation change retires observation even with identical IDs");}
 {State s;s.hello(hello,expected());s.accept(receipt());s.replacementRequested();auto pending=receipt(2);pending["psEndpointOwnership"]["complete"]=false;check(!s.accept(pending)&&s.snapshot().generation==2&&!s.snapshot().valid,"incomplete new target retains generation without false ownership");check(!s.accept(receipt()),"old target cannot return after incomplete replacement");check(s.accept(receipt(2)),"same new target may finish bounded read-only capture");}
 {State s;s.hello(hello,expected());auto r=receipt();auto& ids=r["psEndpointOwnership"]["hidInstanceIds"];ids=Json::array();for(int i=0;i<65;++i)ids.push_back("HID\\NODE"+std::to_string(i));check(s.accept(r),"bounded complete subtree can carry more than64 HID descendants");}
 {State s;s.hello(hello,expected());auto r=receipt();auto& ids=r["psEndpointOwnership"]["hidInstanceIds"];ids=Json::array();for(int i=0;i<129;++i)ids.push_back("HID\\NODE"+std::to_string(i));check(!s.accept(r),"HID evidence cannot exceed bounded producer node count");}
 {State s;s.hello(hello,expected());auto r=receipt();r["psEndpointOwnership"]["extra"]=std::string(9000,'x');check(!s.accept(r),"ownership evidence shares producer byte bound");}
 {State s;s.hello(hello,expected());auto r=receipt();r["psEndpointOwnership"]["hidInstanceIds"]=Json::array();check(!s.accept(r),"no HID descendant cannot claim ready ownership");}
 {State s;s.hello(hello,expected());auto r=receipt();r["psEndpointOwnership"]["bindingGeneration"]=1.5;check(!s.accept(r),"floating generation cannot bind target");}
 {State s;s.hello(hello,expected());auto r=receipt();r["psEndpointOwnership"]["bindingGeneration"]=0;check(!s.accept(r),"zero generation cannot bind target");}
 for(const auto* field:{"schemaVersion","source","complete","bindingGeneration","sdkRootInstanceId","hidInstanceIds"}){State s;s.hello(hello,expected());auto r=receipt();r["psEndpointOwnership"].erase(field);check(!s.accept(r),"missing authority field rejected");}
 {State s;s.hello(hello,expected());auto r=receipt();r["psEndpointOwnership"]["source"]="global-profile-marker";check(!s.accept(r),"profile marker is not SDK instance authority");}
 {State s;s.hello(hello,expected());auto r=receipt();r["psEndpointOwnership"]["complete"]="true";check(!s.accept(r),"string completeness cannot claim readiness");}
 {State s;s.hello(hello,expected());auto r=receipt();r["psEndpointOwnership"]["bindingGeneration"]=-1;check(!s.accept(r),"negative generation rejected");}
 {State s;s.hello(hello,expected());auto r=receipt();r["psEndpointOwnership"]["hidInstanceIds"].push_back("hid\\hidmaestro_vid_054c_pid_0df2\\a");check(!s.accept(r),"duplicate HID IDs rejected case-insensitively");}
 {State s;s.hello(hello,expected());auto r=receipt();r["psEndpointOwnership"]["sdkRootInstanceId"]=std::string("SWD\\A\0B",7);check(!s.accept(r),"embedded control byte cannot be an instance ID");}
 {State s;s.hello(hello,expected());s.accept(receipt());s.clear();check(!s.capable()&&!s.snapshot().valid&&!s.accept(receipt()),"Host clear retires capability and previous target");}
 {State s;auto other=expected();other.persona="steamdeck";s.hello(hello,other);check(!s.capable()&&!s.accept(receipt()),"PS ownership does not become a common whole-controller gate");}
 std::cout<<"CHECKS="<<checks<<" FAILED="<<failures<<"\n";return failures?1:0;
}
