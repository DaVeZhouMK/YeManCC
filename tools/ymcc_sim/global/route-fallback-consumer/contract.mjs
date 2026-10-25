import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {lockTools, verifyTools, runtimeTools, TOOL_SOURCES} from '../settings-diagnostics-consumer/contract.mjs';
export {lockTools, verifyTools, runtimeTools};
export const MODULE = 'tools/ymcc_sim/global/route-fallback-consumer';
export const CASE_ID = 'robust.route-fallback-consumer';
export const PRODUCT = 'src/robust/routeFallback.ts';
export const FILES = ['adapter.mjs','contract.mjs','build.mjs','browser-cases.mjs','memory-ports.ts','run.mjs','spec.json','README.md'];
export const TOOL_INPUTS = [...new Set([...TOOL_SOURCES,
  'tools/ymcc_sim/global/settings-diagnostics-consumer/contract.mjs'])];
export const sha = value => createHash('sha256').update(value).digest('hex');
export const hash = file => sha(fs.readFileSync(file));
export const need = (ok, reason) => { if (!ok) throw Error(reason); };
export const write = (file, data) => fs.writeFileSync(file, JSON.stringify(data,null,2)+'\n');
export function sourcePins(root) {
  const spec = JSON.parse(fs.readFileSync(path.join(root,MODULE,'spec.json'),'utf8'));
  need(hash(path.join(root,PRODUCT)) === spec.source.sha256, 'ROUTE_SOURCE_CHANGED_REVIEW_REQUIRED');
  return [...new Set([PRODUCT,...TOOL_INPUTS,...FILES.map(file=>MODULE+'/'+file)])]
    .map(relative=>({path:relative,sha256:hash(path.join(root,relative))}));
}
export function verifySources(root, rows) {
  for (const row of rows) need(hash(path.join(root,row.path))===row.sha256, 'SOURCE_CHANGED:'+row.path);
}
export const FAULTS = [
  {id:'delete-busy-startup-clear-guard',caseId:'busy-clear-and-show-retain-owner',
    from:"  if (node?.dataset.startupFailure === 'true' || node?.dataset.recoveryBusy === 'true') return;",
    to:'  // designated negative: deleted clear retention guard',expected:'BUSY_CLEAR_MUST_RETAIN_MODAL'},
  {id:'delete-listener-cleanup',caseId:'clear-returns-listener-inert-and-late-semantic',
    from:"  window.removeEventListener('ipc:gamepad.ui-input', onFallbackAction, true);",
    to:'  // designated negative: deleted subscription cleanup',expected:'CLEAR_LISTENER_MUST_BE_ZERO'},
  {id:'delete-owner-terminal-guard',caseId:'cleared-detached-retry-callback-must-not-reload',
    from:'  return owner.active && currentFallbackOwner === owner\n    && owner.node.isConnected && document.getElementById(FALLBACK_ID) === owner.node;',
    to:'  return true; // designated negative: deleted owner terminal guard',expected:'CLEARED_DETACHED_RETRY_MUST_NOT_RELOAD'},
  {id:'delete-post-confirmation-owner-guard',caseId:'retired-confirmation-cannot-start-reset',
    from:'      if (!ownsRouteFallback(owner)) return;\n      if (!confirmed)',
    to:'      if (!confirmed)',expected:'RETIRED_CONFIRMATION_MUST_NOT_RESET'},
  {id:'delete-post-reset-owner-guard',caseId:'retired-reset-cannot-publish-or-reload',
    from:'      const result = await resetApplicationSettings();\n      if (!ownsRouteFallback(owner)) return;',
    to:'      const result = await resetApplicationSettings();',expected:'RETIRED_RESET_MUST_NOT_RELOAD'},
];
