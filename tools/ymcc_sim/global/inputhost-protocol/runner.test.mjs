import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {getInputHostProtocolAdapters,toolchainFiles,REPO,MODULE} from './adapter.mjs';
import {evidencePath,inside} from './provenance.mjs';
import {parseArgs} from './run.mjs';
import {extract,closeBlock} from './extract-parser.mjs';
import {validateAdapters,fingerprintArtifacts} from '../runner.mjs';
const here=path.dirname(fileURLToPath(import.meta.url));
test('two existing actual-consumer fixtures retain their explicit source and tool inputs',()=>{const cases=getInputHostProtocolAdapters();assert.equal(cases.length,2);for(const c of cases){assert(c.evidenceInputs.includes('InputHost/Program.cs'));assert(c.artifactInputs.some(p=>path.basename(p)==='csc.dll'));assert(c.artifactInputs.some(p=>path.basename(p)==='System.Text.Json.dll'));assert.equal(c.scope,'behavioral-mock');}assert(cases[0].evidenceInputs.includes('tools/inputhost_cpu_cost_window_selftest.cs'));assert(cases[1].evidenceInputs.includes('tools/inputhost_optional_trigger_selftest.cs'));assert(cases[1].evidenceInputs.includes(MODULE+'/ProtocolEntry.cs'));assert(!cases[0].evidenceInputs.includes(MODULE+'/extract-parser.mjs'));assert(!cases[0].evidenceInputs.includes(MODULE+'/ProtocolEntry.cs'));});
test('fresh output cannot be in repository or on the installed/system drive',()=>{assert.throws(()=>evidencePath(REPO),/REPO_BOUNDARY/);assert.throws(()=>evidencePath('C:/inputhost-fixture-must-not-be-created'),/SYSTEM_VOLUME/);assert.throws(()=>evidencePath('relative/path'),/ABSOLUTE/);});
test('CLI cannot enter a benchmark or select an arbitrary script',()=>{assert.throws(()=>parseArgs(['--case','controller.inputhost-optional-trigger-protocol','--benchmark','anything']),/ARGUMENTS/);assert.throws(()=>parseArgs(['--case','arbitrary-script','--evidence-dir','D:/Codex/must-not-exist']),/CASE_UNKNOWN/);});
test('unique source extraction ignores comment and quoted braces and keeps full method',()=>{const text='private static bool Exact() { string s="}"; /* } */ if(true) { return true; } return false; }\nprivate void Other(){}';const r=extract(text,'private static bool Exact()');assert.equal(r.text,'private static bool Exact() { string s="}"; /* } */ if(true) { return true; } return false; }');assert.equal(r.end,r.text.length);});
test('nonunique source anchors refuse rather than selecting a convenient implementation',()=>{assert.throws(()=>extract('void A(){} void A(){}','void A()'),/NONUNIQUE/);assert.throws(()=>extract('void A(){}','void Missing()'),/NONUNIQUE/);});
test('unterminated production block is never compiled as a partial consumer',()=>{assert.throws(()=>closeBlock('{ /* no completion */',0),/UNCLOSED/);});
test('path membership does not confuse siblings with descendants',()=>{assert.equal(inside('D:/case','D:/case-other/file'),false);assert.equal(inside('D:/case','D:/case/build/file'),true);});
test('bad CLI produces no evidence directory or external invocation',()=>{const target='D:/Codex/inputhost-no-write-'+process.pid;assert.equal(fs.existsSync(target),false);const call=spawnSync(process.execPath,[path.join(here,'run.mjs'),'--case','controller.inputhost-cpu-cost-window','--evidence-dir',target,'--benchmark'],{windowsHide:true,encoding:'utf8',timeout:10000});assert.equal(call.status,1);assert.match(call.stderr,/ARGUMENTS_INVALID/);assert.equal(fs.existsSync(target),false);});

test('both adapters use a registered controller domain and pass the unchanged global validator',()=>{const domains=JSON.parse(fs.readFileSync(path.join(REPO,'tools/ymcc_sim/global/domains.json'),'utf8'));const cases=getInputHostProtocolAdapters();for(const a of cases){assert(domains.domains.some(d=>d.id===a.domain));assert.equal(a.domain,'controller');assert.equal(a.id.split('.')[0],a.domain);assert(a.artifactInputs.length>0);}assert.doesNotThrow(()=>validateAdapters(cases,REPO));});

