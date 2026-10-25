import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import {resolveTdpToolchain} from './tool-scope.mjs';
function fixture(t){
 const parent=fs.realpathSync(os.tmpdir()),root=fs.mkdtempSync(path.join(parent,'ymcc-tdp-tools-'));
 t.after(()=>{assert.equal(path.dirname(fs.realpathSync(root)),parent);assert(path.basename(root).startsWith('ymcc-tdp-tools-'));fs.rmSync(root,{recursive:true,force:true});});
 const relative='node_modules/esbuild/lib/main.js',file=path.join(root,relative);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,'reviewed compiler bytes');
 const pin=crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const old=path.join(root,'previous-checkout',relative);
 const manifest={schemaVersion:1,esbuildModule:old,esbuildModuleRepoRelative:relative,files:[{path:old,repoRelative:relative,sha256:pin}]};
 return {root,relative,file,manifest,pin};
}
test('reviewed tool pins relocate to a separate checkout without requiring its old directory',t=>{const f=fixture(t),before=JSON.stringify(f.manifest),resolved=resolveTdpToolchain(f.root,f.manifest);assert.equal(resolved.esbuildModule,fs.realpathSync(f.file));assert.equal(resolved.files.length,f.manifest.files.length);assert.equal(resolved.files[0].sha256,f.pin);assert.equal(resolved.files[0].path,fs.realpathSync(f.file));assert.equal(JSON.stringify(f.manifest),before);assert.equal(fs.existsSync(f.manifest.esbuildModule),false);});
test('corrupted relocated bytes fail the original reviewed pin',t=>{const f=fixture(t);fs.writeFileSync(f.file,'changed compiler bytes');assert.throws(()=>resolveTdpToolchain(f.root,f.manifest),/RELOCATED_TOOL_BYTES/);});
test('an extra stale pin remains mandatory',t=>{const f=fixture(t);f.manifest.files.push({path:f.file,sha256:'0'.repeat(64)});assert.throws(()=>resolveTdpToolchain(f.root,f.manifest),/RELOCATED_TOOL_BYTES/);});
test('traversal, rooted paths, ADS and empty components are rejected',t=>{const f=fixture(t);for(const p of ['node_modules/../outside','node_modules//esbuild/lib/main.js','node_modules/esbuild/lib/main.js:stream',path.resolve(f.file)]){const m=structuredClone(f.manifest);m.files[0].repoRelative=p;assert.throws(()=>resolveTdpToolchain(f.root,m),/RELOCATED_TOOL_PATH/);}});
test('relative aliases must name the original reviewed dependency suffix',t=>{const f=fixture(t);f.manifest.files[0].repoRelative='node_modules/esbuild/package.json';assert.throws(()=>resolveTdpToolchain(f.root,f.manifest),/RELOCATED_TOOL_DECLARATION/);});
test('dependency junction escape is rejected even with matching bytes',t=>{const f=fixture(t),outside=path.join(f.root,'outside');fs.mkdirSync(outside);fs.writeFileSync(path.join(outside,'main.js'),'reviewed compiler bytes');const link=path.join(f.root,'node_modules','escape');fs.symlinkSync(outside,link,process.platform==='win32'?'junction':'dir');const rel='node_modules/escape/main.js';f.manifest.files[0]={...f.manifest.files[0],path:path.join(f.root,'previous',rel),repoRelative:rel};assert.throws(()=>resolveTdpToolchain(f.root,f.manifest),/RELOCATED_TOOL_ESCAPE/);});
test('legacy absolute descriptors keep their original byte obligations',t=>{const f=fixture(t);const legacy={schemaVersion:1,files:[{path:f.file,sha256:f.pin}]};assert.equal(resolveTdpToolchain(f.root,legacy).files[0].path,f.file);fs.writeFileSync(f.file,'changed');assert.throws(()=>resolveTdpToolchain(f.root,legacy),/RELOCATED_TOOL_BYTES/);});