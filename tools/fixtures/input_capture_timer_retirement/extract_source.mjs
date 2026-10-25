// Bind relevant main source bytes only; production functions are never compiled/executed.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';
import {extract} from '../legion_motion_consumer/extract_main.mjs';
export const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
export function freeze(repo,out){
 const bytes=fs.readFileSync(path.join(repo,'native/main.cpp')),text=bytes.toString('utf8');
 const functions=['inputCaptureTimerEventProc','inputCaptureWorkerFault','inputCaptureThreadProc'].map(name=>extract(text,name));
 const callback=functions[0].code,fault=functions[1].code,worker=functions[2].code;
 const creation=/captureTimerId = timeSetEvent\([\s\S]*?\);/.exec(worker);assert(creation,'Timer creation');
 assert(/TIME_PERIODIC\s*\|\s*TIME_KILL_SYNCHRONOUS\);/.test(creation[0]),'Current source must retain complete-callback synchronous retirement');
 for(const header of ['} catch (const std::exception& error) {','} catch (...) {'])assert(worker.replaceAll('\r\n','\n').includes(header+'\n        g_inputCaptureStop.store(true, std::memory_order_release);\n        inputCaptureWorkerFault'),'Fault stop must precede cancellation');
 const interval=/^static constexpr UINT32 kInputCaptureGamepadPublishIntervalMs = 8;/m.exec(text);assert(interval);
 assert(callback.includes('std::unique_lock<std::mutex> tickLock(g_inputCaptureBusTickMx, std::try_to_lock);'));
 assert(!/tickLock\.(unlock|release)\s*\(/.test(callback),'Do not invent successful-lock early unlock');
 assert(callback.indexOf('if (event) SetEvent(event);')>callback.indexOf('std::unique_lock<std::mutex> tickLock'));
 assert(fault.includes('inputCaptureAppend(record);')&&fault.includes('writeLog(')&&!fault.includes('g_inputCaptureStop'),'WorkerFault has no direct stop access/store');
 const killSites=[...worker.matchAll(/timeKillEvent\(captureTimerId\);/g)];assert.equal(killSites.length,4);
 const paths=[...worker.matchAll(/timeKillEvent\(captureTimerId\);[\s\S]*?std::lock_guard<std::mutex> tickLock\(g_inputCaptureBusTickMx\);[\s\S]*?inputCaptureShutdown\(\)/g)];assert.equal(paths.length,3);
 assert(worker.includes('CloseHandle(captureTimer);'));
 const entries=functions.map(({code,...e})=>e);
 const region=(symbol,code,base=0)=>{const start=text.indexOf(code,base);assert(start>=0);return {symbol,code,sha256:sha(code),startLine:text.slice(0,start).split('\n').length,startUtf8Byte:Buffer.byteLength(text.slice(0,start)),endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,start+code.length))};};
 const anchors=[region('timer-creation-periodic-synchronous-retirement',creation[0]),region('source-period-8ms',interval[0])];
 fs.writeFileSync(path.join(out,'main.source.snapshot.cpp'),bytes);
 for(const f of functions)fs.writeFileSync(path.join(out,f.symbol+'.cpp.txt'),f.code);
 const binding={source:'native/main.cpp',sourceSha256:sha(bytes),sourceBytes:bytes.length,functions:entries,anchors,cancellationSites:killSites.map((m,index)=>({index:index+1,workerOffset:m.index,code:m[0]})),normalPlusCatchKillDrainShutdownPaths:paths.length,
  sourceOnlyConclusions:{successfulTryLock:'tickLock is declared at function scope; explicit SetEvent and explicit tail diagnostics precede its destruction when it owns the mutex. No early unlock is present.',unlockedTail:'Failed try_lock still executes SetEvent and diagnostics without owning BUS; helper tail class uses only this busy-skip path.',preBus:'Role/cost/effective-period work precedes try_lock; a callback already entered can be paused here.',faultStop:'WorkerFault body logs but contains no direct g_inputCaptureStop access/store; this is not a transitive call-graph proof.',shutdown:'Three normal/catch paths kill the timer, drain BUS and call Shutdown; final cancellation and event CloseHandle are also frozen.'},
  executionBoundary:'Entire source functions and snippets are source-only. The WinMM helper does not execute main callback, worker, fault logger, Session, DLL, HID or Host. No attribution to supplied device logs.'};
 fs.writeFileSync(path.join(out,'SOURCE-BINDINGS.json'),JSON.stringify(binding,null,2)+'\n');return binding;
}
export function verify(repo,before){const b=fs.readFileSync(path.join(repo,'native/main.cpp')),text=b.toString('utf8');return {methodsStable:before.functions.every(e=>extract(text,e.symbol).sha256===e.sha256),anchorsStable:before.anchors.every(a=>text.includes(a.code)),wholeMainDrifted:sha(b)!==before.sourceSha256,currentMainSha256:sha(b)};}
