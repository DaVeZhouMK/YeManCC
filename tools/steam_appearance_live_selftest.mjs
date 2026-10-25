import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {execFileSync,execFile}from 'node:child_process';
import{promisify}from 'node:util';
import{targets,connect}from './fixtures/decky_cdp.mjs';
const out=path.resolve('../../Temp/steam-appearance-validation/live');fs.mkdirSync(out,{recursive:true});
const helper='C:/SOFT/YeMan/PowerControl/steam-appearance.ps1';
function command(action,payload){const args=['-NoProfile','-ExecutionPolicy','Bypass','-File',helper,'-Action',action];if(payload)args.push('-Payload',Buffer.from(JSON.stringify(payload),'utf8').toString('base64'));const output=execFileSync('powershell.exe',args,{encoding:'utf8',timeout:65000,windowsHide:true});return JSON.parse(output.trim().replace(/^\uFEFF/,''));}
async function metrics(){
 const tab=(await targets()).find(t=>t.title==='Steam 大屏幕模式');if(!tab)throw Error('Live Steam Big Picture target missing');const client=await connect(tab.webSocketDebuggerUrl);
 try{return {targetId:tab.id,title:tab.title,...await client.evaluate(`({styleCount:document.querySelectorAll('.css-loader-style').length,roundStyles:Array.from(document.querySelectorAll('.css-loader-style')).filter(x=>x.textContent.includes('--round-radius-size: 10px !important')).map(x=>({id:x.id,length:x.textContent.length})),radius:getComputedStyle(document.documentElement).getPropertyValue('--round-radius-size').trim()})`)}}finally{client.close()}
}
const report={scope:'Installed helper and real CSSLoader backend, live Steam main-page DOM; source switch parameters restored',cases:[],metrics:{}};
let toggled=false,baseline,originals;
try{
 baseline=command('Status').snapshot;assert(baseline.backendReady&&baseline.steamRunning);originals=Object.fromEntries(baseline.themes.map(row=>[row.name,JSON.parse(fs.readFileSync(path.join('C:/Users/DaVe/homebrew/themes',row.name,'config_USER.json'),'utf8'))]));
 report.baseline=baseline;report.metrics.before=await metrics();console.log('BEFORE',JSON.stringify(report.metrics.before));assert(report.metrics.before.roundStyles.length>0);
 if(process.argv.includes('--probe')){fs.writeFileSync(path.join(out,'probe.json'),JSON.stringify(report,null,2));process.exit(0)}
 const pids=()=>JSON.parse(execFileSync('powershell.exe',['-NoProfile','-Command',"ConvertTo-Json -Compress -InputObject @(Get-Process -Name 'CssLoader-Standalone-Headless' | Select-Object -ExpandProperty Id)"],{encoding:'utf8',windowsHide:true}));
 const beforePids=pids();const asyncExec=promisify(execFile);const starts=await Promise.all(Array.from({length:4},()=>asyncExec('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',helper,'-Action','Ensure'],{encoding:'utf8',windowsHide:true,timeout:45000})));
 assert.deepEqual(pids(),beforePids);assert(starts.every(value=>JSON.parse(value.stdout).started===false));report.cases.push('Four simultaneous real loader starts reuse the same PyInstaller parent/worker group');
 const round=baseline.themes.find(row=>row.name==='Round');toggled=true;const off=command('Apply',{expectedRevision:baseline.revision,states:{Round:false}});assert(off.ok&&off.snapshot.configuredMatchesBackend);report.off=off;
 for(let i=0;i<10;i++){report.metrics.off=await metrics();if(report.metrics.off.roundStyles.length===0)break;await new Promise(resolve=>setTimeout(resolve,250))}
 assert.equal(report.metrics.off.roundStyles.length,0);report.cases.push('Round OFF removes its injected style from actual Steam Big Picture DOM');
 const on=command('Apply',{expectedRevision:off.snapshot.revision,states:{Round:round.configured}});assert(on.ok&&on.snapshot.configuredMatchesBackend);report.on=on;
 for(let i=0;i<10;i++){report.metrics.on=await metrics();if(report.metrics.on.roundStyles.length>0)break;await new Promise(resolve=>setTimeout(resolve,250))}
 if(round.configured){assert(report.metrics.on.roundStyles.length>0);assert.equal(report.metrics.on.radius,report.metrics.before.radius)}report.cases.push('Round ON restores its actual computed CSS radius without restarting Steam');
 const refresh=command('Refresh');assert(refresh.ok);report.refresh=refresh;report.metrics.refreshed=await metrics();report.cases.push('Refresh rereads disk and reapplies CSS to the same running Steam main target');
 for(const name of Object.keys(originals)){const current=JSON.parse(fs.readFileSync(path.join('C:/Users/DaVe/homebrew/themes',name,'config_USER.json'),'utf8'));assert.deepEqual(current,originals[name])}report.cases.push('Every original enabled state and nested parameter value matches the pre-test configuration');
 let current=command('Status').snapshot;
 const heroOff=command('Apply',{expectedRevision:current.revision,states:{'Art Hero':false}});assert(heroOff.ok);assert(heroOff.snapshot.themes.find(row=>row.name==='Centered Game Text').configured);assert(heroOff.snapshot.themes.find(row=>row.name==='Mini Carousel').configured);assert(heroOff.snapshot.configuredMatchesBackend);report.cases.push('Live Art Hero OFF reconciles its dependency cascade and retains centered text / mini carousel independently');
 current=heroOff.snapshot;const heroOn=command('Apply',{expectedRevision:current.revision,states:{'Art Hero':true}});assert(heroOn.ok);
 current=heroOn.snapshot;const allOff=command('Apply',{expectedRevision:current.revision,states:Object.fromEntries(current.themes.map(row=>[row.name,false]))});assert(allOff.ok);report.metrics.allOff=await metrics();assert.equal(report.metrics.allOff.styleCount,0);report.cases.push('Master OFF removes all seven managed theme styles from the actual main page');
 const allOn=command('Apply',{expectedRevision:allOff.snapshot.revision,states:Object.fromEntries(baseline.themes.map(row=>[row.name,row.configured]))});assert(allOn.ok);report.metrics.allOn=await metrics();assert.equal(report.metrics.allOn.styleCount,report.metrics.before.styleCount);assert.equal(report.metrics.allOn.radius,report.metrics.before.radius);report.cases.push('Master ON restores the exact initial style count and effective radius without restarting Steam');
 for(const name of Object.keys(originals)){assert.deepEqual(JSON.parse(fs.readFileSync(path.join('C:/Users/DaVe/homebrew/themes',name,'config_USER.json'),'utf8')),originals[name])}report.cases.push('All seven enabled states and every nested parameter survive live individual, dependency and master-switch cycles');
 report.ok=true;console.log('PASS: '+report.cases.length+' real Steam/backend checks');
}catch(error){report.ok=false;report.error=error.stack;console.error(error);process.exitCode=1}
finally{
 if(toggled&&baseline){try{const current=command('Status').snapshot;const restoreStates=Object.fromEntries(baseline.themes.filter(original=>current.themes.find(row=>row.name===original.name)?.configured!==original.configured).map(row=>[row.name,row.configured]));if(Object.keys(restoreStates).length){report.restore=command('Apply',{expectedRevision:current.revision,states:restoreStates})}}catch(error){report.restoreError=error.stack;process.exitCode=1}}
 fs.writeFileSync(path.join(out,'live-result.json'),JSON.stringify(report,null,2));
}