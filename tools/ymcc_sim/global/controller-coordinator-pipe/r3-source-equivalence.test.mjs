import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import test from 'node:test';import assert from 'node:assert/strict';
import {R3_LAYOUT,verifyManagedSourceRows,resolveR3ManifestPin,fileHash,verifyManagedBuilderAdapters} from './r3-source-equivalence.mjs';
import {resolveEquivalenceContext} from './r3-maintenance-context.mjs';
const {manifest,repo,value,foreign}=resolveEquivalenceContext();
const changed=edit=>{const copy=structuredClone(value);edit(copy);return copy;};
test('all20 actual original sources, snapshots and current member bytes agree',()=>{const rows=verifyManagedSourceRows(value,repo);assert.equal(rows.length,20);assert(rows.every(r=>r.actualOriginalPath&&r.actualSnapshot&&r.currentPath));});
test('same20 member bytes at another source root are verified without rewriting original paths',()=>{const rows=verifyManagedSourceRows(value,foreign);assert.equal(rows.length,20);assert(rows.some(r=>path.resolve(r.currentPath)!==path.resolve(r.actualOriginalPath)));assert.deepEqual(value.inputBindings.sources.map(r=>r.path),rows.map(r=>r.actualOriginalPath));});
test('a missing source member cannot qualify by count-free equivalence',()=>assert.throws(()=>verifyManagedSourceRows(changed(v=>v.inputBindings.sources.pop()),repo),/EXACT20_SOURCE/));
test('a duplicate member cannot replace a required source member',()=>assert.throws(()=>verifyManagedSourceRows(changed(v=>v.inputBindings.sources[1]=structuredClone(v.inputBindings.sources[0])),repo),/EXACT20_SOURCE_MEMBERSHIP/));
for(const relative of ['../InputHost/Program.cs','InputHost/../Program.cs','C:/foreign.cs','/foreign.cs','InputHost//Program.cs','InputHost\\Program.cs'])test('escaping/noncanonical relative is rejected: '+relative,()=>assert.throws(()=>verifyManagedSourceRows(changed(v=>v.inputBindings.sources[0].relative=relative),repo),/STRICT_RELATIVE_PATH/));
test('same-sized unknown member list is rejected',()=>assert.throws(()=>verifyManagedSourceRows(changed(v=>v.inputBindings.sources[0].relative='InputHost/Foreign.cs'),repo),/EXACT20_SOURCE_MEMBERSHIP/));
test('changed current source bytes cannot borrow a matching compiled manifest',()=>{const target=path.resolve(foreign,value.inputBindings.sources[0].relative);assert.throws(()=>verifyManagedSourceRows(value,foreign,{hashFile:p=>path.resolve(p)===target?'0'.repeat(64):fileHash(p)}),/CURRENT_SOURCE_CHANGED/);});
test('changed original source cannot borrow a matching current/snapshot copy',()=>{const target=path.resolve(value.inputBindings.sources[0].path);assert.throws(()=>verifyManagedSourceRows(value,foreign,{hashFile:p=>path.resolve(p)===target?'0'.repeat(64):fileHash(p)}),/ORIGINAL_SOURCE_CHANGED/);});
test('changed controlled compile snapshot is rejected',()=>{const target=path.resolve(value.inputBindings.sources[0].snapshot);assert.throws(()=>verifyManagedSourceRows(value,foreign,{hashFile:p=>path.resolve(p)===target?'0'.repeat(64):fileHash(p)}),/SNAPSHOT_CHANGED/);});
test('source manifest hash cannot be substituted even if all20 sources are the same',()=>{const pin={manifest:path.relative(R3_LAYOUT.workspaceRoot,manifest).replaceAll('\\','/'),sha256:'0'.repeat(64)};assert.throws(()=>resolveR3ManifestPin(pin),/MANIFEST_IDENTITY_MISMATCH/);});
test('foreign artifact root is rejected without weakening the Build root gate',()=>assert.throws(()=>resolveR3ManifestPin({manifest:'Build/Validation/Foreign/fixture-manifest.json',sha256:'0'.repeat(64)}),/MANIFEST_OUTSIDE_DECLARED_BUILD/));
test('executed output-root builder adapter and exact baseline are both bound',()=>assert.equal(verifyManagedBuilderAdapters(value,repo).length,1));


// A full source admission belongs to one call, rather than each of twenty rows.
// All row, original/snapshot/current hashes and path checks still execute below.
test('one managed20 validation performs one source admission and still checks every row',()=>{
 const original=fs.readFileSync,expected=path.resolve(R3_LAYOUT.fixedRunInputBinding.path).toLowerCase();let admissions=0;
 fs.readFileSync=function(file,...args){if(typeof file==='string'&&path.resolve(file).toLowerCase()===expected)admissions++;return original.call(fs,file,...args);};
 try{const rows=verifyManagedSourceRows(value,repo);assert.equal(rows.length,20);assert.equal(admissions,2,'one hash read plus one JSON read for the single admission');for(let i=0;i<20;i++)assert.equal(rows[i].sha256,value.inputBindings.sources[i].sha256);}
 finally{fs.readFileSync=original;}
});
