import{lockMonitorToolScope,verifyMonitorToolScope}from './tool-authority.mjs';
import {classifyTemplateLabelContract} from './retained-label-contract.mjs';
import fs from'node:fs';import path from'node:path';import{pathToFileURL}from'node:url';import{spawnSync}from'node:child_process';
import{root,hash,readJson,assertionCalls,retained,current}from'./common.mjs';
import{OLD_LABEL_ASSERT,NEW_LABEL_ASSERT}from'./preset-contract.mjs';
import{resolveTemplateArtifact,copyTemplateArtifact,verifyTemplateCopy}from'./template-artifact.mjs';
export function runRetained(out,{ids=['control-clock'],templateMode='original',templateArtifact,toolAuthority}={}){const results=[];const tools=toolAuthority??lockMonitorToolScope(root,{templateArtifact});verifyMonitorToolScope(tools);
 for(const spec of retained.filter(item=>ids.includes(item.id))){const dir=path.join(out,spec.id);fs.mkdirSync(dir,{recursive:true});const file=path.join(root,spec.source),original=fs.readFileSync(file,'utf8');let text=original;
 if(spec.id==='control-clock'){const old="path.resolve(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/desktop-output-20261011/control-clock')";if(text.split(old).length!==2)throw Error('CONTROL_OUTPUT_ANCHOR_CHANGED');text=text.replace(old,JSON.stringify(dir));}
 let templateCopy=null,artifactRedirection=null;
 if(spec.id==='templates'){
  templateCopy=copyTemplateArtifact(templateArtifact??resolveTemplateArtifact(root),dir);
  const from="path.join(root, 'PowerControl/RTSS-Overlays/YeManOBS-W-1.ovl')",to=JSON.stringify(templateCopy.ownedCopy.path);
  if(text.split(from).length!==2)throw Error('TEMPLATE_ARTIFACT_OVL_ANCHOR_CHANGED');
  const beforeAsset=assertionCalls(text);text=text.replace(from,to);const afterAsset=assertionCalls(text);
  if(JSON.stringify(beforeAsset)!==JSON.stringify(afterAsset))throw Error('TEMPLATE_ARTIFACT_ORIGINAL_ASSERTION_CHANGED');
  artifactRedirection={originalExpression:from,ownedExpression:to,originalProducer:spec.source,originalProducerSha256:hash(original),beforeAssertionCalls:beforeAsset,afterAssertionCalls:afterAsset,allAssertionsExactlyPreserved:true,scope:'one ovlPath expression only; no original assertions, labels, ratios or product defaults changed by artifact redirection'};
  fs.writeFileSync(path.join(dir,'template-artifact.json'),JSON.stringify({templateCopy,artifactRedirection},null,2));
 }
 text=text.replace('await build({','await __auditedBuild({');if(text===original&&spec.id==='control-clock')throw Error('RETAINED_BUILD_ANCHOR');
 const sourceLabelContract=spec.id==='templates'?classifyTemplateLabelContract(original):null;
 const metadataChanges=[];
 if(spec.id==='templates'&&templateMode==='migrated'&&sourceLabelContract.mode==='legacy-label-expectation'){
  if(text.split(OLD_LABEL_ASSERT).length!==2)throw Error('EXPECTED_ONE_LABEL_METADATA_ASSERTION');
  text=text.replace(OLD_LABEL_ASSERT,NEW_LABEL_ASSERT);metadataChanges.push({before:OLD_LABEL_ASSERT,after:NEW_LABEL_ASSERT,scope:'label metadata only'});
 }
 const before=assertionCalls(original),after=assertionCalls(text),remaining=[...after];
 for(const call of before){const expected=metadataChanges.length?call.replace(OLD_LABEL_ASSERT,NEW_LABEL_ASSERT):call;const index=remaining.indexOf(expected);if(index<0)throw Error('ORIGINAL_ASSERTION_CHANGED:'+call);remaining.splice(index,1);}
 if(spec.id==='templates'){
  const old="const cases = []; const check = (label, fn) => { fn(); cases.push(label); };";
  const next="const cases = []; const __fullCases=[]; const check = (label, fn) => { try{fn();__fullCases.push({label,status:'PASS'});}catch(error){__fullCases.push({label,status:'FAIL',error:error.message,stack:error.stack});} cases.push(label); };";
  if(text.split(old).length!==2)throw Error('TEMPLATE_CASE_REPORTER_CHANGED');text=text.replace(old,next);
  text=text.replace("const cases = [];","await __verifyPresetContract(m,out); const cases = [];");
  text=text.replace('passed: cases.length','passed: __fullCases.filter(c=>c.status===\'PASS\').length');
  text+="\nconst __fullReport={...report,status:__fullCases.every(c=>c.status==='PASS')?'PASS':'FAIL',passed:__fullCases.filter(c=>c.status==='PASS').length,failed:__fullCases.filter(c=>c.status==='FAIL').length,fullCases:__fullCases};fs.writeFileSync(path.join(out,'TEMPLATE-MODEL-SELFTEST.json'),JSON.stringify(__fullReport,null,2));process.exitCode=__fullReport.failed?1:0;\n";
 }

 const script=path.join(dir,'retained.mjs');const prefix=(spec.id==='templates'?'import{verifyPresetContract as __verifyPresetContract}from '+JSON.stringify(pathToFileURL(path.join(root,'tools/ymcc_sim/global/monitor-output-signals/preset-contract.mjs')).href)+';\n':'')+'import {auditedBuild as __auditedBuild,finalizeRetainedToolRuntime as __finalizeToolRuntime} from '+JSON.stringify(pathToFileURL(path.join(root,'tools/ymcc_sim/global/monitor-output-signals/retained-runtime.mjs')).href)+';\n';
 fs.writeFileSync(script,prefix+text+"\n__finalizeToolRuntime();\n");const run=spawnSync(process.execPath,[script],{cwd:root,encoding:'utf8',windowsHide:true,timeout:30000,maxBuffer:1024*1024,
 env:{...process.env,YMCC_MONITOR_MODEL_OUT:dir,YMCC_OUTPUT_LOAD_RECEIPT:path.join(dir,'loads.json')}});
 fs.writeFileSync(path.join(dir,'stdout.log'),run.stdout??'');fs.writeFileSync(path.join(dir,'stderr.log'),run.stderr??'');
 if(templateCopy){try{templateCopy=verifyTemplateCopy(templateCopy);}catch(error){fs.writeFileSync(path.join(dir,'template-artifact.json'),JSON.stringify({templateCopy,artifactRedirection,status:'FAIL',error:error.message},null,2));throw error;}fs.writeFileSync(path.join(dir,'template-artifact.json'),JSON.stringify({templateCopy,artifactRedirection},null,2));}
 verifyMonitorToolScope(tools);
 const receiptFile=path.join(dir,spec.receipt);
 const receipt=fs.existsSync(receiptFile)?readJson(receiptFile):null,loads=fs.existsSync(path.join(dir,'loads.json'))?readJson(path.join(dir,'loads.json')):null;
 if(loads?.toolAuthority&&JSON.stringify(loads.toolAuthority)!==JSON.stringify(tools))throw Error('MONITOR_TOOL_RETAINED_AUTHORITY_MISMATCH');
 const status=run.status===0&&receipt&&loads?.toolScopeVerified===true&&receipt.status!=='FAIL'?'PASS':'FAIL';
 results.push({sourceApproval:false,toolAuthority:loads?.toolAuthority??null,toolRuntime:loads?.toolRuntime??null,signal:run.signal, id:spec.id,status,pid:run.pid,command:{executable:process.execPath,args:[script],cwd:root,timeoutMs:30000},source:spec.source,sourceSha256:hash(original),sourceLabelContract,allOriginalAssertionsPreserved:metadataChanges.length===0,allBehaviorAssertionsPreserved:true,metadataAssertionChanges:metadataChanges,originalAssertionCalls:before.length,afterAssertionCalls:after.length,
 presetContract:fs.existsSync(path.join(dir,'preset-contract.json'))?readJson(path.join(dir,'preset-contract.json')):null,
 templateArtifact:templateCopy,artifactRedirection,ownedSourceHashes:templateCopy?{[templateCopy.ownedCopy.path]:templateCopy.ownedCopy.sha256}:{},
 receipt,loadedSourceHashes:loads?.loadedSourceHashes??{},exitCode:run.status,error:run.error?.message??null,
 executionMeaning:spec.id==='control-clock'?'Named refcount clock/desktop/relay operations; final monitorHudHost text assertion remains source-contract only':'Formal pure model/coordinator with injected routing deps; no Steam runtime behavior'});
 }
 return results;
}
