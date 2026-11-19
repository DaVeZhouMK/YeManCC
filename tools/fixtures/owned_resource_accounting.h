#pragma once
// Private fixture interpretation of its existing POD API log. No API calls,
// allocation, inventory HANDLE closure, type allowance or count correction.
namespace owned_resource {
enum Domain:unsigned {Event=1,File,Thread,Algorithm,Hash};
inline constexpr unsigned lifeCapacity=2048,faultCapacity=256;
inline constexpr auto apiCapacity=sizeof(resource_diag::api)/sizeof(resource_diag::api[0]);
struct Life {
 unsigned serial=0,domain=0,createSequence=0,returnSequence=0,parentSerial=0,returnAttempts=0;
 std::uintptr_t handle=0;LONG createStatus=0,returnStatus=0;DWORD returnError=0;
 bool live=false,refused=false;
};
struct Fault {const char* code=nullptr;unsigned sequence=0;std::uintptr_t handle=0;LONG status=0;DWORD error=0;};
struct Evaluation {
 Life lives[lifeCapacity]{};Fault faults[faultCapacity]{};
 unsigned used=0,faultsUsed=0,begin=0,end=0,debt=0,refusals=0;
 unsigned created[6]{},returned[6]{};bool strictSuccess=false,faultOverflow=false;
};
inline Evaluation initial{},afterLate{};
inline const char* name(unsigned d)noexcept{
 switch(d){case Event:return "CreateEvent/CloseHandle";case File:return "CreateFile/CloseHandle";
 case Thread:return "std::thread/native_handle/join";case Algorithm:return "BCrypt provider/close";case Hash:return "BCrypt hash/destroy";default:return "UNKNOWN";}
}
inline void fault(Evaluation& o,const char* code,unsigned seq=0,std::uintptr_t h=0,LONG status=0,DWORD error=0)noexcept{
 o.strictSuccess=false;
 if(o.faultsUsed<faultCapacity)o.faults[o.faultsUsed++]={code,seq,h,status,error};else o.faultOverflow=true;
}
inline bool kernel(unsigned d)noexcept{return d==Event||d==File||d==Thread;}
inline Life* find(Evaluation& o,std::uintptr_t h,unsigned domain=0)noexcept{
 for(unsigned i=o.used;i>0;--i){auto& l=o.lives[i-1];if(l.live&&l.handle==h&&(domain?l.domain==domain:kernel(l.domain)))return &l;}
 return nullptr;
}
inline Life* create(Evaluation& o,unsigned domain,unsigned seq,const resource_diag::Api& a,unsigned parent=0)noexcept{
 if(!a.handle||a.handle==static_cast<std::uintptr_t>(-1)){fault(o,"successful-create-invalid-handle",seq,a.handle,a.status,a.error);return nullptr;}
 if(find(o,a.handle,kernel(domain)?0:domain))fault(o,"active-handle-collision",seq,a.handle,a.status,a.error);
 if(o.used>=lifeCapacity){fault(o,"lifecycle-table-full",seq,a.handle,a.status,a.error);return nullptr;}
 auto& l=o.lives[o.used++];l={};l.serial=seq+1;l.domain=domain;l.createSequence=seq;l.parentSerial=parent;
 l.handle=a.handle;l.createStatus=a.status;l.live=true;++o.created[domain];return &l;
}
inline void finish(Evaluation& o,Life* l,unsigned seq,const resource_diag::Api& a,bool success)noexcept{
 if(!l){fault(o,"return-without-live-creation",seq,a.handle,a.status,a.error);return;}
 ++l->returnAttempts;l->returnStatus=a.status;l->returnError=a.error;
 if(!success){l->refused=true;++o.refusals;fault(o,"actual-return-refused",seq,a.handle,a.status,a.error);return;}
 l->live=false;l->returnSequence=seq;++o.returned[l->domain];
 // A later successful return does not clear any earlier refused return fault.
}
inline void evaluate(Evaluation& o,unsigned begin,unsigned end,bool peerQuiescent)noexcept{
 o.used=0;o.faultsUsed=0;o.begin=begin;o.end=end;o.debt=0;o.refusals=0;o.strictSuccess=true;o.faultOverflow=false;
 for(unsigned i=0;i<6;++i){o.created[i]=0;o.returned[i]=0;}
 if(!peerQuiescent){fault(o,"peer-not-joined-no-pod-read",begin);return;}
 if(begin>end||end>=apiCapacity||end>resource_diag::apiCount.load(std::memory_order_acquire))fault(o,"api-window-invalid-or-full",end);
 if(begin>=apiCapacity)return;
 const auto limit=(std::min)(end,static_cast<unsigned>(apiCapacity));
 for(unsigned seq=begin;seq<limit;++seq){
  const auto& a=resource_diag::api[seq];
  // Api has no stored sequence. seq is its reserved array slot, not a field.
  // After join, main-thread sequencing + the original note fetch_add contract
  // permit reading each slot. Empty/invalid slots fail; reordering/corruption
  // with otherwise valid fields cannot be independently proved by this ABI.
  if(!a.thread){fault(o,"unpublished-or-unknown-api-record",seq,a.handle,a.status,a.error);continue;}
  switch(a.kind){
   case resource_diag::EventCreate:
   case resource_diag::FileCreate:
   case resource_diag::ThreadCreate:
    if(a.status!=0)create(o,a.kind==resource_diag::EventCreate?Event:(a.kind==resource_diag::FileCreate?File:Thread),seq,a);
    else fault(o,"actual-create-failed",seq,a.handle,a.status,a.error);
    break;
   case resource_diag::HandleClose:{
    auto* l=find(o,a.handle);
    if(l&&l->domain==Thread){fault(o,"thread-close-is-not-join-proof",seq,a.handle,a.status,a.error);break;}
    finish(o,l,seq,a,a.status!=0);break;
   }
   case resource_diag::ThreadJoin:finish(o,find(o,a.handle,Thread),seq,a,a.status!=0);break;
   case resource_diag::AlgorithmOpen:
    if(a.status>=0)create(o,Algorithm,seq,a);else fault(o,"provider-create-failed",seq,a.handle,a.status,a.error);break;
   case resource_diag::HashCreate:{
    auto* parent=find(o,a.parent,Algorithm);
    if(!parent)fault(o,"hash-parent-not-live",seq,a.parent,a.status,a.error);
    if(a.status>=0)create(o,Hash,seq,a,parent?parent->serial:0);else fault(o,"hash-create-failed",seq,a.handle,a.status,a.error);break;
   }
   case resource_diag::HashDestroy:finish(o,find(o,a.handle,Hash),seq,a,a.status>=0);break;
   case resource_diag::AlgorithmClose:{
    auto* l=find(o,a.handle,Algorithm);
    if(l)for(unsigned i=0;i<o.used;++i)if(o.lives[i].live&&o.lives[i].parentSerial==l->serial)fault(o,"provider-return-with-live-hash",seq,a.handle,a.status,a.error);
    finish(o,l,seq,a,a.status>=0);break;
   }
   case resource_diag::HashData:
   case resource_diag::HashFinish:
    if(!find(o,a.handle,Hash))fault(o,"hash-use-without-live-creation",seq,a.handle,a.status,a.error);
    if(a.status<0)fault(o,"actual-hash-operation-failed",seq,a.handle,a.status,a.error);break;
   case resource_diag::EventSet:
   case resource_diag::EventReset:
    if(!find(o,a.handle,Event))fault(o,"event-use-without-live-creation",seq,a.handle,a.status,a.error);
    if(!a.status)fault(o,"actual-event-operation-failed",seq,a.handle,a.status,a.error);break;
   case resource_diag::Random:
    if(a.status<0)fault(o,"actual-rng-failed",seq,a.handle,a.status,a.error);break;
   default:fault(o,"unknown-api-kind",seq,a.handle,a.status,a.error);break;
  }
 }
 for(unsigned i=0;i<o.used;++i)if(o.lives[i].live)++o.debt;
 if(o.used>=lifeCapacity)fault(o,"lifecycle-table-full",end);
 if(o.faultsUsed>=faultCapacity)fault(o,"fault-table-full",end);
 if(o.debt)fault(o,"direct-owned-return-debt",end);
}
}
