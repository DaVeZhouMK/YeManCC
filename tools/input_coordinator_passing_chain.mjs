// Post --prepare-hardened-source fixture transform. Pure text only; no compiler/target/loader.
import {createHash} from 'node:crypto';
import profile from './fixtures/input_coordinator_lifecycle/passing_chain_source_profile.json' with {type:'json'};
const sha=s=>createHash('sha256').update(s).digest('hex'),need=(v,m)=>{if(!v)throw Error('PASSING_CHAIN_'+m);};
export const PASSING_CHAIN_SOURCE_PROFILE='B51_IMMUTABLE_LEASE_PHASE_COMPACT_SOURCE_V1';
export const PASSING_CHAIN_TOOL_INPUTS=Object.freeze(['tools/input_coordinator_passing_chain.mjs','tools/fixtures/input_coordinator_lifecycle/passing_chain_source_profile.json']);
export function passingChainProfile(){return structuredClone(profile);}
function keys(value,rows){need(value&&typeof value==='object'&&!Array.isArray(value),'SOURCE_MAP');need(JSON.stringify(Object.keys(value).sort())===JSON.stringify(rows.map(r=>r.path).sort()),'EXACT_SOURCE_SET');}
export function applyPassingChain(sourceTexts,{sourceProfile}={}){
 need(sourceProfile===PASSING_CHAIN_SOURCE_PROFILE,'EXPLICIT_SOURCE_PROFILE_REQUIRED');keys(sourceTexts,profile.inputFiles);for(const r of profile.inputFiles)need(typeof sourceTexts[r.path]==='string'&&sha(sourceTexts[r.path])===r.sha256,'BEFORE_SHA:'+r.path);
 const files={...sourceTexts};for(const row of profile.files){if(row.beforeSha256===null){need(!Object.hasOwn(files,row.path),'NEW_PATH_EXISTS');files[row.path]=row.source;}else{need(sha(files[row.path])===row.beforeSha256,'TRANSFORM_BASE_SHA:'+row.path);for(const h of row.hunks){need(h.before.length&&files[row.path].split(h.before).length===2,'UNIQUE_ANCHOR:'+h.label);files[row.path]=files[row.path].replace(h.before,h.after);}}need(sha(files[row.path])===row.afterSha256,'AFTER_SHA:'+row.path);}
 for(const row of profile.outputFiles)need(sha(files[row.path])===row.sha256,'OUTPUT_SHA:'+row.path);
 return {files,evidence:{schemaVersion:1,status:'SOURCE_ONLY_NOT_COMPILED_NOT_EXECUTED',sourceProfile,stageAfter:profile.stageAfter,budgets:profile.budgets,heldGateMaxMs:20000,files:profile.outputFiles,invariants:profile.invariants,requiredLaunchBindings:profile.requiredLaunchBindings,historicalFixedReference:profile.historicalFixedReference,historicalPassApprovesCurrentSource:false,currentPrimaryAccepted:false,compilerExecuted:false,targetExecuted:false,targetExecutionAllowed:false,wholeGoalComplete:false}};
}
export function restorePassingChain(files,{sourceProfile}={}){
 need(sourceProfile===PASSING_CHAIN_SOURCE_PROFILE,'EXPLICIT_SOURCE_PROFILE_REQUIRED');keys(files,profile.outputFiles);for(const r of profile.outputFiles)need(typeof files[r.path]==='string'&&sha(files[r.path])===r.sha256,'RESTORE_AFTER_SHA:'+r.path);const restored={...files};
 for(const row of [...profile.files].reverse()){if(row.beforeSha256===null){delete restored[row.path];continue;}for(const h of [...row.hunks].reverse()){need(restored[row.path].split(h.after).length===2,'RESTORE_ANCHOR:'+h.label);restored[row.path]=restored[row.path].replace(h.after,h.before);}need(sha(restored[row.path])===row.beforeSha256,'RESTORE_BEFORE_SHA:'+row.path);}keys(restored,profile.inputFiles);return restored;
}
