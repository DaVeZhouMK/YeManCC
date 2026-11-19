// Owned, offscreen native windows only. Never launches or drives YeManCC.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const root=process.cwd(),args=process.argv.slice(2);
if(![2,4].includes(args.length)||args[0]!=='--out'||!path.isAbsolute(args[1])||(args.length===4&&(args[2]!=='--case'||!args[3])))throw Error('Use --out absolute-new-directory [--case name]');
const out=path.resolve(args[1]);if(fs.existsSync(out))throw Error('Output must be new');
fs.mkdirSync(out,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const inputs=[],seen=new Set();
function freeze(file){
 file=path.resolve(file);if(seen.has(file))return;seen.add(file);
 const bytes=fs.readFileSync(file),relative=path.relative(root,file),copy=path.join(out,'source',relative);
 fs.mkdirSync(path.dirname(copy),{recursive:true});fs.writeFileSync(copy,bytes);
 inputs.push({path:file,frozen:copy,sha256:sha(bytes)});
 for(const [,include] of bytes.toString('utf8').matchAll(/^\s*#include\s+"([^"]+)"/gm)){
  const candidates=[path.join(path.dirname(file),include),path.join(root,'native',include)];
  const resolved=candidates.find(f=>fs.existsSync(f));if(!resolved)throw Error('Missing local include '+include);freeze(resolved);
 }
}
freeze(path.join(root,'tools/fixtures/game_overlay_storm_win32.cpp'));
freeze(path.join(root,'deps/json/json.hpp'));
fs.writeFileSync(path.join(out,'inputs.json'),JSON.stringify(inputs,null,2));
const setup='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
for(const value of [out,setup])if(/["%\r\n]/.test(value))throw Error('Unsupported shell path');
const driver=path.join(out,'compile.cmd'),exe=path.join(out,'storm.exe');
fs.writeFileSync(driver,calibrationToolchainPreamble(setup)+`cl /nologo /EHsc /std:c++20 /utf-8 /O1 /DNOMINMAX /DUNICODE /D_UNICODE /I"${path.join(out,'source/native')}" /I"${path.join(out,'source/deps/json')}" "${path.join(out,'source/tools/fixtures/game_overlay_storm_win32.cpp')}" /Fe:"${exe}" /Fo:"${path.join(out,'storm.obj')}" /link user32.lib comctl32.lib\r\nexit /b %errorlevel%\r\n`);
const built=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',driver],{cwd:out,encoding:'utf8',timeout:90000,windowsHide:true});
fs.writeFileSync(path.join(out,'compile.log'),(built.stdout||'')+(built.stderr||''));
if(built.status!==0)throw Error('Compile failed: '+fs.readFileSync(path.join(out,'compile.log'),'utf8'));
const results=[];
const names=['begin-reentry','admission-retry','begin-verify-retired','begin-retired-exception','stale-hook','context-reentry','retire-current-false','retire-current-throws','retire-verify-false','retire-verify-throws','retire-layout-false','retire-layout-throws','retire-submit-false','retire-submit-throws','retire-order-false','retire-order-throws','peer-event-storm','message-storm','thread-storm','post-failure','post-retired-failure','hook-failure','timer-failure','timer-budget','revoke-session-scope','close-storm'];
if(args.length===4&&!names.includes(args[3]))throw Error('Unknown explicit case');
for(const name of args.length===4?[args[3]]:names){
 const file=path.join(out,name+'.json'),start=performance.now();
 const ran=spawnSync(exe,[name,file],{cwd:out,encoding:'utf8',timeout:10000,windowsHide:true});
 const result=fs.existsSync(file)?JSON.parse(fs.readFileSync(file,'utf8')):{case:name,pass:false,failure:ran.error?.message||'missing result',stdout:ran.stdout,stderr:ran.stderr};
 results.push({...result,exitCode:ran.status,elapsedMs:performance.now()-start});
}
for(const input of inputs)if(sha(fs.readFileSync(input.frozen))!==input.sha256)throw Error('Frozen input changed');
const report={status:results.every(r=>r.pass&&r.exitCode===0)?'PASS':'FAIL',scope:'production Carrier; real owned offscreen HWNDs and WinEvent hooks, injected faults at named APIs; no full app, no device or installed-settings interaction',exeSha256:sha(fs.readFileSync(exe)),results};
fs.writeFileSync(path.join(out,'REPORT.json'),JSON.stringify(report,null,2));
console.log(JSON.stringify({status:report.status,cases:results.map(({case:name,pass,failure,facts})=>({name,pass,failure,facts})),out}));
process.exitCode=report.status==='PASS'?0:1;
