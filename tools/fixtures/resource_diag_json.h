#pragma once
J diagnosticSnapshot(const resource_diag::Snapshot& s){
 J rows=J::array();
 for(unsigned i=0;i<s.count;++i){
  const auto& r=s.rows[i];
  rows.push_back({{"handle",std::to_string(r.handle)},{"object",std::to_string(r.object)},{"access",r.access},{"typeIndex",r.typeIndex},
   {"type",r.typeKnown?J(utf8(r.type)):J("UNKNOWN")},{"typeStatus",r.typeStatus},
   {"name",r.nameKnown?J(utf8(r.name)):J("UNKNOWN")},{"nameStatus",r.nameStatus},
   {"diskPath",r.pathKnown?J(utf8(r.path)):J("UNKNOWN")},{"fileType",r.fileType},{"pathError",r.pathError}});
 }
 return {{"pid",s.pid},{"rows",rows},{"count",s.count},{"complete",s.complete},{"overflow",s.overflow},{"queryStatus",s.queryStatus},
  {"queryAttempts",s.queryAttempts},{"systemNeeded",s.systemNeeded},{"handleCountAtEntry",s.handleCountAtEntry},{"handleCountAtExit",s.handleCountAtExit},{"countError",s.countError},
  {"virtualAllocations",s.virtualAllocations},{"virtualFrees",s.virtualFrees},{"memoryError",s.memoryError},
  {"diagnosticHandlesCreated",s.diagnosticHandlesCreated},{"diagnosticDuplicates",s.diagnosticDuplicates}};
}
J diagnosticReport(){
 J calls=J::array();const auto count=resource_diag::apiCount.load(std::memory_order_acquire);
 for(unsigned i=0;i<(std::min)(count,16384u);++i){
  const auto& c=resource_diag::api[i];calls.push_back({{"sequence",i},{"kind",c.kind},{"handle",std::to_string(c.handle)},{"parent",std::to_string(c.parent)},
   {"status",c.status},{"error",c.error},{"thread",c.thread}});
 }
 return {{"before",diagnosticSnapshot(resource_diag::before)},{"after",diagnosticSnapshot(resource_diag::after)},
  {"api",calls},{"apiCount",count},{"apiOverflow",count>16384},
  {"kindMap",{{"1","CreateEventW"},{"2","SetEvent"},{"3","ResetEvent"},{"4","CloseHandle"},{"5","CreateFileW"},{"6","BCryptOpenAlgorithmProvider"},
   {"7","BCryptCreateHash"},{"8","BCryptHashData"},{"9","BCryptFinishHash"},{"10","BCryptDestroyHash"},{"11","BCryptCloseAlgorithmProvider"},
   {"12","BCryptGenRandom"},{"13","std::thread created native handle"},{"14","std::thread join result"}}},
  {"scope","Current PID only; NtQueryObject type; Event/Mutant safe names; disk file paths only. No duplicate/open/close of inventory handles."},
  {"unobserved","CRT-internal stream file APIs are not intercepted; attribution must use actual inventory. No inference from destructor invocation."}};
}
