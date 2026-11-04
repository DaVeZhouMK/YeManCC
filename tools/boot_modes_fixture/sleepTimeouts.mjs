// Explicit scoped mode of boot_modes_selftest; production function slices,
// memory Windows terminals only. No real powercfg, installed UI or devices.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
const require=createRequire(path.resolve('package.json'));
const ts=require('typescript'),{transformSync}=require('esbuild');
const yemanFile='src/bridge/yeman.ts',viewFile='src/views/SleepGuardView.vue';
const names=['PW','SLEEP_TIMEOUT_POWER','SLEEP_TIMEOUT_FIELDS','readPlanPowerIndex','timeoutSecondsToMinutes','readSleepTimeouts','getActiveScheme','reactivateCurrentScheme','setSleepTimeout'];
function select(text,wanted){
 const ast=ts.createSourceFile('actual.ts',text,ts.ScriptTarget.Latest,true),found=new Map();
 for(const n of ast.statements){
  if(ts.isFunctionDeclaration(n)&&n.name&&wanted.includes(n.name.text))found.set(n.name.text,n.getText(ast).replace(/^export\s+/,''));
  if(ts.isVariableStatement(n))for(const d of n.declarationList.declarations)if(ts.isIdentifier(d.name)&&wanted.includes(d.name.text))found.set(d.name.text,'const '+d.getText(ast)+';');
 }
 for(const name of wanted)assert(found.has(name),'Actual declaration missing: '+name);
 return [...found.values()].join('\n')+'\nexport {'+wanted.join(',')+'};';
}
function evaluate(text,globals){const module={exports:{}};vm.runInNewContext(transformSync(text,{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,console,...globals},{timeout:1000});return module.exports;}
const key=(guid,sub,item,ac)=>[guid.toLowerCase(),sub,item,ac?'ACSettingIndex':'DCSettingIndex'].join('|');
const clone=value=>structuredClone(value);
// Independent Windows setting contract: do not seed expected addresses from the product under test.
const expectedFields={screen:{subGroup:'7516b95f-f776-4464-8c53-06167f40cc99',setting:'3c0bc021-c8a8-4e07-a973-6b14cbcb2b7e'},sleep:{subGroup:'238c9fa8-0aad-41ed-83f4-97be242c8f20',setting:'29f6c1db-86da-48c5-9fdb-f2b67b1f44da'},hibernate:{subGroup:'238c9fa8-0aad-41ed-83f4-97be242c8f20',setting:'9d7815a6-7ee4-497e-8888-515a05f02364'}};
function fixture(source){
 const state={calls:[],values:new Map(),live:new Map(),active:'',writeExit:0,activateExit:0,readbackWrong:false,activeUnknown:false,afterWrite:null,beforeActivate:null};
 const registry={read:async(_hive,regPath,name)=>{
  state.calls.push({kind:'read',path:regPath,name});const m=regPath.match(/PowerSchemes\\([^\\]+)\\([^\\]+)\\([^\\]+)$/);assert(m,'Actual registry path');
  return state.readbackWrong?999999:state.values.get(key(m[1],m[2],m[3],name==='ACSettingIndex'))??null;
 }};
 const shell={run:async(exe,args,timeout)=>{
  assert.equal(exe,'powercfg');state.calls.push({kind:'shell',args:[...args],timeout});
  if(['/setacvalueindex','/setdcvalueindex'].includes(args[0])){
   if(!state.writeExit)state.values.set(key(args[1],args[2],args[3],args[0]==='/setacvalueindex'),Number(args[4]));
   state.afterWrite?.();return {exitCode:state.writeExit,stdout:'',stderr:state.writeExit?'write denied':''};
  }
  if(args[0]==='/setactive'){
   state.beforeActivate?.();assert.equal(args[1],'SCHEME_CURRENT','Never activate a stale captured GUID');
   if(!state.activateExit)state.live=new Map([...state.values].filter(([k])=>k.startsWith(state.active.toLowerCase()+'|')));
   return {exitCode:state.activateExit,stdout:'',stderr:state.activateExit?'apply denied':''};
  }
  if(args[0]==='/getactivescheme')return {exitCode:state.activeUnknown?1:0,stdout:state.activeUnknown?'':'Power Scheme GUID: '+state.active,stderr:''};
  throw Error('Unexpected powercfg operation '+args.join(' '));
 }};
 const invoke=async command=>{assert.equal(command,'power.activeScheme');state.calls.push({kind:'active'});return state.activeUnknown?'':state.active;};
 const api=evaluate(select(source,names),{registry,shell,invoke});state.active=api.PW.YEMAN;
 const other='d18873c2-a73f-4d8a-a174-7cdb56832798';
 for(const guid of [api.PW.YEMAN,other,api.PW.WIN_BAL])for(const spec of Object.values(expectedFields))for(const ac of [true,false])state.values.set(key(guid,spec.subGroup,spec.setting,ac),3600);
 state.live=new Map([...state.values].filter(([k])=>k.startsWith(state.active+'|')));
 const stored=(field,ac,guid=api.PW.YEMAN)=>{const s=expectedFields[field];return state.values.get(key(guid,s.subGroup,s.setting,ac));};
 const live=(field,ac,guid=api.PW.YEMAN)=>{const s=expectedFields[field];return state.live.get(key(guid,s.subGroup,s.setting,ac));};
 return {api,state,stored,live,other};
}
function ui(f){
 const script=fs.readFileSync(viewFile,'utf8').match(/<script setup[^>]*>([\s\S]*?)<\/script>/)[1];
 const busy={value:false},timeoutLoading={value:false},msg={value:''},sleepTimeouts={acScreen:60,dcScreen:60,acSleep:60,dcSleep:60,acHibernate:60,dcHibernate:60};
 const functions=evaluate(select(script,['refreshSleepTimeouts','onSleepTimeout']),{busy,timeoutLoading,msg,sleepTimeouts,readSleepTimeouts:f.api.readSleepTimeouts,setSleepTimeout:f.api.setSleepTimeout});
 return {functions,busy,timeoutLoading,msg,sleepTimeouts};
}
export async function runSleepTimeouts(out,args=[]){
 const beforeDir=args.includes('--before-dir')?args[args.indexOf('--before-dir')+1]:null;
 assert(beforeDir,'--before-dir is required for the pre-fix regression negative');
 const source=fs.readFileSync(yemanFile,'utf8'),old=fs.readFileSync(path.join(beforeDir,yemanFile),'utf8');
 const files=[yemanFile,viewFile,'tools/boot_modes_selftest.mjs','tools/boot_modes_fixture/sleepTimeouts.mjs'];
 const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex'),pins=Object.fromEntries(files.map(file=>[file,hash(file)]));
 const report={pass:false,scope:'Actual AST declaration slices of timeout bridge/current-plan refresh/readback and Vue save handler; memory Windows terminals. No installed app, hardware, real powercfg or browser. Boot-mode dispatcher and full UI engine NOT_RUN.',cases:[],sourceHashes:pins,realWindowsWrites:0,actualWindowsIdleAcceptance:'NOT_RUN'};
 const check=async(name,run)=>{try{await run();report.cases.push({name,pass:true});}catch(error){report.cases.push({name,pass:false,error:error.stack||String(error)});}};
 const shellCalls=f=>f.state.calls.filter(c=>c.kind==='shell'),applied=f=>shellCalls(f).filter(c=>c.args[0]==='/setactive');
 await check('Screen/sleep/hibernate addresses match independent Windows GUIDs including the 666-second report',async()=>{const f=fixture(source);assert.deepEqual(clone(f.api.SLEEP_TIMEOUT_FIELDS),expectedFields);const reportText=fs.readFileSync(path.join(beforeDir,'../Report.1.html'),'utf8');assert(reportText.includes(expectedFields.screen.subGroup+'\\'+expectedFields.screen.setting));assert(reportText.includes('0000029a'));assert.equal(parseInt('29a',16),666);});
 await check('Old source reproduces saved 60s but live policy remains 3600s',async()=>{const f=fixture(old);await f.api.setSleepTimeout('screen',true,1);assert.equal(f.stored('screen',true),60);assert.equal(f.live('screen',true),3600);assert.equal(applied(f).length,0);});
 for(const field of ['screen','sleep','hibernate'])for(const ac of [true,false])await check(`${field}/${ac?'AC':'DC'} one minute updates only its 60-second live policy`,async()=>{
  const f=fixture(source),before=clone(f.state.values);await f.api.setSleepTimeout(field,ac,1);assert.equal(f.stored(field,ac),60);assert.equal(f.live(field,ac),60);assert.equal(applied(f).length,1);
  const spec=expectedFields[field],edited=key(f.api.PW.YEMAN,spec.subGroup,spec.setting,ac);
  for(const [k,v]of before)if(k!==edited)assert.equal(f.state.values.get(k),v,'Other row/side/plan preserved');
  assert.deepEqual(shellCalls(f).map(c=>c.args[0]),[ac?'/setacvalueindex':'/setdcvalueindex','/setactive']);
 });
 await check('Six fields read correct AC/DC values without writes or activation',async()=>{const f=fixture(source);let n=1;for(const field of ['screen','sleep','hibernate'])for(const ac of [true,false]){const s=expectedFields[field];f.state.values.set(key(f.api.PW.YEMAN,s.subGroup,s.setting,ac),60*n++);}assert.deepEqual(clone(await f.api.readSleepTimeouts()),{acScreen:1,dcScreen:2,acSleep:3,dcSleep:4,acHibernate:5,dcHibernate:6});assert.equal(shellCalls(f).length,0);});
 await check('Never=0 stays zero in six fields; no direct screen-off/sleep commands',async()=>{for(const field of ['screen','sleep','hibernate'])for(const ac of [true,false]){const f=fixture(source);await f.api.setSleepTimeout(field,ac,0);assert.equal(f.stored(field,ac),0);assert.equal(f.live(field,ac),0);assert.equal(shellCalls(f).length,2);}});
 await check('Inactive alternative with a similar name stays untouched and inactive YeMan is only saved',async()=>{const f=fixture(source);f.state.active=f.other;const before=clone([...f.state.values].filter(([k])=>k.startsWith(f.other)));await f.api.setSleepTimeout('screen',true,1);assert.equal(f.stored('screen',true),60);assert.equal(applied(f).length,0);assert.equal(f.state.active,f.other);assert.deepEqual([...f.state.values].filter(([k])=>k.startsWith(f.other)),before);});
 await check('User scheme change while saving is preserved without switching to YeMan',async()=>{const f=fixture(source);f.state.afterWrite=()=>{f.state.active=f.api.PW.WIN_BAL;};await f.api.setSleepTimeout('screen',true,1);assert.equal(f.state.active,f.api.PW.WIN_BAL);assert.equal(applied(f).length,0);assert.equal(f.stored('screen',true,f.api.PW.WIN_BAL),3600);});
 await check('Last-moment scheme change stays active through SCHEME_CURRENT',async()=>{const f=fixture(source);f.state.beforeActivate=()=>{f.state.active=f.api.PW.WIN_BAL;};await f.api.setSleepTimeout('screen',true,1);assert.equal(f.state.active,f.api.PW.WIN_BAL);assert.equal(applied(f)[0].args[1],'SCHEME_CURRENT');assert.equal(f.stored('screen',true,f.api.PW.WIN_BAL),3600);});
 await check('Denied write fails before activation with original values preserved',async()=>{const f=fixture(source);f.state.writeExit=5;await assert.rejects(f.api.setSleepTimeout('screen',true,1),/写入失败/);assert.equal(applied(f).length,0);assert.equal(f.stored('screen',true),3600);assert.equal(f.live('screen',true),3600);});
 await check('Readback mismatch refuses an applied-success claim',async()=>{const f=fixture(source);f.state.readbackWrong=true;await assert.rejects(f.api.setSleepTimeout('screen',true,1),/回读不一致/);assert.equal(applied(f).length,0);assert.equal(f.live('screen',true),3600);});
 await check('Activation failure reports saved but incomplete and keeps old live policy',async()=>{const f=fixture(source);f.state.activateExit=5;await assert.rejects(f.api.setSleepTimeout('screen',true,1),/已保存.*刷新未完成/);assert.equal(f.stored('screen',true),60);assert.equal(f.live('screen',true),3600);});
 await check('Unknown active plan refuses to switch or falsely report completed activation',async()=>{const f=fixture(source);f.state.activeUnknown=true;await assert.rejects(f.api.setSleepTimeout('screen',true,1),/已保存.*无法确认/);assert.equal(applied(f).length,0);});
 await check('Nonfinite value and unsupported field fail before all Windows terminals',async()=>{for(const [field,minutes]of [['screen',NaN],['screen',Infinity],['other',1]]){const f=fixture(source);await assert.rejects(f.api.setSleepTimeout(field,true,minutes),/参数无效/);assert.equal(f.state.calls.length,0);}});
 await check('Actual UI retains saved 1 minute when apply fails and displays that failure',async()=>{const f=fixture(source),v=ui(f);f.state.activateExit=5;await v.functions.onSleepTimeout('screen',true,1);assert.equal(v.sleepTimeouts.acScreen,1);assert.match(v.msg.value,/已保存.*刷新未完成/);assert.equal(v.busy.value,false);assert.equal(v.sleepTimeouts.dcScreen,60);});
 await check('UI rereads denied writes then permits one explicit successful retry',async()=>{const f=fixture(source),v=ui(f);f.state.writeExit=5;await v.functions.onSleepTimeout('screen',true,1);assert.equal(v.sleepTimeouts.acScreen,60);assert.match(v.msg.value,/写入失败/);f.state.writeExit=0;await v.functions.onSleepTimeout('screen',true,1);assert.equal(v.sleepTimeouts.acScreen,1);assert.equal(v.msg.value,'');assert.equal(applied(f).length,1);});
 await check('Busy/loading UI suppresses duplicate mutations',async()=>{for(const stateKey of ['busy','timeoutLoading']){const f=fixture(source),v=ui(f);v[stateKey].value=true;await v.functions.onSleepTimeout('screen',true,1);assert.equal(f.state.calls.length,0);}});
 await check('Common current-plan activation validates nonzero exit for existing callers',async()=>{const f=fixture(source);f.state.activateExit=5;await assert.rejects(f.api.reactivateCurrentScheme(),/刷新失败/);});
 assert.deepEqual(Object.fromEntries(files.map(file=>[file,hash(file)])),pins,'Source changed during regression');
 report.passed=report.cases.filter(c=>c.pass).length;report.failed=report.cases.length-report.passed;report.pass=report.failed===0;
 fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'sleep-timeouts.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({pass:report.pass,passed:report.passed,failed:report.failed,output:out},null,2));if(!report.pass)process.exitCode=1;
}