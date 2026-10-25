#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <iostream>
#include <cstdint>
#include "json.hpp"
using json=nlohmann::json;
static std::string W2U(const std::wstring& value){return std::string(value.begin(),value.end());}
#include "msi_foundation_process_fixture.h"
static SERVICE_STATUS_PROCESS serviceStatus{};
static unsigned statusCalls=0;static unsigned deniedStatus=0;
static std::function<void(unsigned)> onStatus;
static BOOL memoryStatus(SC_HANDLE,SC_STATUS_TYPE type,LPBYTE data,DWORD size,LPDWORD bytes){
    ++statusCalls;if(onStatus)onStatus(statusCalls);
    if(type!=SC_STATUS_PROCESS_INFO||size!=sizeof(SERVICE_STATUS_PROCESS))FoundationMemory::unsafeRights=true;
    if(deniedStatus==statusCalls){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    memcpy(data,&serviceStatus,sizeof(serviceStatus));*bytes=sizeof(serviceStatus);return TRUE;
}
#define QueryServiceStatusEx memoryStatus
#define GetTickCount64() FoundationMemory::clock()
/* PRODUCTION_FOUNDATION_OBSERVATION */
static int cases=0,failures=0;
static void check(bool pass,const char* text){++cases;if(!pass)++failures;std::cout<<(pass?"PASS ":"FAIL ")<<text<<"\n";}
static void reset(){FoundationMemory::reset();serviceStatus={};serviceStatus.dwProcessId=42;serviceStatus.dwCurrentState=SERVICE_RUNNING;serviceStatus.dwServiceType=SERVICE_WIN32_OWN_PROCESS;statusCalls=deniedStatus=0;onStatus={};FoundationMemory::add(42,4,100);}
static json observe(){const auto anchor=serviceStatus;return msiFoundationObserveProcesses(reinterpret_cast<SC_HANDLE>(1),anchor,true);}
static bool bounded(const json& r){return FoundationMemory::opens==FoundationMemory::closes&&!FoundationMemory::unsafeRights&&FoundationMemory::snapshots<=2&&r["children"].size()<=16;}
static bool unknownAbsence(const json& r){return r["complete"]==false&&r["incomplete"]==true&&r["childrenAbsenceKnown"]==false;}
int main(){
 reset();FoundationMemory::add(100,42,200);FoundationMemory::add(101,100,300);FoundationMemory::add(102,1,400);auto r=observe();
 check(r["complete"]==true&&r["children"].size()==1&&r["children"][0]["pid"]==100,"only direct parent PID candidates, no grandchildren");
 check(r["parentProcess"]["pid"]==42&&r["parentProcess"]["creationTime100ns"]==100&&r["parentProcess"]["imagePath"]=="C:\\MSI\\process-42.exe","Foundation original-handle identity retained");
 check(r["children"][0]["parentPid"]==42&&r["children"][0]["creationTime100ns"]==200&&r["children"][0]["imagePath"]=="C:\\MSI\\process-100.exe","child creation time and image belong to original handle");
 check(r["children"][0]["heldHandleAndSecondSnapshotConsistent"]==true&&r["heldServiceHandleAndStatusReadbacksConsistent"]==true&&statusCalls==2&&FoundationMemory::snapshots==2,"SCM readbacks and second snapshot bind held handles");
 check(r["parentPidOnlyMatch"]==true&&r["inputDependencyProven"]==false&&r["stopAuthorization"]==false&&r["atomicSnapshot"]==false,"static relationship never proves dependency or grants control");
 check(bounded(r),"query/synchronize only, no inherited handles, bounded and balanced");
 reset();r=observe();check(r["complete"]==true&&r["childrenAbsenceKnown"]==true&&FoundationMemory::snapshots==1,"empty children requires complete scan and live stable Foundation");
 reset();serviceStatus.dwProcessId=0;serviceStatus.dwCurrentState=SERVICE_STOPPED;r=observe();check(unknownAbsence(r)&&FoundationMemory::processOpens==0&&FoundationMemory::snapshots==0,"stopped PID zero is unavailable, not no children");
 reset();serviceStatus.dwCurrentState=SERVICE_START_PENDING;r=observe();check(unknownAbsence(r)&&FoundationMemory::processOpens==0,"unstable service state refuses process attribution");
 reset();r=msiFoundationObserveProcesses(nullptr,{},false,ERROR_ACCESS_DENIED);check(unknownAbsence(r)&&r["serviceStatusWin32Error"]==ERROR_ACCESS_DENIED&&FoundationMemory::snapshots==0,"missing service status never enumerates processes");
 reset();FoundationMemory::processes[42]->denied=true;r=observe();check(unknownAbsence(r)&&r["parentProcess"]["openWin32Error"]==ERROR_ACCESS_DENIED&&FoundationMemory::snapshots==0,"parent access denial is not empty success");
 reset();FoundationMemory::processes[42]->timesDenied=true;r=observe();check(unknownAbsence(r)&&r["parentProcess"]["creationTimeRead"]==false,"parent time query failure forbids binding");
 reset();FoundationMemory::processes[42]->pathDenied=true;r=observe();check(unknownAbsence(r)&&r["parentProcess"]["imagePathRead"]==false,"parent path query failure retained");
 reset();FoundationMemory::processes[42]->waitFailed=true;r=observe();check(unknownAbsence(r)&&r["parentProcess"]["handleAliveKnown"]==false,"unknown original handle liveness is not alive");
 reset();deniedStatus=1;r=observe();check(unknownAbsence(r)&&r["serviceAfterParentOpen"]["win32Error"]==ERROR_ACCESS_DENIED&&FoundationMemory::snapshots==0,"SCM anchor denial before enumeration refuses attribution");
 reset();onStatus=[](unsigned n){if(n==1)serviceStatus.dwProcessId=43;};r=observe();check(unknownAbsence(r)&&FoundationMemory::snapshots==0,"service PID changed after opening parent rejected");
 reset();FoundationMemory::add(100,42,200);onStatus=[](unsigned n){if(n==2)serviceStatus.dwCurrentState=SERVICE_STOP_PENDING;};r=observe();check(unknownAbsence(r)&&r["children"][0]["heldHandleAndSecondSnapshotConsistent"]==false,"service state changed after enumeration invalidates association");
 reset();FoundationMemory::add(100,42,200);deniedStatus=2;r=observe();check(unknownAbsence(r)&&r["serviceAfterEnumeration"]["stateRead"]==false,"final SCM failure cannot certify stable PID");
 reset();FoundationMemory::add(100,42,200);FoundationMemory::onSnapshot=[](unsigned n){if(n==2){FoundationMemory::processes[42]->alive=false;FoundationMemory::add(42,4,900);}};r=observe();check(unknownAbsence(r)&&r["parentProcess"]["originalHandleAlive"]==false&&r["children"][0]["heldHandleAndSecondSnapshotConsistent"]==false,"same numeric parent PID reused, original handle exit rejects");
 reset();FoundationMemory::add(100,42,50);r=observe();check(unknownAbsence(r)&&r["children"][0]["parentLifetimeCompatible"]==false,"historical child predates current Foundation PID lifetime");
 reset();FoundationMemory::add(100,42,200);FoundationMemory::onOpen=[](DWORD pid){if(pid==100){FoundationMemory::processes[100]->alive=false;FoundationMemory::add(100,999,400);}};r=observe();check(unknownAbsence(r)&&r["children"][0]["snapshotParentPidRevalidated"]==false,"child PID reused before OpenProcess must match fresh snapshot");
 reset();FoundationMemory::add(100,42,200);FoundationMemory::onOpen=[](DWORD pid){if(pid==100){FoundationMemory::processes[100]->alive=false;FoundationMemory::add(100,42,400);}};r=observe();check(r["complete"]==true&&r["children"][0]["creationTime100ns"]==400&&r["children"][0]["heldHandleAndSecondSnapshotConsistent"]==true,"same-parent PID reuse before open reports only current held instance");check(r["firstSnapshotProcessIdentityKnown"]==false&&r["children"][0]["firstSnapshotProcessIdentityKnown"]==false&&r["noAbaProven"]==false&&r["initialServiceStatusToOpenProcessIdentityKnown"]==false&&r["stopAuthorization"]==false&&!r.contains("associationStable")&&!r.contains("serviceIdentityStable"),"same-parent reuse never identifies first snapshot generation or proves no ABA");
 reset();FoundationMemory::add(100,42,200);FoundationMemory::onSnapshot=[](unsigned n){if(n==2){FoundationMemory::processes[100]->alive=false;FoundationMemory::add(100,42,400);}};r=observe();check(unknownAbsence(r)&&r["children"][0]["originalHandleAlive"]==false&&r["children"][0]["heldHandleAndSecondSnapshotConsistent"]==false,"child PID reused after capture cannot replace held child identity");
 reset();FoundationMemory::add(100,42,200)->denied=true;r=observe();check(unknownAbsence(r)&&r["children"].size()==1&&r["children"][0]["openWin32Error"]==ERROR_ACCESS_DENIED,"denied child retained as unknown candidate");
 reset();FoundationMemory::add(100,42,200)->pathDenied=true;r=observe();check(unknownAbsence(r)&&r["children"][0]["imagePathRead"]==false,"child path denial never becomes complete inventory");
 reset();FoundationMemory::failSnapshot=1;r=observe();check(unknownAbsence(r)&&r["candidateEnumeration"]["win32Error"]==ERROR_ACCESS_DENIED,"snapshot creation failure is incomplete");
 reset();FoundationMemory::failFirst=1;r=observe();check(unknownAbsence(r)&&r["candidateEnumeration"]["reason"]=="enumeration-query-failed","first entry query failure is not no children");
 reset();FoundationMemory::failNextAt=1;r=observe();check(unknownAbsence(r)&&r["candidateEnumeration"]["complete"]==false,"next entry query failure is incomplete");
 reset();FoundationMemory::add(100,42,200);FoundationMemory::failSnapshot=2;r=observe();check(unknownAbsence(r)&&r["children"][0]["heldHandleAndSecondSnapshotConsistent"]==false,"revalidation snapshot failure prevents child identity certification");
 reset();for(DWORD n=0;n<17;++n)FoundationMemory::add(100+n,42,200+n);r=observe();check(unknownAbsence(r)&&r["truncated"]==true&&r["children"].size()==16,"seventeenth child truncates output at sixteen");check(bounded(r),"truncated observation still releases every original handle");
 reset();for(DWORD n=100;n<4300;++n)FoundationMemory::add(n,1,200+n);r=observe();check(unknownAbsence(r)&&r["candidateEnumeration"]["entriesScanned"]==4096&&r["truncated"]==true,"unrelated process enumeration stops at entry cap");
 reset();FoundationMemory::tickStep=101;r=observe();check(unknownAbsence(r)&&r["truncated"]==true&&FoundationMemory::snapshots==0,"soft budget exhaustion stops between read APIs");
 reset();FoundationMemory::processes[42]->created=0;r=observe();check(unknownAbsence(r)&&FoundationMemory::snapshots==0,"invalid zero creation time cannot anchor PID lifetime");
 check(bounded(r),"error observation keeps memory boundary and balanced handles");
 std::cout<<"cases="<<cases<<" failures="<<failures<<" liveProcessCalls=0 liveServiceCalls=0 RESULT="<<(failures?"FAIL":"PASS")<<"\n";return failures?1:0;
}
