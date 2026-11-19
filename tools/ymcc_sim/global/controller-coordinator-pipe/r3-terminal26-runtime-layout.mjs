import path from 'node:path';
import {deepStrictEqual} from 'node:assert/strict';
import {currentLifecycleModePlan,currentLifecycleModeLabel} from './r3-current-run.mjs';

export const TERMINAL26_RUNTIME_LAYOUT_KIND='EXACT26_PIPE_AND_ORIGINAL16_RUNTIME_PATHS_V1';
export const TERMINAL26_PUBLIC_RUNTIME_POLICY='owned-direct-child-v1';
export const TERMINAL26_REUSABLE_RUNTIME_LAYOUT_KIND='EXACT26_OWNED_PUBLIC_RUNTIME_ROOTS_V2';
export const TERMINAL26_MSI_PARAMETER_LAYOUT='msi-policy-boundaries-r2';
export const TERMINAL26_MSI_EXIT_PARAMETER_LAYOUT='msi-policy-boundaries-r3-exit-process';
const need=(ok,code)=>{if(!ok)throw Error('TERMINAL26_RUNTIME_LAYOUT_'+code);};

// Describes paths only. The observer owns build-root and no-reparse admission.
export function terminal26RuntimeLayout(runtimeRoot,{runtimeParameterLayout,runtimeReusePolicy}={}){
 need(typeof runtimeRoot==='string'&&!/[\0\r\n]/.test(runtimeRoot)&&path.isAbsolute(runtimeRoot),'ABSOLUTE_RUNTIME_ROOT_REQUIRED');
 const root=path.resolve(runtimeRoot);
 need(runtimeReusePolicy===undefined||runtimeReusePolicy===TERMINAL26_PUBLIC_RUNTIME_POLICY,'UNKNOWN_PUBLIC_RUNTIME_POLICY');
 if(runtimeReusePolicy!==undefined)need(runtimeParameterLayout===undefined,'PUBLIC_POLICY_PRIVATE_PARAMETERS_CONFLICT');
 need(path.basename(root)==='qualification-runtime','QUALIFICATION_RUNTIME_ROOT_REQUIRED');
 const plan=currentLifecycleModePlan();
 need(Array.isArray(plan)&&plan.length===16,'ORIGINAL16_PLAN_REQUIRED');
 const labels=new Set(['pipe']);
 const lifecycle=plan.map(test=>{
  const label=currentLifecycleModeLabel(test);
  need(typeof label==='string'&&label.length>0&&!/[\\/\0\r\n:]/.test(label)&&!label.includes('..')&&path.basename(label)===label,'SINGLE_COMPONENT_LABEL_REQUIRED');
  need(!labels.has(label),'UNIQUE_LABEL_REQUIRED');labels.add(label);
  return {...test,label,path:path.join(root,label)};
 });
 if(runtimeParameterLayout!==undefined)need([TERMINAL26_MSI_PARAMETER_LAYOUT,TERMINAL26_MSI_EXIT_PARAMETER_LAYOUT].includes(runtimeParameterLayout),'UNKNOWN_PARAMETER_LAYOUT');
 const parameters=runtimeParameterLayout===undefined?undefined:{id:runtimeParameterLayout,positivePair:{mode:'recovery-protection',originalCaseSelector:'msi31',originalCaseOrdinals:[7,8],path:path.join(root,'positive-msi31')},cases:['clock','off','new-request','intent','exit'].map(boundary=>({boundary,mode:'recovery-protection',originalCaseSelector:'msi31',originalCaseOrdinals:[7,8],path:path.join(root,'msi31-'+boundary),...(boundary==='exit'&&runtimeParameterLayout===TERMINAL26_MSI_EXIT_PARAMETER_LAYOUT?{phases:['pending','success'].map(phase=>({phase,path:path.join(root,'msi31-exit-'+phase)}))}:{})}))};
 return {kind:runtimeReusePolicy===undefined?TERMINAL26_RUNTIME_LAYOUT_KIND:TERMINAL26_REUSABLE_RUNTIME_LAYOUT_KIND,root,pipe:path.join(root,'pipe'),lifecycle,...(parameters?{parameters}:{}),...(runtimeReusePolicy===undefined?{}:{publicRuntime:{policyId:TERMINAL26_PUBLIC_RUNTIME_POLICY,base:path.join(path.dirname(root),'r'),childNamePattern:'^[0-9a-f]{16}$',freshRequired:true}})};
}

