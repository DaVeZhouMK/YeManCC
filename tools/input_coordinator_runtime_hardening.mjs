// Pure source transform for the existing SDK-free coordinator fixture. No target execution.
import {createHash} from 'node:crypto';
import patch from './fixtures/input_coordinator_lifecycle/runtime_hardening.json' with {type:'json'};
const hash=s=>createHash('sha256').update(s).digest('hex');
const need=(v,m)=>{if(!v)throw Error('RUNTIME_HARDENING_'+m);};
export const RUNTIME_HARDENING_TOOL_INPUTS=['tools/input_coordinator_runtime_hardening.mjs','tools/fixtures/input_coordinator_lifecycle/runtime_hardening.json'];
export function runtimeHardeningProfile(){return structuredClone(patch);}
export function applyRuntimeHardening(sourceTexts){
 need(sourceTexts&&typeof sourceTexts==='object'&&!Array.isArray(sourceTexts),'SOURCE_MAP_REQUIRED');
 const expected=patch.files.filter(r=>r.beforeSha256!==null).map(r=>r.path).sort();
 need(JSON.stringify(Object.keys(sourceTexts).sort())===JSON.stringify(expected),'EXACT_INPUT_SET');
 const files={};
 for(const row of patch.files){
  if(row.beforeSha256===null){need(!Object.hasOwn(sourceTexts,row.path),'NEW_FILE_ALIAS');files[row.path]=row.source;}
  else{let text=sourceTexts[row.path];need(typeof text==='string'&&hash(text)===row.beforeSha256,'BEFORE_SHA:'+row.path);
   for(const h of row.hunks){need(typeof h.before==='string'&&h.before.length>0&&text.split(h.before).length===2,'ANCHOR:'+h.label);text=text.replace(h.before,h.after);}
   files[row.path]=text;
  }
  need(hash(files[row.path])===row.afterSha256,'AFTER_SHA:'+row.path);
 }
 const evidence={schemaVersion:1,status:'SOURCE_ONLY_NOT_COMPILED_NOT_EXECUTED',kind:patch.kind,originalAssertionsAndCaseOrderUnchanged:true,nativeBudgetMs:60000,managedExitBudgetMs:7000,files:patch.files.map(r=>({path:r.path,beforeSha256:r.beforeSha256,afterSha256:r.afterSha256})),targetExecuted:false,wholeGoalComplete:false};
 return {files,evidence};
}
export function restoreRuntimeHardening(files){
 need(files&&JSON.stringify(Object.keys(files).sort())===JSON.stringify(patch.files.map(r=>r.path).sort()),'EXACT_RESTORE_SET');
 const restored={};
 for(const row of patch.files){let text=files[row.path];need(typeof text==='string'&&hash(text)===row.afterSha256,'RESTORE_AFTER_SHA:'+row.path);if(row.beforeSha256===null)continue;
  for(const h of [...row.hunks].reverse()){need(text.split(h.after).length===2,'RESTORE_ANCHOR:'+h.label);text=text.replace(h.after,h.before);}
  need(hash(text)===row.beforeSha256,'RESTORE_BEFORE_SHA:'+row.path);restored[row.path]=text;
 }return restored;
}
