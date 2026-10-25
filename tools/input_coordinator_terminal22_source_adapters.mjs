// Explicit newer compile guard only; this specification grants no runtime authority.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const profileFile=fileURLToPath(new URL('./ymcc_sim/global/controller-coordinator-pipe/r3-terminal22-native-source-profile.json',import.meta.url));
const PROFILE_SHA256='f6916a908291a9246d3638278fa6579e173330a4a1a4b1278ecff1a491ac8107';
export function terminal22SteamCompileSpec(sourceRoot){
 assert(typeof sourceRoot==='string'&&path.isAbsolute(sourceRoot),'TERMINAL22_ABSOLUTE_CAPTURE_SOURCE_REQUIRED');
 assert.equal(hash(fs.readFileSync(profileFile)),PROFILE_SHA256,'TERMINAL22_NATIVE_PROFILE_CHANGED');
 const profile=JSON.parse(fs.readFileSync(profileFile,'utf8'));
 assert.equal(profile.kind,'CURRENT105_TERMINAL22_STEAM_COMPILE_GUARD_V1');
 assert.equal(profile.managedInputContract,'current105-terminal22-v1');
 const baseline=fileURLToPath(new URL('./ymcc_sim/global/controller-coordinator-pipe/r3-current105-source-adapter-profile.json',import.meta.url));
 assert.equal(hash(fs.readFileSync(baseline)),profile.baselineProfileSha256,'TERMINAL22_BASELINE_PROFILE_CHANGED');
 for(const row of [profile.runtimeHeader,...profile.extraCompileHeaders]){
  assert(typeof row.relative==='string'&&!path.isAbsolute(row.relative)&&!row.relative.includes('..')&&!row.relative.includes('\\'),'TERMINAL22_NATIVE_HEADER_PATH_REQUIRED');
  assert.equal(hash(fs.readFileSync(path.join(sourceRoot,row.relative))),row.sha256,'TERMINAL22_REVIEWED_NATIVE_HEADER_CHANGED:'+row.relative);
 }
 return {steamRecovery:{runtimeHeader:{...profile.runtimeHeader,path:path.join(sourceRoot,profile.runtimeHeader.relative)},startSha256:profile.startSha256,bridgeSha256:profile.bridgeSha256}};
}
