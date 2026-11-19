// Prepare immediately before compiling main.cpp. Force-include the generated
// header only into that translation unit. Seal AFTER link AND manifest embedding;
// never pair a fresh pending identity with an old EXE after a failed build.
// The random identity is per actual native build, not a package/version/PID/time.
// Repacking or reinstalling the same binary keeps the same identity. Consumers
// verify the executing literal and keep permanent seen/used history for grants.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
export const prefix='YMCC-NATIVE-BUILD:v1:';
const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const hex=value=>typeof value==='string'&&/^[a-f0-9]{64}$/.test(value);
function write(file,bytes){
  const temp=file+'.tmp-'+crypto.randomBytes(8).toString('hex');
  try{fs.writeFileSync(temp,bytes,{flag:'wx'});fs.renameSync(temp,file);}
  finally{if(fs.existsSync(temp))fs.unlinkSync(temp);}
}
function directory(value){assert(typeof value==='string'&&path.isAbsolute(value),'NATIVE_BUILD_ABSOLUTE_DIRECTORY_REQUIRED');return path.resolve(value);}
export function prepare(output){
  output=directory(output);fs.mkdirSync(output,{recursive:true});
  const nativeBuildId=crypto.randomBytes(32).toString('hex');
  const pending={schema:'ymcc.native-build.pending.v1',nativeBuildId};
  write(path.join(output,'native-build-identity.pending.json'),JSON.stringify(pending)+'\n');
  write(path.join(output,'native-build-identity.generated.h'),'#pragma once\n#define YMCC_NATIVE_BUILD_MARKER "'+prefix+nativeBuildId+'"\n');
  return pending;
}
export function inspect(executable){
  assert(typeof executable==='string'&&path.isAbsolute(executable),'NATIVE_BUILD_ABSOLUTE_EXE_REQUIRED');
  const info=fs.statSync(executable);assert(info.isFile()&&info.size<=256*1024*1024,'NATIVE_BUILD_EXE_SIZE_INVALID');
  const bytes=fs.readFileSync(executable);
  assert(bytes.length>=64&&bytes.readUInt16LE(0)===0x5a4d,'NATIVE_BUILD_PE_REQUIRED');
  const offset=bytes.readUInt32LE(0x3c);
  assert(offset>=64&&offset<=bytes.length-24&&bytes.readUInt32LE(offset)===0x4550&&bytes.readUInt16LE(offset+4)===0x8664,'NATIVE_BUILD_X64_PE_REQUIRED');
  const matches=[...bytes.toString('latin1').matchAll(/YMCC-NATIVE-BUILD:v1:([a-f0-9]{64})\x00/g)];
  const identities=[...new Set(matches.map(x=>x[1]))];
  assert(identities.length===1,'NATIVE_BUILD_EXACT_MARKER_REQUIRED');
  return {nativeBuildId:identities[0],nativeBytes:bytes.length,nativeSha256:digest(bytes)};
}
export function seal(output,executable){
  output=directory(output);
  const pending=JSON.parse(fs.readFileSync(path.join(output,'native-build-identity.pending.json'),'utf8'));
  assert(Object.keys(pending).sort().join(',')==='nativeBuildId,schema'&&pending.schema==='ymcc.native-build.pending.v1'&&hex(pending.nativeBuildId),'NATIVE_BUILD_PENDING_INVALID');
  const header=fs.readFileSync(path.join(output,'native-build-identity.generated.h'),'utf8');
  assert(header==='#pragma once\n#define YMCC_NATIVE_BUILD_MARKER "'+prefix+pending.nativeBuildId+'"\n','NATIVE_BUILD_HEADER_CHANGED');
  const facts=inspect(executable);assert(facts.nativeBuildId===pending.nativeBuildId,'NATIVE_BUILD_STALE_EXE');
  const receipt={schema:'ymcc.native-build-identity.v1',...facts};
  write(path.join(output,'native-build-identity.json'),JSON.stringify(receipt,null,2)+'\n');
  return receipt;
}
export function verify(receipt,executable,runningMarker){
  assert(receipt&&Object.keys(receipt).sort().join(',')==='nativeBuildId,nativeBytes,nativeSha256,schema'&&receipt.schema==='ymcc.native-build-identity.v1'&&hex(receipt.nativeBuildId)&&hex(receipt.nativeSha256)&&Number.isSafeInteger(receipt.nativeBytes)&&receipt.nativeBytes>0,'NATIVE_BUILD_RECEIPT_INVALID');
  const actual=inspect(executable);
  assert(actual.nativeBuildId===receipt.nativeBuildId&&actual.nativeBytes===receipt.nativeBytes&&actual.nativeSha256===receipt.nativeSha256,'NATIVE_BUILD_RECEIPT_MISMATCH');
  // A native-supplied marker establishes the executing build. Omitting this
  // argument deliberately verifies disk only; that cannot grant recovery.
  if(runningMarker!==undefined)assert(runningMarker===prefix+receipt.nativeBuildId,'NATIVE_BUILD_RUNNING_MARKER_MISMATCH');
  return {verified:true,scope:runningMarker===undefined?'disk-only':'native-running-marker-and-sealed-artifact',...actual};
}
if(process.argv[1]&&pathToFileURL(path.resolve(process.argv[1])).href===import.meta.url){
  const [operation,output,executable]=process.argv.slice(2);
  assert(['prepare','seal'].includes(operation)&&process.argv.length===(operation==='prepare'?4:5),'NATIVE_BUILD_ARGUMENTS_INVALID');
  console.log(JSON.stringify(operation==='prepare'?prepare(output):seal(output,executable)));
}