export function terminal26PreparedRuntimeLayout(n){
 const runtimeParameterLayout=n?.runtimeParameterLayout;
 const runtimeReusePolicy=n?.runtimeReusePolicy;
 if(runtimeParameterLayout!==undefined){
  need(['msi-real-policy-return-r7-20261115','msi-real-policy-return-r8-r5-20261117'].includes(n?.native?.productPolicySuccessor?.id),'PARAMETERS_REQUIRE_REVIEWED_MSI_PRODUCT');
  need(n?.fixtureInputSuccessor?.id==='msi-policy-fixture-input-r2-20261115','PARAMETERS_REQUIRE_EXACT_FIXTURE_INPUT');
 }
 // A short-lived r2 producer already compiled the exact two exit-process paths.
 // Retain that historical shape without rewriting its receipt. The caller must
 // still prove the complete generated mutex boundary against actual compiled
 // bytes; this shape reader grants no executable/source/runtime authority.
 const legacyExit=runtimeParameterLayout===TERMINAL26_MSI_PARAMETER_LAYOUT&&
  n?.runtimeLayout?.parameters?.cases?.some(row=>row?.boundary==='exit'&&Object.hasOwn(row,'phases'));
 if(legacyExit||runtimeParameterLayout===TERMINAL26_MSI_EXIT_PARAMETER_LAYOUT)
  need(n?.native?.productPolicySuccessor?.id==='msi-real-policy-return-r8-r5-20261117','EXIT_PHASES_REQUIRE_R8_POLICY');
 const layout=terminal26RuntimeLayout(n?.runtimeRoot,{runtimeParameterLayout:legacyExit?TERMINAL26_MSI_EXIT_PARAMETER_LAYOUT:runtimeParameterLayout,runtimeReusePolicy});
 if(legacyExit)layout.parameters.id=TERMINAL26_MSI_PARAMETER_LAYOUT;
 deepStrictEqual(n.runtimeLayout,layout,'TERMINAL26_RUNTIME_LAYOUT_EXACT_PREPARED_LAYOUT_REQUIRED');
 return layout;
}

export function terminal26PreparedRuntimeGenerationOptions(n){
 const layout=terminal26PreparedRuntimeLayout(n);
 const splitExit=layout.parameters?.cases.some(row=>row.boundary==='exit'&&Object.hasOwn(row,'phases'));
 return {runtimeParameterLayout:splitExit?TERMINAL26_MSI_EXIT_PARAMETER_LAYOUT:n.runtimeParameterLayout,...(n.runtimeReusePolicy===undefined?{}:{runtimeReusePolicy:n.runtimeReusePolicy})};
}

export function terminal26PublicRuntimeDirectory(n,nonce){
 const layout=terminal26PreparedRuntimeLayout(n);
 need(layout.kind===TERMINAL26_REUSABLE_RUNTIME_LAYOUT_KIND&&layout.publicRuntime?.policyId===TERMINAL26_PUBLIC_RUNTIME_POLICY,'PUBLIC_RUNTIME_POLICY_REQUIRED');
 need(typeof nonce==='string'&&/^[0-9a-f]{16}$/.test(nonce),'EXACT_PUBLIC_RUNTIME_NONCE_REQUIRED');
 return path.join(layout.publicRuntime.base,nonce);
}

export function terminal26RuntimePaths(runtimeRoot,options={}){
 const layout=terminal26RuntimeLayout(runtimeRoot,options);
 return [layout.pipe,...layout.lifecycle.map(test=>test.path),...(layout.parameters?[layout.parameters.positivePair.path,...layout.parameters.cases.flatMap(test=>[test.path,...(test.phases??[]).map(phase=>phase.path)])]:[])];
}
