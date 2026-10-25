// Bind only the current target-duty environment leaves; execute the real runtime and file store.
import fs from 'node:fs';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {extract} from '../legion_motion_consumer/extract_main.mjs';
import {coldReturnContext,COLD_RETURN_INPUTS} from './cold-return-context.mjs';
export const TARGET_RETURN_INPUTS=Object.freeze([
 'native/virtual_target_return_runtime.h','native/virtual_target_return_session.h',
 'native/virtual_target_return_store.h','native/virtual_target_return_record.h',
 'native/virtual_target_return_process.h','native/virtual_target_return_physical_path.h',
 'native/hidhide_cli_writer_runtime.h','native/hidhide_cli_writer_store.h','native/hidhide_cli_writer_record.h',...COLD_RETURN_INPUTS
]);
export function targetReturnContext({sourceFile,resolveInput}){
 const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
 const main=fs.readFileSync(sourceFile,'utf8'),runtimeFile=resolveInput(TARGET_RETURN_INPUTS[0]),original=fs.readFileSync(runtimeFile,'utf8');
 const request=extract(main,'inputHostRequestPreparedLocked');
 assert(request.code.includes('inputHostTargetReturnBeforeCreateLocked(command,sequenceValue)'),'CURRENT_TARGET_DUTY_REQUEST_REQUIRED');
 const leaves=[
  {name:'inputHostTargetReturnRecordPath',body:'if(fixtureFeedbackTargetReturnPath.empty())throw std::runtime_error("fixture-private-target-return-path-not-armed");return fixtureFeedbackTargetReturnPath;'},
  {name:'inputHostTargetReturnMachineBinding',body:'return "feedback-fixture-machine";'},
  {name:'inputHostTargetReturnProbePhysicalWriter',body:'(void)owner;throw std::runtime_error("fixture-unexpected-physical-process-probe");'},
  {name:'inputHostTargetReturnVerifyPhysicalPath',body:'(void)root;(void)target;throw std::runtime_error("fixture-unexpected-physical-path-query");'}
 ];
 let transformed=original;const bindings=[];
 for(const leaf of leaves){
  const before=extract(original,leaf.name),open=before.code.indexOf('{');
  assert(open>=0);const after=before.code.slice(0,open+1)+'\n    '+leaf.body+'\n}';
  assert.equal(transformed.split(before.code).length,2,'UNIQUE_TARGET_ENVIRONMENT_LEAF:'+leaf.name);
  transformed=transformed.replace(before.code,()=>after);
  bindings.push({...before,code:undefined,file:runtimeFile,originalSourceSha256:digest(original),compiledSha256:digest(after),compiledCode:after,environmentLeaf:true});
 }
 let restored=transformed;for(const row of bindings)restored=restored.replace(row.compiledCode,()=>extract(original,row.symbol).code);
 assert.equal(restored,original,'TARGET_ENVIRONMENT_RESTORATION_MUST_BE_WHOLE_HEADER');
 const actualFunctions=['inputHostTargetReturnCaptureOwner','note','contextCurrent','sameFlow','currentOwner','open','contains','flushProven','newKey','inputHostTargetReturnBeforeCreateLocked','inputHostTargetReturnAcceptPsLocked','inputHostTargetReturnAcceptedRecreateLocked','inputHostTargetReturnTargetExitedLocked','inputHostTargetReturnAfterPsQuarantineLocked','physicalStamp','physicalWritersQuiescent','physicalContext','physicalOriginWriterSettled','pruneEmptyPhysicals','inputHostTargetReturnBeforeHideLocked','inputHostTargetReturnAfterPhysicalRestoreLocked'];
 for(const name of actualFunctions){const before=extract(original,name),after=extract(transformed,name);assert.equal(after.code,before.code,'TARGET_DUTY_CORE_CHANGED:'+name);bindings.push({...before,code:undefined,file:runtimeFile,originalSourceSha256:digest(original),environmentLeaf:false,compiledSha256:after.sha256});}

 // Compile the current durable CLI-writer algorithms as well. Only filesystem
 // location and external owner/job observations are environment leaves.
 const writerRuntimeFile=resolveInput('native/hidhide_cli_writer_runtime.h');
 const writerOriginal=fs.readFileSync(writerRuntimeFile,'utf8');
 const writerLeaves=[
  {name:'hidHideCliWriterRecordPath',body:'if(fixtureFeedbackTargetReturnPath.empty())throw std::runtime_error("fixture-private-cli-writer-path-not-armed");return (std::filesystem::path(fixtureFeedbackTargetReturnPath).parent_path()/L"cli-writer.json").wstring();'},
  {name:'hidHideCliWriterProbe',body:'(void)identity;throw std::runtime_error("fixture-unexpected-cli-writer-process-probe");'},
  {name:'hidHideCliWriterProbeJob',body:'(void)name;throw std::runtime_error("fixture-unexpected-cli-writer-job-probe");'}
 ];
 let writerTransformed=writerOriginal;const writerBindings=[];
 for(const leaf of writerLeaves){
  const before=extract(writerOriginal,leaf.name),open=before.code.indexOf('{');
  assert(open>=0);const after=before.code.slice(0,open+1)+'\n    '+leaf.body+'\n}';
  assert.equal(writerTransformed.split(before.code).length,2,'UNIQUE_WRITER_ENVIRONMENT_LEAF:'+leaf.name);
  writerTransformed=writerTransformed.replace(before.code,()=>after);
  const binding={...before,code:undefined,file:writerRuntimeFile,originalSourceSha256:digest(writerOriginal),compiledSha256:digest(after),compiledCode:after,environmentLeaf:true};
  writerBindings.push(binding);bindings.push(binding);
 }
 let writerRestored=writerTransformed;
 for(const row of writerBindings)writerRestored=writerRestored.replace(row.compiledCode,()=>extract(writerOriginal,row.symbol).code);
 assert.equal(writerRestored,writerOriginal,'WRITER_ENVIRONMENT_RESTORATION_MUST_BE_WHOLE_HEADER');
 const writerFunctions=['hidHideCliWriterMachineBinding','hidHideCliWriterCapture','note','open','clearSettled','settleOwned','quiescent'];
 for(const name of writerFunctions){
  const before=extract(writerOriginal,name),after=extract(writerTransformed,name);
  assert.equal(after.code,before.code,'CLI_WRITER_CORE_CHANGED:'+name);
  bindings.push({...before,code:undefined,file:writerRuntimeFile,originalSourceSha256:digest(writerOriginal),environmentLeaf:false,compiledSha256:after.sha256});
 }
 // Physical return is outside this request/receipt fixture. Preserve the actual
 // reaper source identity and fail if that unexercised boundary is ever reached.
 const reaper=extract(main,'hidHideCliReapPendingLocked');
 const reaperAdapter=reaper.code.slice(0,reaper.code.indexOf('{')+1)+'\n    (void)absoluteDeadlineTick;throw std::runtime_error("fixture-unexpected-cli-writer-reap");\n}';
 bindings.push({...reaper,code:undefined,file:sourceFile,originalSourceSha256:digest(main),compiledSha256:digest(reaperAdapter),compiledCode:reaperAdapter,environmentLeaf:true,unreachedPhysicalBoundary:true});
 function declaration(name,pattern){const matches=[...main.matchAll(pattern)];assert.equal(matches.length,1,'ACTUAL_TARGET_CONTEXT_DECLARATION:'+name);const m=matches[0];bindings.push({symbol:name,kind:'declaration',file:sourceFile,sha256:digest(m[0]),startUtf8Byte:Buffer.byteLength(main.slice(0,m.index)),endUtf8ByteExclusive:Buffer.byteLength(main.slice(0,m.index+m[0].length)),environmentLeaf:false});return m[0];}
 const declarations=[
  declaration('g_inputHostSwitchRecoveryStartRequestIdAndPower',/^static thread_local std::uint64_t g_inputHostSwitchRecoveryStartRequestId=0,g_inputHostSwitchRecoveryStartPower=0;\r?$/gm),
  declaration('g_psEndpointOwnership',/^static ymcc::psEndpointOwnership::State g_psEndpointOwnership;\r?$/gm),
  declaration('g_hidHideMx',/^static std::mutex g_hidHideMx;\r?$/gm),
  declaration('g_hidHidePendingChildProcess',/^static HANDLE g_hidHidePendingChildProcess = nullptr;\r?$/gm),
  declaration('g_hidResolveCacheGeneration',/^static std::atomic<std::uint64_t> g_hidResolveCacheGeneration\{1\};\r?$/gm)
 ];
 const physicalTypes=['HidHidePhysicalIdentity','HidHideVisibilitySnapshot'].map(name=>{const item=extract(main,name,'struct');bindings.push({...item,code:undefined,file:sourceFile,environmentLeaf:false});return item.code;});
 const physicalHelpers=['ascii_lower','trim_ascii','hidHideUpper','hidHideContainsPath','hidHideIsVirtualIdentity'].map(name=>{const item=extract(main,name);bindings.push({...item,code:undefined,file:sourceFile,environmentLeaf:false});return item.code;});

 const cold=coldReturnContext({sourceFile,resolveInput});bindings.push(...cold.bindings);
 const coldInclude='#include "virtual_target_return_cold_runtime.h"';
 assert.equal(transformed.split(coldInclude).length,2,'CURRENT_COLD_RETURN_INCLUDE_REQUIRED');
 const compiledRuntime=transformed.replace(coldInclude,()=>cold.code);
 assert.equal(compiledRuntime.replace(cold.code,()=>coldInclude),transformed,'COLD_INLINE_RESTORATION');
 const code=String.raw`
#include <hidusage.h>
#include <cctype>
#include <set>
static std::wstring fixtureFeedbackTargetReturnPath;
static std::uint64_t fixtureFeedbackTargetReturnCaseSerial=0;
`+physicalTypes.join('\n')+'\n'+physicalHelpers.join('\n')+'\n'+declarations.join('\n')+'\n'+writerTransformed.replace(/^#pragma once\r?\n/,'')+'\n'+reaperAdapter+'\n'+cold.prefix+'\n'+compiledRuntime.replace(/^#pragma once\r?\n/,'')+String.raw`
// These original consumers are compiled unchanged but not claimed exercised by a request-only fixture.
[[maybe_unused]] static const auto fixtureUncalledCliWriterSettlement=&ymcc::hidHideCliWriterRuntime::settleOwned;
[[maybe_unused]] static const auto fixtureUncalledAcceptedPsDuty=&inputHostTargetReturnAcceptPsLocked;
[[maybe_unused]] static const auto fixtureUncalledTargetSettlement=&inputHostTargetReturnTargetExitedLocked;
[[maybe_unused]] static const auto fixtureUncalledPsQuarantineSettlement=&inputHostTargetReturnAfterPsQuarantineLocked;
[[maybe_unused]] static const auto fixtureUncalledRecreateSettlement=&inputHostTargetReturnAcceptedRecreateLocked;
[[maybe_unused]] static const auto fixtureUncalledPhysicalReservation=&inputHostTargetReturnBeforeHideLocked;
[[maybe_unused]] static const auto fixtureUncalledPhysicalSettlement=&inputHostTargetReturnAfterPhysicalRestoreLocked;
static void fixtureBeginTargetReturnCase(const std::filesystem::path& root){
 if(!root.is_absolute()||!std::filesystem::is_directory(root)||std::filesystem::is_symlink(root))
  throw std::runtime_error("fixture-target-return-owned-case-root-required");
 // Existing cases model independent product lifetimes; leave each real record as evidence.
 ymcc::inputHost::targetReturnRuntime::state=ymcc::inputHost::targetReturnRuntime::State{};
 const auto directory=root/L"target-return-private"/(L"case-"+std::to_wstring(++fixtureFeedbackTargetReturnCaseSerial));
 if(std::filesystem::exists(directory))throw std::runtime_error("fixture-target-return-fresh-case-required");
 std::filesystem::create_directories(directory);fixtureFeedbackTargetReturnPath=(directory/L"record.json").wstring();
}
static json fixtureObserveTargetReturnReservation(const char* command,bool accepted){
 if(!accepted||!command||(std::strcmp(command,"PREPARE_TARGET")!=0&&std::strcmp(command,"RECREATE_TARGET")!=0))
  return {{"observed",false},{"scope","historical-record-only"}};
 // Independent JSON observation of actual persisted bytes, not the runtime's own success flag.
 const auto path=std::filesystem::path(fixtureFeedbackTargetReturnPath);
 if(!std::filesystem::is_regular_file(path)||std::filesystem::is_symlink(path)||std::filesystem::file_size(path)>64*1024)
  throw std::runtime_error("fixture-target-return-record-missing-or-invalid");
 std::ifstream stream(path,std::ios::binary);json record;stream>>record;
 if(!record.is_object()||record.value("schemaVersion",0)!=1||record.value("machineBinding",std::string{})!="feedback-fixture-machine"||
    !record.contains("obligations")||!record["obligations"].is_array())
  throw std::runtime_error("fixture-target-return-actual-record-shape-invalid");
 bool found=false;for(const auto& item:record["obligations"]){
  if(!item.is_object()||!item.value("targetMayExist",false)||!item.contains("origin"))continue;
  const auto& origin=item["origin"];
  found=origin.value("hostInstanceId",std::string{})==g_inputHostHostInstanceId&&
   origin.value("runId",std::string{})==g_inputHostRunId&&origin.value("epoch",0ULL)==g_inputHostEpoch&&
   origin.value("targetId",std::string{})==kInputHostTargetId&&origin.value("persona",std::string{})==fixturePersona&&
   origin.at("parent").value("pid",0ULL)==GetCurrentProcessId()&&
   origin.at("host").value("pid",0ULL)==GetProcessId(g_inputHostProcess)&&
   !origin.at("parent").value("creationTime",std::string{}).empty()&&
   !origin.at("host").value("creationTime",std::string{}).empty();
  if(found)break;
 }
 if(!found)throw std::runtime_error("fixture-target-return-current-owned-duty-not-persisted");
 return {{"observed",true},{"path",W2U(fixtureFeedbackTargetReturnPath)},
  {"revision",record.at("revision")},{"origins",record["obligations"].size()},
  {"scope","historical-record-only;not-endpoint-return-or-cold-recovery-proof"}};
}
`;
 return {code,bindings,inputs:[sourceFile,...TARGET_RETURN_INPUTS.map(resolveInput)].map(file=>({file,sha256:digest(fs.readFileSync(file))})),boundary:{actualReservationDeadlineOwnerAndAtomicStore:true,wholeHeaderRestoredWithOnlyDeclaredEnvironmentLeaves:true,unreachedPhysicalOsLeavesThrow:true,currentPhysicalAlgorithmsCompiledVerbatim:true,actualPhysicalReservationExercised:false,environmentLeaves:[...leaves,...writerLeaves].map(r=>r.name),coldReturn:cold.boundary,cliWriter:{currentRuntimeStoreAndRecordCompiled:true,wholeRuntimeRestoredWithOnlyDeclaredEnvironmentLeaves:true,sourceExactMachineBindingAndBorrowedHandleCapture:true,externalProcessAndJobProbesThrow:true,reaperAdapterThrowsIfReached:true,reaperOriginalSourceSha256:reaper.sha256,recordPath:'Fresh cli-writer.json sibling of the private target-return record.',settlementOrQuiescenceExercised:false},recordPath:'Fresh private child of actual existing owned native case root; all bytes retained.',machineIdentity:'Fixed fixture marker; never a real machine binding.',processIdentity:'Original real borrowed-handle capture for this fixture parent and owned managed child only.',independentOnDiskRecordObservation:true,oldBusinessChecksAndCountUnchanged:true,actualRuntimeSettlementExecuted:false,physicalOrColdRecoveryAcceptance:false}};
}
