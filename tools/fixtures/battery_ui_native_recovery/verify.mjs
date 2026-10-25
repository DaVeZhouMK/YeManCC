import fs from 'node:fs';
import path from 'node:path';
import {read,write,need,hash,inside,verifyOwnedJob} from './contract.mjs';
export function verifyExecution({run,root,nonce,variant,expected,component,tool,command}){
 const config=read(path.join(run,'config.json'));
 need(hash(path.join(run,'config.json'))===expected.configSha256&&config.artifactSnapshotSha256===expected.artifactSnapshotSha256&&hash(config.artifactSnapshot)===expected.artifactSnapshotSha256,'BATTERY_UI_PREEXECUTION_INPUT_BINDING_CHANGED');
 need(JSON.stringify(read(config.artifactSnapshot).files)===JSON.stringify(tool.files),'BATTERY_UI_COMPLETE_TOOL_LIST_CHANGED');
 const result=read(path.join(run,'result.json')),job=verifyOwnedJob(read(path.join(run,'owned-job-result.json')),{nonce,root:run,expectedExit:expected.naturalExit});
 need(command.natural&&command.exitCode===expected.naturalExit&&result.failure===null&&result.errors.length===0,'BATTERY_UI_HARNESS_OR_TERMINAL_FAILED');
 need(result.status===expected.status&&JSON.stringify(result.failed)===JSON.stringify(expected.failed),'BATTERY_UI_FIXED_ACTUAL_ASSERTION_MATRIX');
 need(result.variant===variant&&JSON.stringify(result.cases.map(x=>x.case))===JSON.stringify(['structured-native-failure','throw-control','fresh-unknown-control']),'BATTERY_UI_CASE_MATRIX_CHANGED');
 need(result.cases.slice(1).every(x=>x.status==='PASS'),'BATTERY_UI_ORIGINAL_CONTROLS_FAILED');
 need(['actualNativeHeader','actualHostController','actualBridge','mountedActualComponent'].every(k=>result[k]===true)&&!result.handConstructedFailureState&&!result.directReleaseBypass&&!result.realHostMain&&!result.realNamedPipe&&!result.realHardware,'BATTERY_UI_REAL_CONSUMER_BOUNDARY_CHANGED');
 need(path.resolve(result.componentSource.path)===path.resolve(component)&&result.componentSource.sha256===hash(component),'BATTERY_UI_ACTUAL_COMPILED_COMPONENT_CHANGED');
 const pinned=new Map(tool.files.map(x=>[path.resolve(x.path).toLowerCase(),x.sha256]));
 const originalSources=read(path.join(run,'source-inputs.json'));
 need(JSON.stringify(originalSources)===JSON.stringify(result.sourceHashes),'BATTERY_UI_CONSUMER_SOURCE_LEDGER_CHANGED');
 for(const x of originalSources)need(inside(path.join(root,'input-sources'),x.path)&&hash(x.path)===x.sha256,'BATTERY_UI_CAPTURED_CONSUMER_SOURCE_CHANGED');
 const observed=read(path.join(run,'observed-node-tool-inputs.json'));
 for(const x of observed)need(fs.existsSync(x.path)&&hash(x.path)===x.sha256&&(inside(root,x.path)||pinned.get(path.resolve(x.path).toLowerCase())===x.sha256),'BATTERY_UI_UNDECLARED_NODE_OR_ESM_INPUT:'+x.path);
 const transcript=read(path.join(run,'actual-transcript.json')),native=transcript.filter(x=>x.side==='native'),host=transcript.filter(x=>x.side==='host');
 need(new Set(host.map(x=>x.request.id)).size===host.length&&host.length>20,'BATTERY_UI_HOST_REQUEST_LEDGER');
 for(const x of host)need(x.request.id===x.response.id&&x.response.ok===true,'BATTERY_UI_HOST_RESPONSE_CORRELATION');
 for(const x of native)need(x.request.id===x.response.id&&x.response.openPipes===0&&x.response.openEvents===0&&x.response.launchAttempts===0,'BATTERY_UI_NATIVE_RETURN_OR_HANDLES');
 const main=result.cases[0],at=main.atFailure;
 need(main.before.raw===0x55&&main.before.writes===1&&at.host.raw===0x55&&at.host.writes===1&&at.host.memoryRecords===2&&at.text==='当前不可用','BATTERY_UI_OWNERSHIP_OR_ERROR_HIDDEN');
 const failed=native.filter(x=>x.scenario==='structured-native-failure'&&x.request.fault==='reconnect-timeout');need(failed.length===1,'BATTERY_UI_ACTUAL_FAILURE_SOURCE');
 const failure=failed[0],state=failure.response.result;
 need(failure.request.request.op==='get'&&state.ok===false&&state.supported===false&&state.kind==='unsupported'&&state.choices.length===0&&state.batteryPresent===true&&state.reason==='充电控制通道未恢复，请重新读取','BATTERY_UI_NATIVE_STRUCTURED_FAILURE_FIELDS');
 need(JSON.stringify(failure.response.actualHostExchanges.map(x=>x.request.op))===JSON.stringify(['hello']),'BATTERY_UI_FAILED_GET_FORWARDED_MUTATION');
 need(at.browserCalls.filter(x=>x.cmd==='battery.charge.get'&&JSON.stringify(x.reply)===JSON.stringify(state)).length===1,'BATTERY_UI_FAILURE_DID_NOT_REACH_FULL_BRIDGE');
 const mainNative=native.filter(x=>x.scenario==='structured-native-failure');
 need(mainNative.filter(x=>x.request.request.op==='set').length===1,'BATTERY_UI_AUTOMATIC_SET_REPLAY');
 if(variant==='positive'){
  need(at.dropdowns===1&&at.applyButtons===1&&main.released?.host.raw===0xaa&&main.released.host.writes===2&&main.released.host.memoryRecords===1,'BATTERY_UI_EXPLICIT_UI_RELEASE_INCOMPLETE');
  const releases=mainNative.filter(x=>x.request.request.op==='release');
  need(releases.length===1&&releases[0].response.result.ok===true&&releases[0].response.result.managed===false,'BATTERY_UI_RELEASE_NOT_ORIGINAL_CONTROLLER');
 }else need(at.dropdowns===0&&at.applyButtons===0&&main.released===null,'BATTERY_UI_SOURCE_FAULT_NOT_CAUGHT_WITHOUT_BYPASS');
 const processes=read(path.join(run,'processes.json'));
 need(processes.length===3&&new Set(processes.map(x=>x.label)).size===3&&['host','native','owned-headless-browser'].every(label=>processes.some(x=>x.label===label)),'BATTERY_UI_OWNED_CHILD_LEDGER');
 for(const x of processes)need(x.pid>0&&x.exitCode===0&&x.signal===null,'BATTERY_UI_OWNED_CHILD_NOT_NATURAL_ZERO');
 for(const name of ['host-runtime-boundary.json','guardian-runtime-boundary.json']){
  const receipt=read(path.join(run,name));need(receipt.status==='PASS'&&receipt.unknown.length===0,'BATTERY_UI_RUNTIME_BOUNDARY_FAILED');
  for(const x of receipt.loaded)need(hash(x.path)===x.sha256&&(inside(root,x.path)||pinned.get(path.resolve(x.path).toLowerCase())===x.sha256),'BATTERY_UI_LOADED_MODULE_IDENTITY_CHANGED');
 }
 const boundary=read(path.join(run,'host-native-boundary.json'));
 need(boundary.status==='PASS'&&boundary.nativeAttempts.length===0&&boundary.scope.productionMainInvoked===false&&read(path.join(run,'native-port-receipt.json')).launchAttempts===0,'BATTERY_UI_NATIVE_OR_MAIN_BOUNDARY_FAILED');
 const proof={status:'PASS',variant,semanticStatus:result.status,naturalExit:command.exitCode,ownedJob:job,observedNodeInputs:observed.length,hostRpcPairs:host.length,nativeUiRequests:native.length,actualFailure:state,atFailure:{text:at.text,dropdowns:at.dropdowns,applyButtons:at.applyButtons,raw:at.host.raw,writes:at.host.writes},actualUiRelease:variant==='positive',processes,component:{path:component,sha256:hash(component)},actualAssertionsUnchanged:true};
 write(path.join(run,'verified-roundtrip.json'),proof);return proof;
}
