import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
export const entryProductSources=Object.freeze(['gameInputOverride','settingsRepository','deckyGlobalActions','virtualTargetPolicy','gyroPresetModel','deckyInputOptions','virtualTargetPolicyState','ipc','controllerPersona','api','inputConfigHash','hcInputUtils','quickAppDefaults','settingsSnapshot','inputStartupPolicy','settingsBaseline'].map(n=>'src/bridge/'+n+'.ts').concat('PowerControl/settings-baseline.json'));
export const toolchainManifest=path.join(root,'tools/ymcc_sim/global/controller-input-entry-lifecycle/toolchain-inputs.json');
export function entryToolchainFiles(repo=root){const spec=JSON.parse(fs.readFileSync(path.join(repo,'tools/ymcc_sim/global/controller-input-entry-lifecycle/toolchain-inputs.json'),'utf8'));return spec.files.map(r=>({...r,path:r.repoRelative?path.join(repo,r.repoRelative):path.resolve(r.path)}));}
export function verifyEntryToolchain(){const files=entryToolchainFiles();assert(files.length>0&&files.length<64);for(const r of files){const b=fs.readFileSync(r.path);assert.equal(crypto.createHash('sha256').update(b).digest('hex'),r.sha256,'Entry tool dependency changed: '+r.path);assert.equal(b.length,r.bytes);}const exe=files.find(r=>r.role==='node'),binary=files.find(r=>r.role==='esbuild-binary');assert(exe&&binary);assert.equal(path.resolve(process.execPath).toLowerCase(),path.resolve(exe.path).toLowerCase(),'Use pinned Node');if(process.env.ESBUILD_BINARY_PATH)assert.equal(path.resolve(process.env.ESBUILD_BINARY_PATH).toLowerCase(),path.resolve(binary.path).toLowerCase(),'Foreign esbuild override refused');return files;}
