import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import os from 'node:os';import path from 'node:path';import {createHash} from 'node:crypto';import {parseLifecycleLogData,readLifecycleLogData,LIFECYCLE_LOG_PRODUCER} from './lifecycle-log-data.mjs';
const body='record("owned");\nreturn;',spec={schemaVersion:1,relative:LIFECYCLE_LOG_PRODUCER,key:'appendNativeLifecycleLog',decodedUtf8Sha256:createHash('sha256').update(body).digest('hex'),decodedUtf8Bytes:Buffer.byteLength(body)},row="['appendNativeLifecycleLog',"+JSON.stringify(body)+']';
test('reads the exact known decoded C++ value without executing surrounding JS',()=>{const a=parseLifecycleLogData('throw Error("must not execute");\n'+row,spec),b=parseLifecycleLogData(row+'; optionalSuite("new case");',spec);assert.equal(a.body,body);assert.equal(b.body,body);assert.notEqual(a.wholeSourceSha256,b.wholeSourceSha256);assert.equal(a.decodedUtf8Sha256,b.decodedUtf8Sha256);assert.equal(a.producerExecuted,false);});
test('a different valid quoting of the same data retains the same value identity',()=>{const single="['appendNativeLifecycleLog','record(\"owned\");\\nreturn;']";assert.equal(parseLifecycleLogData(single,spec).body,body);});
for(const [name,source,pattern] of [
 ['missing row','const x=1;',/EXACTLY_ONE/],['duplicate row',row+';'+row,/EXACTLY_ONE/],
 ['template expression',"['appendNativeLifecycleLog',`anything`]",/QUOTED/],
 ['expression suffix',row.slice(0,-1)+' + execute()]',/SUFFIX/],
 ['changed body',"['appendNativeLifecycleLog','return;']",/DATA_VALUE/],
 ['unescaped newline',"['appendNativeLifecycleLog','bad\nbody']",/UNESCAPED/],
 ['unsupported escape',"['appendNativeLifecycleLog','\\q']",/UNSUPPORTED/],
 ['octal escape',"['appendNativeLifecycleLog','\\01']",/OCTAL/],
 ['bad hex',"['appendNativeLifecycleLog','\\xZZ']",/HEX/],
 ['unterminated',"['appendNativeLifecycleLog','bad",/UNTERMINATED/],
 ['body over bound',"['appendNativeLifecycleLog','"+'x'.repeat(65537)+"']",/BODY_TOO_LARGE/],
 ['source over bound','x'.repeat(4*1024*1024+1)+row,/PRODUCER_TOO_LARGE/],
])test(name+' is rejected before a value can authorize compilation',()=>assert.throws(()=>parseLifecycleLogData(source,spec),pattern));
test('wrong pin, length, producer path or row name cannot grant a value',()=>{for(const bad of [{...spec,decodedUtf8Sha256:'0'.repeat(64)},{...spec,decodedUtf8Bytes:1},{...spec,relative:'../other.mjs'},{...spec,key:'other'}])assert.throws(()=>parseLifecycleLogData(row,bad),/CURRENT_LOG_/);});
test('filesystem reader remains within the explicit producer path and does not import it',()=>{const root=fs.mkdtempSync(path.join(os.tmpdir(),'ymcc-log-data-'));fs.mkdirSync(path.join(root,'tools'));const marker=path.join(root,'unexpected.txt');fs.writeFileSync(path.join(root,LIFECYCLE_LOG_PRODUCER),'throw Error("do not run");\n'+row);assert.equal(readLifecycleLogData(root,spec).body,body);assert.equal(fs.existsSync(marker),false);assert.throws(()=>readLifecycleLogData('.',spec),/ABSOLUTE_ROOT/);});
