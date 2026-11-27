import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import {fileURLToPath} from 'node:url';
import {getControllerCoordinatorLifecycleAdapters} from './adapter.mjs';
import {currentLifecycleModePlan,currentLifecyclePreparedRegistration} from '../controller-coordinator-pipe/r3-current-run.mjs';
import {simulationCorePath} from '../release-host-environment.mjs';
import {executeAdapter,verdictFor} from '../runner.mjs';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const prefix='ymcc-coordinator-prerequisite-';

test('current registration reads each original polarity once for both lists',()=>{
 const roles=[...new Set(currentLifecycleModePlan().map(row=>row.role))], seen=[];
 const registration=currentLifecyclePreparedRegistration(repo,{readRole(role,root){
  assert.equal(root,repo);assert(!seen.includes(role));seen.push(role);
  return {sources:['common-source',role+'-source'],artifacts:['common-artifact',role+'-artifact']};
 }});
 assert.equal(roles.length,5);assert.deepEqual(seen,roles);
 assert.deepEqual(registration,{evidenceInputs:['common-source',...roles.map(role=>role+'-source')],artifactInputs:['common-artifact',...roles.map(role=>role+'-artifact')]});
});

test('combined current registration keeps role refusal and the aggregate artifact cap',()=>{
 const seen=[];
 assert.throws(()=>currentLifecyclePreparedRegistration(repo,{readRole(role){
  seen.push(role);
  if(role==='bypassAdmission')throw Error('ORIGINAL_ROLE_MISSING');
  return {sources:[],artifacts:[]};
 }}),/ORIGINAL_ROLE_MISSING/);
 assert.deepEqual(seen,['positive','bypassAdmission']);
 const fixture=count=>({readRole(role){return {sources:[],artifacts:Array.from({length:count},(_,i)=>role+'-artifact-'+i)};}});
 assert.equal(currentLifecyclePreparedRegistration(repo,fixture(614)).artifactInputs.length,3070);
 assert.throws(()=>currentLifecyclePreparedRegistration(repo,fixture(615)),/ORIGINAL_GLOBAL_ARTIFACT_LIMIT3072/);
});
function directoryIdentity(dir){
 const stat=fs.lstatSync(dir,{bigint:true});
 assert(stat.isDirectory()&&!stat.isSymbolicLink()&&path.relative(dir,fs.realpathSync.native(dir))==='','Redirected coordinator prerequisite directory: '+dir);
 return stat;
}
function outputRoot(t){
 const explicit=process.env.YMCC_ADAPTER_PREREQUISITE_EVIDENCE;
 if(explicit){
  const out=path.resolve(explicit);
  directoryIdentity(path.dirname(out));
  // Explicit evidence is retained at the requested path, so never reuse it.
  fs.mkdirSync(out);
  return out;
 }
 const parent=simulationCorePath('Workspace'),parentIdentity=directoryIdentity(parent);
 const out=fs.mkdtempSync(path.join(parent,prefix)),outIdentity=directoryIdentity(out);
 t.after(()=>{
  assert.equal(path.relative(parent,path.dirname(out)),'');assert(path.basename(out).startsWith(prefix));
  for(const [dir,expected] of [[parent,parentIdentity],[out,outIdentity]]){
   const actual=directoryIdentity(dir);
   assert(actual.dev===expected.dev&&actual.ino===expected.ino,'Coordinator prerequisite directory identity changed: '+dir);
  }
  fs.rmSync(out,{recursive:true});
 });
 return out;
}

test('coordinator prerequisite isolation preserves BLOCKED and ready registration identity',async t=>{
 const out=outputRoot(t);
 let artifactReads=0;
 const blocked=getControllerCoordinatorLifecycleAdapters({readSources(){throw Error('fixture manifest unavailable')},readArtifacts(){artifactReads++;throw Error('must not manufacture artifacts')}})[0];
 assert.match(blocked.blockedReason,/COORDINATOR_LIFECYCLE_PREREQUISITE_UNAVAILABLE/);
 assert.deepEqual(blocked.artifactInputs,[]);assert.equal(artifactReads,0);
 const artifactBlocked=getControllerCoordinatorLifecycleAdapters({readSources:()=>['current-source'],readArtifacts(){throw Error('fixture binary unavailable')}})[0];
 assert.match(artifactBlocked.blockedReason,/fixture binary unavailable/);
 const ready=getControllerCoordinatorLifecycleAdapters({readSources:()=>['current-source'],readArtifacts:()=>['pinned-artifact']})[0];
 assert.equal(ready.blockedReason,undefined);
 assert.deepEqual(ready.evidenceInputs,['current-source']);assert.deepEqual(ready.artifactInputs,['pinned-artifact']);

 const result=await executeAdapter(blocked,{repoRoot:repo,runDir:out});
 assert.equal(result.status,'BLOCKED');assert.equal(result.claims.installedApplicationExercised,false);
 const verdict=verdictFor({results:[result],selectedCount:1});assert.notEqual(verdict.status,'PASS');
 fs.writeFileSync(path.join(out,'PREREQUISITE-ISOLATION.json'),JSON.stringify({pass:true,missingFixtureRemainsBlocked:true,selectedMissingFixtureCannotPass:verdict.status,readyIdentityPreserved:true,productProcessesStarted:0},null,2),{flag:'wx'});
 console.log('ADAPTER_PREREQUISITE_ISOLATION_PASS');
});
