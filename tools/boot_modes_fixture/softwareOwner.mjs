import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {extract} from '../fixtures/legion_motion_consumer/extract_main.mjs';
import {calibrationToolchainPreamble} from '../fixtures/hc_reviewed_calibration/toolchain.mjs';
import {require,evidenceInputs,fingerprints} from './harness.mjs';

export async function runSoftwareBridge(out){
 const root=path.resolve(import.meta.dirname,'../..');
 const compiled=await require('esbuild').build({
  stdin:{contents:"export {shell} from './src/bridge/api.ts';export {fxSound} from './src/bridge/fxSound.ts';export {calls} from 'bridge-memory-ipc';",resolveDir:root,loader:'ts',sourcefile:'boot-software-bridge.ts'},
  absWorkingDir:root,bundle:true,platform:'node',format:'esm',write:false,metafile:true,
  plugins:[{name:'owned-ipc',setup(build){
   build.onResolve({filter:/^(\.\/ipc|bridge-memory-ipc)$/},()=>({path:'bridge-memory-ipc',namespace:'owned-ipc'}));
   build.onLoad({filter:/.*/,namespace:'owned-ipc'},()=>({contents:"export const calls=[];export async function invoke(command,args){calls.push({command,args:structuredClone(args)});return command==='shell.execute'?true:{ok:true,state:{known:true,installed:true,running:true,enabled:true,selected:true,preset:'owned',output:'owned',reason:''}};}",loader:'js'}));
  }}]
 });
 const file=path.join(out,'software-bridge.mjs');fs.writeFileSync(file,compiled.outputFiles[0].text,{flag:'wx'});
 const api=await import((await import('node:url')).pathToFileURL(file));
 const context={input:{revision:7},extensions:{bootModes:{schema:1,revision:3,current:'steam'}}};
 const expected=[];
 await api.shell.execute('owned.exe');expected.push({command:'shell.execute',args:{program:'owned.exe',args:[]}});
 await api.shell.execute('owned.exe',['a'],context);expected.push({command:'shell.execute',args:{program:'owned.exe',args:['a'],bootContext:context}});
 await api.fxSound.setEnabled(true);expected.push({command:'fxsound.setEnabled',args:{enabled:true}});
 await api.fxSound.setEnabled(true,4);expected.push({command:'fxsound.setEnabled',args:{enabled:true,expectedGeneration:4}});
 await api.fxSound.setEnabled(true,undefined,context);expected.push({command:'fxsound.setEnabled',args:{enabled:true,bootContext:context}});
 await api.fxSound.setEnabled(true,4,context);expected.push({command:'fxsound.setEnabled',args:{enabled:true,expectedGeneration:4,bootContext:context}});
 assert.deepEqual(api.calls,expected);
 const inputs=evidenceInputs(compiled.metafile).filter(file=>!file.startsWith('owned-ipc:')),result={status:'PASS',actualRequests:6,sourceHashes:fingerprints(inputs),
  nativeEndpoint:'NOT_RUN in bridge scope',realEffects:0};
 fs.writeFileSync(path.join(out,'software-bridge-result.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});return result;
}

export async function runSoftwareNative(out,args){
 const root=path.resolve(import.meta.dirname,'../..'),started=Date.now();
 const selection=args.find(v=>v.startsWith('--software-native-selection='))?.split('=')[1]??'current';
 assert(['current','inverse-shell','inverse-fx'].includes(selection));
 const dir=path.join(out,'native-software');fs.mkdirSync(dir);
 const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const pin=file=>({path:file,sha256:hash(file),bytes:fs.statSync(file).size});
 const write=(name,value)=>fs.writeFileSync(path.join(dir,name),JSON.stringify(value,null,2)+'\n',{flag:'wx'});
 const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8'),header=fs.readFileSync(path.join(root,'native/fxsound_control.h'),'utf8');
 const fragmentNames=['ymSettingsBootWriteMatches','mouseModeBootOwnerCheck'];
 const fragments=fragmentNames.map(name=>extract(main,name));
 const steamHeader=fs.readFileSync(path.join(root,'native/steam_launch_path.h'),'utf8'),normalizers=[...steamHeader.matchAll(/^inline std::wstring lower\([^\r\n]+/gm)];assert.equal(normalizers.length,1,'one real inline Steam normalizer');const steamClassification={code:normalizers[0][0]};assert(steamClassification.code.endsWith('}'));
 const registration=name=>{const start=main.indexOf('    ipc_on("'+name+'",'),end=main.indexOf('\n    });',start);assert(start>0&&end>start,name);return main.slice(start,end+'\n    });'.length);};
 let shell=registration('shell.execute'),fx=registration('fxsound.setEnabled');
 if(selection==='inverse-shell'){
  const expected='if(owner&&!owner())throw std::runtime_error("BOOT_STARTUP_REQUEST_EXPIRED");';
  assert.equal(shell.split(expected).length,2);shell=shell.replace(expected,'/* semantic inverse: omit only final native boot owner admission */');
 }
 if(selection==='inverse-fx'){
  const expected='&&(!owner||owner())';assert.equal(fx.split(expected).length,2);
  fx=fx.replace(expected,'/* semantic inverse: omit only owner from existing service predicate */');
 }
 const stateStart=main.indexOf('    const auto fxSoundStateJson='),stateEnd=main.indexOf('    ipc_on("fxsound.getState"',stateStart);assert(stateStart>0&&stateEnd>stateStart);
 const controlStart=header.indexOf('inline ControlResult setEnabled('),controlEnd=header.lastIndexOf('\n}');assert(controlStart>0&&controlEnd>controlStart);
 const control=header.slice(controlStart,controlEnd);assert(control.trim().endsWith('}'));
 const source=fs.readFileSync(path.join(root,'tools/boot_modes_fixture/softwareOwner.cpp.in'),'utf8')
  .replace('// ORIGINAL_STEAM_CLASSIFICATION','namespace ymcc::steamlaunch {\n'+steamClassification.code+'\n}')
  .replace('// ORIGINAL_OWNER_GUARD',fragments.map(v=>v.code).join('\n'))
  .replace('// ORIGINAL_FXSOUND_CONTROL',control)
  .replace('// ORIGINAL_STATE_JSON',main.slice(stateStart,stateEnd))
  .replace('// ORIGINAL_HANDLERS',shell+'\n'+fx);
 const cpp=path.join(dir,'probe.cpp'),exe=path.join(dir,'probe.exe');fs.writeFileSync(cpp,source,{flag:'wx'});
 const inputs=['native/main.cpp','native/steam_launch_path.h','native/fxsound_control.h','native/settings_baseline.h','PowerControl/settings-defaults-v1.json','tools/boot_modes_fixture/softwareOwner.mjs','tools/boot_modes_fixture/softwareOwner.cpp.in','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'].map(v=>pin(path.join(root,v)));
 const preamble=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');
 const command=path.join(dir,'compile.cmd');
 fs.writeFileSync(command,preamble+'set "CL="\r\nset "_CL_="\r\nset "LINK="\r\nset "_LINK_="\r\ncl.exe /nologo /std:c++20 /EHsc /utf-8 /W4 /permissive- /O2 /DNDEBUG /MD /I"'+root+'/native" /I"'+root+'/deps/json" "'+cpp+'" /Fo"'+dir+'/probe.obj" /Fe"'+exe+'" /sourceDependencies "'+dir+'/dependencies.json" /link user32.lib ole32.lib\r\nexit /b %errorlevel%\r\n',{flag:'wx'});
 write('TASK.json',{existingId:'settings.boot-modes-runtime',selection,inputs,fragments:fragments.map(({code,...identity})=>identity),generated:pin(cpp),originalNativeCompileLimitMs:60000,scope:'Actual native owner guard, shell/FxSound IPC and original FxSound setEnabled body. Settings/process/config/audio are inert memory leaves. No Win32 storage or installed program actions.'});
 const execute=(label,program,argv,timeout)=>{
  const begin=Date.now(),p=spawnSync(program,argv,{cwd:dir,env:process.env,windowsHide:true,encoding:'utf8',timeout,maxBuffer:8*1024*1024});
  for(const stream of ['stdout','stderr'])fs.writeFileSync(path.join(dir,label+'.'+stream+'.raw'),p[stream]??'',{flag:'wx'});
  write(label+'.process.json',{program,arguments:argv,cwd:dir,exitCode:p.status,error:p.error?.message??null,signal:p.signal,elapsedMs:Date.now()-begin});
  assert(!p.error&&!p.signal,'Owned native process did not finish');return p;
 };
 const compiler=execute('compile','cmd.exe',['/d','/c',command],60000);
 assert.equal(compiler.status,0,'Original native software source failed compilation');
 const run=execute('execute',exe,[path.join(root,'PowerControl/settings-defaults-v1.json'),selection],12000);
 assert(run.status===0||run.status===1,'Native consumer exited abnormally; inspect original process/raw before parsing');
 const actual=JSON.parse(run.stdout);
 if(selection==='current'){
  assert.equal(run.status,0,actual.error);assert.equal(actual.status,'PASS');assert.equal(actual.rows.length,216);
 }else {assert.equal(run.status,1);assert.equal(actual.rows.length,1);assert.match(actual.error,/expired native startup action reached an effect/);}
 const drift=inputs.filter(v=>hash(v.path)!==v.sha256);assert.equal(drift.length,0,'Native input changed during original consumer execution');
 const result={status:selection==='current'?'PASS':'EXPECTED_SEMANTIC_REJECTION',selection,actualRows:actual.rows.length,
  elapsedMs:Date.now()-started,inputs,generated:pin(cpp),executable:pin(exe),actual:pin(path.join(dir,'execute.stdout.raw')),
  originalFxSoundServiceBody:true,realProcessOrDeviceActions:0,fullNativeMain:'NOT_RUN'};
 write('RESULT.json',result);return result;
}
