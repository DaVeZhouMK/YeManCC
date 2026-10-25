import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import {spawnSync} from 'node:child_process';import {createHash} from 'node:crypto';import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
export function verifyMouseSettingsShow(root,out){
 fs.mkdirSync(out,{recursive:true});const inputs=['native/mouse_backend_process.h','tools/mouse_backend_settings_show_selftest.cpp'];const hash=p=>createHash('sha256').update(fs.readFileSync(path.join(root,p))).digest('hex');const sourceHashes=Object.fromEntries(inputs.map(p=>[p,hash(p)])),original=fs.readFileSync(path.join(root,inputs[0]),'utf8'),results=[];
 for(const variant of ['current','old-hidden-start']){
  const dir=path.join(out,variant);fs.mkdirSync(dir,{recursive:true});let header=original;
  if(variant==='old-hidden-start'){const anchor='STARTUPINFOW startup{sizeof(startup)};';assert(header.includes(anchor));header=header.replace(anchor,anchor+'\n        startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;');}
  fs.writeFileSync(path.join(dir,'mouse_backend_process.h'),header);
  const command=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+'cl /nologo /std:c++20 /utf-8 /EHsc /W4 /O2 /MD /I"'+dir+'" "'+path.join(root,inputs[1])+'" /Fo"'+dir+'/probe.obj" /Fe"'+dir+'/probe.exe" /link user32.lib\r\nexit /b %errorlevel%\r\n';fs.writeFileSync(path.join(dir,'compile.cmd'),command);
  const build=spawnSync('cmd.exe',['/d','/c',path.join(dir,'compile.cmd')],{cwd:dir,windowsHide:true,encoding:'utf8',timeout:60000});fs.writeFileSync(path.join(dir,'compile.log'),(build.stdout||'')+'\n'+(build.stderr||''));assert.equal(build.status,0,'Settings probe compile failed: '+dir);
  const child=path.join(dir,'JoyxSettingsProbeChild.exe');fs.copyFileSync(path.join(dir,'probe.exe'),child);
  const run=spawnSync(path.join(dir,'probe.exe'),[child,path.join(dir,'child.json'),'visible'],{cwd:dir,windowsHide:true,encoding:'utf8',timeout:15000});fs.writeFileSync(path.join(dir,'execution.log'),(run.stdout||'')+'\n'+(run.stderr||''));assert(!run.error&&!run.signal,'Settings probe must exit naturally');const report=JSON.parse(run.stdout);const rejected=variant==='old-hidden-start';assert.equal(run.status,rejected?1:0,JSON.stringify(report));assert.equal(report.firstShowVisible,!rejected);assert.equal(report.childReport.privateDesktop,true);assert.equal(report.productionBackendStartOk,true);results.push({variant,expectedRejection:rejected,...report});
 }
 for(const [p,h]of Object.entries(sourceHashes))assert.equal(hash(p),h,'Settings probe source changed: '+p);
 const report={status:'PASS',cases:2,scope:'actual production process starter and synthetic GUI on private unshown desktop; no installed backend operation',sourceHashes,results};fs.writeFileSync(path.join(out,'settings-first-show-result.json'),JSON.stringify(report,null,2));return report;
}
