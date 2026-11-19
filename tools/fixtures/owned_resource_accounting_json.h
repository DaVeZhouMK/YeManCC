#pragma once
J directOwnedReport(const owned_resource::Evaluation& e){
 J lives=J::array(),faults=J::array(),counts=J::array();
 for(unsigned i=0;i<e.used;++i){const auto& l=e.lives[i];lives.push_back({{"creationSerial",l.serial},{"domain",owned_resource::name(l.domain)},
  {"handle",std::to_string(l.handle)},{"createSequence",l.createSequence},{"returnSequence",l.live?J():J(l.returnSequence)},
  {"parentCreationSerial",l.parentSerial},{"createStatus",l.createStatus},{"returnStatus",l.returnStatus},{"returnLastError",l.returnError},
  {"returnAttempts",l.returnAttempts},{"refusalObserved",l.refused},{"liveDebt",l.live}});}
 for(unsigned i=0;i<e.faultsUsed;++i){const auto& f=e.faults[i];faults.push_back({{"code",f.code},{"sequence",f.sequence},{"handle",std::to_string(f.handle)},{"status",f.status},{"lastError",f.error}});}
 for(unsigned i=1;i<6;++i)counts.push_back({{"domain",owned_resource::name(i)},{"created",e.created[i]},{"returned",e.returned[i]}});
 return {{"scope","Direct fixture API-owned lifecycle only: event/file successful CloseHandle; std::thread successful join plus original owner checks; CNG hash destroy/provider close. No process-wide verdict."},
  {"strictSuccess",e.strictSuccess},{"apiBegin",e.begin},{"apiEnd",e.end},{"creationSerialRule","absolute POD log sequence + 1; numeric reuse creates a new lifecycle"},
  {"counts",counts},{"lifecycles",lives},{"faults",faults},{"faultOverflow",e.faultOverflow},{"liveDebt",e.debt},{"refusals",e.refusals},
  {"unclassifiedProcessResources","UNKNOWN; no subtraction, numeric/type whitelist or closure"},{"processWideResourcePass",false}};
}
