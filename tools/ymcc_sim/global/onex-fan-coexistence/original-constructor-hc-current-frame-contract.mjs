// Immutable-role contract and exact policy/runtime-pin projection. No launcher or target loader.
import path from 'node:path';
import {need} from './default-case-contract.mjs';
import {originalConstructorContract,assessOriginalConstructorResult} from './prepare-original-constructors.mjs';
import {samePath,within,exactPin} from './original-constructor-native-current-frame-contract.mjs';
import nativeBasis from './original-constructor-native-fault-basis.json' with {type:'json'};
import counterBasis from './constructor-counterexample-basis.json' with {type:'json'};
const equal=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
export function originalHcCurrentFrameContract(mode){
 const source=originalConstructorContract(),role=source.negativeRoles.find(r=>r.mode===mode),image=counterBasis.roles.find(r=>r.mode===mode);
 need(['capabilities','hotkeys','dispose'].includes(mode)&&role&&image,'HC_FRAME_EXACT_ORIGINAL_ROLE');need(role.failedIds.length===image.expectedFailedCases,'HC_FRAME_ORIGINAL_FAILURE_COUNT');
 return {schemaVersion:1,mode,cases:89,caseIds:[...source.caseIds],parts:[79,7,3],faultPin:structuredClone(image.pin),proofPin:structuredClone(image.proof),changedMethod:image.changedMethod,failedIds:[...role.failedIds],expectedFailures:role.failedIds.length,errorFragment:role.errorFragment,expectedExitCode:2,budgetMs:60000,parentBudgetMs:360000,disposeCleanupRequired:mode==='dispose'?86:null,originalRawSelectedMethods:130,executionApprovalGranted:false};
}
function moveCopiedPins(v,mapping){
 if(typeof v==='string'){const m=path.isAbsolute(v)&&mapping.find(m=>samePath(m.before.path,v));return m?m.after.path:v;}
 if(Array.isArray(v))return v.map(x=>moveCopiedPins(x,mapping));
 if(v&&typeof v==='object'){const out=Object.fromEntries(Object.entries(v).map(([k,x])=>[k,moveCopiedPins(x,mapping)])),m=typeof v.path==='string'&&mapping.find(m=>samePath(m.before.path,v.path));if(m&&Object.hasOwn(v,'sha256'))out.sha256=m.after.sha256;return out;}return v;
}
export function projectHcCurrentFrame({role,plan,policy,runtimePins,newRoot,newRuntimePinsPath,newFaultPin,copied37}){
 const c=originalHcCurrentFrameContract(role);need([4,5].includes(policy.schemaVersion)&&plan.args.length===10&&plan.args[0]==='original-constructors'&&plan.args[7]==='all','HC_FRAME_ORIGINAL_ENTRY_AND_ALL');
 need(path.isAbsolute(newRoot)&&!samePath(newRoot,plan.ownedRoot)&&within(newRoot,newRuntimePinsPath)&&within(newRoot,newFaultPin.path)&&newFaultPin.sha256===c.faultPin.sha256,'HC_FRAME_OWNED_EXACT_ROLE_IMAGE');
 need(copied37.length===37&&plan.outputs.length===37&&new Set(copied37.map(m=>path.resolve(m.before.path).toUpperCase())).size===37&&new Set(copied37.map(m=>path.resolve(m.after.path).toUpperCase())).size===37,'HC_FRAME_COMPLETE37_MAPPING');
 for(const old of plan.outputs){const m=copied37.find(m=>exactPin(m.before,old));need(m&&m.after.sha256===old.sha256&&samePath(m.after.path,path.join(newRoot,path.relative(plan.ownedRoot,old.path)))&&within(plan.ownedRoot,old.path),'HC_FRAME_SAME37_PIN_COPY');}
 const ctx=policy.contexts.find(x=>x.id==='ConstructorContext');need(ctx&&ctx.managed.length===9,'HC_FRAME_CONSTRUCTOR9');const hcRows=ctx.managed.filter(m=>samePath(m.path,plan.args[1])&&m.sha256===nativeBasis.images.hc&&m.identity===counterBasis.identity);need(hcRows.length===1,'HC_FRAME_EXACT_HC_IN_CONSTRUCTOR_ONLY');const sourceHc=hcRows[0];
 need(!policy.contexts.filter(x=>x.id!=='ConstructorContext').some(x=>x.managed.some(m=>exactPin(m,sourceHc))),'HC_FRAME_NO_OTHER_REAL_HC_SCOPE');
 const fixture=policy.contexts[0].managed.filter(m=>m.sha256==='6d7bff05991ee407538b4ef35e12092b21a155d15ab5ca60fe0eae21846391f6');need(fixture.length===1,'HC_FRAME_EXACT_DEFAULT_FIXTURE');
 need(samePath(plan.args[9],policy.contexts[2].managed.find(m=>m.sha256===nativeBasis.images.host)?.path),'HC_FRAME_CLEAN_HOST_FIXED');
 const p=moveCopiedPins(policy,copied37);p.ownedRoot=newRoot;const next=p.contexts.find(x=>x.id==='ConstructorContext'),index=next.managed.findIndex(m=>exactPin(m,sourceHc));need(index>=0,'HC_FRAME_UNMOVED_HC_SOURCE');next.managed[index]={...next.managed[index],path:newFaultPin.path,sha256:newFaultPin.sha256};
 let requests=0;for(const context of p.contexts)for(const req of context.requests)if(samePath(req.targetPath,sourceHc.path)){need(req.targetContext==='ConstructorContext'&&req.requestedIdentity===sourceHc.identity,'HC_FRAME_EXACT_HC_REQUEST');req.targetPath=newFaultPin.path;requests++;}need(requests===1,'HC_FRAME_ONE_ORIGINAL_HC_REQUEST');
 need(equal(p.exactAliases,moveCopiedPins(policy.exactAliases,copied37)),'HC_FRAME_ALIAS_CONSERVATION');need(p.contexts[2].id===policy.contexts[2].id&&equal(p.contexts[2].managed,policy.contexts[2].managed),'HC_FRAME_PRODUCT_ID_AND_CLEAN_PAYLOAD_UNCHANGED');
 need(equal(p.contexts.map(c=>c.managed.length),policy.contexts.map(c=>c.managed.length))&&equal(p.contexts.map(c=>c.requests.length),policy.contexts.map(c=>c.requests.length)),'HC_FRAME_FULL_INVENTORY_CONSERVATION');
 const keys=Object.keys(runtimePins).filter(k=>samePath(k,sourceHc.path));need(keys.length===1&&runtimePins[keys[0]]===nativeBasis.images.hc&&!Object.keys(runtimePins).some(k=>samePath(k,newFaultPin.path)),'HC_FRAME_RUNTIME_ORIGINAL_SOURCE_PIN');
 // This is the original constructor-proof.mjs behavior: retain every original pin and append the fault image.
 const nextRuntimePins={...runtimePins,[newFaultPin.path]:newFaultPin.sha256},args=structuredClone(plan.args);args[1]=newFaultPin.path;args[5]=newRuntimePinsPath;args[6]=path.join(newRoot,'Build','original-constructors','result.json');
 const inverse=structuredClone(p),inverseCtx=inverse.contexts.find(x=>x.id==='ConstructorContext');inverseCtx.managed[index]=structuredClone(sourceHc);for(const context of inverse.contexts)for(const req of context.requests)if(req.targetContext==='ConstructorContext'&&samePath(req.targetPath,newFaultPin.path))req.targetPath=sourceHc.path;const restored=moveCopiedPins(inverse,copied37.map(m=>({before:m.after,after:m.before})));restored.ownedRoot=policy.ownedRoot;need(equal(restored,policy),'HC_FRAME_COMPLETE_POLICY_INVERSE');
 const inverseRuntime={...nextRuntimePins};delete inverseRuntime[newFaultPin.path];need(equal(inverseRuntime,runtimePins),'HC_FRAME_RUNTIME_INVERSE');
 return {policy:p,args,runtimePins:nextRuntimePins,sourceHc,faultPin:newFaultPin,contract:c,migration:{copied37:structuredClone(copied37),sourceHc:structuredClone(sourceHc),faultPin:structuredClone(newFaultPin),changedManagedScope:'ConstructorContext',changedHcRequests:1,unchangedProductId:policy.contexts[2].id,aliasRequestingContextReplacements:0,fullArgsChangedIndices:[1,5,6],businessArgv0IsFullArgsIndex1:true,inversePolicyEqualsSource:true,inverseRuntimeEqualsSource:true,defaultFixtureUnchanged:true,cleanHostUnchanged:true},executionApprovalGranted:false};
}
export function assessHcCurrentFrameResult(result,{role,settingsRoot}={}){
 const c=originalHcCurrentFrameContract(role);need(Array.isArray(result.cases)&&equal(result.cases.map(x=>x.id),c.caseIds),'HC_FRAME_ORIGINAL89_ORDER');
 return assessOriginalConstructorResult(result,{mode:role,hcSha256:c.faultPin.sha256,hostSha256:nativeBasis.images.host,settingsRoot});
}
