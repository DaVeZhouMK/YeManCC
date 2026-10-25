import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
import {spawnSync} from 'node:child_process';
const [repoRoot,evidenceDir]=process.argv.slice(2);
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const require=createRequire(path.join(repoRoot,'package.json'));
const esbuild=require('esbuild');
const loadedInputs=new Map();
function toolchain(){
 const installs=process.env['ProgramFiles(x86)']||'C:/Program Files (x86)';
 const msvcRoot=path.join(installs,'Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC');
 const sdkRoot=path.join(installs,'Windows Kits/10');
 const versions=directory=>fs.readdirSync(directory).sort((a,b)=>b.localeCompare(a,undefined,{numeric:true}));
 const msvc=versions(msvcRoot).map(version=>path.join(msvcRoot,version)).find(root=>fs.existsSync(path.join(root,'bin/Hostx64/x64/cl.exe')));
 const sdk=versions(path.join(sdkRoot,'Include')).find(version=>fs.existsSync(path.join(sdkRoot,'Include',version,'ucrt/crtdbg.h'))&&fs.existsSync(path.join(sdkRoot,'Lib',version,'ucrt/x64/libucrt.lib')));
 if(!msvc||!sdk)throw Object.assign(Error('REVIEWED_NATIVE_TOOLCHAIN_UNAVAILABLE'),{code:'REVIEWED_NATIVE_TOOLCHAIN_UNAVAILABLE'});
 const bin=path.join(msvc,'bin/Hostx64/x64');
 return {compiler:path.join(bin,'cl.exe'),bin,include:[path.join(msvc,'include'),...['ucrt','shared','um'].map(name=>path.join(sdkRoot,'Include',sdk,name))].join(';'),lib:[path.join(msvc,'lib/x64'),path.join(sdkRoot,'Lib',sdk,'ucrt/x64'),path.join(sdkRoot,'Lib',sdk,'um/x64')].join(';'),sdk};
}
const tool='tools/ymcc_sim/global/background-signals';
const boundary=path.join(repoRoot,tool,'io-boundary.ts');
const plugin={name:'background-reviewed-io-and-exact-inputs',setup(build){
 build.onResolve({filter:/^\.\.?\//},args=>{const name=path.basename(args.path).replace(/\.ts$/,'');if(['ipc','uiSettings','api','steamHttp'].includes(name)&&args.importer.startsWith(path.join(repoRoot,'src','bridge')))return {path:boundary};});
 build.onLoad({filter:/\.(?:[cm]?js|tsx?|json)$/},args=>{const bytes=fs.readFileSync(args.path);const relative=path.relative(repoRoot,args.path).replaceAll('\\','/'),digest=sha(bytes);if(loadedInputs.has(relative)&&loadedInputs.get(relative)!==digest)throw Error('COMPILED_INPUT_CHANGED_DURING_BUILD:'+relative);loadedInputs.set(relative,digest);return {contents:bytes,loader:path.extname(args.path)==='.ts'?'ts':path.extname(args.path)==='.json'?'json':'js'};});
}};
const result={schemaVersion:1,status:'FAIL',stage:'compile-actual-product',sourceChanges:[],sourceHashes:[],compiledInputs:[],bundles:[],checks:[],inputConsumerStateFeedback:[],executedProductModules:[],remainingGaps:['Full App.vue media element rendering is not executed; existing UI bindings are source references only.','Native C++ test executes extracted production functions with isolated paths/settings, not production IPC or an installed application.','No desktop wallpaper, user settings, real process identification, network or physical sleep performed.'],safety:{appStarted:false,hostStarted:false,desktopWallpaperChanged:false,userConfigurationRead:false}};
function execute(file,args,options={}){const response=spawnSync(file,args,{cwd:repoRoot,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:2*1024*1024,...options});return {exitCode:response.status,timedOut:response.error?.code==='ETIMEDOUT',errorCode:response.error?.code??null,stdout:response.stdout??'',stderr:response.stderr??''};}
try{
 for(const [id,entry] of [['original-dynamic','tools/dynamic_background_selftest.ts'],['behavior',`${tool}/behavior-fixture.ts`]]){
  const output=path.join(evidenceDir,id+'.mjs');
  const built=await esbuild.build({absWorkingDir:repoRoot,entryPoints:[entry],outfile:output,bundle:true,platform:'node',format:'esm',target:'node22',metafile:true,logLevel:'silent',plugins:[plugin]});
  fs.writeFileSync(path.join(evidenceDir,id+'.metafile.json'),JSON.stringify(built.metafile,null,2));
  result.bundles.push({id,path:path.basename(output),sha256:sha(fs.readFileSync(output)),esbuildVersion:esbuild.version});
 }
 result.compiledInputs=[...loadedInputs].map(([path,sha256])=>({path,sha256})).sort((a,b)=>a.path.localeCompare(b.path));
 result.stage='original-real-source-tests';
 const original=execute(process.execPath,[path.join(evidenceDir,'original-dynamic.mjs')]);
 fs.writeFileSync(path.join(evidenceDir,'original-dynamic.log'),original.stdout+original.stderr);
 result.originalDynamic={status:original.exitCode===0&&!original.errorCode?'PASS':'FAIL',exitCode:original.exitCode,scope:'behavioral-mock',actualConsumer:'unmodified dynamic_background_selftest.ts with reviewed IO boundary injection'};
 const nativeOutput=path.join(evidenceDir,'native');
 const originalNativeSource=fs.readFileSync(path.join(repoRoot,'tools/default_background_selftest.mjs'),'utf8');
 const retiredAssertion="assert(settings.includes('默认 MP4：{{ DEFAULT_BACKGROUND_VIDEO_PATH }}'));";
 if(originalNativeSource.split(retiredAssertion).length!==2)throw Error('DEFAULT_BACKGROUND_TEST_ADAPTATION_CONTRACT_CHANGED');
 let nativeSource=originalNativeSource.replace(retiredAssertion,"assert(settings.includes('void loadBackgroundState();') && settings.includes('await backgroundGet()')); // Current SettingsView consumer binding; UI wording is not the native file contract.");
 const nativeTools=toolchain();
 const compilerStart=nativeSource.indexOf('const compile = '),compilerEnd=nativeSource.indexOf("fs.writeFileSync(path.join(out, 'build.log')");
 if(compilerStart<0||compilerEnd<=compilerStart)throw Error('DEFAULT_BACKGROUND_COMPILER_WRAPPER_CONTRACT_CHANGED');
 const directCompile=`const nativeTools=${JSON.stringify(nativeTools)};\nconst built=spawnSync(nativeTools.compiler,['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/I'+path.join(root,'deps/json'),path.join(out,'fixture.cpp'),'/Fo'+path.join(out,'fixture.obj'),'/Fe'+path.join(out,'fixture.exe')],{cwd:out,encoding:'utf8',windowsHide:true,timeout:30000,env:{...process.env,INCLUDE:nativeTools.include,LIB:nativeTools.lib,PATH:nativeTools.bin+';'+process.env.PATH}});\n`;
 nativeSource=nativeSource.slice(0,compilerStart)+directCompile+nativeSource.slice(compilerEnd);
 nativeSource="import {createHash} from 'node:crypto';\n"+nativeSource;
 const readCapture="const nativeSourceReads=[];const readSource=(file,encoding)=>{const data=fs.readFileSync(file);nativeSourceReads.push({path:path.relative(root,file).replaceAll('\\\\','/'),sha256:createHash('sha256').update(data).digest('hex')});return encoding?data.toString(encoding):data;};process.on('exit',()=>fs.writeFileSync(path.join(out,'source-reads.json'),JSON.stringify(nativeSourceReads,null,2)));\n";
 nativeSource=nativeSource.replace("const main = fs.readFileSync",readCapture+"const main = fs.readFileSync");
 nativeSource=nativeSource.replaceAll("fs.readFileSync(path.join(root,", "readSource(path.join(root,");
 const adaptedPath=path.join(evidenceDir,'default-background-current-ui.mjs');fs.writeFileSync(adaptedPath,nativeSource);
 result.nativeToolchain={compiler:nativeTools.compiler,compilerSha256:sha(fs.readFileSync(nativeTools.compiler)),sdk:nativeTools.sdk};
 result.nativeTestAdaptation={originalPath:'tools/default_background_selftest.mjs',originalSha256:sha(Buffer.from(originalNativeSource)),adaptedSha256:sha(Buffer.from(nativeSource)),scope:'source-contract UI anchor plus compiler environment; C++ extraction/paths/behavior unchanged',reason:'Current SettingsView no longer renders the retired default MP4 wording; exact backgroundGet/load binding remains'};
 const native=execute(process.execPath,[adaptedPath],{env:{...process.env,YMCC_DEFAULT_BACKGROUND_OUT:nativeOutput}});
 fs.writeFileSync(path.join(evidenceDir,'original-native.log'),native.stdout+native.stderr);
 const match=/([0-9]+) production native checks passed/.exec(native.stdout);
 result.nativeSourceInputs=JSON.parse(fs.readFileSync(path.join(nativeOutput,'source-reads.json'),'utf8'));
 result.nativeArtifacts=['fixture.cpp','fixture.exe'].map(file=>({path:'native/'+file,sha256:sha(fs.readFileSync(path.join(nativeOutput,file)))}));
 const mainSource=fs.readFileSync(path.join(repoRoot,'native/main.cpp'),'utf8');
 const begin=mainSource.indexOf('static const std::wstring DEFAULT_BACKGROUND_VIDEO_DIR ='),end=mainSource.indexOf('// Dynamic Steam media is a consumer');
 const clearStart=mainSource.indexOf('    ipc_on("background.clear", [](const json&) -> json {'),clearEnd=mainSource.indexOf('\n    });',clearStart);
 result.executedNativeRegions=[{path:'native/main.cpp',startLine:mainSource.slice(0,begin).split('\n').length,endLine:mainSource.slice(0,end).split('\n').length,sha256:sha(Buffer.from(mainSource.slice(begin,end))),functions:['default_background_state','background_state']},
  {path:'native/main.cpp',startLine:mainSource.slice(0,clearStart).split('\n').length,endLine:mainSource.slice(0,clearEnd).split('\n').length,sha256:sha(Buffer.from(mainSource.slice(clearStart,clearEnd))),functions:['ipc background.clear lambda']}];
 result.originalNative={status:native.exitCode===0&&!native.errorCode&&Number(match?.[1])>=12?'PASS':'FAIL',exitCode:native.exitCode,timedOut:native.timedOut,checks:Number(match?.[1]??0),scope:'behavioral-mock',actualConsumer:'reviewed derived wrapper reuses original default_background_selftest.mjs production C++ extraction and isolated behavior; current UI anchor and explicit SDK compiler environment',sourceContractOnly:['UI path text','optional native virtual-host mapping']};
 result.stage='formal-consumer-behavior';
 const behavior=execute(process.execPath,[path.join(evidenceDir,'behavior.mjs')]);
 fs.writeFileSync(path.join(evidenceDir,'behavior.log'),behavior.stdout+behavior.stderr);
 const raw=behavior.stdout.split(/\r?\n/).find(line=>line.startsWith('BACKGROUND_BEHAVIOR_RECEIPT '));
 if(!raw)throw Object.assign(Error('BACKGROUND_BEHAVIOR_RECEIPT_MISSING'),{code:'BACKGROUND_BEHAVIOR_RECEIPT_MISSING'});
 const receipt=JSON.parse(raw.slice('BACKGROUND_BEHAVIOR_RECEIPT '.length));
 result.behavior={exitCode:behavior.exitCode,timedOut:behavior.timedOut,receipt};
 result.checks=receipt.checks??[];result.inputConsumerStateFeedback=result.checks.map(x=>({id:x.id,...x.inputConsumerStateFeedback}));
 result.executedProductModules=receipt.executedProductModules??[];
 result.stage='verify-exact-compiled-inputs';
 result.changedCompiledInputs=result.compiledInputs.filter(item=>sha(fs.readFileSync(path.join(repoRoot,item.path)))!==item.sha256);
 result.status=result.originalDynamic.status==='PASS'&&result.originalNative.status==='PASS'&&behavior.exitCode===0&&receipt.status==='PASS'&&result.checks.length===20&&result.checks.every(x=>x.status==='PASS')&&result.changedCompiledInputs.length===0?'PASS':'FAIL';
 result.compatibility={schemaVersion:1,status:result.status==='PASS'?'PASS':'FAIL',hcRuntimeRequired:false,
  requiredExports:['backgroundGet','backgroundInstall','backgroundClear','dynamicBackgroundGet','dynamicBackgroundInstallUrl','dynamicBackgroundInstallOnline','dynamicBackgroundClear','getBackgroundOpacity','setBackgroundOpacity','getBackgroundBlur','setBackgroundBlur','notifyBackgroundChanged','nextLowBatteryStatic','shouldSkipVideoReconcile','refreshDynamicBackground','getDynamicBackgroundState'],
  nativeConsumers:['default_background_state','background_state','ipc background.clear lambda'],
  boundaryContract:'real formal TS consumers plus actual SharedGameMedia/revision fence; only IO injected; extracted native paths/settings isolated',
  missingInterfaceBehavior:'esbuild/native compilation or required feedback validation fails; no silent fallback'};
 result.stage='complete';
}catch(error){result.failure={code:error.code??error.message,exceptionType:error.name};}
fs.writeFileSync(path.join(evidenceDir,'worker-result.json'),JSON.stringify(result,null,2));
console.log(JSON.stringify({status:result.status,stage:result.stage,checks:result.checks.length,failure:result.failure??null}));
process.exitCode=result.status==='PASS'?0:1;



