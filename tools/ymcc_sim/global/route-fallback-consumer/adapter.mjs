/** Candidate factory only. Main separately registers this ID. */
import path from 'node:path';
import {MODULE,CASE_ID,FILES,TOOL_INPUTS,PRODUCT,lockTools} from './contract.mjs';
const ROOT=path.resolve(import.meta.dirname,'../../../..');
export function getRouteFallbackConsumerAdapters(repoRoot=ROOT) {
  let artifactInputs=[],blockedReason;
  try { artifactInputs=lockTools(repoRoot).artifacts.map(row=>row.path); }
  catch(error){blockedReason='ROUTE_FALLBACK_TOOLS_UNAVAILABLE:'+error.message;}
  return [{id:CASE_ID,domain:'robust',title:'Actual routeFallback TS, native DOM ownership and memory-only recovery effects',
    kind:'node',path:MODULE+'/run.mjs',args:['--repo-root',repoRoot,'--evidence-dir','{outDir}/route-fallback'],
    timeoutMs:30000,profiles:['desktop','full'],scope:'behavioral-mock',
    evidenceInputs:[...new Set([PRODUCT,...TOOL_INPUTS,...FILES.map(file=>MODULE+'/'+file)])],
    artifactInputs,resultFiles:['route-fallback/result.json'],...(blockedReason?{blockedReason}:{}),
    completionContract:{schemaVersion:1,rules:[{line:'PASS '+CASE_ID}]},
    lifecycleContract:{schemaVersion:1,features:[{id:'robust.route-fallback',persistence:'persistent',
      rationale:'The real route fallback retains an owned modal DOM node, capture subscription and borrowed app inert state across calls. Startup-failure and in-flight recovery deliberately survive clear. A click or promise settlement is a child operation, not complete parent destruction.',
      sourceInputs:[PRODUCT],resources:['dom-nodes','subscriptions','borrowed-inert-state'],evidenceFile:'route-fallback/result.json'}]},
    boundary:'Real TS compiled with the existing declared esbuild; owned Chromium DOM. Only dialog.confirm/resetApplicationSettings/reload are memory effects. No installed app/config/file writes or browser navigation. Raw source failures and missing full exit/sleep/healthy recovery remain unaccepted.'}];
}
