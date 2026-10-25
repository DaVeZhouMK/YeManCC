// Reusable finite metadata tests. Only tiny self-owned files are created and removed.
// Never imports current-prepared, a producer, compiler, qualified native or business fixture.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {resolveFrozenExpectationToolPins,frozenExpectationArtifactPath} from './r3-frozen-expectation-tool.mjs';
const relative='tools/ymcc_sim/global/controller-coordinator-pipe/r3-current105-expectations.json';
const sha=bytes=>createHash('sha256').update(bytes).digest('hex'),digest=file=>sha(fs.readFileSync(file));
const inside=(root,file)=>{const rel=path.relative(path.resolve(root),path.resolve(file));return rel!==''&&!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep);};
export async function runFrozenExpectationToolSelftest({tempParent,manifestFile=null,legacyModule=null}={}){
 assert(typeof tempParent==='string'&&path.isAbsolute(tempParent),'explicit absolute tempParent');
 const parent=path.resolve(tempParent);assert(fs.statSync(parent).isDirectory());assert(!fs.lstatSync(parent).isSymbolicLink());assert.equal(fs.realpathSync(parent).toLowerCase(),parent.toLowerCase());
 const legacy=legacyModule?await import(pathToFileURL(legacyModule)):null;
 const owned=fs.mkdtempSync(path.join(parent,'.r3-frozen-expectation-gates-'));assert(inside(parent,owned));
 const repo=path.join(owned,'repository'),originRepo=path.join(owned,'origin'),archivePath=path.join(repo,relative),originPath=path.join(originRepo,relative),productPath=path.join(repo,'native/main.cpp');
 const bytes=JSON.stringify({schemaVersion:1,fixture:'self-owned historical expectation',caseNames:['first','last']})+'\n',currentBytes=JSON.stringify({schemaVersion:1,fixture:'self-owned changed origin'})+'\n';
 fs.mkdirSync(path.dirname(archivePath),{recursive:true});fs.mkdirSync(path.dirname(originPath),{recursive:true});fs.mkdirSync(path.dirname(productPath),{recursive:true});fs.writeFileSync(productPath,'// Self-owned text only; never compiled or executed.\n');
 const original={path:originPath,sha256:sha(bytes)},archive={path:archivePath,sha256:sha(bytes)},other={path:productPath,sha256:digest(productPath)};
 const profile={currentPrimaryAccepted:false,executionFrame:'FROZEN_R63_PRODUCT_WITH_EXPLICIT_OWNED_CORE75_TOOL_OVERLAY',frozenExpectationOriginRepository:originRepo,frozenExpectationToolOrigin:original,expectations:archive};
 const inputs=[original,archive,other],valid=()=>({pins:structuredClone(inputs),profile:structuredClone(profile),repo,authorityTools:[structuredClone(original)]}),tests=[];
 const reset=()=>{if(fs.existsSync(archivePath)&&fs.lstatSync(archivePath).isDirectory())fs.rmdirSync(archivePath);fs.writeFileSync(archivePath,bytes);fs.writeFileSync(originPath,currentBytes);};
 const gate=(name,fn)=>{try{reset();fn();tests.push({name,status:'PASS'});}catch(error){tests.push({name,status:'FAIL',error:error.message});}};
 const reject=(name,edit,code)=>gate(name,()=>{const v=valid();edit(v);assert.throws(()=>resolveFrozenExpectationToolPins(v),new RegExp('R3_FROZEN_EXPECTATION_'+code));});
 const strictPins=rows=>{for(const row of rows)assert.equal(digest(row.path),row.sha256,'strict unchanged pin '+row.path);};
 try{
  // The original18 boundary semantics and names are retained.
  gate('one exact frozen alias accepted; product pin untouched and duplicate archive deduplicated',()=>{const r=resolveFrozenExpectationToolPins(valid());assert.equal(r.aliases.length,1);assert.equal(r.pins.length,2);assert.deepEqual(r.pins.find(p=>p.path===other.path),other);assert.equal(r.originFileRead,false);assert.equal(frozenExpectationArtifactPath(original,r.aliases),archive.path);assert.deepEqual(r.aliases[0].original,original);strictPins(r.pins);});
  gate('default current profile without explicit alias preserves every original pin',()=>{const v=valid();delete v.profile.frozenExpectationToolOrigin;const r=resolveFrozenExpectationToolPins(v);assert.deepEqual(r.pins,inputs);assert.equal(r.aliases.length,0);});
  reject('primary acceptance cannot opt into frozen archive',v=>{v.profile.currentPrimaryAccepted=true;},'EXPLICIT_FROZEN_FRAME');
  reject('unknown execution frame rejected',v=>{v.profile.executionFrame='current';},'EXPLICIT_FROZEN_FRAME');
  reject('same repository cannot be origin replay',v=>{v.profile.frozenExpectationOriginRepository=repo;},'ORIGIN_REPOSITORY');
  reject('another tool cannot be relocated',v=>{v.profile.frozenExpectationToolOrigin.path=path.join(originRepo,'tools/other.json');},'ONLY_EXPECTATION_ORIGIN');
  reject('wrong archived bytes expected hash rejected',v=>{v.profile.expectations.sha256='0'.repeat(64);},'SAME_BYTES_EXACT_ARCHIVE');
  reject('archive cannot be outside exact repository expectation path',v=>{v.profile.expectations.path=path.join(owned,'outside',relative);},'SAME_BYTES_EXACT_ARCHIVE');
  reject('origin without historical authority rejected',v=>{v.authorityTools=[];},'HISTORICAL_AUTHORITY_TOOL');
  reject('duplicate origin authority rejected',v=>{v.authorityTools.push(structuredClone(original));},'HISTORICAL_AUTHORITY_TOOL');
  reject('origin omitted from actual inputs rejected',v=>{v.pins=v.pins.filter(p=>p.path!==originPath);},'EXACT_ORIGINAL_INPUT');
  reject('second origin input rejected',v=>{v.pins.push(structuredClone(original));},'EXACT_ORIGINAL_INPUT');
  reject('different actual origin hash rejected',v=>{v.pins[0].sha256='0'.repeat(64);},'EXACT_ORIGINAL_INPUT');
  reject('pin cannot smuggle unknown fields',v=>{v.profile.frozenExpectationToolOrigin.allowAll=true;},'EXACT_PIN_SHAPE');
  reject('wrong archive file digest rejected even matching declared origin',v=>{v.profile.expectations.sha256='0'.repeat(64);v.profile.frozenExpectationToolOrigin.sha256='0'.repeat(64);v.pins[0].sha256='0'.repeat(64);v.authorityTools[0].sha256='0'.repeat(64);},'ARCHIVE_BYTES');
  reject('conflicting archive pin refused',v=>{v.pins[1].sha256='0'.repeat(64);},'ARCHIVE_PIN_CONFLICT');
  gate('artifact mapping rejects same path wrong hash',()=>assert.throws(()=>frozenExpectationArtifactPath({path:originPath,sha256:'0'.repeat(64)},[{original,archive}]),/ARTIFACT_IDENTITY/));
  gate('unrelated artifact path stays original',()=>assert.equal(frozenExpectationArtifactPath(other,[{original,archive}]),other.path));
  // Controls exercise concrete exported-helper and immutable-binding boundaries.
  reject('conflicting second authority hash rejected even with one matching row',v=>{v.authorityTools.push({path:originPath,sha256:'0'.repeat(64)});},'HISTORICAL_AUTHORITY_TOOL');
  gate('forged product-source artifact alias rejected',()=>assert.throws(()=>frozenExpectationArtifactPath(other,[{original:other,archive:{path:path.join(repo,'native/redirected.cpp'),sha256:other.sha256}}]),/LIVE_ALIAS_REQUIRED/));
  gate('forged expectation artifact alias cannot bypass explicit resolver admission',()=>assert.throws(()=>frozenExpectationArtifactPath(original,[{original,archive}]),/LIVE_ALIAS_REQUIRED/));
  gate('live archive alias cannot be retargeted after validation',()=>{const r=resolveFrozenExpectationToolPins(valid());assert.throws(()=>{r.aliases[0].archive.path=other.path;},TypeError);assert.equal(frozenExpectationArtifactPath(original,r.aliases),archive.path);});
  gate('diagnostic alias clone retains provenance but grants no mapping authority',()=>{const r=resolveFrozenExpectationToolPins(valid()),copy=structuredClone(r.aliases);assert.deepEqual(copy[0].original,original);assert.throws(()=>frozenExpectationArtifactPath(original,copy),/LIVE_ALIAS_REQUIRED/);});
  gate('default current stale origin is still rejected by strict file digest checks',()=>{const v=valid();delete v.profile.frozenExpectationToolOrigin;const r=resolveFrozenExpectationToolPins(v);assert.throws(()=>strictPins(r.pins));});
  gate('actual origin read count is zero despite changed live bytes',()=>{const readBefore=fs.readFileSync;let originReads=0;fs.readFileSync=function(file,...args){if(typeof file==='string'&&path.resolve(file).toLowerCase()===originPath.toLowerCase()){++originReads;throw Error('origin read forbidden');}return readBefore.call(fs,file,...args);};try{const r=resolveFrozenExpectationToolPins(valid());assert.equal(originReads,0);assert.equal(r.originFileRead,false);}finally{fs.readFileSync=readBefore;}});
  gate('missing historical origin does not cause a read or search fallback',()=>{fs.unlinkSync(originPath);const r=resolveFrozenExpectationToolPins(valid());assert.equal(r.aliases.length,1);assert.equal(r.originFileRead,false);});
  gate('later archive mutation remains detectable by the exact resolved pin',()=>{const r=resolveFrozenExpectationToolPins(valid());fs.writeFileSync(archivePath,'{"selfOwnedDrift":true}\n');assert.throws(()=>strictPins(r.pins));});
  gate('resolver does not mutate caller profile or historical input provenance',()=>{const v=valid(),snapshot=structuredClone(v);resolveFrozenExpectationToolPins(v);assert.deepEqual(v,snapshot);});
  reject('relative current repository refused',v=>{v.repo='relative-repo';},'ABSOLUTE_REPOSITORY');
  reject('relative historical repository refused',v=>{v.profile.frozenExpectationOriginRepository='relative-origin';},'ORIGIN_REPOSITORY');
  reject('nonboolean primary acceptance refused',v=>{v.profile.currentPrimaryAccepted='false';},'EXPLICIT_FROZEN_FRAME');
  reject('no B54 frame fallback',v=>{v.profile.executionFrame='B54_FIXED_POSITIVE';},'EXPLICIT_FROZEN_FRAME');
  reject('archive pin extra fields refused',v=>{v.profile.expectations.allowAll=true;},'EXACT_PIN_SHAPE');
  reject('control characters in an origin pin path refused',v=>{v.profile.frozenExpectationToolOrigin.path+='\n';},'EXACT_PIN_SHAPE');
  gate('archive directory refused as a metadata file',()=>{fs.unlinkSync(archivePath);fs.mkdirSync(archivePath);assert.throws(()=>resolveFrozenExpectationToolPins(valid()),/ARCHIVE_BYTES/);});
  gate('archive under a redirected repository ancestor refused',()=>{const link=path.join(owned,'redirected-repository');fs.symlinkSync(repo,link,process.platform==='win32'?'junction':'dir');const v=valid();v.repo=link;v.profile.expectations.path=path.join(link,relative);assert.throws(()=>resolveFrozenExpectationToolPins(v),/ARCHIVE_NO_REDIRECTION/);});
  if(manifestFile){
   gate('five exact integration hunks reverse to current G before bytes',()=>{const m=JSON.parse(fs.readFileSync(manifestFile,'utf8')),change=m.changes.find(c=>c.relative.endsWith('/r3-current-prepared.mjs'));let s=fs.readFileSync(change.after.path,'utf8');for(const h of [...change.hunks].reverse()){assert.equal(s.split(h.after).length,2);s=s.replace(h.after,()=>h.before);}assert.equal(sha(s),m.currentGBaseline.sha256);assert.equal(s,m.currentGBaseline.code);});
   gate('current managed-receipt registration and strict scopedPin path retained',()=>{const m=JSON.parse(fs.readFileSync(manifestFile,'utf8')),change=m.changes[0],s=fs.readFileSync(change.after.path,'utf8');assert(s.includes("'r3-frozen-expectation-tool.mjs','r3-current-managed-receipt.mjs','r3-current-routing.mjs'"));assert(s.includes('for(const row of pinned)scopedPin(row)'));assert(s.includes('after.actualTools.map(r=>frozenExpectationArtifactPath(r,frozenTools.aliases))'));});
  }
  if(legacy){
   gate('stage control reproduces forged artifact mapping while candidate rejects it',()=>{const aliases=[{original:other,archive:{path:path.join(repo,'native/redirected.cpp'),sha256:other.sha256}}];assert.equal(legacy.frozenExpectationArtifactPath(other,aliases),aliases[0].archive.path);assert.throws(()=>frozenExpectationArtifactPath(other,aliases),/LIVE_ALIAS_REQUIRED/);});
   gate('stage control reproduces mutable archive pointer while candidate freezes it',()=>{const a=legacy.resolveFrozenExpectationToolPins(valid());a.aliases[0].archive.path=other.path;assert.equal(legacy.frozenExpectationArtifactPath(original,a.aliases),other.path);const b=resolveFrozenExpectationToolPins(valid());assert.throws(()=>{b.aliases[0].archive.path=other.path;},TypeError);});
   gate('stage control reproduces contradictory authority acceptance while candidate refuses',()=>{const v=valid();v.authorityTools.push({path:originPath,sha256:'0'.repeat(64)});assert.doesNotThrow(()=>legacy.resolveFrozenExpectationToolPins(v));assert.throws(()=>resolveFrozenExpectationToolPins(v),/HISTORICAL_AUTHORITY_TOOL/);});
  }
 }finally{
  // Verify the absolute recursive-delete target remains the newly owned fixture root.
  const resolved=fs.realpathSync(owned);assert(inside(parent,resolved));assert(path.basename(resolved).startsWith('.r3-frozen-expectation-gates-'));assert.equal(resolved.toLowerCase(),path.resolve(owned).toLowerCase());assert(!fs.lstatSync(owned).isSymbolicLink());fs.rmSync(resolved,{recursive:true,force:true});assert(!fs.existsSync(owned));
 }
 return {schemaVersion:1,status:tests.every(t=>t.status==='PASS')?'PASS_FROZEN_EXPECTATION_SMALL_JSON_CONTROLS':'FAIL',passed:tests.filter(t=>t.status==='PASS').length,total:tests.length,original18NamesRetained:true,tests,selfOwnedFixtureBytes:Buffer.byteLength(bytes)+Buffer.byteLength(currentBytes),temporaryFixturesRemoved:true,originFileReadInAdmittedReplay:false,producerExecutions:0,compilers:0,targetExecutions:0,primaryOrOldStageWrites:0};
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const args=process.argv.slice(2);const options={};
 try{assert(args.length>=2&&args.length%2===0,'--temp-parent ABS_EXISTING [--manifest ABS_JSON] [--legacy-module ABS_MJS] required');for(let i=0;i<args.length;i+=2){const key={'--temp-parent':'tempParent','--manifest':'manifestFile','--legacy-module':'legacyModule'}[args[i]];assert(key&&!Object.hasOwn(options,key)&&path.isAbsolute(args[i+1]),'explicit unique absolute option');options[key]=args[i+1];}const result=await runFrozenExpectationToolSelftest(options);console.log(JSON.stringify(result,null,2));if(result.status==='FAIL')process.exitCode=1;}catch(error){console.error(error.stack);process.exitCode=1;}
}