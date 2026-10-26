// Current cold-return code is compiled unchanged; its external IO leaves refuse execution.
import fs from 'node:fs';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {extract} from '../legion_motion_consumer/extract_main.mjs';
export const COLD_RETURN_INPUTS=Object.freeze([
 'native/virtual_target_return_cold_runtime.h','native/virtual_target_return_cold_execution.h',
 'native/virtual_target_return_pnp_absence.h','native/msi_native_foreground_isolation_runtime.h',
 'native/native_foreground_source_policy.h','native/msi_native_foreground_isolation.h',
 'native/one_shot_helper_diagnostic.h'
]);
export function coldReturnContext({sourceFile,resolveInput}){
 const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
 const main=fs.readFileSync(sourceFile,'utf8'),file=resolveInput(COLD_RETURN_INPUTS[0]),original=fs.readFileSync(file,'utf8');
 const foregroundFile=resolveInput('native/msi_native_foreground_isolation_runtime.h'),foreground=fs.readFileSync(foregroundFile,'utf8');
 const bindings=[];
 function declaration(text,input,symbol,pattern){
  const found=[...text.matchAll(pattern)];assert.equal(found.length,1,'COLD_RETURN_DECLARATION:'+symbol);
  const match=found[0],code=match[0];
  bindings.push({symbol,kind:'declaration',file:input,originalSourceSha256:digest(text),sha256:digest(code),startUtf8Byte:Buffer.byteLength(text.slice(0,match.index)),endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,match.index+code.length)),environmentLeaf:false});
  return code;
 }
 function variable(text,input,symbol){return declaration(text,input,symbol,new RegExp('^static [^\\r\\n;{}()=]*\\b'+symbol+'\\b[ \\t]*(?=[=;{])[^\\r\\n]*','gm'));}
 function member(text,input,symbol,kind='function'){
  const row=extract(text,symbol,kind);bindings.push({...row,code:undefined,file:input,originalSourceSha256:digest(text),environmentLeaf:false});return row.code;
 }
 const stateNames=['g_exitRequested','g_shortcutRecordingActive','g_inputCaptureStop','g_externalPriorityVirtualSuspended','g_powerLifecycle','g_inputHostActive','g_inputHostReleaseQuarantined','g_msiSteamDeckReleaseDebt','g_hidHideVisibilityJournal','g_physicalReleaseWindowNeutral','g_physicalReleaseWindowStage','g_physicalReleaseWindowTransition'];
 const prefix=[
  '#include "native_foreground_source_policy.h"','#include "one_shot_helper_diagnostic.h"',
  member(main,sourceFile,'HidHideVisibilityJournal','struct'),
  member(foreground,foregroundFile,'NativeForegroundRefreshTarget','struct'),
  member(foreground,foregroundFile,'MsiNativeForegroundLease','struct'),
  declaration(main,sourceFile,'PowerLifecycle',/^enum class PowerLifecycle[^\r\n]+/gm),
  ...stateNames.map(name=>variable(main,sourceFile,name)),
  ...['g_msiNativeForegroundMx','g_msiNativeForegroundLease'].map(name=>variable(foreground,foregroundFile,name)),
  member(main,sourceFile,'physicalReleaseWindowActive'),member(main,sourceFile,'msiForegroundIsolationBorrowBusy'),
  'static std::string inputHostConfiguredPersona();'
 ];
 const absence=extract(original,'inputHostTargetReturnColdAbsence');
 const compiledAbsence=absence.code.slice(0,absence.code.indexOf('{')+1)+'\n    (void)knownIds;throw std::runtime_error("fixture-unexpected-cold-return-pnp-observation");\n}';
 assert.equal(original.split(absence.code).length,2,'UNIQUE_COLD_ABSENCE_ENVIRONMENT_LEAF');

 let transformed=original.replace(absence.code,()=>compiledAbsence);
 // The PnP observation leaf above always throws in this fixture. Bind its real
 // result/reason contract verbatim; do not admit the OS enumeration APIs merely
 // to compile an operation this request-only fixture must never execute.
 const pnpFile=resolveInput('native/virtual_target_return_pnp_absence.h'),pnp=fs.readFileSync(pnpFile,'utf8');
 const pnpStatic=pnp.replace(/^inline /gm,'static ');
 const pnpReason=extract(pnpStatic,'reasonName'),pnpStart=pnp.indexOf('enum class State ');
 assert(pnpStart>=0&&pnp.indexOf('enum class State ',pnpStart+1)<0,'UNIQUE_PNP_PUBLIC_CONTRACT');
 const pnpContract=Buffer.from(pnp).subarray(Buffer.byteLength(pnp.slice(0,pnpStart)),pnpReason.endUtf8ByteExclusive).toString('utf8');
 assert(pnpContract.endsWith(pnpReason.code.replace(/^static /,'inline ')),'PNP_PUBLIC_CONTRACT_BOUNDARY');
 const pnpInclude='#include "virtual_target_return_pnp_absence.h"';
 assert.equal(transformed.split(pnpInclude).length,2,'UNIQUE_PNP_OBSERVATION_INCLUDE');
 const pnpBinding='namespace ymcc::targetReturn::pnpAbsence {\n'+pnpContract+'\n}';
 transformed=transformed.replace(pnpInclude,()=>pnpBinding);
 assert.equal(transformed.replace(pnpBinding,()=>pnpInclude).replace(compiledAbsence,()=>absence.code),original,'COLD_HEADER_WHOLE_RESTORATION');
 bindings.push({symbol:'pnpAbsence public result/reason contract',kind:'source-range',file:pnpFile,originalSourceSha256:digest(pnp),sha256:digest(pnpContract),startUtf8Byte:Buffer.byteLength(pnp.slice(0,pnpStart)),endUtf8ByteExclusive:pnpReason.endUtf8ByteExclusive,environmentLeaf:false,originalPnpVerifierExecuted:false});
 bindings.push({...absence,code:undefined,file,originalSourceSha256:digest(original),compiledSha256:digest(compiledAbsence),compiledCode:compiledAbsence,environmentLeaf:true});
 for(const symbol of ['inputHostTargetReturnHistorySettledLocked','inputHostTargetReturnColdHidden','inputHostTargetReturnColdPassLocked','inputHostTargetReturnQueueColdStartup']){
  const before=extract(original,symbol),after=extract(transformed,symbol);
  assert.equal(after.code,before.code,'COLD_RETURN_CORE_CHANGED:'+symbol);
  bindings.push({...before,code:undefined,file,originalSourceSha256:digest(original),compiledSha256:after.sha256,environmentLeaf:false});
 }
 assert.equal(extract(transformed,'ColdEnvironment','struct').code,extract(original,'ColdEnvironment','struct').code,'COLD_ENVIRONMENT_ALGORITHM_CHANGED');
 bindings.push({...extract(original,'ColdEnvironment','struct'),code:undefined,file,originalSourceSha256:digest(original),environmentLeaf:false});
 const cli=extract(main,'hidHideRun');
 const cliAdapter=cli.code.slice(0,cli.code.indexOf('{')+1)+'\n    (void)arguments;(void)output;(void)exitCodeOut;(void)absoluteDeadlineTick;throw std::runtime_error("fixture-unexpected-cold-return-cli-call");\n}';
 bindings.push({...cli,code:undefined,file:sourceFile,originalSourceSha256:digest(main),compiledSha256:digest(cliAdapter),compiledCode:cliAdapter,environmentLeaf:true});
 prefix.push(cliAdapter);
 const uncalled=['inputHostTargetReturnHistorySettledLocked','inputHostTargetReturnColdHidden','inputHostTargetReturnColdPassLocked','inputHostTargetReturnQueueColdStartup'].map((symbol,index)=>'[[maybe_unused]] static const auto fixtureUncalledColdReturn'+index+'=&'+symbol+';').join('\n');
 return {prefix:prefix.join('\n')+'\n',code:transformed.replace(/^#pragma once\r?\n/,'')+'\n'+uncalled,bindings,inputs:COLD_RETURN_INPUTS.map(resolveInput),boundary:{wholeColdHeaderRestoredWithDeclaredPnpObservationBinding:true,pnpPublicResultAndReasonContractVerbatim:true,pnpOsEnumerationCompiled:false,actualColdEnvironmentAndExecutionAlgorithmsCompiled:true,actualStateAndForegroundTypesCompiled:true,cliAdapterThrowsIfReached:true,pnpObservationThrowsIfReached:true,actualColdExecutionExercised:false,physicalOrColdAcceptance:false}};
}
