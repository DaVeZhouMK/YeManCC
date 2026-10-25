import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';import {createHash} from 'node:crypto';import {spawnSync} from 'node:child_process';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {closeBrace} from './fixtures/hc_remaining_model_capabilities/expectations.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const runner=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(runner),'..'),workspace=path.resolve(repo,'../../..');
const control=process.argv[2]||'';if(!['','--ignore-owner','--ignore-cancellation'].includes(control)||process.argv.length>3)throw Error('Unknown arguments');
const root=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/WindowsInventory-20261018');
const out=path.join(root,new Date().toISOString().replace(/[:.]/g,'-')+(control||'-baseline'));fs.mkdirSync(out,{recursive:true});
const sha=b=>createHash('sha256').update(b).digest('hex'),inputs=[];
const freeze=p=>{const b=fs.readFileSync(p);inputs.push({path:p,sha256:sha(b)});return b;};
const result={schema:1,status:'running',control,configurations:[],sourceBindings:[],newProductDlls:[],hardwareIo:false,normalAppStarted:false,wholeNativeThreadProcExecuted:false};
const save=()=>fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2)+'\n');save();
try{
 const source=freeze(path.join(repo,'native/main.cpp')).toString('utf8');fs.writeFileSync(path.join(out,'main.cpp.snapshot'),source);
 let fixture=freeze(path.join(repo,'tools/fixtures/hc_windows_inventory/harness.cpp')).toString('utf8');
 const bindings=[['PROVIDER','inputCaptureSensorProviderKey'],['JSON','inputCaptureWindowsInventoryJson'],['OBSERVE','inputCaptureObserveWindowsInventory']];
 for(const [marker,name]of bindings){const part=extract(source,name);result.sourceBindings.push({...part,code:undefined});fixture=fixture.replace('// @@'+marker+'_FUNCTION@@',part.code);}
 const actor=extract(source,'inputCaptureSensorThreadProc');const marker='// Metadata census runs after every command in this batch is acknowledged.';
 const start=actor.code.indexOf('if(!shutdown)',actor.code.indexOf(marker)),open=actor.code.indexOf('{',start),end=closeBrace(actor.code,open)+1;
 if(start<0||open<0||end<=open||!actor.code.slice(0,start).includes('g_sensorThreadMailbox.complete(ticket,success);'))throw Error('Census must follow ticket acknowledgement');
 const commandLoop=actor.code.indexOf('while (const auto ticket=g_sensorThreadMailbox.take())'),commandOpen=actor.code.indexOf('{',commandLoop),commandEnd=closeBrace(actor.code,commandOpen)+1;
 if(!(commandEnd<start&&actor.code.indexOf('bool shutdown=false;')<commandLoop))throw Error('Census must follow entire command batch');
 const tail=actor.code.slice(start,end);result.sourceBindings.push({symbol:'inputCaptureSensorThreadProc:inventoryTail',sha256:sha(tail),wholeActorSha256:actor.sha256,afterCommandLoop:true});
 fixture=fixture.replace('// @@ACTOR_TAIL@@',tail);if(fixture.includes('@@'))throw Error('Unresolved fixture markers');
 const files=['sensor_inventory.h','sensor_inventory_cache.h','sensor_thread_mailbox.h','windows_motion_submission.h'];
 for(const name of files)fs.writeFileSync(path.join(out,name),freeze(path.join(repo,'native',name)));
 const jsonPath=path.join(repo,'deps/json/json.hpp');fs.writeFileSync(path.join(out,'json.hpp'),freeze(jsonPath));
 freeze(runner);freeze(path.join(repo,'tools/fixtures/legion_motion_consumer/extract_main.mjs'));freeze(path.join(repo,'tools/fixtures/hc_remaining_model_capabilities/expectations.mjs'));freeze(path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'));
 if(control==='--ignore-owner'){
  const original=/ymcc::windows_motion::current\(ticket\.binding\)&&g_inputCaptureSink&&\s+ticket\.binding\.activeBinding\.get\(\)==&g_inputCaptureSink->activeBindingGeneration/;
  if(!original.test(fixture))throw Error('Negative owner anchor');fixture=fixture.replace(original,'ymcc::windows_motion::current(ticket.binding)');
 }
 if(control==='--ignore-cancellation'){
  const header=path.join(out,'sensor_inventory.h'),original=fs.readFileSync(header,'utf8');
  const changed=original.replaceAll('if(cancelled())','if(false)');if(changed===original)throw Error('Negative cancellation anchor');fs.writeFileSync(header,changed);
 }
 fs.writeFileSync(path.join(out,'inventory.cpp'),fixture);
 const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
 for(const configuration of ['Release','Checked']){
  const exe=path.join(out,configuration+'.exe'),bat=path.join(out,'build-'+configuration+'.cmd');
  const flags=configuration==='Release'?'/O2 /DNDEBUG':'/Od /Zi /D_ITERATOR_DEBUG_LEVEL=2 /MDd';
  fs.writeFileSync(bat,calibrationToolchainPreamble(vcvars)+`cl /nologo /std:c++17 /EHsc /utf-8 ${flags} /Fe:"${exe}" "${path.join(out,'inventory.cpp')}"\r\nexit /b %errorlevel%\r\n`);
  const build=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',bat],{cwd:out,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:4*1024*1024});
  fs.writeFileSync(path.join(out,'build-'+configuration+'.log'),(build.stdout||'')+(build.stderr||'')+(build.error?.stack||''));if(build.status!==0)throw Error('Compile '+configuration+' failed: '+build.status);
  const report=path.join(out,configuration+'.json'),run=spawnSync(exe,[report],{cwd:out,windowsHide:true,encoding:'utf8',timeout:30000,maxBuffer:4*1024*1024});
  fs.writeFileSync(path.join(out,'run-'+configuration+'.log'),(run.stdout||'')+(run.stderr||'')+(run.error?.stack||''));
  if(!fs.existsSync(report))throw Error('Executable report absent');const data=JSON.parse(fs.readFileSync(report,'utf8'));
  result.configurations.push({configuration,exitCode:run.status,...data});
  if(control){if(run.status!==1||data.status!=='fail'||!data.failures?.length)throw Error('Negative control must fail executable assertions');}
  else if(run.status!==0||data.status!=='pass')throw Error('Executable assertions failed: '+configuration);
 }
 result.inputIntegrity=inputs.map(p=>({...p,unchanged:sha(fs.readFileSync(p.path))===p.sha256}));if(result.inputIntegrity.some(p=>!p.unchanged))throw Error('Frozen inputs changed during execution');
 result.status=control?'negative-control-detected':'pass';save();console.log(JSON.stringify({status:result.status,out,configurations:result.configurations.map(({configuration,checks,failures})=>({configuration,checks,failures}))},null,2));
}catch(error){result.status='fail';result.error=error.stack;save();console.error(error.stack);console.error(out);process.exitCode=1;}
