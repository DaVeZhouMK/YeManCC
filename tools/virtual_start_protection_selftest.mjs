import {prepareStartupCompiler,runStartupObserver} from './ymcc_sim/global/controller-start-protection/startup-observer.mjs';
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import{spawnSync}from'node:child_process';import{fileURLToPath}from'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),args=process.argv.slice(2);if(args.length!==2||args[0]!=='--out')throw Error('Use --out ABS_FRESH');const out=path.resolve(args[1]);if(fs.existsSync(out))throw Error('Fresh evidence required');fs.mkdirSync(out,{recursive:true});const hash=b=>crypto.createHash('sha256').update(b).digest('hex'),header=fs.readFileSync(path.join(repo,'native/virtual_start_protection.h'),'utf8'),fixture=fs.readFileSync(path.join(repo,'tools/fixtures/virtual_start_protection.cpp'),'utf8');const sources=['native/virtual_start_protection.h','tools/fixtures/virtual_start_protection.cpp','tools/virtual_start_protection_selftest.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','native/main.cpp','native/startup_readiness.h','native/game_input_override.h','native/input_shortcut_toggle.h','native/controller_persona.h','deps/json/json.hpp','tools/startup_readiness_native_probe.py','tools/ymcc_sim/global/controller-start-protection/startup-observer.mjs'].map(p=>({path:p,sha256:hash(fs.readFileSync(path.join(repo,p)))}));
const mutations=[{name:'positive',header},{name:'premature-fallback',header:header.replace(/(bool executionInspectionCompleteLocked\(Tick now\) \{)[\s\S]*?\n    \}/,'$1\n        return viewLocked(now).locked;\n    }'),expected:'fallback cannot reserve inside execution inspection'},{name:'no-settle',header:header.replace('executionInspectionMs = 10000','executionInspectionMs = 0'),expected:'terminal plus9999 remains locked'},{name:'no-maximum',header:header.replace('maxMs = 30000','maxMs = 90000'),expected:'pending maximum30000 releases'},{name:'stale-completion',header:header.replace('ticket != id_ || ',''),expected:'stale ticket cannot settle new request'},{name:'skip-verification-window',header:header.replace('!view.locked || view.verificationRemainingMs','!view.locked'),expected:'verification cannot commit before the full ten-second check window'},{name:'opposite-verified-terminal',header:header.replace('phase_!="verifying" ||\n                !view.locked || ',''),expected:'verified success cannot later also become failure'}];
const selectedChecks={'premature-fallback':1,'no-settle':18,'no-maximum':2,'stale-completion':4,'skip-verification-window':2,'opposite-verified-terminal':7};
for(const m of mutations.slice(1))if(m.header===header)throw Error('CLASS_MUTATION_NOT_APPLIED:'+m.name);
const results=[];
const compiler=prepareStartupCompiler(out);
for(const config of ['Release','Checked'])for(const m of mutations){
 const root=path.join(out,config,m.name);fs.mkdirSync(root,{recursive:true});
 fs.writeFileSync(path.join(root,'virtual_start_protection.h'),m.header);
 fs.writeFileSync(path.join(root,'fixture.cpp'),fixture);
 const exe=path.join(root,'fixture.exe');
 const {process:built,buildMs}=compiler.build(root,path.join(root,'fixture.cpp'),exe,config);
 if(built.status!==0||built.error||built.signal)throw Error('COMPILE_FAILED:'+root+':'+(built.error??built.signal??built.status));
 const negative=m.name!=='positive',args=negative?[m.name]:[];
 const runStart=performance.now();
 const r=spawnSync(exe,args,{cwd:root,windowsHide:true,encoding:'utf8',timeout:10000});
 const runMs=performance.now()-runStart,stdout=r.stdout??'',stderr=r.stderr??'';
 fs.writeFileSync(path.join(root,'stdout.log'),stdout);fs.writeFileSync(path.join(root,'stderr.log'),stderr);
 const expectedFailures=negative?Array(m.name==='no-settle'?2:1).fill(m.expected):[];
 const checks=negative?selectedChecks[m.name]:81,failed=expectedFailures.length;
 const expectedLines=[`CASE=${m.name}`,...expectedFailures.map(x=>'FAIL '+x),`CHECKS=${checks} FAILED=${failed}`];
 const observed=stdout.trimEnd().split(/\r?\n/);
 const passed=!r.error&&!r.signal&&stderr===''&&r.status===(negative?1:0)&&
  observed.length===expectedLines.length&&observed.every((line,i)=>line===expectedLines[i]);
 const parsed=/^CHECKS=(\d+) FAILED=(\d+)$/m.exec(stdout);
 results.push({configuration:config,name:m.name,checks:parsed?+parsed[1]:null,failed:parsed?+parsed[2]:null,exitCode:r.status,
  expectedNegative:negative,expected:m.expected,passed,selected:args[0]??null,failureNames:observed.filter(x=>x.startsWith('FAIL ')).map(x=>x.slice(5)),
  buildMs,runMs,exeSha256:hash(fs.readFileSync(exe)),headerSha256:hash(m.header)});
}
const startupObserver=runStartupObserver(repo,out,compiler);
const changed=sources.filter(r=>hash(fs.readFileSync(path.join(repo,r.path)))!==r.sha256).map(r=>r.path);const result={status:results.every(r=>r.passed)&&startupObserver.every(r=>r.passed)&&!changed.length?'PASS':'FAIL',sources,changed,results,startupObserver,compilerSetup:compiler.summary,productClassExecuted:true,realThreadsAndMutex:true,realTimeSleep:false,deviceIo:false};fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result));process.exitCode=result.status==='PASS'?0:1;
