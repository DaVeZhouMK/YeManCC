import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {createServer} from 'node:http';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';

// Real SettingsView + the existing production-engine adapter. All IPC stays in memory.
export async function hiddenRecoveryCases({height,open,command,press,actionCase,state,page,task}) {
  await page.addInitScript(() => {
    window.__hiddenRecoveryProbe={enabled:new URLSearchParams(location.search).get('recoveryEnabled')!=='false',reads:0,writes:[]};
  });
  const absent=async f=>{
    assert.equal(await f.locator('.steam-recovery-diagnostics,.sleep-recovery-auto,.recovery-link-states,.recovery-refresh').count(),0,'RECOVERY_UI_MOUNTED');
    assert.equal(await f.getByRole('switch',{name:'睡眠唤醒 Steam 自动修复',exact:true}).count(),0,'RECOVERY_SWITCH_MOUNTED');
    assert.equal(await f.getByRole('button',{name:'检查状态',exact:true}).count(),0,'RECOVERY_CHECK_MOUNTED');
    const p=await f.evaluate(()=>({probe:window.__hiddenRecoveryProbe,calls:(window.__previewCalls||[]).filter(c=>/^(steamGameLink\.|steam\.gameLink|sleepRecovery\.)/.test(c.cmd)),writes:window.__sleepWrites||[],focus:window.__gamepadAudit.focusables().filter(el=>el.closest('.navrail,[hidden],[inert],[aria-hidden="true"],.steam-recovery-diagnostics,.sleep-recovery-auto')||el.matches(':disabled')).map(el=>el.textContent)}));
    assert.equal(p.probe.reads,0,'HIDDEN_AUTO_RECOVERY_READ');assert.deepEqual(p.probe.writes,[]);assert.deepEqual(p.calls,[],'HIDDEN_RECOVERY_IPC');assert.deepEqual(p.writes,[]);assert.deepEqual(p.focus,[]);
    return p;
  };
  for(const enabled of [true,false]) {
    let f=await open('/settings',height,{nativeWidth:'1',keepAlive:'1'});
    if(!enabled){const u=new URL(f.url());u.searchParams.set('recoveryEnabled','false');await f.goto(u.href);await f.waitForFunction(()=>window.__previewReady&&window.__gamepadAudit);}
    await absent(f);
    await press(f,f.locator('[data-settings-section="diagnostics"]'));
    await f.getByRole('button',{name:'导出日志',exact:true}).waitFor();
    await absent(f); // Hard failure: the original mounted card must fail here.
    const prefix='settings/hidden-recovery/'+(enabled?'saved-on':'saved-off');
    await state(f,prefix,height);
    await actionCase('Hidden recovery stays unread after native event replay / '+enabled,height,async()=>{
      await f.evaluate(()=>{
        window.__emitNativeEvent('sleep.recovery-auto',{enabled:window.__hiddenRecoveryProbe.enabled,phase:'waiting',generation:12});
        for(const type of ['ipc:power.resume-ready','ipc:power.resumed'])window.dispatchEvent(new Event(type));
        document.dispatchEvent(new Event('visibilitychange'));
      });
      await command(f,'nav-down');await absent(f);
      assert.equal(await f.evaluate(()=>window.__hiddenRecoveryProbe.enabled),enabled,'saved choice changed');
    });
    await actionCase('Diagnostic logs A/export, modal B, section B and LB/RB / '+enabled,height,async()=>{
      for(const name of ['高频日志 · 关','删除日志','导出日志']){
        const button=f.getByRole('button',{name,exact:true});
        assert.equal(await button.count(),1,'remaining log button missing: '+name);
        assert.equal(await button.evaluate(el=>window.__gamepadAudit.focusables().includes(el)),true);
      }
      await press(f,f.getByRole('button',{name:'导出日志',exact:true}));
      await f.waitForFunction(()=>window.__previewCalls.some(c=>c.cmd==='logs.exportAll'));
      const del=f.getByRole('button',{name:'删除日志',exact:true});await press(f,del);
      await f.locator('.diagnostic-log-delete-dialog').waitFor();
      await state(f,prefix+'/log-confirm',height,'.diagnostic-log-delete-dialog');
      await command(f,'back');await f.locator('.diagnostic-log-delete-dialog').waitFor({state:'detached'});
      assert.equal(await del.evaluate(el=>el===document.activeElement),true,'modal B restores log trigger');
      assert.equal(await f.evaluate(()=>window.__previewCalls.filter(c=>c.cmd==='logs.deleteCurrent').length),0);
      await command(f,'back');assert.equal(await f.locator('[data-settings-section="diagnostics"]').evaluate(el=>el===document.activeElement),true,'B returns to diagnostics category');
      await command(f,'page-next');await f.waitForFunction(()=>window.__currentAuditRoute()!=='/settings');
      await command(f,'page-prev');await f.waitForFunction(()=>window.__currentAuditRoute()==='/settings');
      await press(f,f.locator('[data-settings-section="diagnostics"]'));await absent(f);
    });
    await actionCase('Tab and cached-route reentry keep recovery unmounted / '+enabled,height,async()=>{
      await press(f,f.locator('[data-settings-section="appearance"]'));await absent(f);
      await press(f,f.locator('[data-settings-section="diagnostics"]'));await absent(f);
      await f.evaluate(()=>window.__navigate('/steam'));await f.locator('.route-view[data-route="/steam"]').waitFor();await absent(f);
      await f.evaluate(()=>window.__navigate('/settings'));await f.locator('.route-view[data-route="/settings"]').waitFor();
      await press(f,f.locator('[data-settings-section="diagnostics"]'));await absent(f);
      assert.equal(await f.evaluate(()=>window.__hiddenRecoveryProbe.enabled),enabled);
    });
    // The inherited graph walk checks every enabled stop in all four directions,
    // reverse reachability, visual rows and clipping through all scroll ancestors.
    await state(f,prefix+'/reentered',height);
    const geometry=await f.evaluate(()=>{
      const el=document.querySelector('.app-content');el.scrollTop=el.scrollHeight;
      return {viewport:[innerWidth,innerHeight],clientHeight:el.clientHeight,scrollHeight:el.scrollHeight,scrollTop:el.scrollTop};
    });
    await state(f,prefix+'/after-scroll',height);
    fs.writeFileSync(path.join(task,'evidence','diagnostics-geometry-'+height+'-'+enabled+'.json'),JSON.stringify(geometry,null,2));
    await page.locator('.screen').screenshot({path:path.join(task,'evidence','hidden-recovery-'+height+'-'+enabled+'.png')});
  }
}

