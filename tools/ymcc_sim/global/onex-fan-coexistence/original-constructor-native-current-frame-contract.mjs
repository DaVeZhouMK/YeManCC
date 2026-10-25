// Exact positive-frame admission and reversible metadata projection. No launcher.
import path from 'node:path';
import {need,digest} from './default-case-contract.mjs';
import {assessOriginalConstructorResult} from './prepare-original-constructors.mjs';
import basis from './original-constructor-native-fault-basis.json' with {type:'json'};
export const samePath=(a,b)=>typeof a==='string'&&typeof b==='string'&&path.resolve(a).toUpperCase()===path.resolve(b).toUpperCase();
const equal=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const key=p=>path.resolve(p).toUpperCase();
export const productId=file=>'Harness.ProductContext|'+key(path.dirname(file));
export const within=(r,p)=>{const s=path.relative(r,p);return !!s&&!path.isAbsolute(s)&&s!=='..'&&!s.startsWith('..'+path.sep);};
export function normalized(v,root){if(typeof v==='string'&&path.isAbsolute(v)&&(samePath(v,root)||within(root,v)))return '$OWNED/'+path.relative(root,v).replaceAll('\\','/');if(Array.isArray(v))return v.map(x=>normalized(x,root));if(v&&typeof v==='object')return Object.fromEntries(Object.entries(v).map(([k,x])=>[k,normalized(x,root)]));return v;}
export const exactPin=(a,b)=>!!a&&!!b&&samePath(a.path,b.path)&&a.sha256===b.sha256;
function natural(p,budget){need(p?.code===0&&p.signal===null&&p.error===null&&p.expired===false&&p.budgetMs===budget&&Number.isFinite(p.durationMs)&&p.durationMs>=0&&p.durationMs<=budget,'CURRENT_FRAME_NATURAL_PROCESS');}
export function validatePositiveCurrentFrame(f,{modelOnly=false}={}){
 const {plan:p,policy:q,result:r,provenance:v,process:proc,launch,metadataProcess:mp,metadataReceipt:mr,preload,preloadProcess:pp,derivedDefault44:d,originalPlan:o}=f;
 need(v?.kind==='original89-positive-frame-provenance'&&v.evidenceKind===(modelOnly?'MODEL_ONLY':'ACTUAL_PARENT_POSITIVE'),'CURRENT_FRAME_EXPLICIT_ACTUAL_POSITIVE_REQUIRED');
 if(!modelOnly)need(p.modelOnly!==true&&r.MODEL_ONLY!==true&&r.modelOnly!==true,'CURRENT_FRAME_MODEL_MARKER_CANNOT_PROMOTE');
 need([4,5].includes(q.schemaVersion)&&v.authoritySchema===q.schemaVersion,'CURRENT_FRAME_EXPLICIT_SCHEMA4_OR5');
 // Check the real89 oracle before inventories: an actual86FAIL frame can never become a base.
 const assessment=assessOriginalConstructorResult(r,{hcSha256:basis.images.hc,hostSha256:basis.images.host,settingsRoot:p.settingsRoot});
 need(r.nativeMathFaultCleanup===0&&r.status==='PASS','CURRENT_FRAME_POSITIVE_RESULT');
 need(exactPin(v.pins.positivePlan,f.positivePlanPin)&&exactPin(v.pins.positiveResult,f.positiveResultPin)&&exactPin(v.pins.policy,p.policy)&&exactPin(v.pins.originalPlan,p.originalPlan)&&p.originalPlan.sha256===basis.references.originalPlan.sha256,'CURRENT_FRAME_POSITIVE_PINS');
 need(samePath(p.businessResult,f.positiveResultPin.path)&&samePath(q.ownedRoot,p.ownedRoot)&&samePath(q.entry,p.producer.path),'CURRENT_FRAME_RESULT_POLICY_ROOT');
 need((p.originalBudgetMs??p.budgetMs)===60000&&p.parentBudgetMs===360000&&v.budgets.native===60000&&v.budgets.parent===360000&&v.expectedPositiveExit===0,'CURRENT_FRAME_ORIGINAL_BUDGETS');
 need(p.producer.sha256===basis.images.producer&&p.launcher.sha256===basis.images.launcher&&p.dotnet.sha256===o.dotnet.sha256&&exactPin(v.processProgram,p.dotnet),'CURRENT_FRAME_ORIGINAL_TOOLS');
 need(p.args.length===10&&p.args[0]==='original-constructors'&&p.args[7]==='all'&&p.args.every((x,i)=>[5,6].includes(i)||x===o.args[i])&&samePath(p.args[5],p.runtimePins.path)&&samePath(p.args[6],p.businessResult),'CURRENT_FRAME_ORIGINAL_TEN_ARGS');
 need(equal(v.retainedPolicy,q),'CURRENT_FRAME_COMPLETE_POLICY_INVENTORY');
 need(q.contexts.length===3&&q.contexts[0].id==='Default'&&q.contexts[0].kind==='default'&&q.contexts[1].id==='ConstructorContext'&&q.contexts[1].managed.length===9&&q.contexts[2].id===productId(p.args[9])&&q.contexts[2].managed.length===2,'CURRENT_FRAME_CONTEXTS');
 need(q.contexts.slice(1).every(c=>c.kind==='custom'&&exactPin(c.registrationAssembly,p.producer)&&c.registrationAssembly.identity===basis.identities.producer)&&q.contexts[2].registrationType==='Harness+ProductContext','CURRENT_FRAME_REGISTRATION');
 need(q.contexts[2].managed.some(x=>samePath(x.path,p.args[9])&&x.sha256===basis.images.host)&&q.contexts[2].managed.some(x=>samePath(x.path,o.backend.path)&&x.sha256===basis.images.backend&&x.identity===basis.identities.backend),'CURRENT_FRAME_FIXED_HOST_BACKEND');
 const productRequests=q.contexts[2].requests.filter(r=>r.targetContext===q.contexts[2].id);need(productRequests.length===2&&q.contexts[2].managed.every(m=>productRequests.filter(r=>samePath(r.targetPath,m.path)&&r.requestedIdentity===m.identity).length===1),'CURRENT_FRAME_EXACT_TWO_PRODUCT_REQUESTS');
 const expected9=f.originalPolicy.contexts[1].managed.map(m=>{if(!within(o.ownedRoot,m.path))return m;const relative=path.relative(o.ownedRoot,m.path),next=p.outputs.find(x=>path.relative(p.ownedRoot,x.path)===relative);need(next,'CURRENT_FRAME_CONSTRUCTOR_MEMBER_SOURCE');return {...m,path:next.path,sha256:next.sha256};});need(equal(q.contexts[1].managed,expected9)&&q.contexts[1].registrationType===f.originalPolicy.contexts[1].registrationType,'CURRENT_FRAME_CONSTRUCTOR9_UNCHANGED');
 const actual=p.outputs.map(x=>({relative:path.relative(p.ownedRoot,x.path).replaceAll('\\','/'),sha256:x.sha256})).sort((a,b)=>a.relative.localeCompare(b.relative));
 need(p.outputs.length===37&&new Set(actual.map(x=>x.relative.toUpperCase())).size===37&&actual.every(x=>within(p.ownedRoot,path.join(p.ownedRoot,x.relative)))&&equal(actual.map(x=>x.relative),basis.original37.map(x=>x.relative)),'CURRENT_FRAME_COMPLETE37_SHAPE');
 need(v.outputOrigins.length===37&&equal(v.outputOrigins.map(x=>({relative:x.relative,sha256:x.source.sha256})).sort((a,b)=>a.relative.localeCompare(b.relative)),actual),'CURRENT_FRAME_COMPLETE37_ORIGINS');
 for(const row of actual){const old=basis.original37.find(x=>x.relative===row.relative);if(row.sha256!==old.sha256){need(/\/(ScopedLoadAuthority\.dll|ScopedLoadAuthority\.pdb)$/.test(row.relative),'CURRENT_FRAME_ONLY_AUTHORITY_OUTPUT_REPLACEMENT');const kind=row.relative.endsWith('.dll')?'dll':'pdb';need(row.sha256===v.authorityOutputs[kind].sha256,'CURRENT_FRAME_AUTHORITY_SOURCE');}}
 need(exactPin(v.pins.derivedDefault44,p.derivedDefault44)&&exactPin(v.pins.authorityReview,p.authorityReview)&&d.originalNormalizedSha256===basis.default44ContextSha256&&d.inverseRecoversOriginalContext===true,'CURRENT_FRAME_BASE44_LINEAGE');
 const restored=structuredClone(d.context);need(restored.managed.length===44,'CURRENT_FRAME_BASE44');for(const m of restored.managed){const relative=path.relative(d.ownedRoot,m.path).replaceAll('\\','/'),old=basis.original37.find(x=>x.relative===relative);if(old)m.sha256=old.sha256;}
 need(digest(normalized(restored,d.ownedRoot))===basis.default44ContextSha256,'CURRENT_FRAME_BASE44_COMPUTED_INVERSE');
 const ext=v.defaultExtensions;need(Array.isArray(ext)&&new Set(ext.map(x=>key(x.path))).size===ext.length&&ext.every(x=>x.role==='framework'&&q.contexts[0].managed.some(y=>equal(x,y))&&!d.context.managed.some(y=>samePath(x.path,y.path))),'CURRENT_FRAME_EXACT_DEFAULT_EXTENSIONS');
 const base=q.contexts[0].managed.filter(x=>!ext.some(y=>samePath(x.path,y.path)));need(equal(normalized(base,p.ownedRoot),normalized(d.context.managed,d.ownedRoot)),'CURRENT_FRAME_DEFAULT_PIN_CONSERVATION');
 need(q.contexts[0].managed.length===44+ext.length&&new Set(q.contexts[0].managed.map(x=>key(x.path))).size===q.contexts[0].managed.length,'CURRENT_FRAME_DEFAULT_NOT_COUNT_ALLOWLIST');
 for(const c of q.contexts){need(new Set(c.managed.map(x=>key(x.path))).size===c.managed.length,'CURRENT_FRAME_UNIQUE_MANAGED');for(const req of c.requests){const target=q.contexts.find(x=>x.id===req.targetContext);need(target&&target.managed.some(x=>samePath(x.path,req.targetPath)&&x.identity===req.requestedIdentity)||q.exactAliases.some(a=>a.requestedIdentity===req.requestedIdentity&&a.targetContext===req.targetContext&&samePath(a.provider.path,req.targetPath)&&a.requestingContexts.includes(c.id)),'CURRENT_FRAME_REQUEST_EXACT_PROVIDER');}}
 const oldId=q.contexts[2].id;need(Array.isArray(q.exactAliases)&&q.exactAliases.length>0,'CURRENT_FRAME_EXPLICIT_ALIASES');
 for(const a of q.exactAliases){need(typeof a.requestedIdentity==='string'&&a.requestedIdentity.includes(', Version=')&&a.targetContext==='Default'&&q.contexts[0].managed.some(m=>equal(m,a.provider))&&new Set(a.requestingContexts).size===a.requestingContexts.length&&a.requestingContexts.filter(x=>x===oldId).length===1&&a.requestingContexts.every(x=>q.contexts.some(c=>c.id===x)),'CURRENT_FRAME_EXACT_ALIAS_SCOPE');}
 natural(proc,60000);need(equal(proc.args,[p.launcher.path,'run',p.policy.path,p.policy.sha256,v.pins.launch.path,...p.args])&&launch.exitCode===0&&launch.failure===null,'CURRENT_FRAME_PROCESS_PLAN_RECEIPT');
 need(Array.isArray(launch.actualLoadedManaged)&&[p.producer,p.launcher].every(t=>launch.actualLoadedManaged.some(x=>exactPin(x,t))),'CURRENT_FRAME_POSITIVE_LOADED_TOOLS');
 natural(mp,v.metadataBudgetMs);need(equal(mp.args,[p.launcher.path,'validate',p.policy.path,p.policy.sha256,v.pins.metadataReceipt.path])&&mr.exitCode===0&&mr.failure===null&&mr.productLoaded===false,'CURRENT_FRAME_METADATA_EXACT_POLICY');
 natural(pp,v.preloadBudgetMs);need(exactPin(preload.plan,f.positivePlanPin)&&exactPin(preload.process,v.pins.preloadProcess)&&exactPin(preload.preloadReceipt,v.pins.preloadReceipt)&&preload.allPinsAdmitted===true&&preload.defaultPins===q.contexts[0].managed.length&&preload.productAssembliesObserved===0&&preload.producerExecuted===false&&preload.loadPinExecuted===false,'CURRENT_FRAME_PRELOAD_EXACT_BASE');
 return {status:modelOnly?'PASS_MODEL_FRAME_ONLY_NOT_ACTUAL_ADMISSION':'PASS_EXPLICIT_ACTUAL_ORIGINAL89_FRAME',schema:q.schemaVersion,defaultManaged:q.contexts[0].managed.length,cases:89,assessment,modelOnly,executionApproved:false};
}
function transform(v,mapping){if(typeof v==='string'){const entry=path.isAbsolute(v)&&mapping.find(m=>samePath(v,m.before.path));return entry?entry.after.path:v;}if(Array.isArray(v))return v.map(x=>transform(x,mapping));if(v&&typeof v==='object'){const out=Object.fromEntries(Object.entries(v).map(([k,x])=>[k,transform(x,mapping)]));const entry=typeof v.path==='string'&&mapping.find(m=>samePath(v.path,m.before.path));if(entry&&Object.hasOwn(v,'sha256'))out.sha256=entry.after.sha256;return out;}return v;}
export function projectCurrentFramePolicy({policy,plan,newRoot,mapping}){
 need(!samePath(plan.ownedRoot,newRoot)&&path.isAbsolute(newRoot),'CURRENT_FRAME_NEW_ROOT');
 const oldId=productId(plan.args[9]),host=mapping.find(m=>samePath(m.before.path,plan.args[9]));need(host&&host.before.sha256===basis.images.host&&host.after.sha256!==basis.images.host&&within(newRoot,host.after.path),'CURRENT_FRAME_EXACT_FAULT_MAPPING');const newId=productId(host.after.path),p=transform(policy,mapping);p.ownedRoot=newRoot;
 need(p.contexts[2].id===oldId,'CURRENT_FRAME_OLD_PRODUCT_ID');p.contexts[2].id=newId;
 for(const c of p.contexts)for(const r of c.requests)if(r.targetContext===oldId)r.targetContext=newId;
 let aliasReplacements=0;for(const a of p.exactAliases){need(a.requestingContexts.filter(x=>x===oldId).length===1&&!a.requestingContexts.includes(newId),'CURRENT_FRAME_ALIAS_REPLACE_ONCE');a.requestingContexts=a.requestingContexts.map(x=>{if(x===oldId){aliasReplacements++;return newId;}return x;});}
 need(!JSON.stringify(p.contexts).includes(oldId)&&!p.exactAliases.some(a=>a.requestingContexts.includes(oldId)),'CURRENT_FRAME_NO_STALE_PRODUCT_SCOPE');
 const args=structuredClone(plan.args);args[6]=path.join(newRoot,'Build','original-constructors','result.json');args[9]=host.after.path;
 const inverse=transform(p,mapping.map(m=>({before:m.after,after:m.before})));inverse.ownedRoot=policy.ownedRoot;inverse.contexts[2].id=oldId;for(const c of inverse.contexts)for(const r of c.requests)if(r.targetContext===newId)r.targetContext=oldId;for(const a of inverse.exactAliases)a.requestingContexts=a.requestingContexts.map(x=>x===newId?oldId:x);
 need(equal(inverse,policy),'CURRENT_FRAME_COMPLETE_POLICY_INVERSE');need(equal(p.contexts.map(c=>c.managed.length),policy.contexts.map(c=>c.managed.length))&&equal(p.contexts.map(c=>c.requests.length),policy.contexts.map(c=>c.requests.length)),'CURRENT_FRAME_FULL_INVENTORY_CONSERVATION');
 return {policy:p,args,oldProductId:oldId,newProductId:newId,aliasReplacements,inverseEqualsSource:true,mapping:structuredClone(mapping),executionApproved:false};
}
