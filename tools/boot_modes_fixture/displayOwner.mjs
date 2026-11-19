import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createHash} from 'node:crypto';import {pathToFileURL} from 'node:url';import {spawnSync} from 'node:child_process';
import {require} from './harness.mjs';import {extract} from '../fixtures/legion_motion_consumer/extract_main.mjs';import {calibrationToolchainPreamble} from '../fixtures/hc_reviewed_calibration/toolchain.mjs';
export async function runDisplayOwnerNative(out,args=[]){
 const root=process.cwd(),dir=path.join(out,'native-display');fs.mkdirSync(dir);
 const before=args.includes('--display-owner-before'),selection=before?'before':'all',hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const pin=file=>({path:file,bytes:fs.statSync(file).size,sha256:hash(file)}),write=(name,value)=>fs.writeFileSync(path.join(dir,name),JSON.stringify(value,null,2)+'\n',{flag:'wx'});
 const memory=`export const calls=[];export async function invoke(command,args){if(command!=='display.setMode')throw Error('Unexpected API command '+command);calls.push(structuredClone(args));return {...args,ok:true};}`;
 const built=await require('esbuild').build({stdin:{contents:"export {display} from './src/bridge/api.ts';export {calls} from 'display-owned-memory';",resolveDir:root,sourcefile:'display-owned-api.ts',loader:'ts'},absWorkingDir:root,bundle:true,platform:'node',format:'esm',write:false,metafile:true,plugins:[{name:'display-owned-api',setup(build){build.onResolve({filter:/^(display-owned-memory|\.\/ipc)$/},()=>({path:'terminal',namespace:'display-owned'}));build.onLoad({filter:/.*/,namespace:'display-owned'},()=>({contents:memory,loader:'ts'}));}}]});
 const apiFile=path.join(dir,'api.mjs');fs.writeFileSync(apiFile,built.outputFiles[0].text,{flag:'wx'});const api=await import(pathToFileURL(apiFile));
 const defaults=JSON.parse(fs.readFileSync(root+'/PowerControl/settings-defaults-v1.json','utf8')),mode={id:'1280x720@60/0',width:1280,height:720,refresh:60,orientation:0},modes=[];
 for(const name of ['global','xbox','steam','xbox-steam']){
  const input=structuredClone(defaults.input);input.revision=7;
  const context={input,extensions:{bootModes:{schema:1,revision:3,current:name,global:{display:{brightness:50,resolution:'1920x1080',hz:'120'}}}}};
  const submitted=structuredClone(context);await api.display.setMode(mode,{origin:'boot-mode',bootContext:context,fields:['resolution']});const request=api.calls.at(-1);context.input.revision=999;
  if(!before)assert.deepEqual(request.bootContext,submitted,'Actual display API dropped or retained a mutable owner');
  assert.equal(request.origin,'boot-mode');assert.deepEqual(request.preferenceFields,['resolution']);modes.push({mode:name,request,expectedBootContext:submitted});
 }
 await api.display.setMode(mode);const ordinary=api.calls.at(-1);assert(!('bootContext'in ordinary));assert(!('origin'in ordinary));
 let invalidContextRejected=false;
 if(!before){const count=api.calls.length;await assert.rejects(async()=>api.display.setMode(mode,{origin:'ordinary',bootContext:modes[0].expectedBootContext}),/INVALID_BOOT_PREFERENCE_CONTEXT/);assert.equal(api.calls.length,count);invalidContextRejected=true;}
 const wire=path.join(dir,'api-wire.json');write('api-wire.json',{modes,ordinary,invalidContextRejected,beforeNativeControlInjectsDroppedContext:before});
 const main=fs.readFileSync(root+'/native/main.cpp','utf8'),baseline=fs.readFileSync(root+'/tools/fixtures/settings_baseline_native.cpp','utf8');
 const slice=(source,start,end)=>{const at=source.indexOf(start);assert(at>=0,start);const finish=source.indexOf(end,at+start.length);assert(finish>at,end);return source.slice(at,finish);};
 const owner=['ymSettingsBootWriteMatches','mouseModeBootOwnerCheck'].map(name=>extract(main,name).code).join('\n');
 const types=slice(main,'struct DisplayModeInfo {','// Resolution choices must follow')+slice(main,'static std::string displayModeId(','static std::vector<DisplayModeInfo> enumerateCurrentDisplayModes(');
 const ports=slice(baseline,'static DEVMODEW fixtureDisplayCurrent{};','// SETTINGS_BASELINE_DISPLAY_REMEMBER');
 const registration=slice(main,'static void reg_display() {','static void reg_gyro() {');
 const source=fs.readFileSync(root+'/tools/boot_modes_fixture/displayOwner.cpp.in','utf8').replace('// ORIGINAL_OWNER',owner).replace('// ORIGINAL_DISPLAY_TYPES',types).replace('// ORIGINAL_DISPLAY_PORTS',ports).replace('// ORIGINAL_DISPLAY_REGISTRATION',registration);
 const cpp=path.join(dir,'probe.cpp'),exe=path.join(dir,'probe.exe');fs.writeFileSync(cpp,source,{flag:'wx'});
 const apiInputs=Object.keys(built.metafile.inputs).filter(file=>!file.startsWith('display-owned:')&&file!=='display-owned-api.ts');
 const inputs=[...new Set(['native/main.cpp','native/settings_baseline.h','native/boot_mode_preferences.h','deps/json/json.hpp','PowerControl/settings-defaults-v1.json','tools/boot_modes_fixture/displayOwner.mjs','tools/boot_modes_fixture/displayOwner.cpp.in','tools/fixtures/settings_baseline_native.cpp','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs',...apiInputs])].map(file=>pin(path.resolve(root,file)));
 const command=path.join(dir,'compile.cmd'),preamble=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');
 fs.writeFileSync(command,preamble+'set "CL="\r\nset "_CL_="\r\nset "LINK="\r\nset "_LINK_="\r\ncl.exe /nologo /std:c++20 /EHsc /utf-8 /W4 /permissive- /O2 /DNDEBUG /MD /I"'+root+'/native" /I"'+root+'/deps/json" "'+cpp+'" /Fo"'+dir+'/probe.obj" /Fe"'+exe+'" /sourceDependencies "'+dir+'/dependencies.json" /link user32.lib\r\nexit /b %errorlevel%\r\n',{flag:'wx'});
 const execute=(label,program,argv,timeout)=>{const started=performance.now(),p=spawnSync(program,argv,{cwd:dir,env:process.env,windowsHide:true,encoding:'utf8',timeout,maxBuffer:16*1024*1024});for(const stream of ['stdout','stderr'])fs.writeFileSync(path.join(dir,label+'.'+stream+'.raw'),p[stream]??'',{flag:'wx'});write(label+'.process.json',{program,args:argv,cwd:dir,exitCode:p.status,signal:p.signal,error:p.error?.message??null,elapsedMs:performance.now()-started});assert(!p.error&&!p.signal,'Display producer did not complete');return p;};
 write('TASK.json',{originalCase:'actual display platform rejects invented brightness and retains saved but inactive power-plan receipts',selection,expectedRows:before?1:160,expectedControls:before?0:6,compileLimitMs:45000,executionLimitMs:12000,inputs,generated:pin(cpp),apiWire:pin(wire),actualConsumer:'Original reg_display/owner and existing settings_baseline_native display ports. Monitor APIs and settings storage are inert; no real display calls or installed settings.',beforeControl:before?'Actual old API drop recorded; native negative explicitly restores same context to isolate missing native guard':null});
 assert.equal(execute('compile','cmd.exe',['/d','/c',command],45000).status,0,'Original display consumer compilation failed');
 const raw=path.join(dir,'observations.jsonl');fs.closeSync(fs.openSync(raw,'wx'));const execution=execute('execute',exe,[root+'/PowerControl/settings-defaults-v1.json',wire,selection,raw],12000);
 assert([0,1].includes(execution.status));const actual=JSON.parse(execution.stdout);
 if(before){assert.equal(execution.status,1);assert.equal(actual.rows.length,1);assert.equal(actual.error,'expired native display request was accepted');assert(modes.every(row=>!('bootContext'in row.request)));}
 else{assert.equal(execution.status,0,actual.error);assert.equal(actual.status,'PASS');assert.equal(actual.rows.length,160);assert.equal(actual.controls.length,6);}
 for(const row of inputs)assert.equal(hash(row.path),row.sha256,'Display input changed during producer '+row.path);
 const result={status:before?'EXPECTED_ORIGINAL_SEMANTIC_FAILURE':'PASS',selection,actualRows:actual.rows.length,actualControls:actual.controls.length,inputs,generated:pin(cpp),apiWire:pin(wire),actual:pin(path.join(dir,'execute.stdout.raw')),observations:pin(raw),executable:pin(exe),realMonitorWrites:0,fullNativeMain:'NOT_RUN'};write('RESULT.json',result);return result;
}
