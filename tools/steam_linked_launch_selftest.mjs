import assert from 'node:assert/strict';import fs from 'node:fs';import vm from 'node:vm';import {transformSync} from 'esbuild';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';

function parseOptions(argv) {
  const options={processStatusOnly:false,rtssProcessIoOnly:false,processSource:null,apiSource:null,ownedRoot:null,out:null};
  const seen=new Set();
  for(let i=0;i<argv.length;i++) {
    const arg=argv[i];
    if(arg==='--process-status-only') {
      if(seen.has(arg))throw Error('Duplicate '+arg);
      seen.add(arg);options.processStatusOnly=true;continue;
    }
    if(arg==='--rtss-process-io-only') {
      if(seen.has(arg))throw Error('Duplicate '+arg);
      seen.add(arg);options.rtssProcessIoOnly=true;continue;
    }
    const key={'--process-source':'processSource','--api-source':'apiSource','--owned-root':'ownedRoot','--out':'out'}[arg];
    if(key) {
      if(seen.has(arg)||i+1>=argv.length||argv[i+1].startsWith('--'))throw Error('Missing or duplicate '+arg);
      seen.add(arg);options[key]=argv[++i];continue;
    }
    // Preserve the original positional output argument.
    if(!arg.startsWith('-')&&options.out===null){options.out=arg;continue;}
    throw Error('Unexpected argument '+arg);
  }
  if(options.processStatusOnly&&options.rtssProcessIoOnly)throw Error('Select one process test mode');
  if(!(options.processStatusOnly||options.rtssProcessIoOnly)&&(options.processSource!==null||options.apiSource!==null||options.ownedRoot!==null))
    throw Error('Process source overrides require an explicit selected mode');
  if((options.processSource===null)!==(options.ownedRoot===null))
    throw Error('--process-source and --owned-root must be supplied together');
  return options;
}
if(process.argv.includes('--rtss-process-native-only')) {
  const {rtssProcessNativeCli}=await import('./fixtures/rtss_process_native.mjs');
  await rtssProcessNativeCli(process.argv.slice(2));
  process.exit(process.exitCode??0);
}
if(process.argv.includes('--process-status-native-only')) {
  const {processStatusNativeCli}=await import('./fixtures/steam_process_status_native.mjs');
  await processStatusNativeCli(process.argv.slice(2));
  process.exit(process.exitCode??0);
}
const options=parseOptions(process.argv.slice(2));
const rawSha256=bytes=>createHash('sha256').update(bytes).digest('hex');
const repoRoot=fs.realpathSync(process.cwd());
const repoBridgeFile=path.resolve(repoRoot,'src/bridge/yeman.ts');
const selectedInputs=new Map();
const selectedCode=new Map();
let processBridgeFile=repoBridgeFile,ownedRootReal=null;
const contained=(root,file)=>{const relative=path.relative(root,file);return relative!==''&&relative!=='..'&&!relative.startsWith('..'+path.sep)&&!path.isAbsolute(relative);};
if(options.processSource!==null) {
  if(!path.isAbsolute(options.processSource)||!path.isAbsolute(options.ownedRoot))
    throw Error('--process-source and --owned-root must be absolute paths');
  ownedRootReal=fs.realpathSync(options.ownedRoot);
  if(!fs.statSync(ownedRootReal).isDirectory())throw Error('--owned-root must be a directory');
  if(ownedRootReal===repoRoot||ownedRootReal===path.parse(ownedRootReal).root||contained(ownedRootReal,repoRoot))
    throw Error('--owned-root must identify a private tree, not the repository or its parent');
  processBridgeFile=fs.realpathSync(options.processSource);
  if(!contained(ownedRootReal,processBridgeFile)||!fs.statSync(processBridgeFile).isFile())
    throw Error('--process-source must resolve to a file inside --owned-root');
  if(processBridgeFile===fs.realpathSync(repoBridgeFile))
    throw Error('Use the default source to test the current repository bridge');
}
let processApiFile=path.resolve(repoRoot,'src/bridge/api.ts');
if(options.apiSource!==null) {
  if(!options.rtssProcessIoOnly||!ownedRootReal||!path.isAbsolute(options.apiSource))throw Error('--api-source requires private RTSS IO selection');
  processApiFile=fs.realpathSync(options.apiSource);
  if(!contained(ownedRootReal,processApiFile)||!fs.statSync(processApiFile).isFile())throw Error('--api-source must stay inside --owned-root');
}
function selectedInput(file) {
  const actualFile=fs.realpathSync(file);
  if(!selectedInputs.has(actualFile)) {
    const raw=fs.readFileSync(actualFile);
    selectedInputs.set(actualFile,{file:actualFile,rawSha256:rawSha256(raw),bytes:raw.length,text:raw.toString('utf8')});
  }
  return selectedInputs.get(actualFile);
}
const privateFamilyExport='\nmodule.exports.__processStatusRtssFamilyAlive = rtssFamilyAlive;\n';
const privateRtssIoExport='\nmodule.exports.__rtssProcessIoResolveRtssDir = resolveRtssDir;\nmodule.exports.__rtssProcessIoRestartRtss = restartRtss;\n';
const cases=[];async function check(name,fn){await fn();cases.push(name)}
function evaluate(file,imports={},rtssIoState=null) {
  const isProcessBridge=(options.processStatusOnly||options.rtssProcessIoOnly)&&path.resolve(file)===repoBridgeFile;
  let code;
  if(options.processStatusOnly||options.rtssProcessIoOnly) {
    const input=selectedInput(isProcessBridge?processBridgeFile:options.rtssProcessIoOnly&&path.resolve(file)===path.resolve(repoRoot,'src/bridge/api.ts')?processApiFile:file);
    const access=options.rtssProcessIoOnly?privateRtssIoExport:privateFamilyExport;
    const key=input.file+(isProcessBridge?(options.rtssProcessIoOnly?'#private-rtss-io':'#private-family'):'');
    if(!selectedCode.has(key))selectedCode.set(key,transformSync(input.text+(isProcessBridge?access:''),{loader:'ts',format:'cjs'}).code);
    code=selectedCode.get(key);
  } else code=transformSync(fs.readFileSync(file,'utf8'),{loader:'ts',format:'cjs'}).code;
  const module={exports:{}};
  vm.runInNewContext(code,{module,exports:module.exports,console,setTimeout:options.rtssProcessIoOnly&&rtssIoState?((fn,ms)=>{(rtssIoState.waits??=[]).push(ms);fn();return 0;}):setTimeout,clearTimeout,performance:{now:()=>Date.now()},window:{addEventListener(){},removeEventListener(){}},require(name){if(Object.hasOwn(imports,name))return imports[name];throw Error('Unexpected import '+name)}});
  return module.exports;
}
function rtssProcessIoPort(state,cmd,args) {
  const take=(queue,fallback)=>queue?.length?queue.shift():fallback;
  if(cmd==='proc.identities') {
    const reply=take(state.identityReplies,{known:true,processes:[]});
    if(reply instanceof Error)throw reply;return reply;
  }
  if(cmd==='rtss.setAffinityExact') {
    const reply=take(state.affinityReplies,{ok:true,matched:true,applied:true,error:0,reason:''});
    (state.affinityResults??=[]).push(reply instanceof Error?{thrown:reply.message}:reply);
    if(reply instanceof Error)throw reply;return reply;
  }
  if(cmd==='fs.exists')return take(state.existsReplies,false);
  if(cmd==='proc.running') {
    const family=args.names.includes('RTSSHooksLoader*');
    return take(family?state.familyReplies:state.runningReplies,Object.fromEntries(args.names.map(name=>[name,!family])));
  }
  if(cmd==='shell.hidden')return {ok:true,pid:701};
  if(cmd==='shell.run') {
    if(args.program==='cmd')return {exitCode:0,stdout:'',stderr:''};
    const script=args.args?.at(-1);
    if(/^powershell(?:\.exe)?$/i.test(args.program)&&typeof script==='string'&&script.includes('$c=@(')) {
      (state.directoryScripts??=[]).push(script);
      const reply=take(state.directoryReplies,'');if(reply instanceof Error)throw reply;
      return {exitCode:0,stdout:reply,stderr:''};
    }
  }
  throw Error('Unexpected RTSS process I/O IPC '+cmd);
}
function fixture(){const state={subscriptions:new Map(),calls:[],running:false,result:{ok:true},startup:{known:true,enabled:false,conflict:false},service:{ok:true,known:true,installed:true,automatic:true,running:true},error:null};const invoke=async(cmd,args)=>{state.calls.push({cmd,args:JSON.parse(JSON.stringify(args))});if(options.rtssProcessIoOnly)return rtssProcessIoPort(state,cmd,args);if(cmd==='power.lifecycle')return {generation:1,phase:'ready',hardwareWritesAllowed:true};if(cmd==='proc.running'){if(options.processStatusOnly){if(Object.hasOwn(state,'processError'))throw state.processError;if(Object.hasOwn(state,'processReply'))return state.processReply;}return{steam:state.running};}if(cmd==='fs.readDir')return [];if(cmd==='steam.ui.launch'){if(state.error)throw Error(state.error);return state.running?{...state.result,alreadyRunning:true,dispatched:false}:state.result;}if(cmd==='steam.startup.get')return state.startup;if(cmd==='steam.startup.set'){state.startup.enabled=args.enabled;return{ok:true,enabled:args.enabled,...(state.service?{service:state.service}:{})}}throw Error('Unexpected IPC '+cmd)};const on=(event,listener)=>{assert.equal(typeof listener,'function');let set=state.subscriptions.get(event);if(!set)state.subscriptions.set(event,set=new Set());set.add(listener);return()=>{set.delete(listener);if(!set.size)state.subscriptions.delete(event);};};const api=evaluate('src/bridge/api.ts',{'./ipc':{invoke,on}});const bridge=evaluate('src/bridge/yeman.ts',{'./api':api,'./ipc':{invoke,on},'./frameRateModel':evaluate('src/bridge/frameRateModel.ts'),'./tdpRuntimeState':evaluate('src/bridge/tdpRuntimeState.ts'),'./gamingHomePolicy':evaluate('src/bridge/gamingHomePolicy.ts'),'./tdpStaticDiagnostics':{},'./rtssOverlay':{},'./settingsRepository':{setSettingsDirectory(){}},'./tdpCircuitBreaker':{},'./controllerShortcutRules':{OEM_EMPTY_SNAPSHOT:{}}},options.rtssProcessIoOnly?state:null);return {state,bridge};}
async function runProcessStatusCases() {
  // This is the real full bridge; only its IPC ports are deterministic fakes.
  const source=selectedInput(processBridgeFile);
  const rows=[];
  const errorText=error=>error instanceof Error?error.message:String(error?.message??error);
  const publicTargets=[
    {method:'rtssRunning',names:['RTSS']},
    {method:'hwiNfoRunning',names:['HWiNFO64']},
    {method:'steamRunning',names:['steam']},
  ];
  const publicReplies=[
    {label:'true',reply:name=>({[name]:true}),expected:true},
    {label:'false',reply:name=>({[name]:false}),expected:false},
    {label:'native-reject',nativeError:'native process status unavailable',unknown:true},
    {label:'null',reply:()=>null,unknown:true},
    {label:'empty-object',reply:()=>({}),unknown:true},
    {label:'wrong-type',reply:name=>({[name]:'false'}),unknown:true},
    {label:'array',reply:()=>[],unknown:true},
    {label:'undefined',reply:()=>undefined,unknown:true},
    {label:'primitive',reply:()=>false,unknown:true},
  ];
  async function runCase(target,sample) {
    const row={name:target.method+' / '+sample.label,method:target.method,requestedNames:target.names,expected:sample.unknown?'REJECT_UNKNOWN':sample.expected,status:'FAIL',failures:[]};
    let state;
    const verify=(label,fn)=>{try{fn();}catch(error){row.failures.push({assertion:label,error:errorText(error)});}};
    try {
      const fixtureState=fixture();state=fixtureState.state;
      if(Object.hasOwn(sample,'nativeError'))state.processError=new Error(sample.nativeError);
      else state.processReply=sample.reply(target.names[0]);
      let value,rejected=false,failure;
      try {value=await fixtureState.bridge[target.method]();}
      catch(error){rejected=true;failure=error;}
      row.actual=rejected?{settled:'rejected',error:errorText(failure)}:{settled:'fulfilled',valueType:typeof value,value};
      verify('result preserves known boolean or rejects unknown',()=>{
        if(sample.unknown) {
          assert.equal(rejected,true,'unknown must reject, never resolve false or undefined');
          assert.match(errorText(failure),/状态待确认|状态验证失败/,'must reject with process-status unknown, not an accidental shell error');
        } else {
          assert.equal(rejected,false,'known native boolean must not reject');
          assert.equal(typeof value,'boolean');assert.equal(value,sample.expected);
        }
      });
    } catch(error) {row.failures.push({assertion:'actual bridge fixture and invocation',error:errorText(error)});}
    row.calls=state?.calls??[];
    // Always inspect all calls even when the result assertion or VM setup failed.
    verify('one proc.running request; no shell/fs/launch or other IPC',()=>assert.deepEqual(row.calls,[{cmd:'proc.running',args:{names:target.names}}]));
    row.status=row.failures.length?'FAIL':'PASS';rows.push(row);
  }
  for(const target of publicTargets)for(const sample of publicReplies)await runCase(target,sample);
  const family={method:'__processStatusRtssFamilyAlive',names:['RTSS','RTSSHooksLoader*','EncoderServer*']};
  const familyReply=(main,hooks,encoder)=>({'RTSS':main,'RTSSHooksLoader*':hooks,'EncoderServer*':encoder});
  const familyReplies=[
    {label:'first-main-true',reply:()=>familyReply(true,false,false),expected:true},
    {label:'first-wildcard-true',reply:()=>familyReply(false,true,false),expected:true},
    {label:'second-wildcard-true',reply:()=>familyReply(false,false,true),expected:true},
    {label:'known-all-false',reply:()=>familyReply(false,false,false),expected:false},
    {label:'unrequested-extra-true',reply:()=>({...familyReply(false,false,false),unrelated:true}),expected:false},
    {label:'missing-partial-after-true',reply:()=>({'RTSS':true}),unknown:true},
    {label:'missing-main-with-wildcard-true',reply:()=>({'RTSSHooksLoader*':true,'EncoderServer*':false}),unknown:true},
    {label:'missing-last-with-wildcard-true',reply:()=>({'RTSS':false,'RTSSHooksLoader*':true}),unknown:true},
    {label:'wrong-type-main',reply:()=>familyReply(0,false,false),unknown:true},
    {label:'wrong-type-first-wildcard',reply:()=>familyReply(true,'false',false),unknown:true},
    {label:'wrong-type-last-wildcard',reply:()=>familyReply(true,false,null),unknown:true},
    {label:'native-reject',nativeError:'native family status unavailable',unknown:true},
    {label:'null',reply:()=>null,unknown:true},
    {label:'empty-object',reply:()=>({}),unknown:true},
    {label:'array',reply:()=>[],unknown:true},
    {label:'undefined',reply:()=>undefined,unknown:true},
    {label:'primitive',reply:()=>true,unknown:true},
  ];
  for(const sample of familyReplies)await runCase(family,sample);
  const failed=rows.filter(row=>row.status==='FAIL').length;
  const sourceDrift=[...selectedInputs.values()].filter(input=>rawSha256(fs.readFileSync(input.file))!==input.rawSha256).map(({file,rawSha256})=>({file,expectedRawSha256:rawSha256}));
  const toolFile=fs.realpathSync(fileURLToPath(import.meta.url)),toolRaw=fs.readFileSync(toolFile);
  const result={ok:failed===0&&sourceDrift.length===0,sourceDrift,selection:'process-status-only',passed:rows.length-failed,failed,cases:rows,
    source:{file:source.file,rawSha256:source.rawSha256,bytes:source.bytes,override:options.processSource!==null,ownedRoot:ownedRootReal},
    evaluatedSources:[...selectedInputs.values()].map(({file,rawSha256,bytes})=>({file,rawSha256,bytes})),
    tool:{file:toolFile,rawSha256:rawSha256(toolRaw),bytes:toolRaw.length},
    testOnlyPrivateExport:{symbol:'rtssFamilyAlive',property:'__processStatusRtssFamilyAlive',appendedSource:privateFamilyExport},
    defaultCasesExecuted:0,realSteamOperations:0,realInstallationWrites:0};
  if(options.out)fs.writeFileSync(options.out,JSON.stringify(result,null,2));
  console.log(JSON.stringify(result,null,2));
  if(failed||sourceDrift.length)process.exitCode=1;
}
async function runRtssProcessIoCases() {
  const source=selectedInput(processBridgeFile),rows=[];
  const firstDir='D:\\Running RTSS',secondDir='E:\\Registry RTSS',birth='133413576000000001';
  const identity=(name='RTSS',pid=701,processCreated=birth,path=firstDir+'\\'+name+'.exe')=>({pid,name,path,processCreated});
  const known=processes=>({known:true,processes});
  const errorText=error=>String(error?.message??error);
  const resolve=bridge=>bridge.__rtssProcessIoResolveRtssDir();
  const restart=bridge=>bridge.__rtssProcessIoRestartRtss();
  const calls=(state,cmd)=>state.calls.filter(call=>call.cmd===cmd);
  const noWrites=state=>assert.equal(calls(state,'rtss.setAffinityExact').length,0);
  const capture=async(fn)=>{try{return {value:await fn(),rejected:false};}catch(error){return {rejected:true,kind:error?.constructor?.name,message:errorText(error)};}};
  async function checkIo(name,run) {
    const row={name,status:'FAIL',failures:[]};let state;
    try {const f=fixture();state=f.state;await run(f.bridge,state);}
    catch(error){row.failures.push({assertion:'actual private consumer',error:errorText(error)});}
    row.calls=state?.calls??[];row.directoryScripts=state?.directoryScripts??[];
    row.affinityResults=state?.affinityResults??[];row.waits=state?.waits??[];
    try {
      assert(calls({calls:row.calls},'proc.identities').length>0,'consumer must request native identities');
      for(const call of row.calls) {
        assert(['proc.identities','rtss.setAffinityExact','fs.exists','proc.running','shell.run','shell.hidden'].includes(call.cmd),'unexpected IPC '+call.cmd);
        if(call.cmd==='shell.run')for(const arg of call.args.args??[])assert(!/Get-Process|ProcessorAffinity/i.test(String(arg)),'no PowerShell process discovery or affinity fallback');
      }
    } catch(error){row.failures.push({assertion:'native identities and no process scripts',error:errorText(error)});}
    row.status=row.failures.length?'FAIL':'PASS';rows.push(row);
  }
  const seed=(state,affinity=known([]))=>{state.identityReplies=[known([identity()]),affinity];state.directoryReplies=[firstDir];};
  await checkIo('native running path precedes environment and registry candidates',async(bridge,state)=>{
    seed(state);state.directoryReplies=['noise\n\\\\server\\share\n'+firstDir+'\n'];
    assert.equal(await resolve(bridge),firstDir);
    assert.deepEqual(calls(state,'proc.identities').map(call=>call.args),[{names:['RTSS']}]);
    const script=state.directoryScripts[0];
    assert(script.startsWith("$c=@('D:\\Running RTSS\\RTSS.exe')"));
    assert(script.indexOf("'D:\\Running RTSS\\RTSS.exe'")<script.indexOf('$env:ProgramFiles'));
    assert(script.includes('RTSSHooks64.dll')&&script.includes('Test-Path -LiteralPath'));noWrites(state);
  });
  await checkIo('apostrophe dollar and backtick paths remain single-quoted PS literals',async(bridge,state)=>{
    const hostile="D:\\O'Brien\\$cache\\\u0060tick\\RTSS.exe",directory="D:\\O'Brien\\$cache\\\u0060tick";
    state.identityReplies=[known([identity('RTSS',701,birth,hostile)])];state.directoryReplies=[directory];
    assert.equal(await resolve(bridge),directory);
    assert(state.directoryScripts[0].startsWith("$c=@('D:\\O''Brien\\$cache\\\u0060tick\\RTSS.exe')"));noWrites(state);
  });
  await checkIo('live cache hit avoids second identity and directory queries',async(bridge,state)=>{
    seed(state);state.existsReplies=[true];assert.equal(await resolve(bridge),firstDir);
    const n=state.calls.length;assert.equal(await resolve(bridge),firstDir);
    assert.deepEqual(state.calls.slice(n),[{cmd:'fs.exists',args:{path:firstDir+'\\RTSS.exe'}}]);
  });
  await checkIo('invalidated cache re-queries identities and directory candidates',async(bridge,state)=>{
    state.identityReplies=[known([identity()]),known([identity('RTSS',702,birth,secondDir+'\\RTSS.exe')])];
    state.directoryReplies=[firstDir,secondDir];state.existsReplies=[false];
    assert.equal(await resolve(bridge),firstDir);assert.equal(await resolve(bridge),secondDir);
    assert.equal(calls(state,'proc.identities').length,2);assert.equal(state.directoryScripts.length,2);
  });
  await checkIo('known empty with no file candidates preserves NotInstalled',async(bridge,state)=>{
    state.identityReplies=[known([])];const result=await capture(()=>resolve(bridge));
    assert(result.rejected);assert.equal(result.kind,'RtssNotInstalledError');noWrites(state);
  });
  await checkIo('native unknown can still use a valid environment or registry file',async(bridge,state)=>{
    state.identityReplies=[{known:false,processes:[],reason:'query-failed'}];state.directoryReplies=[secondDir];
    assert.equal(await resolve(bridge),secondDir);assert(state.directoryScripts[0].startsWith('$c=@()'));noWrites(state);
  });
  const unknownReplies=[
    ['native-reject',new Error('identity query failed')],['unknown',{known:false,processes:[]}],
    ['null',null],['empty-object',{}],['array',[]],['bad-processes',{known:true,processes:{}}],
    ['bad-pid',known([identity('RTSS',0)])],['bad-birth',known([identity('RTSS',701,'0')])],
    ['bad-name',known([identity('notepad')])],['bad-path',known([identity('RTSS',701,birth,'relative\\RTSS.exe')])],
  ];
  for(const [label,reply] of unknownReplies)await checkIo('unknown directory is not NotInstalled / '+label,async(bridge,state)=>{
    state.identityReplies=[reply];const result=await capture(()=>resolve(bridge));
    assert(result.rejected);assert.equal(result.kind,'Error');assert.match(result.message,/待确认/);
    assert.equal(state.directoryScripts.length,1);assert(state.directoryScripts[0].startsWith('$c=@()'));noWrites(state);
  });
  await checkIo('restart forwards exact native PID birth string and path only',async(bridge,state)=>{
    const identities=[identity(),identity('EncoderServer64',702,'133413576000000003'),identity('RTSSHooksLoader64',703,'000133413576000000005')];
    seed(state,known(identities));await restart(bridge);
    assert.deepEqual(calls(state,'proc.identities').map(call=>call.args),[{names:['RTSS']},{names:['RTSS','EncoderServer64','RTSSHooksLoader64']}]);
    assert.deepEqual(calls(state,'rtss.setAffinityExact').map(call=>call.args),identities.map(row=>({pid:row.pid,processCreated:row.processCreated,path:row.path})));
    assert.equal(calls(state,'shell.hidden').length,1);assert.equal(state.directoryScripts.length,1);
  });
  const malformedRows=[
    ['pid-zero',identity('RTSS',0)],['pid-negative',identity('RTSS',-1)],['pid-fraction',identity('RTSS',1.5)],
    ['pid-string',{...identity(),pid:'701'}],['pid-overflow',identity('RTSS',0x100000000)],
    ['birth-number',{...identity(),processCreated:133413576000000000}],['birth-zero',identity('RTSS',701,'0000')],
    ['birth-exponent',identity('RTSS',701,'1e17')],['birth-negative',identity('RTSS',701,'-1')],
    ['birth-u64-overflow',identity('RTSS',701,'18446744073709551616')],['duplicate-pid',identity('RTSS',702)],
    ['empty-path',identity('RTSS',701,birth,'')],['relative-path',identity('RTSS',701,birth,'RTSS.exe')],
    ['path-null-byte',identity('RTSS',701,birth,firstDir+'\\RTSS.exe\u0000extra')],
    ['unrequested-name',identity('notepad')],['path-name-mismatch',identity('RTSS',701,birth,firstDir+'\\notepad.exe')],
  ];
  for(const [label,bad] of malformedRows)await checkIo('malformed identity rejects whole affinity receipt without writes / '+label,async(bridge,state)=>{
    seed(state,known([identity('EncoderServer64',702),bad]));await restart(bridge);
    noWrites(state);assert.equal(calls(state,'shell.hidden').length,1);assert.equal(state.directoryScripts.length,1);
  });
  const affinityUnknown=[['known-empty',known([])],['native-reject',new Error('inventory unavailable')],['unknown',{known:false,processes:[]}],['null',null],['empty-object',{}],['array',[]],['known-error',{known:true,processes:[identity()],error:5}]];
  for(const [label,reply] of affinityUnknown)await checkIo('optional affinity unknown never retries launch / '+label,async(bridge,state)=>{
    seed(state,reply);await restart(bridge);noWrites(state);assert.equal(calls(state,'shell.hidden').length,1);
  });
  await checkIo('optional affinity rejection continues remaining identities without relaunch',async(bridge,state)=>{
    seed(state,known([identity(),identity('EncoderServer64',702)]));
    state.affinityReplies=[new Error('denied'),{ok:true,matched:true,applied:true,error:0,reason:''}];
    await restart(bridge);assert.equal(calls(state,'rtss.setAffinityExact').length,2);
    assert.equal(state.affinityResults[0].thrown,'denied');assert.equal(calls(state,'shell.hidden').length,1);
  });
  await checkIo('negative affinity receipt remains negative without fabricated writes',async(bridge,state)=>{
    seed(state,known([identity()]));const denied={ok:false,matched:false,applied:false,error:5,reason:'identity-mismatch'};
    state.affinityReplies=[denied];await restart(bridge);assert.deepEqual(state.affinityResults,[denied]);
    assert.equal(calls(state,'rtss.setAffinityExact').length,1);assert.equal(calls(state,'shell.hidden').length,1);
  });
  await checkIo('existing taskkill hidden launch and bounded poll delays retain order',async(bridge,state)=>{
    seed(state);const family=alive=>({'RTSS':alive,'RTSSHooksLoader*':false,'EncoderServer*':false});
    state.familyReplies=[family(true),family(true),family(false)];state.runningReplies=[{RTSS:false},{RTSS:true}];await restart(bridge);
    const cmd=calls(state,'shell.run').find(call=>call.args.program==='cmd');
    assert.deepEqual(cmd.args.args,['/c','taskkill /F /IM RTSS.exe /IM RTSSHooksLoader64.exe /IM RTSSHooksLoader32.exe /IM EncoderServer64.exe /IM EncoderServer.exe']);
    assert.deepEqual(state.waits,[100,100]);assert.equal(calls(state,'shell.hidden').length,1);
    assert(state.calls.indexOf(cmd)<state.calls.indexOf(calls(state,'shell.hidden')[0]));noWrites(state);
  });
  const sourceDrift=[...selectedInputs.values()].filter(input=>rawSha256(fs.readFileSync(input.file))!==input.rawSha256).map(({file,rawSha256})=>({file,expectedRawSha256:rawSha256}));
  const failed=rows.filter(row=>row.status==='FAIL').length;
  const toolFile=fs.realpathSync(fileURLToPath(import.meta.url)),toolRaw=fs.readFileSync(toolFile);
  const result={ok:failed===0&&sourceDrift.length===0,selection:'rtss-process-io-only',passed:rows.length-failed,failed,cases:rows,sourceDrift,
    source:{file:source.file,rawSha256:source.rawSha256,bytes:source.bytes,override:options.processSource!==null,ownedRoot:ownedRootReal},
    evaluatedSources:[...selectedInputs.values()].map(({file,rawSha256,bytes})=>({file,rawSha256,bytes})),
    tool:{file:toolFile,rawSha256:rawSha256(toolRaw),bytes:toolRaw.length},testOnlyPrivateExport:privateRtssIoExport,
    defaultCasesExecuted:0,processStatusCasesExecuted:0,realProcessOrShellOperations:0,realInstallationWrites:0};
  if(options.out)fs.writeFileSync(options.out,JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
  if(failed||sourceDrift.length)process.exitCode=1;
}
if(options.rtssProcessIoOnly) {
  await runRtssProcessIoCases();
} else if(options.processStatusOnly) {
  await runProcessStatusCases();
} else {
await check('actual bridge initialization is observation-free',()=>assert.equal(fixture().state.calls.length,0));
await check('actual power lifecycle subscriptions do not launch or observe Steam',async()=>{const {state}=fixture();assert.deepEqual([...state.subscriptions.keys()],['power.suspending','power.resuming','power.resumed']);for(const set of state.subscriptions.values())for(const listener of set)listener({generation:1});await Promise.resolve();assert.deepEqual(state.calls.map(call=>call.cmd),['power.lifecycle']);});
await check('actual bridge rejects implicit startup',async()=>{const {state,bridge}=fixture();await assert.rejects(bridge.launchSteam(),/手动启动/);assert.equal(state.calls.length,0)});
await check('actual bridge uses native launch exclusively without VBS dependency',async()=>{const {state,bridge}=fixture();await bridge.launchSteam({userInitiated:true});assert.deepEqual(state.calls.map(x=>x.cmd),['steam.ui.launch']);assert.deepEqual(state.calls[0].args,{userInitiated:true});});
await check('existing Steam cannot get startup flags or mode switch',async()=>{const {state,bridge}=fixture();state.running=true;await bridge.launchSteam({userInitiated:true});assert.deepEqual(state.calls,[{cmd:'steam.ui.launch',args:{userInitiated:true}}])});
await check('old EXE error never falls back to BAT or VBS',async()=>{const {state,bridge}=fixture();state.error='unknown native command';await assert.rejects(bridge.launchSteam({userInitiated:true}),/unknown native/);assert(!state.calls.some(x=>x.cmd.startsWith('shell.')||x.cmd.startsWith('fs.')))});
await check('startup read is native observation only',async()=>{const {state,bridge}=fixture();assert.equal(await bridge.steamMasterOn(),false);assert.deepEqual(state.calls,[{cmd:'steam.startup.get',args:{}}])});
await check('startup enable and disable never start Steam',async()=>{const {state,bridge}=fixture();await bridge.steamMasterSet(true);await bridge.steamMasterSet(false);assert.deepEqual(state.calls,[{cmd:'steam.startup.set',args:{enabled:true}},{cmd:'steam.startup.set',args:{enabled:false}}])});
await check('startup enable requires current native EarlyStart confirmation and never falls back to launch',async()=>{for(const service of [null,{ok:false,known:false,installed:false,automatic:false,running:false,reason:'service-missing'}]){const {state,bridge}=fixture();state.service=service;await assert.rejects(bridge.steamMasterSet(true),/EarlyStart Service/);assert.deepEqual(state.calls,[{cmd:'steam.startup.set',args:{enabled:true}}])}});
await check('unknown startup cannot be displayed as confirmed off',async()=>{const {state,bridge}=fixture();state.startup.known=false;await assert.rejects(bridge.steamMasterOn(),/读取失败/)});
await check('unrelated earlystart entry produces conflict without overwrite',async()=>{const {state,bridge}=fixture();state.startup.conflict=true;await assert.rejects(bridge.steamMasterOn(),/其他自定义命令/);assert.equal(state.calls.length,1)});
await check('standalone VBS and BAT remain available but outside product calls',()=>{const bridge=fs.readFileSync('src/bridge/yeman.ts','utf8'),view=fs.readFileSync('src/views/SteamView.vue','utf8');assert(!bridge.includes('YeManSteam.vbs'));assert(!bridge.includes('YeManSteam.bat'));assert(!view.includes('shell.hidden'));for(const file of ['PowerControl/YeManSteam.vbs','PowerControl/YeManSteam.bat','PowerControl/YeManSteam2.bat'])assert(fs.existsSync(file));const native=fs.readFileSync('native/steam_native_launch_windows.h','utf8');assert(!/wscript|cscript|powershell|cmd.exe|CreateJobObject|AssignProcessToJobObject|Sleep\(|SetTimer/.test(native));const core=fs.readFileSync('native/steam_native_launch.h','utf8');assert(core.includes('origin==Origin::Manual\n        ? io.spawnManualSteam(executable,startupArguments)'));assert(core.indexOf('io.spawnManualSteam(executable,startupArguments)')<core.indexOf('const auto addons=io.addons()'));assert.equal((native.match(/ShellExecuteExW\(&request\)/g)||[]).length,1);const manualShell=native.slice(native.indexOf('bool spawnSteamShell('),native.indexOf('bool spawnManualSteam('));assert(manualShell.includes('!manualSteamFallbackAllowed(executable)'));assert(manualShell.includes('request.lpVerb=L"open"'));assert(manualShell.includes('request.lpFile=executable.c_str();request.lpParameters=arguments.c_str()'));assert(native.includes('CloseHandle(child.hThread);CloseHandle(child.hProcess)'));assert(native.includes('absentExcept(child.dwProcessId)'));});
await check('portable linked configuration follows the same PowerControl root as native',async()=>{const {state,bridge}=fixture();bridge.setPowerControlDir('D:\\Portable YMCC\\PowerControl');await bridge.steamCustomAddons();assert.deepEqual(state.calls,[{cmd:'fs.readDir',args:{path:'D:\\Portable YMCC\\PowerControl\\YeManSteam'}}])});
const result={ok:true,passed:cases.length,cases,realSteamOperations:0,realInstallationWrites:0};if(options.out)fs.writeFileSync(options.out,JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
}