const ownFile=fileURLToPath(import.meta.url);
if(process.argv[1]&&path.resolve(process.argv[1])===ownFile)await main();
async function main(){
  const root=path.resolve(path.dirname(ownFile),'..'),base=path.resolve(process.argv[2]||path.join(root,'work/steam-recovery-hidden-ui-20261022'));
  const allowed=path.join(root,'work','steam-recovery-hidden-ui-20261022');
  assert.equal(base,allowed,'Keep all artifacts in the authorized evidence directory');
  const mode=process.argv[3]||'focused';assert.ok(['focused','full','negative'].includes(mode));
  const sha=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  const nativeDefault='native/sleep_steam_preferences.h';
  assert.match(fs.readFileSync(path.join(root,nativeDefault),'utf8'),/inline\s+constexpr\s+bool\s+kAutoWakeRecoveryDefaultEnabled\s*=\s*true\s*;/,'native default must already be ON (read only)');
  assert.match(fs.readFileSync(path.join(root,'native/main.cpp'),'utf8'),/g_sleepRecoveryAutoEnabled\{ymcc::sleepprefs::kAutoWakeRecoveryDefaultEnabled\}/);
  const task=path.join(base,mode);assert.ok(!fs.existsSync(task),'Use a fresh evidence run directory');fs.mkdirSync(task,{recursive:true});
  const templates=path.join(root,'tools/ymcc_sim/global/ui-navigation/templates');fs.cpSync(templates,task,{recursive:true});
  fs.mkdirSync(path.join(task,'evidence'),{recursive:true});
  fs.copyFileSync(ownFile,path.join(task,'hiddenSettingsCases.mjs'));
  const original=fs.readFileSync(path.join(task,'steamDiagnosticsCases.mjs'),'utf8');
  const pivot=original.indexOf("  f=await open('/settings',height,{nativeWidth:'1'});");assert.ok(pivot>0);
  fs.writeFileSync(path.join(task,'steamDiagnosticsCases.mjs'),"import {hiddenRecoveryCases} from './hiddenSettingsCases.mjs';\n"+original.slice(0,pivot)+"  await hiddenRecoveryCases({height,open,command,press,actionCase,state,page,task});\n}\n");
  let fixture=fs.readFileSync(path.join(task,'sleepRecoveryFixture.ts'),'utf8');
  fixture=fixture.replace('enabled:true',"enabled:(window as any).__hiddenRecoveryProbe?.enabled!==false");
  fixture=fixture.replace('async()=>({...value})',"async()=>{if((window as any).__hiddenRecoveryProbe)(window as any).__hiddenRecoveryProbe.reads++;return {...value};}");
  fixture=fixture.replace('value={...value,enabled};',"if((window as any).__hiddenRecoveryProbe)(window as any).__hiddenRecoveryProbe.writes.push(enabled);value={...value,enabled};");
  fs.writeFileSync(path.join(task,'sleepRecoveryFixture.ts'),fixture);
  let verify=fs.readFileSync(path.join(task,'verify.mjs'),'utf8');
  verify="import {hiddenRecoveryCases} from './hiddenSettingsCases.mjs';\n"+verify.replace("const proofInputs=[","const proofInputs=['hiddenSettingsCases.mjs','build.mjs',");
  if(mode!=='full'){
    const start=verify.indexOf('  await naturalNavigationCases('),end=verify.indexOf(' }finally{await context.close();');assert.ok(start>0&&end>start);
    verify=verify.slice(0,start)+'  await hiddenRecoveryCases({height,open,command,press,actionCase,state,page,task});\n'+verify.slice(end);
  }
  fs.writeFileSync(path.join(task,'verify.mjs'),verify);
  if(mode==='negative'){
    const target=path.join(task,'Overrides/src/views/SettingsView.vue');fs.mkdirSync(path.dirname(target),{recursive:true});
    fs.copyFileSync(path.join(base,'SettingsView.before.vue'),target);
  }
  const env={...process.env,YMCC_UI_SOURCE_ROOT:root};for(const key of ['NODE_OPTIONS','NODE_PATH','PLAYWRIGHT_NODEJS_PATH','YMCC_UI_TOOL_HELPER_URL','YMCC_UI_TOOL_MANIFEST','YMCC_SIM_GLOBAL_RUN'])delete env[key];
  async function run(file,args,log,timeout=480000){
    const fd=fs.openSync(path.join(task,log),'w');
    try{return await new Promise((resolve,reject)=>{const child=spawn(process.execPath,[file,...args],{cwd:root,env,windowsHide:true,stdio:['ignore',fd,fd],timeout});child.on('error',reject);child.on('exit',(code,signal)=>resolve({code,signal}));});}finally{fs.closeSync(fd);}
  }
  const templateHashes={};for(const name of fs.readdirSync(templates,{recursive:true,withFileTypes:true}).filter(x=>x.isFile())){const rel=path.relative(templates,path.join(name.parentPath||name.path,name.name));templateHashes[rel]=sha(path.join(templates,rel));}
  const defaultPin=sha(path.join(root,nativeDefault));
  const buildResult=await run(path.join(task,'build.mjs'),[],'build.log');assert.equal(buildResult.code,0,'Actual UI build failed; inspect build.log');
  const buildFile=path.join(task,'build-result.json'),build=JSON.parse(fs.readFileSync(buildFile,'utf8'));
  build.sourceHashes['tools/steam_recovery_hidden_ui_selftest.mjs']=sha(ownFile);build.sourceHashes[nativeDefault]=defaultPin;
  fs.writeFileSync(buildFile,JSON.stringify(build,null,2));
  if(mode!=='negative')assert.equal(build.components.some(x=>/SteamRecoveryDiagnostics|SleepRecoveryPanel/.test(x)),false,'Recovery components still compiled into tested UI');
  const site=path.join(task,'site');const server=createServer((req,res)=>{const rel=new URL(req.url,'http://127.0.0.1').pathname.slice(1)||'index.html',file=path.resolve(site,rel);if(!file.startsWith(site+path.sep)||!fs.existsSync(file)||!fs.statSync(file).isFile()){res.writeHead(404);res.end();return;}res.setHeader('Cache-Control','no-store');res.setHeader('Content-Type',file.endsWith('.js')?'text/javascript; charset=utf-8':file.endsWith('.css')?'text/css; charset=utf-8':'text/html; charset=utf-8');res.end(fs.readFileSync(file));});
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const url='http://127.0.0.1:'+server.address().port+'/';
  fs.writeFileSync(path.join(task,'server.json'),JSON.stringify({url,pid:process.pid,ownedInProcess:true}));
  let result;try{result=await run(path.join(task,'verify.mjs'),[],'verify.log');}finally{await new Promise(resolve=>server.close(resolve));}
  const reportFile=path.join(task,'navigation-gate.json'),report=fs.existsSync(reportFile)?JSON.parse(fs.readFileSync(reportFile,'utf8')):null;
  assert.equal(sha(path.join(root,nativeDefault)),defaultPin,'Native default changed during test');
  const sourcesStable=Object.entries(build.sourceHashes).every(([name,hash])=>sha(path.isAbsolute(name)?name:path.join(root,name))===hash);
  const templatesStable=Object.entries(templateHashes).every(([name,hash])=>sha(path.join(templates,name))===hash);
  const summary={mode,testedAt:new Date().toISOString(),nativeDefault:{file:nativeDefault,enabled:true,readOnly:true,sha256:defaultPin},result,pass:result.code===0&&report?.pass&&sourcesStable&&templatesStable,states:report?.states,directions:report?.directions,actionCases:report?.actionCases,failures:report?.failures,pageErrors:report?.pageErrors,sourcesStable,templatesStable,ownServerClosed:!server.listening,physicalController:false,installedApplicationTouched:false,scope:'Current mainline components and engine; local template contract updated for hidden recovery card; memory IPC only'};
  if(mode==='negative'){summary.expectedFailureDetected=result.code!==0&&JSON.stringify(report).includes('RECOVERY_UI_MOUNTED');summary.pass=summary.expectedFailureDetected;summary.scope='Negative control: pre-patch SettingsView override; never release evidence';}
  fs.writeFileSync(path.join(task,'summary.json'),JSON.stringify(summary,null,2));console.log(JSON.stringify(summary,null,2));
  if(!summary.pass)process.exitCode=1;
}
