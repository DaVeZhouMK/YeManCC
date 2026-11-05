// Execute complete current native request code over real private Windows pipes.
// Managed counterpart runs actual source-extracted pipe loop + HostSession, inert SDK.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
import {generateWireInput} from './ymcc_sim/global/controller-lifecycle-pipe/source-projection.mjs';
import {resolveWireBuild,wireChildEnvironment,wireCompilerPreamble} from './ymcc_sim/global/controller-lifecycle-pipe/wire-contract.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const options={};for(let i=2;i<process.argv.length;i++){const key=process.argv[i];assert(['--out','--managed-dll','--source','--dotnet','--compile-only','--expect-rejection'].includes(key)&&!Object.hasOwn(options,key),'invalid/duplicate option');options[key]=key==='--compile-only'?true:process.argv[++i];}
assert(options['--out']);const root=path.resolve(options['--out']);assert(!fs.existsSync(root),'Fresh output required');fs.mkdirSync(root,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),hash=file=>sha(fs.readFileSync(file));
const source=path.resolve(options['--source']||path.join(repo,'native/main.cpp'));
const headerPath=name=>name.startsWith('HcControllerFeedbackCapabilityCatalog')?path.join(repo,'capabilities',name):path.join(repo,'native',name);
const headerNames=['game_feedback_receipt.h','HcControllerFeedbackCapabilityCatalog.h','HcControllerFeedbackCapabilityCatalog.generated.h','input_host_pipe_transport.h','input_host_transaction_mutex.h','input_host_transport_state.h','input_host_compact_frame_reply.h','input_host_selftest_rules.h','input_frame_submission.h','virtual_output_status.h','controller_persona.h','canonical_frame_fingerprint_memo.h','msi_input_host_transport_evidence.h','msi_steamdeck_release_fence_1726.h','input_chain_command_evidence.h','input_chain_ack_evidence.h'];
const sources=[source,...headerNames.map(headerPath),path.join(repo,'deps/json/json.hpp'),path.join(repo,'tools/fixtures/input_lifecycle_integration/NativeEntry.cpp'),fileURLToPath(import.meta.url),...['tools/ymcc_sim/global/controller-lifecycle-pipe/source-projection.mjs','tools/ymcc_sim/global/controller-lifecycle-pipe/wire-contract.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs','tools/fixtures/hc_remaining_model_capabilities/expectations.mjs','tools/lib/hc_motion_matrix_guard.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'].map(p=>path.join(repo,p))].map(p=>({path:p,sha256:hash(p)}));
const {cpp,bindings}=generateWireInput(fs.readFileSync(source,'utf8'),fs.readFileSync(path.join(repo,'tools/fixtures/input_lifecycle_integration/NativeEntry.cpp'),'utf8'));
const inc=path.join(root,'source');fs.mkdirSync(inc);for(const name of headerNames)fs.copyFileSync(headerPath(name),path.join(inc,name));fs.copyFileSync(path.join(repo,'deps/json/json.hpp'),path.join(inc,'json.hpp'));
fs.writeFileSync(path.join(inc,'fixture.cpp'),cpp);fs.writeFileSync(path.join(root,'source-bindings.json'),JSON.stringify({sources,bindings},null,2));
const configurations=[],fixedBuild=resolveWireBuild(repo);
fs.writeFileSync(path.join(root,'fixed-build-contract.json'),JSON.stringify(fixedBuild,null,2)+'\n',{flag:'wx'});
for(const mode of ['Release','Checked']){
 const dir=path.join(root,mode);fs.mkdirSync(dir);const flags=mode==='Release'?'/O2 /MT /DNDEBUG':'/Od /MTd /RTC1 /D_DEBUG';
 const batch=path.join(dir,'compile.cmd');fs.writeFileSync(batch,wireCompilerPreamble(fixedBuild)+'chcp 65001 >nul\r\n\"'+fixedBuild.compiler+'\" /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /showIncludes /permissive- '+flags+' /I"'+inc+'" "'+path.join(inc,'fixture.cpp')+'" /Fo"'+path.join(dir,'fixture.obj')+'" /Fe"'+path.join(dir,'fixture.exe')+'" advapi32.lib ole32.lib /link /verbose:lib\r\nexit /b %errorlevel%\r\n');
 let p=spawnSync(fixedBuild.cmd,['/d','/c',batch],{cwd:dir,env:wireChildEnvironment(fixedBuild,dir),windowsHide:true,encoding:'utf8',timeout:60000});const compileText=(p.stdout||'')+(p.stderr||'');fs.writeFileSync(path.join(dir,'compile.log'),compileText);assert.equal(p.status,0,'native integration compilation '+mode+'; '+path.join(dir,'compile.log'));
 const includes=[...new Set([...compileText.matchAll(/^\s*(?:Note: including file:|注意:\s*包含文件:)\s*(.+?)\r?$/gm)].map(m=>path.resolve(m[1])))];assert(includes.length>20);const linkerLibraries=[...new Set([...compileText.matchAll(/\b[A-Z]:\\[^\r\n]*?\.lib\b/gi)].map(m=>path.resolve(m[0])))];assert(linkerLibraries.length>0,'actual linker library observation missing');
 if(options['--compile-only']){configurations.push({mode,status:'COMPILE_ONLY_NOT_ACCEPTANCE',includes,linkerLibraries,executableSha256:hash(path.join(dir,'fixture.exe'))});continue;}
 assert(options['--managed-dll']&&fs.existsSync(options['--managed-dll']),'compiled managed fixture required');
 const run=path.join(dir,'run');fs.mkdirSync(run);p=spawnSync(path.join(dir,'fixture.exe'),[options['--dotnet']||'C:/Program Files/dotnet/dotnet.exe',path.resolve(options['--managed-dll']),run],{cwd:dir,env:wireChildEnvironment(fixedBuild,dir),windowsHide:true,encoding:'utf8',timeout:90000,maxBuffer:4*1024*1024});fs.writeFileSync(path.join(dir,'stdout.log'),p.stdout||'');fs.writeFileSync(path.join(dir,'stderr.log'),p.stderr||'');
 let result;try{result=JSON.parse(fs.readFileSync(path.join(run,'result.json'),'utf8'));}catch{try{result=JSON.parse(p.stdout);}catch{result={status:'FAIL',error:'no complete consumer receipt'};}}
 const expected=options['--expect-rejection'];
 if(expected){assert(p.status===1&&result.status==='FAIL'&&result.error.includes(expected),'semantic counterexample not rejected by original assertion');}
 else assert(p.status===0&&result.status==='PASS'&&result.passed&&result.cases.length===16,'native/managed integrated assertions failed; '+path.join(dir,'stderr.log'));
 configurations.push({mode,status:expected?'EXPECTED_SEMANTIC_REJECTION':'PASS',includes,linkerLibraries,exitCode:p.status,result,executableSha256:hash(path.join(dir,'fixture.exe'))});console.log(mode+': '+configurations.at(-1).status+' '+(result.checks??0)+' checks');
}
const changed=sources.filter(s=>hash(s.path)!==s.sha256);assert(changed.length===0,'inputs changed during integration run');
const result={schemaVersion:1,status:options['--compile-only']?'COMPILE_ONLY_NOT_ACCEPTANCE':'PASS',passed:!options['--compile-only'],expectedSemanticRejection:options['--expect-rejection']||null,sources,sourceUnchanged:true,fixedBuildContract:fixedBuild,configurations,managedDll:options['--managed-dll']?{path:path.resolve(options['--managed-dll']),sha256:hash(options['--managed-dll'])}:null,boundary:{normalProductMainExecuted:false,sourceExactNativeRequest:true,sourceExactManagedSession:true,realPrivateWindowsPipes:true,realSdkLoaded:false,deviceIo:false,normalCaptureFrameTicketsArmed:false,deferredAndOneXStartWriteBoundary:'actual-null-TLS;opaque-owner;deadline-entry-trap',fullNativeCoordinatorLifecycleExecuted:false,physicalReturnValidated:false}};
fs.writeFileSync(path.join(root,'RESULT.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({status:result.status,configurations:configurations.map(c=>({mode:c.mode,status:c.status}))}));
