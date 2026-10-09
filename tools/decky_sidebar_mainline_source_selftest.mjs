// Mainline original-preservation proof; no reference to an alternate maintained checkout.
import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {root,task} from './fixtures/decky_task_paths.mjs';
const sha=value=>crypto.createHash('sha256').update(value).digest('hex');
const proof21=path.join(task,'validation/MAINLINE21-NATIVE-UNCHANGED.json');
if(fs.existsSync(proof21)){const before=JSON.parse(fs.readFileSync(proof21,'utf8'));assert.equal(sha(fs.readFileSync(before.source)),before.beforeSha256);fs.writeFileSync(path.join(task,'validation/MAINLINE21-NATIVE-UNCHANGED-VERIFIED.json'),JSON.stringify({...before,verified:true,nativeEditsByGyroWork:0},null,2));console.log('Mainline21 native current source unchanged by gyro work');process.exit(0);}
const stage20=fs.existsSync(path.join(task,'validation/MAINLINE20-NATIVE-ADOPTION.json'));
const proofFile=stage20?'validation/MAINLINE20-NATIVE-ADOPTION.json':fs.existsSync(path.join(task,'validation/MAINLINE19-NATIVE-ADOPTION.json'))?'validation/MAINLINE19-NATIVE-ADOPTION.json':'validation/MAINLINE15-NATIVE-ADOPTION.json';
const proof=JSON.parse(fs.readFileSync(path.join(task,proofFile),'utf8'));
const stage19=proofFile.includes('MAINLINE19');
let body=fs.readFileSync(path.join(root,'native/main.cpp'));
assert.equal(sha(body),proof.candidateSha256);assert.equal(proof.insertions.length,12);
for(const entry of [...proof.insertions].reverse()){assert.equal(sha(body.subarray(entry.offset,entry.offset+entry.bytes)),entry.sha256);body=Buffer.concat([body.subarray(0,entry.offset),body.subarray(entry.offset+entry.bytes)]);}
assert.equal(sha(body),proof.currentOriginal.sha256);
assert.equal(sha(fs.readFileSync(proof.currentOriginal.path)),proof.currentOriginal.sha256);
const runtime=fs.readFileSync(path.join(root,'native/decky_sidebar_runtime.h'),'utf8');
assert.ok(runtime.includes('bool requestBindingRefresh('));assert.ok(runtime.includes('launchIntent_'));assert.ok(!runtime.includes('setInterval'));
const report={project:'YMCC Decky 侧边栏',stage:stage20?'Mainline20':stage19?'Mainline19':'Mainline15',originalNativeBodyByteIdentical:true,insertions:12,mainlineSourceRoot:root,alternateMaintainedSource:false,actualSteam:false};
fs.writeFileSync(path.join(task,stage20?'validation/MAINLINE20-SOURCE-PRESERVATION.json':stage19?'validation/MAINLINE19-SOURCE-PRESERVATION.json':'validation/MAINLINE15-SOURCE-PRESERVATION.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
