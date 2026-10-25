import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {mergeCurrentSourceArtifact} from './contract.mjs';
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const key=p=>path.resolve(p).toLowerCase();
function fixture(t,relative='tools/ymcc_sim/global/source-snapshot.mjs'){
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'ymcc-source-authority-'));
 t.after(()=>{assert.equal(path.dirname(path.resolve(root)),path.resolve(os.tmpdir()));assert(path.basename(root).startsWith('ymcc-source-authority-'));assert.equal(key(fs.realpathSync(root)),key(root));fs.rmSync(root,{recursive:true,force:true});});
 const file=path.join(root,relative);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,'actual-owned-test-source\n');
 const row={path:file,sha256:hash(file),role:'verified tool authority metadata'};
 return {root,file,relative,row,authority:{metadata:[{...row}]}};
}
test('an actual verifier identity keeps one row and both tool/source roles',t=>{
 const f=fixture(t),byPath=new Map([[key(f.file),{...f.row}]]);
 mergeCurrentSourceArtifact(byPath,{repo:f.root,relative:f.relative,authority:f.authority});
 assert.equal(byPath.size,1);const kept=byPath.get(key(f.file));assert.equal(kept.sha256,f.row.sha256);assert.equal(kept.role,f.row.role);assert.equal(kept.sourceArtifact.sha256,f.row.sha256);assert.equal(kept.sourceArtifact.path,f.file);
});
test('ordinary current source still appends without changing any tool row',t=>{
 const f=fixture(t,'native/consumer.h'),tool={path:path.join(f.root,'owned-tool.exe'),sha256:'a'.repeat(64)},byPath=new Map([[key(tool.path),tool]]);
 mergeCurrentSourceArtifact(byPath,{repo:f.root,relative:f.relative,authority:null});
 assert.equal(byPath.size,2);assert.strictEqual(byPath.get(key(tool.path)),tool);assert.equal(byPath.get(key(f.file)).sha256,hash(f.file));
});
test('same path with changed current source is rejected before map mutation',t=>{
 const f=fixture(t),byPath=new Map([[key(f.file),{...f.row}]]),before=byPath.get(key(f.file));fs.appendFileSync(f.file,'tamper\n');
 assert.throws(()=>mergeCurrentSourceArtifact(byPath,{repo:f.root,relative:f.relative,authority:f.authority}),/SOURCE_TOOL_HASH_CONFLICT/);assert.strictEqual(byPath.get(key(f.file)),before);
});
test('tool and authority hash conflicts are independently rejected',t=>{
 const f=fixture(t);
 for(const target of ['tool','authority']){
  const byPath=new Map([[key(f.file),{...f.row,sha256:target==='tool'?'0'.repeat(64):f.row.sha256}]]),authority={metadata:[{...f.row,sha256:target==='authority'?'1'.repeat(64):f.row.sha256}]};
  assert.throws(()=>mergeCurrentSourceArtifact(byPath,{repo:f.root,relative:f.relative,authority}),/SOURCE_TOOL_HASH_CONFLICT/);
 }
});
test('an ordinary source remains forbidden even with identical metadata identity',t=>{
 const f=fixture(t,'native/consumer.h'),byPath=new Map([[key(f.file),{...f.row}]]);
 assert.throws(()=>mergeCurrentSourceArtifact(byPath,{repo:f.root,relative:f.relative,authority:f.authority}),/SOURCE_TOOL_DUPLICATE/);
});
test('verifier path without declared authority remains forbidden',t=>{
 const f=fixture(t);
 for(const authority of [null,{metadata:[]}])assert.throws(()=>mergeCurrentSourceArtifact(new Map([[key(f.file),{...f.row}]]),{repo:f.root,relative:f.relative,authority}),/SOURCE_TOOL_DUPLICATE/);
});
