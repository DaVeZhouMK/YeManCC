import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const out=path.resolve(process.env.YMCC_MSI_FOUNDATION_MUTATION_OUT || 'G:/YeManCC-Work/Build/Validation/MSI-Foundation-Process-R1-20261013/foundation-mutations');
const source=fs.readFileSync(path.join(root,'native/msi_foundation_process_observation.h'),'utf8');
fs.mkdirSync(out,{recursive:true});
const mutants=[
 ['first-snapshot-overclaim','{"firstSnapshotProcessIdentityKnown", false}','{"firstSnapshotProcessIdentityKnown", true}'],
 ['pid-zero-absence-overclaim','{"childrenAbsenceKnown", false}','{"childrenAbsenceKnown", true}'],
 ['ignore-service-pid-change','current.dwProcessId == anchor.dwProcessId','true'],
 ['ignore-original-handle-exit','return wait == WAIT_TIMEOUT;','return true;'],
 ['trust-first-snapshot-parent','child.value("snapshotParentPidRevalidated", false)','true'],
];
const results=[];
for(const [name,from,to] of mutants){
 assert(source.includes(from),'mutation anchor exists '+name);
 const header=path.join(out,name+'.h');fs.writeFileSync(header,source.replace(from,to));
 const child=spawnSync(process.execPath,[path.join(root,'tools/msi_foundation_process_selftest.mjs')],{cwd:root,encoding:'utf8',timeout:60000,env:{...process.env,YMCC_MSI_FOUNDATION_HEADER:header,YMCC_MSI_FOUNDATION_OUT:path.join(out,name)}});
 fs.writeFileSync(path.join(out,name+'.log'),(child.stdout||'')+(child.stderr||''));
 const resultPath=path.join(out,name,'results.txt');const text=fs.existsSync(resultPath)?fs.readFileSync(resultPath,'utf8'):'';
 const caught=child.status!==0&&/^FAIL /m.test(text);
 results.push({name,caught,fixtureRan:text.includes('liveProcessCalls=0 liveServiceCalls=0'),exitCode:child.status});
 console.log((caught?'PASS ':'FAIL ')+name+' rejected by production-helper runtime fixture');
}
fs.writeFileSync(path.join(out,'results.json'),JSON.stringify({mutants:results,liveProcessCalls:0,liveServiceCalls:0},null,2)+'\n');
assert(results.every(r=>r.caught&&r.fixtureRan),'Mutants must fail runtime assertions, not compilation');
