import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {createRequire} from 'node:module';
import {loadUiBuildTools} from './build-tools.mjs';
import {REPO,toolchain,hash,write,verifyTools} from './contract.mjs';
import {nodeBuildConfiguration} from './snapshot-node-binding.mjs';
function root(t){const p=fs.mkdtempSync(path.join(os.tmpdir(),'ymcc-ui-node-loader-'));t.after(()=>{assert.equal(path.dirname(path.resolve(p)),path.resolve(os.tmpdir()));assert.ok(path.basename(p).startsWith('ymcc-ui-node-loader-'));assert.equal(fs.realpathSync(p).toLowerCase(),path.resolve(p).toLowerCase());fs.rmSync(p,{recursive:true,force:true});});return p;}
test('primary uses original pinned package/tool closure and unchanged package configuration',t=>{const r=root(t),tool=toolchain(),artifactSnapshot=path.join(r,'tools.json');write(artifactSnapshot,{files:tool.files});const cfg={...nodeBuildConfiguration(tool,REPO),artifactSnapshot,artifactSnapshotSha256:hash(artifactSnapshot),browserModuleRoot:tool.browserModuleRoot};assert.equal(tool.externalCount,2362);assert.equal(tool.nodeToolBinding,undefined);const loaded=loadUiBuildTools(cfg);assert.equal(typeof loaded.build,'function');assert.equal(typeof loaded.parse,'function');assert.equal(typeof loaded.compileScript,'function');assert.equal(typeof loaded.compileStyle,'function');assert.ok(loaded.chromium);assert.deepEqual(loaded.nodePaths,[path.join(REPO,'node_modules')]);assert.equal(loaded.entries.length,4);});
test('Node entry must be declared by hash even when the file exists',t=>{const r=root(t),tool=toolchain(),artifactSnapshot=path.join(r,'tools.json'),req=createRequire(path.join(REPO,'package.json')),entry=fs.realpathSync(req.resolve('esbuild'));write(artifactSnapshot,{files:tool.files.filter(x=>path.resolve(x.path).toLowerCase()!==path.resolve(entry).toLowerCase())});assert.ok(fs.existsSync(entry));assert.throws(()=>loadUiBuildTools({...nodeBuildConfiguration(tool,REPO),artifactSnapshot,artifactSnapshotSha256:hash(artifactSnapshot),browserModuleRoot:tool.browserModuleRoot}),/NODE_ENTRY_UNDECLARED/);});
test('artifact-list bytes cannot be substituted after the compiler config is pinned',t=>{const r=root(t),artifactSnapshot=path.join(r,'tools.json');write(artifactSnapshot,{files:[]});const before=hash(artifactSnapshot);fs.appendFileSync(artifactSnapshot,' ');assert.throws(()=>loadUiBuildTools({artifactSnapshot,artifactSnapshotSha256:before}),/ARTIFACT_SNAPSHOT_CHANGED/);});
