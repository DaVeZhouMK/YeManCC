import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {fileURLToPath,pathToFileURL} from 'node:url';
const here=path.dirname(fileURLToPath(import.meta.url)),canonicalRoot=path.resolve(here,'../../..');
const support='tools/fixtures/steam_settings_module_profile_preparation';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=(file,bytes=fs.readFileSync(file))=>({path:path.resolve(file),bytes:bytes.length,sha256:sha(bytes)});
export async function prepareModuleProfilePreparation({repoRoot,outputDir}){
 const repo=fs.realpathSync(path.resolve(repoRoot)),out=path.resolve(outputDir);
 assert(!fs.existsSync(out)||fs.readdirSync(out).length===0,'New/empty private output required');
 assert(out!==repo&&out!==canonicalRoot,'Private output directory required');
 const inputs=[];
 const save=(relative,b)=>{const file=path.join(out,relative);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,b,{flag:'wx'});return pin(file);};
 const freeze=(relative,source)=>{const b=fs.readFileSync(source),frozen=save(relative,b);inputs.push({source:pin(source,b),frozen});return b;};
 const main=freeze('source/native/main.cpp',path.join(repo,'native/main.cpp'));
 const runtime=freeze('source/native/steam_settings_runtime.h',path.join(repo,'native/steam_settings_runtime.h'));
 for(const relative of ['tools/steam_settings_batch_selftest.cpp','tools/steam_settings_batch_selftest.mjs',support+'/fixture.inc',support+'/prepare.mjs',support+'/before-queue.inc',support+'/before-queue.source.json','deps/json/json.hpp','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs'])freeze(relative,path.join(canonicalRoot,relative));
 const {extract}=await import(pathToFileURL(path.join(canonicalRoot,'tools/fixtures/legion_motion_consumer/extract_main.mjs')));
 const full=(b,name)=>{const e=extract(b.toString('utf8'),name);assert.equal(b.subarray(e.startUtf8Byte,e.endUtf8ByteExclusive).toString('utf8'),e.code);return e;};
 for(const relative of ['native/log_frequency_policy.h','native/log_snapshot_state.h'])freeze(relative,path.join(repo,relative));
 const originalResource=fs.readFileSync(path.join(here,'before-queue.inc'));
 const provenance=JSON.parse(fs.readFileSync(path.join(here,'before-queue.source.json'),'utf8'));
 assert.equal(provenance.kind,'pinned-original-steam-settings-queue');assert.equal(originalResource.length,provenance.resourceBytes);assert.equal(sha(originalResource),provenance.resourceSha256);
 const beforeQueue=full(originalResource,'steamSettingsQueue');assert.equal(beforeQueue.sha256,provenance.functionSha256);assert.equal(Buffer.byteLength(beforeQueue.code),provenance.functionBytes);
 const helper=full(main,'nativeSteamPrepareSessionModuleProfile'),queue=full(runtime,'steamSettingsQueue');
 const classifier=full(main,'nativeLifecycleEventIsBase'),streaming=full(main,'nativeLifecycleRecordIsStreaming');
 const generated=[save('module-profile-helper.inc',helper.code+'\n'),save('module-profile-queue.inc','#if defined(YMCC_MODULE_PROFILE_BEFORE)\n'+beforeQueue.code+'\n#else\n'+queue.code+'\n#endif\n')];
 const text=runtime.toString('utf8'),end=text.indexOf('static unsigned long long g_steamOverlayPinnedSerial');assert(end>0);generated.push(save('module-profile-state.inc',text.slice(0,end)));
 generated.push(save('module-profile-log-classifier.inc',classifier.code+'\n'+streaming.code+'\n'));
 const program=path.join(out,'fixture.exe');
 const result={schemaVersion:1,status:'PREPARED_SOURCE_ONLY',canonical:'steam-settings-batch',selection:'--module-profile-preparation',program,args:['--module-profile-preparation'],expectedExitCode:0,
  negativeControl:{program,args:['--module-profile-preparation-before-control'],expectedExitCode:1,additionalCompileDefine:'/DYMCC_MODULE_PROFILE_BEFORE=1',expectedAssertion:'original-queue-must-forward-session-preparation-flag'},
  sources:inputs,generated,extracted:[helper,queue,beforeQueue,classifier,streaming].map(({code,...e})=>e),heldBeforeResource:provenance,
  mainSessionBridge:{candidateContainsExactCall:main.includes(Buffer.from('steamSettingsQueue("steam-session",sessionChanged);')),executionScope:'Complete helper and queue, controlled scope/prepare leaves; full observer callback not executed'},
  compile:{program:'cl.exe',cwd:out,args:['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DYMCC_STEAM_MODULE_PROFILE_PREPARATION_ONLY=1','/I'+out,'/I'+path.join(out,'deps/json'),'/sourceDependencies',path.join(out,'fixture.dependencies.json'),path.join(out,'tools/steam_settings_batch_selftest.cpp'),'/Fo'+path.join(out,'fixture.obj'),'/Fe'+program,'/link','/VERBOSE:LIB','kernel32.lib']},
  sourceInputs:[...inputs.map(x=>x.frozen),...generated],originalLimitsMs:{compile:120000,native:120000,outerOwned:120000},defaultGroupExecuted:false,newRegistrationId:null,minimumChanged:false};
 assert(result.mainSessionBridge.candidateContainsExactCall);save('SOURCE-INPUTS.json',JSON.stringify(result,null,2)+'\n');return result;
}
