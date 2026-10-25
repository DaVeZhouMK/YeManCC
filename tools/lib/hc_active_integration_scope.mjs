// Read-only upgrade planning; the human scope, not a catalog change, admits consumers.
const need=(value,code)=>{if(!value)throw Error('ACTIVE_INTEGRATION_SCOPE_'+code);};
export function planEnabledMaintenance({scope,decisions,domains,affectedDomains}) {
 need(scope?.schemaVersion===1&&scope.authority==='direct-human-user','AUTHORITY');
 need(scope.inactiveDisposition?.action==='record-gaps-only'&&scope.inactiveDisposition.newWiring===false&&scope.inactiveDisposition.newTestPromotion===false,'INACTIVE_DISPOSITION');
 need(decisions?.schemaVersion===1&&decisions.authority==='direct-human-user'&&Array.isArray(decisions.entries),'USER_DECISIONS');
 need(Array.isArray(domains)&&Array.isArray(scope.confirmedEnabledCapabilities)&&scope.confirmedEnabledCapabilities.length>0,'ENABLED_CONSUMERS_REQUIRED');
 need(Array.isArray(scope.userExcluded),'USER_EXCLUDED_REQUIRED');
 const byDomain=new Map(domains.map(d=>[d.id,d]));need(byDomain.size===domains.length,'DUPLICATE_DOMAIN');
 const admitted=new Set(),ids=new Set(),excludedIds=new Set(scope.userExcluded),excludedTests=new Set();
 for(const entry of decisions.entries.filter(e=>e.status==='USER_EXCLUDED')){
  need(excludedIds.has(entry.id),'EXCLUSION_MISMATCH:'+entry.id);
  if(typeof entry.excludedSimulationCase==='string')excludedTests.add(entry.excludedSimulationCase);
 }
 for(const capability of scope.confirmedEnabledCapabilities){
  need(typeof capability.id==='string'&&!ids.has(capability.id)&&!excludedIds.has(capability.id),'ENABLED_ID:'+capability.id);ids.add(capability.id);
  need(Array.isArray(capability.libraryDomains)&&capability.libraryDomains.length>0,'ENABLED_DOMAINS:'+capability.id);
  for(const id of capability.libraryDomains){need(byDomain.has(id),'UNKNOWN_DOMAIN:'+id);need(id!=='performance'||scope.preservedBoundaries?.cpuGpuMetadataOnly!==true,'PERFORMANCE_METADATA_ONLY');admitted.add(id);}
 }
 const affected=[...new Set(affectedDomains)];for(const id of affected)need(byDomain.has(id),'UNKNOWN_AFFECTED_DOMAIN:'+id);
 // Runtime/ownership dependencies still matter when an admitted capability shares them.
 const executable=affected.filter(id=>admitted.has(id)||(id==='lifecycle-and-dependencies'&&admitted.size>0));
 const recordOnly=affected.filter(id=>!executable.includes(id));
 const rows=executable.map(id=>byDomain.get(id));
 const tests=[...new Set(rows.flatMap(d=>d.tests??[]))].filter(id=>!excludedTests.has(id));
 const additional=[...new Set(rows.flatMap(d=>d.additionalChecks??[]))];
 return {requiredExistingConsumerTests:tests,requiredAdditionalConsumerChecks:additional,scopeDisposition:{authority:'direct-human-user',scopeFile:'capabilities/ACTIVE-INTEGRATION-SCOPE.json',decisionsFile:'capabilities/USER-SCOPE-DECISIONS.json',confirmedEnabledCapabilities:[...ids],executableAffectedDomains:executable,recordOnlyAffectedDomains:recordOnly,gapRecords:recordOnly.map(id=>({domain:id,disposition:'record-gaps-only',executionAllowed:false,existingChecksRetained:(byDomain.get(id).tests?.length??0)+(byDomain.get(id).additionalChecks?.length??0)})),userExcludedCapabilities:[...excludedIds],userExcludedTestIds:[...excludedTests],catalogChangesDoNotActivateConsumers:true,automaticBinaryReplacement:false}};
}