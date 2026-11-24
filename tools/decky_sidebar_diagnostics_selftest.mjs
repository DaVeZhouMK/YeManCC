import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
const root=path.resolve(import.meta.dirname,'..'),require=createRequire(path.join(root,'package.json')),{transformSync}=require('esbuild');
const module={exports:{}};vm.runInNewContext(transformSync(fs.readFileSync(path.join(root,'src/bridge/deckySidebar.ts'),'utf8'),{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,Error,require:name=>{assert.equal(name,'./ipc');return {invoke(){throw Error('no actual native call');},on(){throw Error('no actual native subscription');}};}});
if(process.argv.includes('--repair-reply-only')){
 const out=process.env.YMCC_DECKY_ARTIFACT_ROOT;if(!out)throw Error('An isolated YMCC_DECKY_ARTIFACT_ROOT is required');
 const {parse,compileScript}=require('vue/compiler-sfc'),{ref,computed}=require('vue');
 const filename=path.join(root,'src/views/SteamView.vue'),view=fs.readFileSync(filename,'utf8');
 const {descriptor,errors}=parse(view,{filename});assert.deepEqual(errors,[]);compileScript(descriptor,{id:'decky-repair-reply',inlineTemplate:true});
 const source=descriptor.scriptSetup.content,start=source.indexOf('const deckyState = ref<DeckySidebarState | null>(null);'),end=source.indexOf('async function setDeckyEnabled(');
 const detail=descriptor.template.content.match(/class="decky-connection-detail">\{\{([^}]+)\}\}/)?.[1];
 assert.ok(start>=0&&end>start&&detail,'production Decky state/repair/view binding must exist');
 const actual=transformSync(
  '(ports=>{const {ref,computed,steamUiState,deckySidebar,deckySidebarCanRepair,deckyConnectionSummary,deckySidebarDescription,showNotice}=ports;'+source.slice(start,end)+
  ';return {acceptDeckyState,repairDeckyConnection,detail:()=>('+detail.replace(/\b(decky[A-Z][A-Za-z]+)\b(?!\.value)/g,'$1.value')+'),busy:()=>deckyBusy.value};})',
  {loader:'ts'}).code;
 // Vue template refs unwrap identifiers, not properties. Preserve the direct
 // expression used by the mounted template, only add runtime ref unwrapping.
 const mount=vm.runInNewContext(actual,{});
 const baselineState={enabled:true,steamKnown:true,steamRunning:true,revision:21,connectionRevision:1,pid:0,phase:'waiting-manual-steam',reason:'decky-context-already-loaded'};
 const identity={known:true,running:true,mode:'bigpicture',pid:124,birth:'456'};
 const cases=[];
 for(const edge of ['stable-success','stable-rejection','new-connected','same-native-wire-connected','launch-blocked','new-repairable','new-power-state','steam-identity-changed','steam-mode-changed','old-state-rejected']){
  let release,reject;const calls=[],notices=[],steamUiState=ref({...identity});
  const api=mount({ref,computed,...module.exports,steamUiState,showNotice:msg=>notices.push(msg),deckySidebar:{repairConnection(...args){calls.push(args);return new Promise((yes,no)=>{release=yes;reject=no;});}}});
  try{
   api.acceptDeckyState(baselineState);assert.equal(calls.length,0);
   const request=api.repairDeckyConnection();assert.equal(api.busy(),true);assert.deepEqual(calls,[[124,'456',21]]);assert.match(api.detail(),/正在修复连接/);
   let phase=baselineState;
   if(edge==='new-connected'||edge==='same-native-wire-connected')phase={...phase,revision:edge==='new-connected'?22:21,connectionRevision:2,pid:98,phase:'loader-started',mirrorConnected:true};
   else if(edge==='new-power-state')phase={...phase,powerReady:false};
   else if(edge==='launch-blocked')phase={...phase,revision:22,phase:'failed',reason:'launch-failed',launchStage:'shell-token-elevated',error:5};
   else if(edge==='new-repairable')phase={...phase,revision:22,pid:98,phase:'loader-started',reason:'mirror-handshake-pending'};
   if(phase!==baselineState)api.acceptDeckyState(phase);
   if(edge==='old-state-rejected')api.acceptDeckyState({...baselineState,revision:20,enabled:false,phase:'disabled'});
   if(edge==='steam-identity-changed')steamUiState.value={...identity,birth:'457'};
   if(edge==='steam-mode-changed')steamUiState.value={...identity,mode:'desktop'};
   const pending=api.detail();
   if(edge==='stable-rejection'||edge==='same-native-wire-connected')reject(Error('simulated timeout'));
   else release({ok:edge==='stable-success'||edge==='old-state-rejected'||edge==='launch-blocked',mutated:false});
   await request;
   assert.equal(api.busy(),false);assert.equal(calls.length,1);
   const after=api.detail();
   if(edge==='stable-rejection')assert.match(after,/正在确认恢复结果/);
   else if(edge==='stable-success'||edge==='old-state-rejected')assert.match(after,/已请求恢复，等待连接/);
   else{
    const authoritative=module.exports.deckyConnectionSummary(phase).detail;
    assert.equal(after,authoritative,'late repair reply must not hide the current status');
    assert.equal(pending,authoritative,'old pending text must not hide the current status');
   }
   cases.push({edge,ok:true,pending,after,calls,notices});
  }catch(error){cases.push({edge,ok:false,error:String(error),after:api.detail(),calls,notices});}
 }
 const ok=cases.every(c=>c.ok);fs.mkdirSync(path.join(out,'validation'),{recursive:true});
 fs.writeFileSync(path.join(out,'validation/REPAIR-REPLY-OWNER.json'),JSON.stringify({ok,cases,scope:'production SFC state and actual detail binding with real Vue refs; deferred native boundary is memory only; no screen/Steam acceptance',nativeCalls:0},null,2));
 console.log(JSON.stringify({ok,cases:cases.length,failures:cases.filter(c=>!c.ok).map(c=>c.edge)}));if(!ok)process.exitCode=1;
}else{
const {deckySidebarSetupFailure:format}=module.exports,cases=[],skipped=[];const launchOnly=process.argv.includes('--launch-only');const check=(name,fn)=>{if(launchOnly&&!name.startsWith('launch-')){skipped.push(name);return;}fn();cases.push(name);};
check('legacy bare resource code produces readable Chinese failure',()=>{assert.match(format(new Error('resource-missing')),/加载器资源尚未部署/);});
check('actual override and missing path are visible rather than guessing normal install',()=>{const s=format(new Error('resource-missing'),{ready:false,reason:'resource-missing',missingPath:'G:/Temp/decky/Loader.exe',directorySource:'environment-override',error:3,logPath:'C:/Local/YeManCC/decky-sidebar.log'});assert.match(s,/缺失路径：G:.*Loader/);assert.match(s,/YEMAN_POWER_CONTROL_DIR/);assert.match(s,/Windows 错误码：3/);assert.match(s,/诊断日志/);});
check('launch-failed includes real Windows error and exact stage, no admin fallback suggested',()=>{const message=module.exports.deckySidebarDescription({enabled:true,phase:'failed',reason:'launch-failed',error:1346,launchStage:'shell-token-duplicate'});assert.match(message,/降权启动失败/);assert.match(message,/1346/);assert.match(message,/shell-token-duplicate/);});
check('launch-linked-token failure names the actual session stage and preserves 1312',()=>{const text=module.exports.deckySidebarDescription({enabled:true,phase:'failed',reason:'launch-failed',error:1312,launchStage:'linked-token-query'});assert.match(text,/关联令牌/);assert.match(text,/1312/);assert.match(text,/linked-token-query/);assert.doesNotMatch(text,/未以管理员权限运行/);});
check('launch-elevated desktop explains the missing ordinary token without demanding more admin rights',()=>{const text=module.exports.deckySidebarDescription({enabled:true,phase:'failed',reason:'launch-failed',error:5,launchStage:'shell-token-elevated'});assert.match(text,/Windows 桌面/);assert.match(text,/没有可用的普通权限/);assert.match(text,/shell-token-elevated/);assert.doesNotMatch(text,/未以管理员权限运行/);});
check('launch-unknown token metadata is not fabricated as a Steam installation failure',()=>{const text=module.exports.deckySidebarDescription({enabled:true,phase:'failed',reason:'launch-failed',error:5,launchStage:'caller-token-elevation-type'});assert.match(text,/令牌类型/);assert.match(text,/caller-token-elevation-type/);assert.doesNotMatch(text,/重装|更新 Steam/);});
check('launch-foreign session is accurately refused',()=>{const text=module.exports.deckySidebarDescription({enabled:true,phase:'failed',reason:'launch-failed',error:5,launchStage:'shell-token-identity'});assert.match(text,/用户或登录会话不同/);assert.match(text,/停止加载器启动/);assert.match(text,/shell-token-identity/);});
check('Steam-present listener-not-ready is a bounded startup wait, not a demand to restart again',()=>{const text=module.exports.deckySidebarDescription({enabled:true,phase:'unavailable',reason:'live-debug-unavailable',bindingRetry:true,bindingAttempts:1});assert.match(text,/有限连接重试/);assert.ok(!text.includes('请正常重启'));});
check('hash mismatch and a read-only followup failure preserve reason rather than report success',()=>{assert.match(format(new Error('resource-hash-mismatch'),null),/校验失败/);assert.match(format(new Error('resource-missing'),{ready:true,reason:'ready'}),/加载器资源尚未部署/);});
check('unknown failure is visible and does not initiate hidden setup/retries',()=>{assert.match(format(new Error('loader-start-failed')),/loader-start-failed/);});
check('pending mode confirmation is distinct from manual admission and never asks for another restart',()=>{const text=module.exports.deckySidebarDescription({enabled:true,phase:'waiting-manual-steam',reason:'steam-ui-state-pending',admissionRetry:true});assert.match(text,/状态尚未就绪/);assert.match(text,/自动复查/);assert.ok(!text.includes('重启'));assert.ok(!text.includes('重开 YMCC'));});
check('exhausted confirmation budget describes an event/manual wait rather than promising ongoing retries',()=>{const text=module.exports.deckySidebarDescription({enabled:true,phase:'waiting-manual-steam',reason:'steam-ui-state-pending',admissionRetry:false});assert.match(text,/仍未确认/);assert.match(text,/界面变化/);assert.ok(!text.includes('正在有限'));});
check('confirmed desktop is visible both during settling and after finite retries end',()=>{for(const admissionRetry of [true,false]){const text=module.exports.deckySidebarDescription({enabled:true,phase:'waiting-manual-steam',reason:'steam-bigpicture-required',admissionRetry});assert.match(text,/桌面模式/);assert.match(text,/大屏/);assert.ok(!text.includes('重开 YMCC'));}});
check('observation-only startup keeps the explicit manual link description',()=>{const text=module.exports.deckySidebarDescription({enabled:true,phase:'waiting-manual-steam',reason:'ymcc-restart-observation-only',admissionRetry:false});assert.match(text,/重开 YMCC 不重新注入/);assert.match(text,/模式按钮/);});
check('crash loop exposes stopped recovery and requires an explicit retry',()=>{const text=module.exports.deckySidebarDescription({enabled:true,phase:'failed',reason:'loader-crash-loop'});assert.match(text,/已停止自动重启/);assert.match(text,/日志/);assert.ok(!text.includes('正在自动恢复'));});
const cpp=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8'),setup=fs.readFileSync(path.join(root,'native/decky_sidebar_setup.h'),'utf8'),view=fs.readFileSync(path.join(root,'src/views/SteamView.vue'),'utf8');
check('native records resolved paths, override source, low frequency runtime and failed preference saves',()=>{for(const text of ['configuredOverride','directorySource','deckySidebarDiagnostic("runtime"','deckySidebarDiagnostic("settings-save"','deckySidebarDiagnostic("set-enabled"'])assert.ok(cpp.includes(text));assert.ok(setup.includes('missingPath'));assert.ok(setup.includes('resourceDirectory'));});
check('dedup, bounded rotate and export use existing native infrastructure, without polling',()=>{assert.ok(cpp.includes('if(!force&&last==signature)return'));const start=cpp.indexOf('static std::wstring deckySidebarLogPath() {'),end=cpp.indexOf('static void deckySidebarAdmissionWakeDiagnostic(',start);assert.ok(start>=0&&end>start);const writer=cpp.slice(start,end);assert.ok(writer.includes('bytes>=ymcc::deckydiag::maxBytes'));assert.ok(writer.includes('fspath::rename(path,path+L".1",rotationError)'));assert.ok(!writer.includes('fspath::remove('));assert.ok(cpp.split('\n').some(line=>line.includes('addRotated(dataDir')&&line.includes('decky-sidebar.log')));assert.ok(view.includes('deckySidebar.environment().catch(()=>null)'));assert.ok(!view.includes("'YMCC 控制台设置失败：' + (error as Error).message"));});
check('Decky connection status follows its own toggle and uses live summary and detail bindings',()=>{
 const integration=view.indexOf('<template #integration>'),end=view.indexOf('</section>',integration);
 assert.ok(integration>=0&&end>integration);const section=view.slice(integration,end);
 const toggle=section.indexOf('@update:model-value="setDeckyEnabled"'),status=section.indexOf('class="decky-connection"');
 assert.ok(toggle>=0&&status>toggle);const statusBlock=section.slice(status,section.indexOf('data-steam-setting="controller-cursor-hide"',status));
 assert.match(statusBlock,/role="status" aria-live="polite"/);assert.ok(statusBlock.includes('{{deckyConnection.label}}'));assert.ok(statusBlock.includes('{{deckyRepairDetail}}'));
 assert.ok(view.includes('deckyConnectionSummary(deckyState.value)'));assert.ok(view.includes('deckyConnection.value.detail'));
});
check('admission diagnostics carry a scheduled recovery and underlying read reason through an explicit allowlist',()=>{const diag=fs.readFileSync(path.join(root,'native/decky_sidebar_diagnostics.h'),'utf8');for(const name of ['admissionRetry','admissionAttempts','admissionDetail','admissionElapsedMs','admissionNextRetryMs','admissionRecovery']){assert.ok(cpp.includes('state.'+name));assert.ok(diag.includes('"'+name+'"'));}const runtime=fs.readFileSync(path.join(root,'native/decky_sidebar_runtime.h'),'utf8');const wake=runtime.slice(runtime.indexOf('bool requestLaunchAdmissionRefresh()'),runtime.indexOf('bool requestBindingRefresh('));assert.ok(!wake.includes('++admissionEpoch_'));assert.ok(runtime.includes('admissionChecks.timeout(now)'));assert.ok(runtime.includes('ymcc-restart-observation-only'));assert.ok(cpp.includes('ymcc::deckysession::bigPicture(session,state)')); assert.ok(cpp.includes('"steam-bigpicture-required", false'));assert.ok(cpp.includes('"steam-ui-state-pending", true'));});
check('slow recovery is described truthfully without exposing implementation timing in the product',()=>{const text=module.exports.deckySidebarDescription({enabled:true,phase:'waiting-manual-steam',reason:'steam-ui-state-pending',admissionRetry:true,admissionRecovery:true});assert.match(text,/自动恢复/);assert.ok(!text.includes('有限'));assert.ok(!text.includes('重启'));assert.ok(cpp.includes('AdmissionReadRefresh'));assert.ok(cpp.includes('pending.admissionRetry&&pending.admissionRecovery'));});
const out=path.resolve(process.env.YMCC_DECKY_ARTIFACT_ROOT||'G:/YeManCC-Work/Build/Tasks/YMCC-Decky-Resource24');fs.mkdirSync(path.join(out,'validation'),{recursive:true});fs.writeFileSync(path.join(out,'validation/DIAGNOSTICS-SELFTEST.json'),JSON.stringify({scope:launchOnly?'launch':'all',fullSuite:!launchOnly,skippedCases:skipped.length,cases:cases.length,results:cases,actualNativeLogging:false,nativeCalls:0,actualSettingsWrites:0},null,2));console.log('Diagnostic/UI checks passed '+cases.length+' scope='+(launchOnly?'launch':'all')+' skipped='+skipped.length);
}
