// Run the original source-backed positive startup/exit group and the partial
// return cases without granting a receipt for the separately pinned full gate.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {REPO, MODULE} from './adapter.mjs';
import {calibrationToolchainPreamble} from '../../../fixtures/hc_reviewed_calibration/toolchain.mjs';

assert.equal(process.argv.length,3,'Give a fresh evidence directory');
const out=path.resolve(process.argv[2]);
fs.mkdirSync(out);
const read=file=>fs.readFileSync(path.join(REPO,file),'utf8');
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const save=(file,value)=>fs.writeFileSync(path.join(out,file),value,{flag:'wx'});
const worker=read(MODULE+'/worker.mjs'),spec=JSON.parse(read(MODULE+'/spec.json'));
const raw=read('native/main.cpp'),main=raw.replace(/\r\n/g,'\n');
// Borrow the original extraction functions only; no compiler or runtime gate
// from the separate formal worker is replaced or marked accepted.
const extraction=worker.slice(worker.indexOf('function definition('),worker.indexOf('function evaluated('));
const {definition,startupGlobals,cleanupDeclaration}=vm.runInNewContext(extraction+';({definition,startupGlobals,cleanupDeclaration})',{spec,createHash,need:(ok,msg)=>assert(ok,msg)});
const defs=spec.reviewedFunctions.map(({name})=>({name,text:definition(main,name)}));
const {retry,startupScope}=startupGlobals(main);
const original=read(MODULE+'/fixture.cpp.in'),cases=read(MODULE+'/exit-return-proof.inc');
let body=original.replace('// EXACT_STARTUP_EXIT_CONSTANTS',retry+'\n'+startupScope).replace('// EXACT_STARTUP_EXIT_FUNCTIONS',cleanupDeclaration(main).text+'\n\n'+defs.map(x=>x.text).join('\n\n'));
const replace=(before,after)=>{assert.equal(body.split(before).length,2,before);body=body.replace(before,()=>after);};
replace('LONG countedNtResume(HANDLE h){','static int remainingSuccessfulResumes=-1;\nLONG countedNtResume(HANDLE h){');
replace('static ThreadState queryThreads(DWORD pid){','static bool withholdThreadProof=false;\nstatic ThreadState queryThreads(DWORD pid){if(withholdThreadProof)return {};');
replace('if(p.nativeFail||!p.alive||p.depth<=0){','if(remainingSuccessfulResumes==0||p.nativeFail||!p.alive||p.depth<=0){');
replace('--p.depth;++ntSuccesses;', '--p.depth;if(remainingSuccessfulResumes>0)--remainingSuccessfulResumes;++ntSuccesses;');
replace(' std::cout<<json({{"terminal","native startup and exit cleanup ownership"}',cases+'\n std::cout<<json({{"terminal","native startup and exit cleanup ownership"}');
save('generated.cpp',body);
save('sources.json',JSON.stringify({mainSha256:sha(raw),originalFixtureSha256:sha(original),casesSha256:sha(cases),generatedSha256:sha(body),functions:defs.map(x=>({name:x.name,sha256:sha(x.text)})),scope:'original startup/exit positive group plus partial exit return; controlled memory ports; not the formal source-pin gate'},null,2));
const cmd=path.join(out,'compile.cmd'),exe=path.join(out,'fixture.exe');
save('compile.cmd',calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+
 `cl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 /I"${REPO}/native" /I"${REPO}/deps/json" generated.cpp /Fo"fixture.obj" /Fe"fixture.exe" /sourceDependencies "dependencies.json"\r\nexit /b %errorlevel%\r\n`);
const invoke=(label,program,args,timeout)=>{const p=spawnSync(program,args,{cwd:out,encoding:'utf8',windowsHide:true,timeout,maxBuffer:16*1024*1024});save(label+'.stdout',p.stdout??'');save(label+'.stderr',p.stderr??'');save(label+'.json',JSON.stringify({exitCode:p.status,error:p.error?.message??null,signal:p.signal}));assert(!p.error&&!p.signal,label);return p;};
assert.equal(invoke('compile','cmd.exe',['/d','/c',cmd],30000).status,0,'Compile failed');
const p=invoke('run',exe,[],12000),rows=p.stdout.trim().split(/\r?\n/).map(JSON.parse),terminal=rows.pop();
assert.deepEqual(rows.slice(0,spec.fixedAssertions.length).map(x=>({case:x.case,expected:x.expected})),spec.fixedAssertions);
assert.equal(rows.length,spec.fixedAssertions.length+7);
console.log(JSON.stringify({groups:rows.length,failed:rows.filter(x=>!x.passed).map(x=>x.case),terminal}));
process.exitCode=p.status;