test('missing compiler/targeting pack metadata constructs and is fingerprinted as missing without running anything',()=>{const paths=toolchainFiles(REPO,{dotnetRoot:'D:/Codex/missing-inputhost-sdk',sdkVersion:'10.0.400',runtimeVersion:'10.0.11',targetingPackVersion:'10.0.11'});assert(paths.some(p=>p.endsWith('MSBuild.dll')));assert(paths.some(p=>p.endsWith('System.Runtime.dll')));const rows=fingerprintArtifacts(paths);assert(rows.some(r=>r.status==='MISSING_OR_NOT_A_FILE'));});


test('fresh explicit non-system evidence directory works outside the original D volume without creation',()=>{
 const fresh=path.join(path.dirname(REPO),'.inputhost-evidence-'+process.pid+'-'+Date.now());
 assert.equal(fs.existsSync(fresh),false);
 assert.equal(evidencePath(fresh),path.resolve(fresh));
 assert.equal(fs.existsSync(fresh),false);
 const compatible='D:/Codex/inputhost-fresh-'+process.pid+'-'+Date.now();
 assert.equal(evidencePath(compatible),path.resolve(compatible));
 assert.equal(fs.existsSync(compatible),false);
});

test('fresh evidence rejects ambiguous volumes and all repository containment before filesystem work',()=>{
 const systemDrive=process.env.SystemDrive||'C:';
 assert.throws(()=>evidencePath(path.join(systemDrive+'/', 'inputhost-never-created-'+process.pid)),/SYSTEM_VOLUME/);
 for(const value of [null,{},'relative/path','D:drive-relative','/rooted-without-volume','G:/bad\nname','G:/bad\rname','G:/bad\0name'])
  assert.throws(()=>evidencePath(value),/ABSOLUTE|LOCAL_VOLUME/);
 for(const value of ['\\\\invalid-host\\unrequested-share\\never-created','\\\\?\\G:\\device-namespace'])
  assert.throws(()=>evidencePath(value),/LOCAL_VOLUME/);
 for(const value of [REPO,path.dirname(REPO),path.join(REPO,'new-evidence'),path.join(REPO,'..',path.basename(REPO),'new-evidence')])
  assert.throws(()=>evidencePath(value),/REPO_BOUNDARY/);
});

test('existing path ancestors and real junctions cannot masquerade as fresh owned evidence',()=>{
 const parent=path.dirname(REPO),root=fs.mkdtempSync(path.join(parent,'.inputhost-path-'));
 const regular=path.join(root,'regular-file'),target=path.join(root,'live-target'),missing=path.join(root,'removed-target');
 const junction=path.join(root,'live-junction'),dangling=path.join(root,'dangling-junction');
 const owned=(value)=>{assert.equal(path.dirname(value),root);assert.equal(inside(root,value),true);};
 try{
  assert.equal(inside(REPO,root),false);assert.equal(path.dirname(root),parent);
  assert.throws(()=>evidencePath(root),/FRESH/);
  fs.writeFileSync(regular,'owned path fixture',{flag:'wx'});
  assert.throws(()=>evidencePath(regular),/ANCESTOR_NOT_DIRECTORY/);
  assert.throws(()=>evidencePath(path.join(regular,'child')),/ANCESTOR_NOT_DIRECTORY|PATH_UNREADABLE/);
  fs.mkdirSync(target);fs.mkdirSync(missing);
  fs.symlinkSync(target,junction,'junction');fs.symlinkSync(missing,dangling,'junction');
  fs.rmdirSync(missing);assert.equal(fs.existsSync(dangling),false);assert.equal(fs.lstatSync(dangling).isSymbolicLink(),true);
  assert.throws(()=>evidencePath(junction),/REPARSE/);
  assert.throws(()=>evidencePath(path.join(junction,'child')),/REPARSE/);
  assert.throws(()=>evidencePath(dangling),/REPARSE/);
 }finally{
  for(const link of [dangling,junction]){owned(link);let info;try{info=fs.lstatSync(link);}catch(error){if(error.code!=='ENOENT')throw error;}if(info){assert.equal(info.isSymbolicLink(),true);fs.unlinkSync(link);}}
  if(fs.existsSync(regular)){owned(regular);assert.equal(fs.lstatSync(regular).isFile(),true);fs.unlinkSync(regular);}
  for(const dir of [missing,target])if(fs.existsSync(dir)){owned(dir);assert.equal(fs.lstatSync(dir).isSymbolicLink(),false);fs.rmdirSync(dir);}
  assert.equal(path.dirname(root),parent);assert.equal(path.basename(root).startsWith('.inputhost-path-'),true);fs.rmdirSync(root);
 }
 assert.equal(fs.existsSync(root),false);
});
