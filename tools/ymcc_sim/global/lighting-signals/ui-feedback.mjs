import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '../../../..');
const out = path.resolve(process.argv[2]);
const sourceIndex = process.argv.indexOf('--component-source');
const componentPath = sourceIndex >= 0 ? path.resolve(process.argv[sourceIndex + 1]) : path.join(repo, 'src/components/ControllerFeedbackCapabilitiesCard.vue');
fs.mkdirSync(out, {recursive:true});
const baseline = fs.readFileSync(path.join(repo, 'tools/controller_lighting_operations_browser_selftest.mjs'), 'utf8');
const component = fs.readFileSync(componentPath);
let script = baseline.replace('return {ok:true}},vibrate:', "if(window.__lightingDefer){return await new Promise((resolve,reject)=>window.__lightingPending.push({resolve,reject}))}const response=window.__lightingNextReply??{ok:true};if(response.throwMessage)throw Error(response.throwMessage);return structuredClone(response)},vibrate:");
if (script === baseline) throw new Error('Original real-component bridge fixture changed; review required');
const readExpression = "fs.readFileSync(a.path,'utf8')";
if (!script.includes(readExpression)) throw new Error('Real component compiler fixture changed; review required');
script = script.replace(readExpression,"fs.readFileSync(a.path.endsWith('ControllerFeedbackCapabilitiesCard.vue')?"+JSON.stringify(componentPath)+":a.path,'utf8')");
const marker = ' assert.deepEqual(errors,[]);';
if (!script.includes(marker)) throw new Error('Original browser assertions changed; review required');
const tests = `
 const feedbackCases=[];
 for(const scenario of [
  {id:'backend-refusal',reply:{ok:false,reason:'light-device-core-missing'},expected:'灯光应用失败：light-device-core-missing'},
  {id:'bridge-exception',reply:{throwMessage:'fixture bridge timeout'},expected:'灯光应用失败：fixture bridge timeout'},
  {id:'missing-acknowledgement',reply:{},expected:'灯光应用失败：未知原因'},
  {id:'accepted-operation',reply:{ok:true},expected:'掌机灯光已应用。'}
 ]){
  await page.evaluate(reply=>{window.__lightingNextReply=reply;window.mount('mini')},scenario.reply);
  await page.waitForFunction(()=>window.f.applies.length===1);
  const actual=await page.getByRole('status').count()?await page.getByRole('status').textContent():null;
  const state=await page.evaluate(()=>({applies:window.f.applies,savedCount:window.f.saved.length}));
  feedbackCases.push({id:scenario.id,input:scenario.reply,consumer:'ControllerFeedbackCapabilitiesCard.applyLed -> rendered status',state,feedback:actual,expected:scenario.expected,status:actual===scenario.expected?'PASS':'FAIL',hardwareIo:false});
 }
 fs.writeFileSync(path.join(out,'ui-feedback-cases.json'),JSON.stringify(feedbackCases,null,2));
 assert.equal(feedbackCases.filter(c=>c.status==='FAIL').length,0,'real component must show refusal, exception, missing acknowledgement and current success');
 cases.push(...feedbackCases.map(c=>c.id));
`;
script = script.replace(marker, tests + marker);
const orderingTests = String.raw`
 for(const height of [720,800]){
  await page.setViewportSize({width:1280,height});
  for(const scenario of [
   {id:'older-success-after-newer-refusal',newReply:{ok:false,reason:'new-request-refused'},oldReply:{ok:true},expected:'灯光应用失败：new-request-refused'},
   {id:'older-refusal-after-newer-success',newReply:{ok:true},oldReply:{ok:false,reason:'old-request-refused'},expected:'掌机灯光已应用。'},
   {id:'older-exception-after-newer-success',newReply:{ok:true},oldReply:{throwMessage:'old bridge timeout'},expected:'掌机灯光已应用。'},
   {id:'older-success-after-newer-exception',newReply:{throwMessage:'new bridge timeout'},oldReply:{ok:true},expected:'灯光应用失败：new bridge timeout'}
  ]){
   await page.evaluate(()=>{window.__lightingDefer=false;window.__lightingNextReply={ok:true};window.mount('mini')});
   await page.waitForFunction(()=>window.f.applies.length===1&&document.querySelector('[role="status"]')?.textContent?.trim()==='掌机灯光已应用。');
   await page.evaluate(()=>{window.__lightingPending=[];window.__lightingDefer=true});
   const brightness=page.getByRole('slider',{name:'亮度',exact:true});
   await brightness.evaluate(el=>{el.value='53';el.dispatchEvent(new Event('input',{bubbles:true}))});
   await page.waitForFunction(()=>window.__lightingPending.length===1);
   await brightness.evaluate(el=>{el.value='74';el.dispatchEvent(new Event('input',{bubbles:true}))});
   await page.waitForFunction(()=>window.__lightingPending.length===2);
   await page.evaluate(async reply=>{const pending=window.__lightingPending[1];if(reply.throwMessage)pending.reject(new Error(reply.throwMessage));else pending.resolve(structuredClone(reply));await new Promise(resolve=>requestAnimationFrame(()=>resolve()));},scenario.newReply);
   const afterNew=(await page.getByRole('status').textContent())?.trim();
   await page.evaluate(async reply=>{const pending=window.__lightingPending[0];if(reply.throwMessage)pending.reject(new Error(reply.throwMessage));else pending.resolve(structuredClone(reply));await new Promise(resolve=>requestAnimationFrame(()=>resolve()));},scenario.oldReply);
   const afterOld=(await page.getByRole('status').textContent())?.trim();const args=await page.evaluate(()=>window.f.applies.slice(1));
   feedbackCases.push({id:scenario.id+'-'+height,input:{viewport:{width:1280,height},...scenario},consumer:'real ControllerFeedbackCapabilitiesCard onBrightness -> saveFeedback -> debounce -> applyLed -> setLed -> role=status',state:{callCount:args.length,args,afterNew,afterOld},feedback:afterOld,expected:scenario.expected,status:args.length===2&&args[0].brightness===53&&args[1].brightness===74&&afterNew===scenario.expected&&afterOld===scenario.expected?'PASS':'FAIL',sourceKind:'source-derived-fixture',hardwareIo:false,physicalEffectVerified:false});
  }
 }
 fs.writeFileSync(path.join(out,'ui-feedback-cases.json'),JSON.stringify(feedbackCases,null,2));
 fs.writeFileSync(path.join(out,'ordering-result.json'),JSON.stringify({status:feedbackCases.every(c=>c.status==='PASS')?'PASS':'FAIL',cases:feedbackCases,hardwareIo:false,physicalEffectVerified:false,scope:'actual Vue component and controls; controlled memory bridge response order, not navigation or physical acceptance'},null,2));
 assert.equal(feedbackCases.filter(c=>c.status==='FAIL').length,0,'latest issued lighting request must retain its own receipt after older replies');
 cases.push(...feedbackCases.filter(c=>c.input?.viewport).map(c=>c.id));
`;
script = script.replace(marker, orderingTests + marker);
const probe = path.join(out, 'real-component-feedback-probe.mjs');
fs.writeFileSync(probe, script);
const ran = spawnSync(process.execPath, [probe,out], {cwd:repo,encoding:'utf8',timeout:60000,windowsHide:true,maxBuffer:1024*1024});
fs.writeFileSync(path.join(out,'feedback-probe.log'), (ran.stdout??'')+'\n'+(ran.stderr??''));
const evidence = {schema:'ymcc.lighting-ui-feedback.v1',status:ran.status===0?'PASS':'FAIL',snapshotOverride:sourceIndex>=0,
 component:componentPath,sourceSha256:crypto.createHash('sha256').update(component).digest('hex'),
 consumer:'real Vue component applyLed -> status ref -> rendered role=status',hardwareIo:false,bridge:'memory fixture',
 sourceKind:'source-derived-fixture',exitCode:ran.status,stdout:ran.stdout,stderr:ran.stderr};
fs.writeFileSync(path.join(out,'feedback-result.json'),JSON.stringify(evidence,null,2));
console.log(JSON.stringify({status:evidence.status,snapshotOverride:evidence.snapshotOverride,sourceSha256:evidence.sourceSha256,hardwareIo:false}));
process.exitCode=ran.status===0?0:1;
