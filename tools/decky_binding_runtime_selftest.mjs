import {selectedNativeTools} from './ymcc_sim/global/release-host-environment.mjs';
import {uniqueAnchor,cppBlock} from './ymcc_sim/global/decky-update-recovery/native-source-admission.mjs';
import {steamSessionObserverPublication,OBSERVER_PUBLICATION_SOURCE,OBSERVER_PUBLICATION_GENERATOR} from './fixtures/steam_session_observer_publication.mjs';
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
const initialPrefix='--initial-update-backend=';
const initialRequested=process.argv.slice(3).some(value=>value.startsWith(initialPrefix));
// Original ready/rebind parameter; no extra canonical ID or default work.
const reportMemoryRequested=initialRequested&&process.argv[4]===initialPrefix+'backend-report-memory';
if(initialRequested){
 assert.equal(process.argv.length,5,'Initial update probe requires one original selection and parameter');
 assert.equal(process.argv[3],'native.typed-ready-rebind','Initial update probe cannot select other original cases');
 assert(['ready','deadline','off','boundaries','backend-outage','backend-outage-sidebar-only','backend-recovers','backend-off','backend-boundaries','backend-notify','backend-report-memory'].includes(process.argv[4].slice(initialPrefix.length)),'Unknown initial update probe parameter');
}
// Parameter of the original typed live-watch case; no default work or new ID.
const watchEndReportPrefix='--watch-end-report=';
const watchEndReportRequested=process.argv.slice(3).some(value=>value.startsWith(watchEndReportPrefix));
if(watchEndReportRequested){
 assert.equal(process.argv.length,5,'Watch end report requires one original selection and parameter');
 assert(['native.typed-healing-child-exit-owned','native.typed-healing-child-exit-retained'].includes(process.argv[3]),'Watch end report cannot select other cases');
 assert.equal(process.argv[4],watchEndReportPrefix+'memory','Unknown watch end report parameter');
}
const bindingReportPrefix='--binding-report=';
const bindingReportRequested=process.argv.slice(3).some(value=>value.startsWith(bindingReportPrefix));
if(bindingReportRequested){
 assert.equal(process.argv.length,5,'Binding report requires one original case and parameter');
 assert.equal(process.argv[3],'native.typed-runtime-live-watch','Binding report cannot select another original case');
 assert(['plan-started','plan-finished','result-copy','trace-started','reason-fallback','reason-copy','reason-controls','reason-listener-control','diagnostic-tail','diagnostic-tail-control','borrowed-diagnostic','borrowed-payloads','borrowed-throws','borrowed-watch','borrowed-post-queue','borrowed-post-metadata'].includes(process.argv[4].slice(bindingReportPrefix.length)),'Unknown binding report parameter');
}
const loaderStartReportPrefix='--loader-start-report=';
const loaderStartReportRequested=process.argv.slice(3).some(value=>value.startsWith(loaderStartReportPrefix));
if(loaderStartReportRequested){
 assert.equal(process.argv.length,5,'Loader start report requires one original selection and parameter');
 assert.equal(process.argv[3],'native.typed-runtime-live-watch','Loader start report cannot select another original case');
 assert.equal(process.argv[4],loaderStartReportPrefix+'memory','Unknown Loader start report parameter');
}
const loaderExitReportPrefix='--loader-exit-report=';
const loaderExitReportRequested=process.argv.slice(3).some(value=>value.startsWith(loaderExitReportPrefix));
if(loaderExitReportRequested){
 assert.equal(process.argv.length,5,'Loader exit report probe requires one selection and parameter');
 assert.equal(process.argv[3],'native.typed-runtime-live-watch','Loader exit report cannot select other original cases');
 assert(['memory','memory-prewait'].includes(process.argv[4].slice(loaderExitReportPrefix.length)),'Unknown Loader exit report parameter');
}
const constructorPrefix='--client-construction=';
const constructorRequested=process.argv.slice(3).some(value=>value.startsWith(constructorPrefix));
// All worker-* scenarios share compiler ports so a built EXE may be reused across
// construction, preparation, cleanup and observation parameters in one batch.
const resourceWorkerRequested=constructorRequested&&process.argv[4]?.startsWith(constructorPrefix+'worker-');
const packetPublicationRequested=constructorRequested&&process.argv[4]===constructorPrefix+'packet-publication-cost';
const runtimeDetailRequested=constructorRequested&&process.argv[4]===constructorPrefix+'runtime-detail-cost';
const runtimePhaseRequested=constructorRequested&&process.argv[4]===constructorPrefix+'runtime-phase-state-cost';
if(constructorRequested){
 assert.equal(process.argv.length,5,'Constructor probe requires one original selection and parameter');
 assert.equal(process.argv[3],(packetPublicationRequested||runtimeDetailRequested||runtimePhaseRequested)?'native.typed-runtime-live-watch':'native.typed-ready-rebind','Constructor probe cannot select other original cases');
 assert(['direct-preserve','runtime-heal','off','admission','serialize-retry','parse-retry','payload-retry','serialize-off','parse-admission','write-attempt-no-replay','written-no-replay','packet-publication-cost','runtime-detail-cost','runtime-phase-state-cost','completion-event-retry','owner-memory-retry','completion-event-off','owner-memory-admission','owner-denied-stops','prepare-changed-retry','prepare-cancel-retry','prepare-job-retry','prepare-port-retry','prepare-changed-off','prepare-job-admission','prepare-cancel-denied','handoff-child-memory','handoff-pipe-memory','handoff-reader-memory','handoff-child-denied','handoff-reader-off','handoff-pipe-admission','handoff-owner-cancelled','worker-start-retry','worker-start-off','worker-start-admission','worker-start-denied','worker-attach-memory','worker-forged-memory','worker-forged-deadline','worker-reply-stall','worker-reply-stall-exhaustion','worker-construction-owner','worker-construction-reserve','worker-construction-reader','worker-construction-owner-off','worker-construction-reader-admission','worker-construction-postwrite','worker-preparation-initial','worker-preparation-request','worker-preparation-serialize','worker-preparation-payload','worker-preparation-payload-off','worker-preparation-request-admission','worker-preparation-write','worker-cleanup-normal','worker-cleanup-diagnostic-ready','worker-cleanup-diagnostic-memory','worker-cleanup-diagnostic-memory-off','worker-cleanup-error-postwrite','worker-cleanup-diagnostic-postwrite','worker-observation-normal','worker-observation-protected','worker-io-send-denied','worker-io-send-invalid','worker-io-wait-invalid'].includes(process.argv[4].slice(constructorPrefix.length)),'Unknown constructor probe parameter');
}
const loaderClosePrefix='--loader-native-close=';
const loaderCloseRequested=process.argv.slice(3).some(value=>value.startsWith(loaderClosePrefix));
if(loaderCloseRequested){
 assert.equal(process.argv.length,5,'Loader native close probe requires one original selection and parameter');
 assert.equal(process.argv[3],'native.typed-runtime-live-watch','Loader native close cannot select other original cases');
 assert(['final','reentry','paused','joint','joint-live'].includes(process.argv[4].slice(loaderClosePrefix.length)),'Unknown Loader native close probe');
}
// Existing typed live-watch case: real own binding-process CloseHandle refusal.
// No product hook, extra default case or new canonical registration.
const blockedOwnerPrefix='--blocked-owner-native';
const blockedOwnerRequested=process.argv.slice(3).some(value=>value.startsWith(blockedOwnerPrefix));
if(blockedOwnerRequested){
 assert.equal(process.argv.length,5,'Blocked owner probe requires one original case and parameter');
 assert.equal(process.argv[3],'native.typed-runtime-live-watch','Blocked owner probe cannot select another original case');
// Optional =automatic waits for the original owner cadence without a new wake;
// =automatic-off proves explicit OFF cannot be revived after native cleanup returns.
 assert([blockedOwnerPrefix,blockedOwnerPrefix+'=automatic',blockedOwnerPrefix+'=automatic-off'].includes(process.argv[4]),'Unknown blocked owner parameter');
}
const repo=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]??'');assert(process.argv[2]&&!fs.existsSync(out),'Use a fresh evidence directory.');fs.mkdirSync(out,{recursive:true});
const source=path.join(out,'source'),pins=[],seen=new Set();const sha=b=>crypto.createHash('sha256').update(b).digest('hex');const search=[path.join(repo,'native'),path.join(repo,'deps/json'),repo];
function freeze(file){file=path.resolve(file);if(seen.has(file.toLowerCase()))return;const relative=path.relative(repo,file);if(relative.startsWith('..')||path.isAbsolute(relative))return;seen.add(file.toLowerCase());const bytes=fs.readFileSync(file),target=path.join(source,relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes);assert.equal(sha(fs.readFileSync(file)),sha(bytes),'Concurrent input change: '+relative);pins.push({path:relative.replaceAll('\\','/'),bytes:bytes.length,sha256:sha(bytes)});for(const m of bytes.toString('utf8').matchAll(/^\s*#\s*include\s*"([^"\r\n]+)"/gm)){const found=[path.resolve(path.dirname(file),m[1]),...search.map(p=>path.resolve(p,m[1]))].find(p=>fs.existsSync(p)&&fs.statSync(p).isFile());if(found)freeze(found);}}
freeze(path.join(repo,'tools/decky_binding_runtime_selftest.cpp'));freeze(path.join(repo,'tools/decky_binding_runtime_selftest.mjs'));freeze(path.join(repo,'deps/json/json.hpp'));freeze(path.join(repo,'native/decky_context_cleanup_worker.h'));
// Derive only the declared private publication port after the product freeze.
freeze(path.join(repo,OBSERVER_PUBLICATION_GENERATOR));
freeze(path.join(repo,'tools/ymcc_sim/global/decky-update-recovery/native-source-admission.mjs'));
const observerHeader=path.join(source,OBSERVER_PUBLICATION_SOURCE),publication=steamSessionObserverPublication(fs.readFileSync(observerHeader));
assert.deepEqual(publication.original,pins.find(row=>row.path===OBSERVER_PUBLICATION_SOURCE),'OBSERVER_PUBLICATION_PREIMAGE_PIN');
fs.writeFileSync(observerHeader,publication.bytes);
const fixturePorts=[{path:observerHeader,bytes:publication.derived.bytes,sha256:publication.derived.sha256,generator:publication.generator,transformation:publication.transformation,original:publication.original,derived:publication.derived}];
// Constructing optional diagnostic arguments happens before the noexcept helper.
// Observe that exact product boundary only for this explicit existing parameter.
if(reportMemoryRequested){
 const relative='native/decky_sidebar_runtime.h',target=path.join(source,relative),before=fs.readFileSync(target),text=before.toString('utf8');
 const code='            using Action=ConnectionRecoveryWindow::Action;if(action==Action::none)return;';
 assert.equal(text.split(code).length-1,1,'Exact optional connection-report boundary');
 const at=text.indexOf(code)+code.length,eol=text.startsWith('\r\n',at)?'\r\n':'\n',anchor=code+eol;
 const marker='            reportAllocationProbe::Scope reportAllocationScope(static_cast<unsigned>(action)); // Private optional-report allocation boundary.'+eol;
 const after=Buffer.from(text.replace(anchor,anchor+marker));
 assert.deepEqual(Buffer.from(after.toString('utf8').replace(marker,'')),before,'REPORT_ALLOCATION_PORT_EXACT_INVERSE');
 const original={path:relative,bytes:before.length,sha256:sha(before)};
 assert.deepEqual(original,pins.find(row=>row.path===relative),'REPORT_ALLOCATION_PORT_ORIGINAL_PIN');
 fs.writeFileSync(target,after);fixturePorts.push({path:target,bytes:after.length,sha256:sha(after),generator:'tools/decky_binding_runtime_selftest.mjs',
  transformation:'One-shot allocation failure only around original optional connection reporting; actions, permissions and deadlines unchanged',
  original,markers:[marker],derived:{path:target,bytes:after.length,sha256:sha(after)}});
}
// Private fault port only for this explicit original-case parameter.
// One optional-report allocation fault on the actual ended/failed packet path.
if(watchEndReportRequested){
 const relative='native/decky_sidebar_runtime.h',target=path.join(source,relative),before=fs.readFileSync(target),text=before.toString('utf8');
 const code='bindingDiagnostic({{"bootstrapStage","failed"},{"reason",reason},{"mutated",nullptr},{"mutationState","unknown"}},"diagnostic");';
 assert.equal(text.split(code).length-1,1,'Exact original ended/failed optional diagnostic');
 const marker='reportAllocationProbe::Scope watchEndAllocationScope(9); /* Private one-shot optional watch end report allocation boundary. */';
 const after=Buffer.from(text.replace(code,marker+code));
 assert.deepEqual(Buffer.from(after.toString('utf8').replace(marker,'')),before,'WATCH_END_REPORT_PORT_EXACT_INVERSE');
 const original={path:relative,bytes:before.length,sha256:sha(before)};assert.deepEqual(original,pins.find(row=>row.path===relative),'WATCH_END_REPORT_ORIGINAL_PIN');
 fs.writeFileSync(target,after);fixturePorts.push({path:target,bytes:after.length,sha256:sha(after),generator:'tools/decky_binding_runtime_selftest.mjs',
  transformation:'One-shot actual optional ended/failed packet report allocation; original healing consumer unchanged',original,markers:[marker],derived:{path:target,bytes:after.length,sha256:sha(after)}});
}
if(loaderExitReportRequested){
 const relative='native/decky_sidebar_runtime.h',target=path.join(source,relative),before=fs.readFileSync(target),text=before.toString('utf8');
 const code='                bindingDiagnostic({{"scope","loader-exit"}';
 const sites=[];for(let at=text.indexOf(code);at>=0;at=text.indexOf(code,at+code.length))sites.push(at);
 assert.equal(sites.length,2,'Both actual optional Loader exit reporting sites are required');
 const markers=sites.map((_,index)=>'                reportAllocationProbe::Scope loaderExitAllocationScope('+(index===0?8:7)+'); // Private exact optional Loader-exit report boundary.\n');
 let changed=text;
 for(const site of sites.toReversed()){
  const guard=text.lastIndexOf('#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE',site),at=text.indexOf('\n',guard)+1;
  assert(guard>=0&&site-guard<100&&at>guard,'Exact original Loader-exit preprocessor boundary');
  changed=changed.slice(0,at)+markers[sites.indexOf(site)]+changed.slice(at);
 }
 assert.deepEqual(Buffer.from(markers.reduce((value,marker)=>value.replace(marker,''),changed)),before,'LOADER_EXIT_REPORT_PORT_EXACT_INVERSE');
 const original={path:relative,bytes:before.length,sha256:sha(before)};
 assert.deepEqual(original,pins.find(row=>row.path===relative),'LOADER_EXIT_REPORT_ORIGINAL_PIN');
 const after=Buffer.from(changed);fs.writeFileSync(target,after);
 fixturePorts.push({path:target,bytes:after.length,sha256:sha(after),generator:'tools/decky_binding_runtime_selftest.mjs',
  transformation:'One-shot optional Loader-exit diagnostic allocation failure at both existing sites; original cleanup/retry and deadlines unchanged',
  original,markers,derived:{path:target,bytes:after.length,sha256:sha(after)}});
}
// Observe the original full internal phase decision only for this explicit cost parameter.
if(runtimePhaseRequested){
 const relative='native/decky_sidebar_runtime.h',target=path.join(source,relative),before=fs.readFileSync(target),text=before.toString('utf8');
 const a='            if(enabled&&known&&steam&&!exitFenced_.load(std::memory_order_acquire)&&config_.connectionRecovery){try{config_.connectionRecovery();}catch(...){}}\n';
 const b='            HANDLE handles[5]{wake_};DWORD count=1;';
 const first='            runtimephaseprobe::Scope phaseStateCost; // Private full phase decision allocation scope.\n';
 const last='            phaseStateCost.complete(); // Keep original State lifetime until the original loop end.\n';
 assert.equal(text.split(a).length-1,1,'Exact original phase decision start');assert.equal(text.split(b).length-1,1,'Exact original phase decision end');
 const after=Buffer.from(text.replace(a,a+first).replace(b,last+b));
 assert.deepEqual(Buffer.from(after.toString('utf8').replace(first,'').replace(last,'')),before,'RUNTIME_PHASE_SCOPE_EXACT_INVERSE');
 const original={path:relative,bytes:before.length,sha256:sha(before)};assert.deepEqual(original,pins.find(v=>v.path===relative),'RUNTIME_PHASE_SCOPE_ORIGINAL_PIN');
 fs.writeFileSync(target,after);fixturePorts.push({path:target,bytes:after.length,sha256:sha(after),generator:'tools/decky_binding_runtime_selftest.mjs',
  transformation:'Only allocation observations around the original complete phase decision; original snapshot lifetime, lock and recovery unchanged',
  original,markers:[first,last],derived:{path:target,bytes:after.length,sha256:sha(after)}});
}
if(loaderCloseRequested){
 const relative='native/decky_sidebar_runtime.h',target=path.join(source,relative),before=fs.readFileSync(target);
 const eol=before.includes(Buffer.from('\r\n'))?'\r\n':'\n';
 const protect='            STARTUPINFOW startup{};startup.cb=sizeof(startup);',close='if(!CloseHandle(job)){';
 assert.equal(before.toString('utf8').split(protect).length-1,1,'Exact original owned Loader startup boundary');
 assert.equal(before.toString('utf8').split(close).length-1,1,'Exact original owned Loader final close boundary');
 const health='                const auto action=connectionFallback.observe(healthNow,health,[&](auto requested){';
 assert.equal(before.toString('utf8').split(health).length-1,1,'Exact current recovery observation boundary');
 const healthMarker='                loaderCloseProbe::observeHealth(health.paused,health.connected,connectionFallback.attempts()); // Private scalar observation only.'+eol;
 const marker='            loaderCloseProbe::protect(job); // Private real-OS fault boundary only.'+eol;
 const terminate='if(!TerminateJobObject(job,0)){',fallback='if(CloseHandle(job))job=nullptr;';
 assert.equal(before.toString('utf8').split(terminate).length-1,1,'Exact original Loader terminate boundary');
 assert.equal(before.toString('utf8').split(fallback).length-1,1,'Exact original Loader fallback close boundary');
 const text=before.toString('utf8').replace(protect,marker+protect).replace(close,'if(!loaderCloseProbe::close(job)){').replace(health,healthMarker+health)
  .replace(terminate,'if(!loaderCloseProbe::terminate(job,0)){').replace(fallback,'if(loaderCloseProbe::fallbackClose(job))job=nullptr;'),after=Buffer.from(text);
 assert.deepEqual(Buffer.from(text.replace(marker,'').replace(healthMarker,'').replace('if(!loaderCloseProbe::close(job)){',close).replace('if(!loaderCloseProbe::terminate(job,0)){',terminate).replace('if(loaderCloseProbe::fallbackClose(job))job=nullptr;',fallback)),before,'LOADER_CLOSE_PORT_EXACT_INVERSE');
 const original={path:relative,bytes:before.length,sha256:sha(before)};
 assert.deepEqual(original,pins.find(row=>row.path===relative),'LOADER_CLOSE_PORT_ORIGINAL_PIN');
 fs.writeFileSync(target,after);
 fixturePorts.push({path:target,bytes:after.length,sha256:sha(after),generator:'tools/decky_binding_runtime_selftest.mjs',
  transformation:'Observe exactly original Loader Job setup and final native close with actual protect-close fixture; no product behavior replacement',
  original,markers:[marker,healthMarker],derived:{path:target,bytes:after.length,sha256:sha(after)}});
}

if(runtimeDetailRequested){
 const relative='native/decky_sidebar_runtime.h',target=path.join(source,relative),before=fs.readFileSync(target),text=before.toString('utf8');
 const eol=before.includes(Buffer.from('\r\n'))?'\r\n':'\n';
 const matches=[...text.matchAll(/^([ \t]*)(?:const )?auto(?:&)? detail=(?:packet\.value\.(?:value\("detail",nlohmann::json::object\(\)\)|at\("detail"\))|std::move\(packet\.value\.at\("detail"\)\));/gm)];
 assert.equal(matches.length,2,'Expected exact initial/watch detail consumers');
 const markers=[];let after=before;
 for(const [index,match] of Array.from(matches.entries()).toReversed()){
  const at=Buffer.byteLength(text.slice(0,match.index)),length=Buffer.byteLength(match[0]);
  const enter=Buffer.from(match[1]+'ymcc::deckybinding::fixture::detailprobe::begin(packet.value,'+(index+1)+'); // Private detail observation only.'+eol);
  const exit=Buffer.from(eol+match[1]+'ymcc::deckybinding::fixture::detailprobe::end(detail); // Private detail observation only.');
  after=Buffer.concat([after.subarray(0,at),enter,after.subarray(at,at+length),exit,after.subarray(at+length)]);markers.push(enter,exit);
 }
 let inverse=after;for(const marker of markers){assert.equal(inverse.indexOf(marker),inverse.lastIndexOf(marker));const at=inverse.indexOf(marker);inverse=Buffer.concat([inverse.subarray(0,at),inverse.subarray(at+marker.length)]);}
 const callBefore='                    bindingDiagnostic(detail,kind.c_str());',callMoved='                    bindingDiagnostic(std::move(detail),kind.c_str());';
 const diagnosticCall=after.toString('utf8').includes(callBefore)?callBefore:callMoved;
 const oldFunctionStart='    void bindingDiagnostic(nlohmann::json detail,const char* kind) noexcept {';
 const borrowedFunctionStart='    void bindingDiagnostic(const nlohmann::json& detail,const char* kind) noexcept {';
 const functionStart=after.toString('utf8').includes(borrowedFunctionStart)?borrowedFunctionStart:oldFunctionStart;
 for(const [anchor,marker,afterAnchor] of [
  [diagnosticCall,Buffer.from('                    ymcc::deckybinding::fixture::diagnosticprobe::begin(detail); // Private diagnostic argument observation only.'+eol),false],
  [functionStart,Buffer.from(eol+'        ymcc::deckybinding::fixture::diagnosticprobe::end(detail); // Private diagnostic argument observation only.'),true]
 ]){
  assert.equal(after.toString('utf8').split(anchor).length-1,1,'Missing exact watch diagnostic boundary');
  const at=after.indexOf(Buffer.from(anchor))+(afterAnchor?Buffer.byteLength(anchor):0);after=Buffer.concat([after.subarray(0,at),marker,after.subarray(at)]);markers.push(marker);
 }
 inverse=after;for(const marker of markers){assert.equal(inverse.indexOf(marker),inverse.lastIndexOf(marker));const at=inverse.indexOf(marker);inverse=Buffer.concat([inverse.subarray(0,at),inverse.subarray(at+marker.length)]);}
 assert.deepEqual(inverse,before,'DETAIL_PORT_EXACT_INVERSE');
 const original={path:relative,bytes:before.length,sha256:sha(before)};assert.deepEqual(original,pins.find(row=>row.path===relative),'DETAIL_PORT_ORIGINAL_PIN');
 fs.writeFileSync(target,after);fixturePorts.push({path:target,bytes:after.length,sha256:sha(after),generator:'tools/decky_binding_runtime_selftest.mjs',transformation:'Observe allocation requests only around two exact original detail selection statements; initial result plus watch envelopes',original,markers:markers.map(marker=>marker.toString('utf8')),derived:{path:target,bytes:after.length,sha256:sha(after)}});
}else if(packetPublicationRequested){
 for(const [relative,anchor,statement] of [
  ['native/decky_binding_client.h','            return {Packet::message, value, {}};','            ymcc::deckybinding::fixture::publicationprobe::begin(value);'],
  ['native/decky_sidebar_runtime.h','            const auto packet=config_.ownedBinding->next();'+String.fromCharCode(10)+'            using Packet=decltype(packet);','            ymcc::deckybinding::fixture::publicationprobe::end(packet);'],
  ['native/decky_sidebar_runtime.h','                const auto packet=config_.ownedBinding->next();using Packet=decltype(packet);','                ymcc::deckybinding::fixture::publicationprobe::end(packet);']
 ]){
  const target=path.join(source,relative),before=fs.readFileSync(target),eol=before.includes(Buffer.from('\r\n'))?'\r\n':'\n';
  let actualAnchor=anchor;
  if(relative.endsWith('client.h')&&!before.toString('utf8').includes(anchor))actualAnchor=anchor.replace('Packet::message, value','Packet::message, std::move(value)');
  assert.equal(before.toString('utf8').split(actualAnchor).length-1,1,'Missing original packet publication checkpoint');
  const afterCall=relative.endsWith('runtime.h'),marker=Buffer.from((afterCall?eol:'')+statement+' // Private publication observation only.'+(afterCall?'':eol));
  const at=before.indexOf(Buffer.from(actualAnchor))+(afterCall?Buffer.byteLength(actualAnchor):0),after=Buffer.concat([before.subarray(0,at),marker,before.subarray(at)]);
  let row=fixturePorts.find(row=>row.original.path===relative);
  const original={path:relative,bytes:before.length,sha256:sha(before)};
  if(!row){assert.deepEqual(original,pins.find(row=>row.path===relative),'PACKET_PORT_ORIGINAL_PIN');row={path:target,generator:'tools/decky_binding_runtime_selftest.mjs',transformation:'Final actual Client packet publication plus immediately following original Runtime consume; no source algorithm replacement',original,markers:[]};fixturePorts.push(row);}
  fs.writeFileSync(target,after);row.markers.push(marker.toString('utf8'));Object.assign(row,{bytes:after.length,sha256:sha(after),derived:{path:target,bytes:after.length,sha256:sha(after)}});
 }
}else if(constructorRequested&&!runtimePhaseRequested){
 const target=path.join(source,'native/decky_binding_client.h'),before=fs.readFileSync(target);
 const eol=before.includes(Buffer.from('\r\n'))?'\r\n':'\n';
 const entries=[
  ['                s = std::make_shared<Session>();','                ymcc::deckybinding::fixture::constructionprobe::enter();'],
  ['            const auto bytes = request.dump();','            ymcc::deckybinding::fixture::constructionprobe::preparation(1);'],
  ['            s->request = worker::parse(bytes); s->identified = true;','            ymcc::deckybinding::fixture::constructionprobe::preparation(2);'],
  ['            const std::vector<std::uint8_t> payload(bytes.begin(), bytes.end());','            ymcc::deckybinding::fixture::constructionprobe::preparation(3);'],
  ['            auto result = s->owner.start(identityCancel);','            ymcc::deckybinding::fixture::constructionprobe::preparation(6);'],
  ['            result = s->owner.writeFrame(payload, 150, result.generation);','            ymcc::deckybinding::fixture::constructionprobe::preparation(4);']
 ];
 let after=before;const markers=[];
 for(const [anchor,statement] of entries){
  const needle=Buffer.from(anchor),marker=Buffer.from(statement+' // Private allocation seam only.'+eol);
  assert.equal(after.toString('utf8').split(anchor).length-1,1,'Missing unique Client preparation anchor');
  const at=after.indexOf(needle);after=Buffer.concat([after.subarray(0,at),marker,after.subarray(at)]);markers.push(marker);
 }
 const ownerCall='            auto result = s->owner.start(identityCancel);';
 const ownerMarker=Buffer.from(eol+'            ymcc::deckybinding::fixture::constructionprobe::ownerReturned(result); // Private original Owner return observation only.');
 const ownerEnd=after.indexOf(Buffer.from(ownerCall))+Buffer.byteLength(ownerCall);assert(ownerEnd>=Buffer.byteLength(ownerCall));after=Buffer.concat([after.subarray(0,ownerEnd),ownerMarker,after.subarray(ownerEnd)]);markers.push(ownerMarker);
 const childChoices=['                s->child.value = s->owner.copyChildProcess();','                s->child.value = s->owner.copyChildProcess(&handoffError);'];
 const attachChoices=['                const bool attached = s->reader.attach(pipe); // Adopt the pipe even if the process copy failed.','                if (!s->reader.attach(pipe, &handoffError)) return failedHandoff();'];
 for(const [choices,statement] of [[childChoices,'                ymcc::deckybinding::fixture::constructionprobe::beforeChild(s->owner);'],[attachChoices,'                ymcc::deckybinding::fixture::constructionprobe::beforeAttach();']]){
  const anchor=choices.find(anchor=>after.toString('utf8').includes(anchor));assert(anchor,'Missing original handoff checkpoint');assert.equal(after.toString('utf8').split(anchor).length-1,1);
  const marker=Buffer.from(statement+' // Private handoff failure boundary only.'+eol),at=after.indexOf(Buffer.from(anchor));after=Buffer.concat([after.subarray(0,at),marker,after.subarray(at)]);markers.push(marker);
 }
 const write='            result = s->owner.writeFrame(payload, 150, result.generation);';
 const written=Buffer.from(eol+'            if(result){++ymcc::deckybinding::fixture::constructionprobe::writeSuccesses;ymcc::deckybinding::fixture::constructionprobe::preparation(5);} // Private post-write seam only.');
 const end=after.indexOf(Buffer.from(write))+Buffer.byteLength(write);after=Buffer.concat([after.subarray(0,end),written,after.subarray(end)]);markers.push(written);
 let reversed=after;for(const marker of markers.toReversed()){assert.equal(reversed.indexOf(marker),reversed.lastIndexOf(marker));const at=reversed.indexOf(marker);reversed=Buffer.concat([reversed.subarray(0,at),reversed.subarray(at+marker.length)]);}
 assert.deepEqual(reversed,before,'PRIVATE_PREPARATION_PORT_EXACT_INVERSE');fs.writeFileSync(target,after);
 const original={path:'native/decky_binding_client.h',bytes:before.length,sha256:sha(before)};
 assert.deepEqual(original,pins.find(row=>row.path===original.path),'CONSTRUCTOR_PORT_ORIGINAL_PIN');
 fixturePorts.push({path:target,bytes:after.length,sha256:sha(after),generator:'tools/decky_binding_runtime_selftest.mjs',
  transformation:'Private actual allocation checkpoints before dump/parse/payload; synthetic write-entry/post-write exceptions separately labelled; exact marker inverse',original,
  markers:markers.map(marker=>marker.toString('utf8')),derived:{path:target,bytes:after.length,sha256:sha(after)}});
}

if(constructorRequested&&!packetPublicationRequested&&!runtimeDetailRequested&&!runtimePhaseRequested){
 const relative='native/decky_sidebar_runtime.h',target=path.join(source,relative),before=fs.readFileSync(target),anchor='        const auto started=config_.ownedBinding->begin(plan.request,intent,plan.delegation);';
 assert.equal(before.toString('utf8').split(anchor).length-1,1);const eol=before.includes(Buffer.from('\r\n'))?'\r\n':'\n';
 const marker=Buffer.from(eol+'        ymcc::deckybinding::fixture::constructionprobe::clientReturned(started); // Private original Client return observation only.');
 const at=before.indexOf(Buffer.from(anchor))+Buffer.byteLength(anchor),after=Buffer.concat([before.subarray(0,at),marker,before.subarray(at)]);
 fs.writeFileSync(target,after);const original={path:relative,bytes:before.length,sha256:sha(before)};assert.deepEqual(original,pins.find(row=>row.path===relative));
 fixturePorts.push({path:target,bytes:after.length,sha256:sha(after),generator:'tools/decky_binding_runtime_selftest.mjs',transformation:'Observe original Client begin return before Runtime branches',original,markers:[marker.toString('utf8')],derived:{path:target,bytes:after.length,sha256:sha(after)}});
}


// Explicit original result-path parameter only. Clear a still-unconsumed TLS
// fault on leaving the original result block, before unrelated context/launch IO.
if(bindingReportRequested&&process.argv[4].startsWith(bindingReportPrefix+'reason-')){
 const relative='native/decky_sidebar_runtime.h',target=path.join(source,relative),before=fs.readFileSync(target);
 const anchor='                    optionalBindingDiagnostic([&]() -> const nlohmann::json& {return detail;},"result");';
 assert.equal(before.toString('utf8').split(anchor).length-1,1,'Exact original binding result block required');
 const at=before.indexOf(Buffer.from(anchor))+Buffer.byteLength(anchor);
 const marker=Buffer.from('\n                    ::bindingReportProbe::ReasonScope bindingReasonScope; // Private original result allocation window only.');
 const after=Buffer.concat([before.subarray(0,at),marker,before.subarray(at)]),inverse=Buffer.concat([after.subarray(0,at),after.subarray(at+marker.length)]);
 assert.deepEqual(inverse,before,'RESULT_REASON_PORT_EXACT_INVERSE');
 const original={path:relative,bytes:before.length,sha256:sha(before)};assert.deepEqual(original,pins.find(row=>row.path===relative));
 fs.writeFileSync(target,after);fixturePorts.push({path:target,bytes:after.length,sha256:sha(after),generator:'tools/decky_binding_runtime_selftest.mjs',transformation:'Scope existing TLS allocation failure only to original result handling; no business replacement or product hook',original,markers:[marker.toString('utf8')],derived:{path:target,bytes:after.length,sha256:sha(after)}});
}


// Explicit original-case diagnosis only. Mark the actual JSON destroy reserve;
// ordinary generator/product dependency bytes remain unchanged.
if(bindingReportRequested&&(process.argv[4].startsWith(bindingReportPrefix+'diagnostic-tail')||['borrowed-diagnostic','borrowed-payloads','borrowed-throws'].includes(process.argv[4].slice(bindingReportPrefix.length)))){
 const relative='deps/json/json.hpp',target=path.join(source,relative),before=fs.readFileSync(target),anchor='                    stack.reserve(object->size());';
 assert.equal(before.toString('utf8').split(anchor).length-1,1,'Exact vendor object destruction reserve required');
 const eol=before.includes(Buffer.from('\r\n'))?'\r\n':'\n',marker=Buffer.from('                    ::bindingReportProbe::JsonDestroyScope jsonDestroyScope(object->size()); // Private actual JSON destruction boundary.'+eol);
 const at=before.indexOf(Buffer.from(anchor)),after=Buffer.concat([before.subarray(0,at),marker,before.subarray(at)]);assert.deepEqual(Buffer.concat([after.subarray(0,at),after.subarray(at+marker.length)]),before,'JSON_DESTROY_DIAGNOSIS_EXACT_INVERSE');
 const original={path:relative,bytes:before.length,sha256:sha(before)};assert.deepEqual(original,pins.find(row=>row.path===relative));fs.writeFileSync(target,after);
 fixturePorts.push({path:target,bytes:after.length,sha256:sha(after),generator:'tools/decky_binding_runtime_selftest.mjs',transformation:'Fixed TLS scope at actual vendor object destroy reserve; no algorithm replacement or product vendor edit',original,markers:[marker.toString('utf8')],derived:{path:target,bytes:after.length,sha256:sha(after)}});
}


// The existing optional dispatch call is the complete lifetime of the extra
// legacy row. This scope is generated only for these explicit parameters.
if(bindingReportRequested&&['borrowed-diagnostic','borrowed-payloads','borrowed-throws'].includes(process.argv[4].slice(bindingReportPrefix.length))){
 const relative='native/decky_sidebar_runtime.h',target=path.join(source,relative),before=fs.readFileSync(target),anchor='    void optionalBindingDiagnostic(Make&& make,const char* kind) noexcept {';
 assert.equal(before.toString('utf8').split(anchor).length-1,1,'Exact original optional diagnostic dispatch required');
 const marker=Buffer.from('\n        ::bindingReportProbe::BorrowedOptionalScope borrowedOptionalScope; // Private complete optional diagnostic lifetime.');
 const at=before.indexOf(Buffer.from(anchor))+Buffer.byteLength(anchor),after=Buffer.concat([before.subarray(0,at),marker,before.subarray(at)]);
 assert.deepEqual(Buffer.concat([after.subarray(0,at),after.subarray(at+marker.length)]),before,'BORROWED_OPTIONAL_SCOPE_EXACT_INVERSE');
 const original={path:relative,bytes:before.length,sha256:sha(before)};assert.deepEqual(original,pins.find(row=>row.path===relative));fs.writeFileSync(target,after);
 fixturePorts.push({path:target,bytes:after.length,sha256:sha(after),generator:'tools/decky_binding_runtime_selftest.mjs',transformation:'Observe the actual optional diagnostic call lifetime without re-evaluating its factory or replacing Runtime',original,markers:[marker.toString('utf8')],derived:{path:target,bytes:after.length,sha256:sha(after)}});
}

// Actual resource construction faults stay in the derived compiler input;
// the original product header remains free of probes in ordinary builds.
if(resourceWorkerRequested){
 const relative='native/decky_resource_worker.h',target=path.join(source,relative),before=fs.readFileSync(target);
 let after=before;const markers=[];
 const entries=[
  [['Owner owner;','ownerStorage.emplace();'],'ymcc::deckybinding::fixture::constructionprobe::preparation(18); /* Private resource constructor allocation boundary. */'],
  [['clientdetail::EventReader reader;','readerStorage.emplace();'],'ymcc::deckybinding::fixture::constructionprobe::preparation(20); /* Private resource constructor allocation boundary. */'],
  [['        auto started=owner.start();'],'        ++ymcc::deckybinding::fixture::constructionprobe::resourceStarts; /* Private resource start observation. */\n'],
  [['                const auto reply=Json::parse(payload,'],'                ymcc::deckybinding::fixture::constructionprobe::preparation(21); /* Private postwrite allocation boundary. */\n']
 ];
 const diagnosticRequested=resourceWorkerRequested;
 const preparationRequested=resourceWorkerRequested;
 if(preparationRequested)entries.push(
  [['    Json result={{"ready",false},{"reason","resource-worker-unavailable"}};','        result={{"ready",false},{"reason","resource-worker-unavailable"}};'],'    ymcc::deckybinding::fixture::constructionprobe::preparation(22); /* Private initial result allocation boundary. */\n'],
  [['        const Json request='],'        ymcc::deckybinding::fixture::constructionprobe::preparation(23); /* Private resource request allocation boundary. */\n'],
  [['resourceStage="send";const auto bytes=request.dump();'],'ymcc::deckybinding::fixture::constructionprobe::preparation(24); /* Private resource serialization boundary. */'],
  [['require(static_cast<bool>(owner.writeFrame(std::vector<std::uint8_t>(bytes.begin(),bytes.end()),150))','const std::vector<std::uint8_t> requestPayload(bytes.begin(),bytes.end());'],'ymcc::deckybinding::fixture::constructionprobe::preparation(25); /* Private resource payload boundary. */']
 );
 if(diagnosticRequested)entries.push(
  [['result["resourceWorkerStage"]=resourceStage;'],'ymcc::deckybinding::fixture::constructionprobe::resourceDiagnostic(owner.snapshot(),reader.snapshot()); /* Private optional diagnostic allocation boundary. */']
 );
 for(const [choices,text] of entries){
  const anchor=choices.find(value=>after.includes(Buffer.from(value)));assert(anchor,'Missing original resource allocation boundary');
  assert.equal(after.toString('utf8').split(anchor).length-1,1);const marker=Buffer.from(text),at=after.indexOf(Buffer.from(anchor));
  after=Buffer.concat([after.subarray(0,at),marker,after.subarray(at)]);markers.push(marker);
 }
 if(diagnosticRequested){
  const errorAnchor='if(workerStarted&&!writeAttempted)prewriteMemoryFailure=true;';
  assert.equal(after.toString('utf8').split(errorAnchor).length-1,1);
  const errorMarker=Buffer.from('ymcc::deckybinding::fixture::constructionprobe::resourceErrorResult(owner.snapshot(),reader.snapshot()); /* Private error result allocation boundary. */');
  let at=after.indexOf(Buffer.from(errorAnchor))+Buffer.byteLength(errorAnchor);after=Buffer.concat([after.subarray(0,at),errorMarker,after.subarray(at)]);markers.push(errorMarker);
  const cleanupChoices=[
   'const auto snapshot=owner.snapshot();const auto resourcesDestroyed=stopped.fullyDestroyed&&readerStopped&&snapshot.handles==0&&snapshot.pendingIo==0&&snapshot.jobActiveProcesses==0;',
   'const auto snapshot=owner.snapshot();resourcesDestroyed=stopped.fullyDestroyed&&readerStopped&&snapshot.handles==0&&snapshot.pendingIo==0&&snapshot.jobActiveProcesses==0;'
  ];
  const cleanupAnchor=cleanupChoices.find(value=>after.includes(Buffer.from(value)));assert(cleanupAnchor);assert.equal(after.toString('utf8').split(cleanupAnchor).length-1,1);
  const cleanupMarker=Buffer.from('ymcc::deckybinding::fixture::constructionprobe::resourceCleanup(snapshot,reader.snapshot(),child.value,resourcesDestroyed); /* Private original cleanup completion observation. */');
  at=after.indexOf(Buffer.from(cleanupAnchor))+Buffer.byteLength(cleanupAnchor);after=Buffer.concat([after.subarray(0,at),cleanupMarker,after.subarray(at)]);markers.push(cleanupMarker);
 }
 let inverse=after;for(const marker of markers.toReversed()){assert.equal(inverse.indexOf(marker),inverse.lastIndexOf(marker));const at=inverse.indexOf(marker);inverse=Buffer.concat([inverse.subarray(0,at),inverse.subarray(at+marker.length)]);}
 assert.deepEqual(inverse,before,'PRIVATE_RESOURCE_CONSTRUCTION_PORT_EXACT_INVERSE');
 const original={path:relative,bytes:before.length,sha256:sha(before)};assert.deepEqual(original,pins.find(row=>row.path===relative));
 fs.writeFileSync(target,after);fixturePorts.push({path:target,bytes:after.length,sha256:sha(after),generator:'tools/decky_binding_runtime_selftest.mjs',
  transformation:'Actual Owner/reader and resource preparation allocation boundaries; actual postwrite JSON parse allocation; exact original start observation',original,markers:markers.map(value=>value.toString('utf8')),derived:{path:target,bytes:after.length,sha256:sha(after)}});
 if(preparationRequested){
  const ownerRelative='native/decky_binding_process_owner.h',ownerTarget=path.join(source,ownerRelative),ownerBefore=fs.readFileSync(ownerTarget);
  const anchor='        return detail::frame(*state_, true, payload, timeoutMs, expectedGeneration);';
  assert.equal(ownerBefore.toString('utf8').split(anchor).length-1,1);
  const marker=Buffer.from('        ymcc::deckybinding::fixture::constructionprobe::preparation(26); /* Private original write entry allocation boundary. */\n');
  const at=ownerBefore.indexOf(Buffer.from(anchor)),ownerAfter=Buffer.concat([ownerBefore.subarray(0,at),marker,ownerBefore.subarray(at)]);
  assert.deepEqual(Buffer.concat([ownerAfter.subarray(0,at),ownerAfter.subarray(at+marker.length)]),ownerBefore);
  const original={path:ownerRelative,bytes:ownerBefore.length,sha256:sha(ownerBefore)};assert.deepEqual(original,pins.find(row=>row.path===ownerRelative));
  fs.writeFileSync(ownerTarget,ownerAfter);fixturePorts.push({path:ownerTarget,bytes:ownerAfter.length,sha256:sha(ownerAfter),generator:'tools/decky_binding_runtime_selftest.mjs',
   transformation:'Actual original Owner writeFrame entry before its first allocation; request payload already evaluated',original,markers:[marker.toString('utf8')],derived:{path:ownerTarget,bytes:ownerAfter.length,sha256:sha(ownerAfter)}});
 }
}

// A resource-only, same-stack observation scope. Probe the old copied
// handle separately; never protect the candidate Owner's actual handle.
if(resourceWorkerRequested){
 const relative='native/decky_resource_worker.h',target=path.join(source,relative),before=fs.readFileSync(target);
 const choices=['child.value=owner.copyChildProcess(&resourceReadError);','child.value=owner.borrowChildProcessForObservation(&resourceReadError);'];
 const anchor=choices.find(value=>before.includes(Buffer.from(value)));assert(anchor);assert.equal(before.toString('utf8').split(anchor).length-1,1);
 const begin=Buffer.from('ymcc::deckybinding::fixture::constructionprobe::resourceObservationBegin(); /* Private resource observation API count. */');
 const end=Buffer.from('ymcc::deckybinding::fixture::constructionprobe::resourceObservationEnd(child.value,owner.snapshot().processId,'+(anchor.includes('copyChildProcess')?'true':'false')+'); /* Private held observation identity/protection. */');
 const at=before.indexOf(Buffer.from(anchor)),after=Buffer.concat([before.subarray(0,at),begin,Buffer.from(anchor),end,before.subarray(at+Buffer.byteLength(anchor))]);
 let inverse=after;for(const value of [end,begin]){assert.equal(inverse.indexOf(value),inverse.lastIndexOf(value));const index=inverse.indexOf(value);inverse=Buffer.concat([inverse.subarray(0,index),inverse.subarray(index+value.length)]);}
 assert.deepEqual(inverse,before);
 let row=fixturePorts.find(value=>value.original?.path===relative);
 const intermediate={path:target,bytes:before.length,sha256:sha(before)};
 if(row){
  assert.deepEqual(intermediate,row.derived,'RESOURCE_OBSERVATION_INTERMEDIATE_PIN');
 }else{
  const original={path:relative,bytes:before.length,sha256:sha(before)};
  assert.deepEqual(original,pins.find(value=>value.path===relative),'RESOURCE_OBSERVATION_ORIGINAL_PIN');
  row={path:target,generator:'tools/decky_binding_runtime_selftest.mjs',transformation:'Actual resource observation call, optionally composed after exact resource allocation/cleanup ports',original,markers:[]};fixturePorts.push(row);
 }
 row.markers.push(begin.toString('utf8'),end.toString('utf8'));
 inverse=after;
 for(const text of row.markers.toReversed()){
  const marker=Buffer.from(text),index=inverse.indexOf(marker);
  assert(index>=0&&index===inverse.lastIndexOf(marker),'RESOURCE_OBSERVATION_COMPOSED_MARKER_UNIQUE');
  inverse=Buffer.concat([inverse.subarray(0,index),inverse.subarray(index+marker.length)]);
 }
 assert.deepEqual({path:relative,bytes:inverse.length,sha256:sha(inverse)},row.original,'RESOURCE_OBSERVATION_COMPOSED_ORIGINAL_PIN');
 assert.deepEqual(inverse,fs.readFileSync(path.join(repo,relative)),'RESOURCE_OBSERVATION_COMPOSED_EXACT_PRODUCT');
 Object.assign(row,{bytes:after.length,sha256:sha(after),derived:{path:target,bytes:after.length,sha256:sha(after)}});
 fs.writeFileSync(target,after);
}

// Explicit resource IO fault scopes, composed onto this same derived header.
if(resourceWorkerRequested){
 const relative='native/decky_resource_worker.h',target=path.join(source,relative),before=fs.readFileSync(target);let after=before;const markers=[];
 const sendChoices=['require(static_cast<bool>(owner.writeFrame(requestPayload,150)),"resource-worker-send-failed");','const auto sent=owner.writeFrame(requestPayload,150);'];
 const send=sendChoices.find(value=>after.includes(Buffer.from(value)));assert(send);
 const sendEnd=send.startsWith('const auto sent')?'require(static_cast<bool>(sent),"resource-worker-send-failed");':send;
 const wait='HANDLE waiting[]{reader.event(),child.value};const auto wait=WaitForMultipleObjects(childExitedObserved?1u:2u,waiting,FALSE,static_cast<DWORD>((std::min)(50ull,deadline-now)));';
 for(const [anchor,text,following] of [
  [send,'ymcc::deckybinding::fixture::constructionprobe::ResourceApiScope resourceSendScope(ymcc::deckybinding::fixture::constructionprobe::resourceSendActive); /* Private actual send API scope. */',false],
  [sendEnd,'resourceSendScope.finish(); /* Private actual send API scope end. */',true],
  [wait,'ymcc::deckybinding::fixture::constructionprobe::ResourceApiScope resourceWaitScope(ymcc::deckybinding::fixture::constructionprobe::resourceWaitActive); /* Private actual wait API scope. */',false],
  [wait,'resourceWaitScope.finish(); /* Private actual wait API scope end. */',true]
 ]){
  const bytes=Buffer.from(anchor);assert.equal(after.indexOf(bytes),after.lastIndexOf(bytes));assert(after.indexOf(bytes)>=0);
  const marker=Buffer.from(text),at=after.indexOf(bytes)+(following?bytes.length:0);after=Buffer.concat([after.subarray(0,at),marker,after.subarray(at)]);markers.push(marker);
 }
 const row=fixturePorts.find(value=>value.original?.path===relative);assert(row);
 assert.deepEqual({path:target,bytes:before.length,sha256:sha(before)},row.derived,'RESOURCE_IO_INTERMEDIATE_EXACT_PIN');
 row.markers.push(...markers.map(value=>value.toString('utf8')));let inverse=after;
 for(const text of row.markers.toReversed()){const marker=Buffer.from(text),at=inverse.indexOf(marker);assert(at>=0&&at===inverse.lastIndexOf(marker));inverse=Buffer.concat([inverse.subarray(0,at),inverse.subarray(at+marker.length)]);}
 assert.deepEqual(inverse,fs.readFileSync(path.join(repo,relative)),'RESOURCE_IO_FULL_PRODUCT_INVERSE');
 Object.assign(row,{bytes:after.length,sha256:sha(after),derived:{path:target,bytes:after.length,sha256:sha(after)}});fs.writeFileSync(target,after);
}

// Explicit focused counterexample, in the frozen compiler input only. Ordinary
// source pins remain the actual product preimage; never label an override as it.
const runtimeOverride=process.env.YMCC_DECKY_RUNTIME_HEADER_OVERRIDE;
if(runtimeOverride){const bytes=fs.readFileSync(runtimeOverride),target=path.join(source,'native/decky_sidebar_runtime.h');fs.writeFileSync(target,bytes);fs.writeFileSync(path.join(out,'SOURCE-OVERRIDES.json'),JSON.stringify({path:runtimeOverride,sha256:sha(bytes),compiledPath:target,reason:'explicit focused semantic counterexample'},null,2)+'\n');}
// Static event binding only: main.cpp is frozen for attribution, never run.
const mainFile=path.join(repo,'native/main.cpp'),mainBytes=fs.readFileSync(mainFile),mainText=mainBytes.toString('utf8');
const exitQuery=mainText.match(/case WM_QUERYENDSESSION:([\s\S]*?)case WM_ENDSESSION:/);
assert(exitQuery&&!exitQuery[1].includes('requestExitFence'),'A query/cancelled OS session end must not seal Decky');
assert(/case WM_ENDSESSION:\s*if \(w\) \{g_deckySidebarRuntime[.]requestExitFence\(\);/.test(mainText),'Confirmed OS session end must fence before serial cleanup');
// The input owner can seal first on unexpected window destruction. Accept
// only that explicit prefix; Decky must still fence before lighting cleanup.
assert(/case WM_DESTROY:\s*(?:\{\s*const bool normalExit=g_exitRequested[.]load\(std::memory_order_acquire\);\s*(?:g_wakeGameRecoveryStatusWindow[.]store\(nullptr, std::memory_order_release\);\s*g_wakeGameRecoveryStatusMailbox[.]retire\(\);\s*)?(?:steamControllerCursorShutdown\(\);\s*)?if\(!normalExit\)inputCaptureBeginExitOwner\(0,"window-destroy"\);[^\r\n]*\s*)?g_deckySidebarRuntime[.]requestExitFence\(\);\s*nativeAyaLightingSessionStop/.test(mainText),'Actual main-window destruction must seal Decky');
assert(/if \(!g_exitRequested[.]compare_exchange_strong\(expected, true\)\) return;\r?\n\s*(?:closeSharedPowerMirrors\(\);\s*)?(?:g_gamepadSerialEventMailbox->retire\(\);\s*)?(?:GameSteamContext::cancelCompletionNotice\(\);\s*g_steamTrackerCompletionDispatcher->retire\(\);\s*)?g_deckySidebarRuntime[.]requestExitFence\(\);/.test(mainText),'Only accepted explicit exit may seal Decky');
const commit=mainText.match(/case WM_POWER_RESUME_COMMIT:[\s\S]*?(?=\n\s*case WM_POWER_INPUT_RECOVER:)/);
assert(commit,'Missing WM_POWER_RESUME_COMMIT block');
const directCall='g_deckySidebarRuntime.requestBindingRefresh(currentPowerGeneration())',sharedCall='deckySidebarResumeAfterIntent(currentPowerGeneration(),"committed-ready")';
const capturedCall='deckySidebarResumeAfterIntent(generation,"committed-ready")';
const call=commit[0].includes(capturedCall)?capturedCall:commit[0].includes(sharedCall)?sharedCall:directCall;
assert.equal(commit[0].split(call).length-1,1,'Committed resume must bind the existing production consumer exactly once');
assert(commit[0].includes('PowerLifecycle::Ready'),'Committed resume must retain the Ready guard');
const consumer={path:'native/main.cpp',sourceSha256:sha(mainBytes),event:'WM_POWER_RESUME_COMMIT',call,callLine:mainText.slice(0,commit.index+commit[0].indexOf(call)).split('\n').length,excerptSha256:sha(Buffer.from(commit[0])),verification:'static call binding; OS event dispatch not executed'};
consumer.terminalExit={confirmedEndSession:true,queryDoesNotFence:true,mainWindowDestroy:true,acceptedNormalExit:true,validation:'Static actual main call binding; original Runtime exit consumer executes separately, no real OS session action'};
// Execute the exact Decky-only consumer from the alternate S0 Ready path too.
const intent=mainText.match(/static bool deckySidebarResumeAfterIntent\(unsigned long long generation(?:,const char\* scope="sleep-intent-ready")?\) \{[\s\S]*?\n\}/);
assert(intent,'Missing alternate S0 Ready consumer');
const abort=mainText.slice(mainText.indexOf('static void sgAbortSleepIntent(const char* reason, ULONGLONG canceledThroughFileTime) {'),mainText.indexOf('// Idle owns no periodic wakeup.',mainText.indexOf('static void sgAbortSleepIntent(const char* reason, ULONGLONG canceledThroughFileTime) {')));
assert.equal(abort.split('deckySidebarResumeAfterIntent(generation)').length-1,1,'Actual S0 Ready path must notify Decky once');
assert(abort.indexOf('g_powerLifecycle.store(PowerLifecycle::Ready')<abort.indexOf('deckySidebarResumeAfterIntent(generation)'),'Decky may only consume committed Ready');
let intentText=intent[0];const override=process.env.YMCC_DECKY_WAKE_READY_OVERRIDE;
if(override){intentText=fs.readFileSync(override,'utf8');consumer.intentOverride={path:override,sha256:sha(Buffer.from(intentText)),scope:'explicit negative variant'};}
fs.writeFileSync(path.join(source,'native/decky_wake_ready_consumer.inc'),intentText+'\n');
const committedBegin=commit[0].indexOf('// YMCC Decky:'),committedEnd=commit[0].indexOf('ipc_emit("power.resumed"');
assert(committedBegin>=0&&committedEnd>committedBegin,'Missing Decky-only committed Ready consumer');
const capturedBegin=commit[0].indexOf(capturedCall),capturedEnd=capturedBegin+capturedCall.length;
const committedConsumer=call===capturedCall?'const auto generation=currentPowerGeneration();\n'+commit[0].slice(capturedBegin,capturedEnd)+';':commit[0].slice(committedBegin,committedEnd).trim();
fs.writeFileSync(path.join(source,'native/decky_wake_committed_consumer.inc'),'inline void productionCommittedReady(){\n'+committedConsumer+'\n}\n');
consumer.committedReady={sourceSha256:sha(Buffer.from(committedConsumer)),execution:call===capturedCall?'Exact captured-generation Decky leaf with current generation supplied by typed fixture; committed fanout not executed':'Exact committed Decky slice compiled and run with real Runtime and typed binding; OS/session boundaries inert'};
consumer.alternateReady={path:'native/main.cpp',function:'deckySidebarResumeAfterIntent',sourceSha256:sha(Buffer.from(intent[0])),actualCallIn:'sgAbortSleepIntent after real Ready',execution:'Actual extracted function; explicit process/session IO adapters'};
const takeFunction=(start,end)=>{const begin=mainText.indexOf(start),finish=mainText.indexOf(end,begin);assert(begin>=0&&finish>begin,'Missing offline production consumer '+start);return mainText.slice(begin,finish).trim();};
const offlinePlan=takeFunction('static ymcc::deckysidebar::BindingPlan deckySidebarOwnedBindingPlan(', 'static void deckySidebarOwnedBindingDiagnostic(');
const offlineDiagnostic=takeFunction('static void deckySidebarOwnedBindingDiagnostic(', '// Existing high-frequency lane only;');

// Exact existing queue implementation. Only the OS message call is a C++ port;
// storage types, ordering/capacity and the actual body remain production bytes.
const postBegin=uniqueAnchor(mainText,'static bool deckySidebarPost(','actual Decky event queue');
const postBlock=cppBlock(mainText,mainText.indexOf('{',postBegin),'actual Decky event queue body');
const postFunction=mainText.slice(postBegin,postBlock.end),postMessage=mainText.match(/^#define WM_DECKY_MIRROR_EVENT[^\r\n]+/m)?.[0];
const postStorage=mainText.match(/^static std::mutex g_deckyEventMutex;\r?\nstatic std::deque<json> g_deckyEvents;\r?\nstatic bool g_deckyEventPosted = false;/m)?.[0];
assert(postMessage&&postStorage,'Exact queue message/storage declarations required');
let postCompilerInput=postFunction;const postMarkers=[];
if(bindingReportRequested&&['borrowed-watch','borrowed-post-queue','borrowed-post-metadata'].includes(process.argv[4].slice(bindingReportPrefix.length))){
 const anchor=postCompilerInput.includes('    try{if(bindingPacketKind)')?'    try{if(bindingPacketKind)':'    if(bindingPacketKind)',at=uniqueAnchor(postCompilerInput,anchor,'actual optional metadata update');
 const marker='    MetadataScope metadataAllocationScope; // Private original metadata allocation boundary.\n';
 postCompilerInput=postCompilerInput.slice(0,at)+marker+postCompilerInput.slice(at);postMarkers.push(marker);
 assert.equal(postCompilerInput.replace(marker,''),postFunction,'POST_METADATA_SCOPE_EXACT_INVERSE');
}
const postSource=postMessage+'\n'+postStorage+'\n'+postCompilerInput+'\n',postFile=path.join(source,'native/decky_post_consumer.inc');
fs.writeFileSync(postFile,postSource);consumer.postQueue={origin:'native/main.cpp',originSha256:sha(mainBytes),functionSha256:sha(Buffer.from(postFunction)),messageSha256:sha(Buffer.from(postMessage)),storageSha256:sha(Buffer.from(postStorage)),markers:postMarkers,compiled:sha(Buffer.from(postSource)),scope:'Full exact existing event queue and actual storage; PostMessageW is the explicit fixed C++ port; no real HWND/API action'};

const admissionStart=mainText.match(/    config[.]launchAdmission=\s*\[\]\(\) -> ymcc::deckysidebar::LaunchAdmission \{/);const admissionBegin=admissionStart?.index??-1;
assert(admissionBegin>=0,'Missing actual launch admission');const admissionClose='\n        };';const admissionEnd=mainText.indexOf(admissionClose,admissionBegin)+admissionClose.length;assert(admissionEnd>admissionBegin,'Missing exact admission lambda end');
const offlineAdmission=mainText.slice(admissionBegin,admissionEnd).trim().replace(/^config.launchAdmission=/,'return ');
const healthBegin=mainText.indexOf('    config.connectionHealth='),healthEnd=mainText.indexOf('\n    };',healthBegin)+7;
assert(healthBegin>=0&&healthEnd>healthBegin,'Missing exact connection health lambda');
const offlineHealth=mainText.slice(healthBegin,healthEnd).trim().replace(/^config.connectionHealth=/,'return ');
const changedGuard=mainText.match(/            if \(state.phase != "starting"[^\r\n]+g_deckyMirrorBroker.stop\(\); \}/)?.[0];
assert(changedGuard,'Missing actual runtime owner broker cleanup guard');
const changedStart=mainText.indexOf('    config.changed='),changedDemand=mainText.indexOf('            g_steamModeObserver.setDemand(',changedStart),changedStop=mainText.indexOf(changedGuard,changedDemand);
assert(changedStart>=0&&changedDemand>changedStart&&changedStop>changedDemand,'Missing exact runtime changed prefix');
const changedPrefix=mainText.slice(changedDemand,changedStop+changedGuard.length);

const identityStart=mainText.indexOf('[](std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> source) {');
assert(identityStart>=0,'Missing exact identity publication consumer');
const identityBody=cppBlock(mainText,mainText.indexOf('{',identityStart),'actual identity publication consumer');
const identityEnd=identityBody.end;
const offlineIdentityPublished=mainText.slice(identityStart,identityEnd).trim();
assert(offlineIdentityPublished.endsWith('}'),'Incomplete identity publication callback');
const stableAt=uniqueAnchor(mainText,'modeObserver.stableBigPicture = []','actual stable Big Picture callback'),stableBlock=cppBlock(mainText,mainText.indexOf('{',stableAt),'actual stable Big Picture callback');
const stableCallback=mainText.slice(stableAt,stableBlock.end).trim().replace(/^modeObserver[.]stableBigPicture = /,'return ');
const stableConsumer='inline auto actualStableBigPicture(){'+stableCallback+';}\n';
let offlineObserveSource='';
if(offlineIdentityPublished.includes('deckySidebarObserveSteamSource(')){
 const at=uniqueAnchor(mainText,'static void deckySidebarObserveSteamSource(','actual Steam source notification forwarding');
 const block=cppBlock(mainText,mainText.indexOf('{',at),'actual Steam source notification forwarding');
 offlineObserveSource=mainText.slice(at,block.end);
}
const offlineText=offlineObserveSource+'\n'+'inline auto actualIdentityPublished(){return '+offlineIdentityPublished+';}\ninline void actualRuntimeChanged(const ymcc::deckysidebar::State& state){'+changedPrefix+'}\n'+offlinePlan+'\n'+offlineDiagnostic+'\ninline auto actualAdmission(){'+offlineAdmission+'}\ninline auto actualConnectionHealth(){'+offlineHealth+'}\n';
fs.writeFileSync(path.join(source,'native/decky_offline_consumers.inc'),offlineText+stableConsumer);
consumer.offlineIdentity={identityPublishedSha256:sha(Buffer.from(offlineIdentityPublished)),planSha256:sha(Buffer.from(offlinePlan)),diagnosticSha256:sha(Buffer.from(offlineDiagnostic)),admissionSha256:sha(Buffer.from(offlineAdmission)),healthSha256:sha(Buffer.from(offlineHealth)),compiledSha256:sha(Buffer.from(offlineText+stableConsumer)),stableSha256:sha(Buffer.from(stableCallback)),scope:'Exact current main plan, diagnostic, admission and connection health lambdas; surrounding process/broker observer boundary adapted; health clock is explicit fixture seam'};

// Execute the real listener-table/owned-Job consumer in the existing Runtime
// ready case. Preserve its entire source after two explicit fixture seams:
// current Runtime passed by reference and exclusive high loopback test port.
const leaseBegin=mainText.indexOf('struct DeckyManagedBackendLease {');
const proofBegin=mainText.indexOf('static bool deckySidebarAcquireManagedBackend(',leaseBegin);
const proofEnd=mainText.indexOf('\n}',proofBegin)+2;
assert(leaseBegin>=0&&proofBegin>leaseBegin&&proofEnd>proofBegin,'Missing native managed backend proof');
const backendLease=mainText.slice(leaseBegin,proofBegin).trim();
const backendCurrentSource=mainText.slice(proofBegin,proofEnd);
const backendProofSource=backendCurrentSource.replaceAll('\r\n','\n').trim();
const signature='static bool deckySidebarAcquireManagedBackend(DeckyManagedBackendLease& result,bool* absent=nullptr)';
assert.equal(backendProofSource.split(signature).length-1,1,'Complete typed absence classifier required');
assert.equal(backendProofSource.split('port(row.dwLocalPort)==1337').length-1,1,'Exact owned backend port seam required');
const backendCompiled=backendProofSource.replace(signature,'static bool deckySidebarAcquireManagedBackend(DeckyManagedBackendLease& result,ymcc::deckysidebar::Runtime& g_deckySidebarRuntime,unsigned short fixturePort,bool* absent=nullptr)').replace('port(row.dwLocalPort)==1337','port(row.dwLocalPort)==fixturePort');

const cleanupWorkerText=fs.readFileSync(path.join(repo,'native/decky_context_cleanup_worker.h'),'utf8');
const actualProcessBirth=cleanupWorkerText.match(/^inline unsigned long long processBirth\(HANDLE process\)\{[^\r\n]+\}$/m)?.[0];
assert(actualProcessBirth,'Missing exact native GetProcessTimes birth function');

const backendText='namespace ymcc::deckyrepair::cleanupworker {\n'+actualProcessBirth+'\n}\nnamespace actualManagedBackend {\n'+backendLease+'\n'+backendCompiled+'\n}\n';
fs.writeFileSync(path.join(source,'native/decky_managed_backend_consumer.inc'),backendText);
fs.writeFileSync(path.join(out,'MANAGED-BACKEND-CONSUMER.json'),JSON.stringify({path:'native/main.cpp',sourceSha256:sha(Buffer.from(backendCurrentSource)),compiledProductSha256:sha(Buffer.from(backendProofSource)),compiledFixtureSha256:sha(Buffer.from(backendText)),actualNativeProcessBirthSha256:sha(Buffer.from(actualProcessBirth)),adaptations:['Runtime reference parameter','exclusive high loopback port'],ownershipAdapter:false,osListenerTableAdapter:false,scope:'Actual production classifier, actual already-owned Runtime Job and actual inert child listeners in existing native.typed-ready-rebind; no production applications or installed port.'},null,2)+'\n');

fs.writeFileSync(path.join(source,'native/main.cpp'),mainBytes);pins.push({path:'native/main.cpp',bytes:mainBytes.length,sha256:sha(mainBytes),role:'static event attribution plus extracted backend proof; complete main not compiled'});
fs.writeFileSync(path.join(out,'RESUME-CONSUMER.json'),JSON.stringify(consumer,null,2)+'\n');fs.writeFileSync(path.join(out,'RESUME-CONSUMER.cpp.txt'),commit[0]+'\n');
fs.writeFileSync(path.join(out,'SOURCE-PINS.json'),JSON.stringify({repo,source,files:pins,fixturePorts},null,2));
const toolDeclaration=selectedNativeTools(),tools=toolDeclaration.native;
const env=Object.fromEntries(Object.entries(process.env).map(([k,v])=>[k.toUpperCase(),v]));env.PATH=path.dirname(tools.compiler)+';'+process.env.SystemRoot+'/System32';env.INCLUDE=tools.include.join(';');env.LIB=tools.lib.join(';');env.CL='';env._CL_='';env.LINK='';env._LINK_='';env.TEMP=path.join(out,'tmp');env.TMP=env.TEMP;fs.mkdirSync(env.TEMP);
const root=path.join(out,'FixtureSteam');fs.mkdirSync(root);const exe=path.join(root,'steam.exe'),object=path.join(out,'binding-client.obj');

const sdkNames=['ws2tcpip.h','iphlpapi.h'];
const sdkPaths=sdkNames.map(name=>{const hit=tools.include.map(dir=>path.join(dir,name)).find(p=>fs.existsSync(p));assert(hit,'Missing known SDK header '+name);return hit;});
const sdkImport=tools.lib.map(dir=>path.join(dir,'iphlpapi.lib')).find(p=>fs.existsSync(p));assert(sdkImport,'Missing known SDK import library');sdkPaths.push(sdkImport);
const backendSdkInputs=sdkPaths.map(p=>{const b=fs.readFileSync(p);return{path:p,bytes:b.length,sha256:sha(b)};});
fs.writeFileSync(path.join(out,'BACKEND-SDK-INPUTS.json'),JSON.stringify({files:backendSdkInputs,scope:'Known added header/import inputs pinned before compiler and checked after native completion. Not complete SDK closure.'},null,2)+'\n');

const args=['/nologo',...((constructorRequested||reportMemoryRequested||loaderExitReportRequested||watchEndReportRequested||loaderStartReportRequested||bindingReportRequested)?['/DYMCC_BINDING_CONSTRUCTOR_FIXTURE']:[]),...((reportMemoryRequested||loaderExitReportRequested||watchEndReportRequested||loaderStartReportRequested)?['/DYMCC_DECKY_REPORT_ALLOCATION_FIXTURE']:[]),...(loaderCloseRequested?['/DYMCC_BINDING_LOADER_NATIVE_CLOSE_FIXTURE']:[]),'/Bv','/std:c++20','/utf-8','/EHsc','/MT','/O2','/W4','/WX','/DNOMINMAX','/DUNICODE','/D_UNICODE','/I'+path.join(source,'native'),'/I'+path.join(source,'deps/json'),path.join(source,'tools/decky_binding_runtime_selftest.cpp'),'/sourceDependencies',path.join(out,'dependencies.json'),'/Fo'+object,'/Fe'+exe,'/link','advapi32.lib','ws2_32.lib','bcrypt.lib','winhttp.lib','user32.lib','userenv.lib','shell32.lib','iphlpapi.lib','/verbose:lib'];

// Close the compiler's actual include set against bytes recorded BEFORE it
// starts. Lexical discovery is a conservative superset, never proof that an
// include was used; /sourceDependencies supplies that actual-used evidence.
const knownInputPins=new Map(),compilerInputFiles=new Map(),lexicallyVisited=new Set();
const inputKey=p=>path.resolve(p).toLowerCase();
const compilerIncludeRoots=[path.join(source,'native'),path.join(source,'deps/json'),...tools.include];
function pinInput(file){const p=path.resolve(file),key=inputKey(p);if(knownInputPins.has(key))return knownInputPins.get(key);const b=fs.readFileSync(p),row={path:p,bytes:b.length,sha256:sha(b)};knownInputPins.set(key,row);return row;}
for(const expected of [...toolDeclaration.files,...(toolDeclaration.additionalActualDependencies??[])]){const actual=pinInput(expected.path);assert.equal(actual.sha256,expected.sha256.toLowerCase(),'Declared original tool input changed: '+expected.path);}
const knownToolInputCount=knownInputPins.size;
function resolveCompilerInput(name,current,quoted){return [...(quoted?[path.resolve(path.dirname(current),name)]:[]),...compilerIncludeRoots.map(dir=>path.resolve(dir,name))].find(p=>fs.existsSync(p)&&fs.statSync(p).isFile());}
const linkLibraryNames=new Set(args.filter(x=>/\.lib$/i.test(x)));
function visitCompilerInput(file){const key=inputKey(file);if(lexicallyVisited.has(key))return;lexicallyVisited.add(key);const row=pinInput(file);compilerInputFiles.set(key,row);const text=fs.readFileSync(file,'utf8');for(const m of text.matchAll(/^\s*#\s*include\s*([<"])([^>"\r\n]+)[>"]/gm)){const found=resolveCompilerInput(m[2],file,m[1]==='"');if(found)visitCompilerInput(found);}for(const m of text.matchAll(/^\s*#\s*pragma\s+comment\s*\(\s*lib\s*,\s*"([^"\r\n]+)"/gim))linkLibraryNames.add(m[1]);}
visitCompilerInput(path.join(source,'tools/decky_binding_runtime_selftest.cpp'));
for(const port of fixturePorts)assert.deepEqual(knownInputPins.get(inputKey(port.path)),{path:path.resolve(port.path),bytes:port.bytes,sha256:port.sha256},'OBSERVER_PUBLICATION_COMPILER_PIN');
for(const name of linkLibraryNames){const file=tools.lib.map(dir=>path.join(dir,name)).find(p=>fs.existsSync(p)&&fs.statSync(p).isFile());if(file)pinInput(file);else assert(!args.includes(name),'Explicit linked SDK import missing: '+name);}
const precompileInputPins=[...knownInputPins.values()];
fs.writeFileSync(path.join(out,'PRECOMPILE-INPUT-CLOSURE.json'),JSON.stringify({files:precompileInputPins,knownToolInputCount,lexicallyDiscoveredCompilerInputs:compilerInputFiles.size,role:'Pre-compiler superset; used include membership comes only from actual compiler dependencies; no business execution'},null,2)+'\n');

const buildStartedAtUtc=new Date().toISOString(),buildBegin=performance.now();
const build=spawnSync(tools.compiler,args,{cwd:out,env,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(out,'compile.log'),(build.stdout??'')+(build.stderr??''));
fs.writeFileSync(path.join(out,'BUILD.json'),JSON.stringify({pid:build.pid,startedAtUtc:buildStartedAtUtc,endedAtUtc:new Date().toISOString(),elapsedMs:performance.now()-buildBegin,timeoutMs:60000,compiler:tools.compiler,compilerSha256:sha(fs.readFileSync(tools.compiler)),args,exit:build.status,error:build.error?.message??null},null,2));assert.equal(build.status,0,'Entry fixture compile failed');

const compilerDependencies=JSON.parse(fs.readFileSync(path.join(out,'dependencies.json'),'utf8').replace(/^\uFEFF/,''));
assert(compilerDependencies?.Data&&Array.isArray(compilerDependencies.Data.Includes),'Actual compiler dependency receipt missing');
const actualUsedFiles=[compilerDependencies.Data.Source,...compilerDependencies.Data.Includes];
const actualLinkLibraries=[...new Set([...(build.stdout??'').matchAll(/[A-Za-z]:\\[^\r\n]*?\.lib(?=[:\s,(]|$)/gi)].map(m=>path.resolve(m[0])))];
const missingUsedInputs=[...actualUsedFiles,...actualLinkLibraries].filter(p=>!knownInputPins.has(inputKey(p)));
const changedClosedInputs=precompileInputPins.filter(row=>{const b=fs.readFileSync(row.path);return b.length!==row.bytes||sha(b)!==row.sha256;}).map(row=>row.path);
const compilerInputClosure={complete:missingUsedInputs.length===0&&changedClosedInputs.length===0,knownToolInputCount,precompilePins:precompileInputPins.length,actualIncludeFiles:compilerDependencies.Data.Includes.length,actualLinkedLibrariesObserved:actualLinkLibraries.length,actualUsedFiles,actualLinkLibraries,missingUsedInputs,changedClosedInputs,scope:'Actual compiler-used include set and traced resolved library paths checked against pre/post pinned superset. Original tool declaration independently checked. Does not imply full product/SDK installation closure.'};
fs.writeFileSync(path.join(out,'COMPILER-INPUT-CLOSURE.json'),JSON.stringify(compilerInputClosure,null,2)+'\n');
assert(compilerInputClosure.complete,'Compiler used an unpinned or changed input; no native execution admitted');

const cef=path.join(root,'bin','cef','fixture','steamwebhelper.exe');fs.mkdirSync(path.dirname(cef),{recursive:true});fs.copyFileSync(exe,cef);
const nativeTimeoutMs=process.argv[3]===undefined?100000:45000; // Whole serial suite only; per-case deadlines and the 135s wrapper stay unchanged.
const runStartedAtUtc=new Date().toISOString(),runBegin=performance.now();
const runArgs=[path.join(out,'runtime-fixtures'),...process.argv.slice(3)];
const run=spawnSync(exe,runArgs,{cwd:out,env,encoding:'utf8',windowsHide:true,timeout:nativeTimeoutMs,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(out,'run.log'),(run.stdout??'')+(run.stderr??''));
const sdkChanged=backendSdkInputs.filter(p=>{const b=fs.readFileSync(p.path);return b.length!==p.bytes||sha(b)!==p.sha256;}).map(p=>p.path);
fs.writeFileSync(path.join(out,'NATIVE-PROCESS.json'),JSON.stringify({pid:run.pid,executable:exe,args:runArgs,sha256:sha(fs.readFileSync(exe)),startedAtUtc:runStartedAtUtc,endedAtUtc:new Date().toISOString(),elapsedMs:performance.now()-runBegin,timeoutMs:nativeTimeoutMs,exitCode:run.status,signal:run.signal,error:run.error?.message??null},null,2)+'\n');
const changed=pins.filter(p=>sha(fs.readFileSync(path.join(repo,p.path)))!==p.sha256).map(p=>p.path);
const result={schema:'ymcc.binding-runtime-build-and-run.v1',at:new Date().toISOString(),status:run.status===0&&changed.length===0&&sdkChanged.length===0?'pass':'fail',compilerExit:build.status,nativeCapMs:nativeTimeoutMs,runExit:run.status,error:run.error?.message??null,sourceInputs:pins.length,changedOriginalInputs:changed,changedKnownSdkInputs:sdkChanged,compilerInputClosure,fixturePorts,managedBackendConsumer:JSON.parse(fs.readFileSync(path.join(out,'MANAGED-BACKEND-CONSUMER.json'),'utf8')),exeSha256:sha(fs.readFileSync(exe)),objectSha256:sha(fs.readFileSync(object)),selection:process.argv[3]??null,globalMissedEventProbeRequested:process.argv.includes('--global-missed-event'),resumeConsumer:consumer,scope:'Real production Runtime/Client/Owner plus source-built protocol peer, inert Loader/CEF. Original 3200ms deadline and release-after-measurement. Not real Steam bootstrap, field input or full package acceptance.',fullRuntimeMigrationPassed:false,hostReleasePassed:false,installed:false,fieldActions:0,sleepActions:0};
fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result));if(result.status!=='pass')process.exitCode=1;


