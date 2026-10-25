import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
const need=(v,c)=>assert.ok(v,c);
export const digest=text=>createHash('sha256').update(text).digest('hex');
// Match the producer's raw UTF-8 decoding (including CRLF) and offset-preserving C++ mask.
export function currentFragments(repo) {
 const text=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8').replace(/^\uFEFF/,'');
 const code=text.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"(?:\\[\s\S]|[^"\\])*"|'(?:\\[\s\S]|[^'\\])*'/g,s=>s.replace(/[^\n]/g,' '));
 const cache=new Map();
 function block(start,semicolon=false){const opening=code.indexOf('{',start);need(opening>=start,'FRAGMENT_OPENING');let at=opening+1,depth=1;while(depth&&at<code.length){depth+=(code[at]==='{')-(code[at]==='}');at++;}need(depth===0,'FRAGMENT_UNCLOSED');return text.slice(start,semicolon?at+1:at);}
 function fn(name){if(cache.has(name))return cache.get(name);const matches=[...code.matchAll(new RegExp('^(?:\\[\\[[^\\n]*?\\]\\]\\s*)?static[^\\n;{}]*\\b'+name+'\\s*\\([^;{}]*\\)\\s*(?:noexcept\\s*)?\\{','gm'))];need(matches.length===1,'CURRENT_FUNCTION_UNIQUE:'+name);const value=block(matches[0].index);cache.set(name,value);return value;}
 function hashes(rows,label){need(rows&&Object.keys(rows).length>0,label+':FUNCTIONS_MISSING');for(const [name,row] of Object.entries(rows)){const hash=typeof row==='string'?row:row.sha256;const value=name==='InputTimerCallbackEvidenceScope'?scope():fn(name);need(digest(value)===hash,label+':CURRENT_FUNCTION_FRAGMENT_MISMATCH:'+name);}}
 function scope(){const m=/^class\s+InputTimerCallbackEvidenceScope\s*\{/m.exec(code);need(m,'CURRENT_SCOPE_MISSING');return block(m.index,true);}
 const helpers={retireResources:'inputCaptureRetireResources',clearExitConfiguration:'inputCaptureClearExitConfiguration',sensorReap:'inputCaptureSensorThreadReap',sensorShutdown:'inputCaptureSensorThreadShutdown',oemTakeCompletion:'oemHidReadTakeCompletion',oemFinalizeClose:'oemHidReadFinalizeClose',oemServiceClosing:'oemHidReadServiceClosing',oemRequestClose:'oemHidReadRequestClose',onexFinalizeClose:'onexBackFinalizeClose',onexSettleWrite:'onexBackSettleWrite',onexRetireStuck:'onexBackRetireStuck',onexRetireReadStuck:'onexBackRetireReadStuck',onexServiceWrite:'onexBackServiceWrite',onexServiceRead:'onexBackServiceRead',emergencyCleanup:'inputCaptureEmergencyCleanup',shutdown:'inputCaptureShutdown',resetFence:'inputHostResetExitFence',readFence:'inputHostExitFenceRaised'};
 function partial(){const result=Object.fromEntries(Object.entries(helpers).map(([k,n])=>[k,digest(fn(n))]));const init=fn('inputCaptureInit'),m=/if\s*\(dsu_start\(\)\)\s*appendNativeLifecycleLog\([\s\S]*?;/.exec(init);need(m,'CURRENT_INIT_PREFIX_BOUNDARY');result.initPrefix=digest(init.slice(0,m.index+m[0].length));const types=[];
  for(const name of ['OnexInitProfile','OnexCapDescriptor','OemHidReadSession','OemHidReadCompletion','OnexBackSession']){const m=new RegExp('^(?:struct|enum class)\\s+'+name+'[^;{\\n]*\\{','m').exec(code);need(m,'CURRENT_RESOURCE_TYPE:'+name);types.push(block(m.index,true));}
  for(const name of ['kOemHidReadDrainNoticeMs','kOnexBackWriteDrainLimitMs','kOnexBackReadDrainLimitMs']){const m=new RegExp('^static constexpr[^;]*\\b'+name+'\\b[^;]*;','m').exec(text);need(m,'CURRENT_RESOURCE_CONSTANT:'+name);types.push(m[0]);}result.resourceTypes=digest(types.join('\n'));return result;
 }
 // Exit blocks are deliberately not whole functions. Require their exact source
 // text and then hash the verbatim block, just as the producer extracts it.
 function exit(){const result={};for(const n of ['inputCaptureStop','nativeLifecycleEventIsBase','inputCaptureExitOwnershipReleased','inputCaptureAwaitStopForExit','exitCleanupThreadProc','tryStartExitCleanupWorker','inputCaptureEnsureExitOwner'])result[n]=digest(fn(n));
  const wnd=fn('WndProc'),start=wnd.indexOf('case WM_DESTROY:')+'case WM_DESTROY:'.length;need(start>=19,'CURRENT_DESTROY_BOUNDARY');const tail=wnd.slice(start),m=/\bif\s*\(g_exitRequested\.load\(\)\)\s*\{/.exec(tail);need(m,'CURRENT_EXIT_REQUEST_BOUNDARY');const absolute=text.indexOf(wnd)+start,opening=code.indexOf('{',absolute+m.index);let at=opening+1,depth=1;while(depth){depth+=(code[at]==='{')-(code[at]==='}');at++;}result.destroyExitRequestedPath=digest(text.slice(absolute,at));
  const mainStart=text.indexOf('int WINAPI wWinMain('),main=block(mainStart),anchor=main.indexOf('closeScreenTouchpadsForExit("message-loop-finally");'),cleanup=main.lastIndexOf('    g_optiCatalogStartup.stop();',anchor),gate=main.lastIndexOf('    inputCaptureEnsureExitOwner(msg.wParam);',cleanup);need(gate>=0&&cleanup>=0,'CURRENT_MAIN_FINALLY_GATE');result.mainFinally=digest(main.slice(gate,main.lastIndexOf('}')));return result;
 }
 return {fn,hashes,partial,exit};
}
