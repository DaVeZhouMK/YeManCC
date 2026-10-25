// Rebind existing source-built R3 native fixtures. This tool never compiles or executes a binary.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {fileURLToPath} from 'node:url';
import {LIFECYCLE_VERSION,R3_SPEC_FILE,R3_SPEC,hash,need} from './r3-contract.mjs';
import {R3_LAYOUT} from './r3-source-equivalence.mjs';
export function bindR3Fixture({repoRoot,buildRoot,mode='pipe',behaviorProof}){
 const workspace=path.resolve(R3_LAYOUT.workspaceRoot),root=path.resolve(buildRoot);need(['pipe','lifecycle'].includes(mode),'R3_BIND_MODE');
 const sourceManifest=path.join(root,'source-manifest.json'),sm=JSON.parse(fs.readFileSync(sourceManifest,'utf8')),proofFile=path.resolve(behaviorProof??path.join(root,'RESULT.json')),proof=JSON.parse(fs.readFileSync(proofFile,'utf8'));
 need(proof.sourceFullTranslationUnit===true||proof.sameCompiledNativeAsPipe,'R3_FULL_NATIVE_COMPILE_PROOF');
 const nativeProof=JSON.parse(fs.readFileSync(path.join(root,'RESULT.json'),'utf8'));
 const tuple=mode==='lifecycle'&&!nativeProof.mutation?proof.results?.find(r=>r.mode==='tuple-protection')?.result:null;
 if(mode==='lifecycle'&&!nativeProof.mutation)need(tuple?.status==='PASS'&&tuple.cases?.length>0&&tuple.forbiddenIo===0,'R3_TUPLE_CONSUMER_EXECUTION_REQUIRED');
 need(nativeProof.changed?.length===0&&nativeProof.sourceFullTranslationUnit===true&&nativeProof.normalProductMainExecuted===false&&nativeProof.realDeviceIo===false,'R3_STABLE_COMPILE_BOUNDARY');
 need(fs.existsSync(path.join(root,'CoordinatorFixture.exe'))&&sm.observedIncludePaths?.length>20&&sm.compiledGenerated?.length>0,'R3_COMPILE_NOT_COMPLETE');
 for(const r of sm.records)need(hash(path.join(repoRoot,r.path))===r.sha256&&hash(r.snapshot)===r.sha256,'R3_BIND_SOURCE_CHANGED:'+r.path);
 for(const r of sm.compiledGenerated)need(hash(r.path)===r.sha256,'R3_BIND_GENERATED_CHANGED:'+r.path);
 const header=sm.records.find(r=>r.path==='native/virtual_start_protection.h');need(header&&fs.readFileSync(header.snapshot,'utf8').includes('lifecycleVersion = "'+LIFECYCLE_VERSION+'"'),'R3_ACTUAL_CLASS_VERSION_REQUIRED');
 const generatedFiles=[path.join(root,'CoordinatorFixture.exe'),sourceManifest,proofFile,path.join(root,'RESULT.json'),...sm.compiledGenerated.map(r=>r.path),...sm.fragments.map(r=>r.file),path.join(root,'fixture.cpp'),path.join(root,'build.cmd'),path.join(root,'build.log')];
 for(const name of ['pipe_cases.inc','ps_wake_cases.inc','tuple_recovery_cases.inc'])if(fs.existsSync(path.join(root,name)))generatedFiles.push(path.join(root,name));
 const generated=[...new Set(generatedFiles)].map(p=>({path:p,sha256:hash(p)}));
 const compilerIncludes=sm.observedIncludePaths.filter(p=>/^c:[\\/]/i.test(p)).map(p=>({path:p,sha256:hash(p)}));
 const vc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC';const versions=fs.readdirSync(path.join(vc,'Tools/MSVC')).filter(n=>/^\d+\.\d+\.\d+$/.test(n)).sort((a,b)=>b.localeCompare(a,undefined,{numeric:true}));need(versions.length>0,'R3_COMPILER_VERSION_REQUIRED');
 const compilerInputs=['cl.exe','link.exe','c1xx.dll','c2.dll','mspdbcore.dll'].map(n=>path.resolve(vc,'Tools/MSVC',versions[0],'bin/Hostx64/x64',n));compilerInputs.push(path.join(vc,'Auxiliary/Build/vcvars64.bat'),path.join(repoRoot,'deps/webview2/build/native/x64/WebView2LoaderStatic.lib'));
 const cmd=fs.readFileSync(path.join(root,'build.cmd'),'utf8'),sdkVersion='10.0.26100.0',libRoots=[path.resolve(vc,'Tools/MSVC',versions[0],'lib/x64'),path.resolve('C:/Program Files (x86)/Windows Kits/10/Lib',sdkVersion,'um/x64'),path.resolve('C:/Program Files (x86)/Windows Kits/10/Lib',sdkVersion,'ucrt/x64')];
 for(const match of cmd.matchAll(/\b([A-Za-z][A-Za-z0-9_]*\.lib)\b/g)){const name=match[1];if(name.toLowerCase()==='webview2loaderstatic.lib')continue;const found=libRoots.map(r=>path.join(r,name)).find(fs.existsSync);need(found,'R3_DECLARED_LINK_LIBRARY_NOT_RESOLVED:'+name);compilerInputs.push(found);}
 // Explicit build/link inputs are declared closure; actual system include files are separately compiler-observed.
 const uniqueCompilerInputs=[...new Set(compilerInputs)];
 const managed=nativeProof.managedProof;let managedPrerequisite=null;if(managed){const rel=path.relative(workspace,managed.manifest);need(!path.isAbsolute(rel)&&!rel.startsWith('..'),'R3_MANAGED_WORKSPACE_SCOPE');managedPrerequisite={manifest:rel.replaceAll('\\','/'),sha256:hash(managed.manifest)};}
 const value={schemaVersion:3,status:'BUILT',lifecycleVersion:LIFECYCLE_VERSION,mode,compileSucceeded:true,meaning:'Source-bound full-TU compilation prerequisite. A raw behavioral FAIL remains a FAIL and may be replayed by the public consumer; it is never promoted here.',root,sources:sm.records,core:sm.core,substitutions:sm.substitutions,fragments:sm.fragments,generated,compilerIncludes,compilerInputs:uniqueCompilerInputs.map(p=>({path:p,sha256:hash(p)})),expectedNegative:!!nativeProof.mutation,mutation:nativeProof.mutation??null,exe:path.join(root,'CoordinatorFixture.exe'),behaviorProof:{path:proofFile,sha256:hash(proofFile),status:proof.status},managedPrerequisite,r3ScenarioContract:{path:R3_SPEC_FILE,sha256:hash(path.join(repoRoot,R3_SPEC_FILE)),requiredPipeGroups:R3_SPEC.pipeCaseNames.length,requiredPipeChecks:R3_SPEC.expectedPipeChecks,normalReturnGroups:R3_SPEC.normalReturnCaseNames.length,normalReturnChecks:R3_SPEC.normalReturnChecks,tupleProtection:tuple?{cases:tuple.cases.length,checks:tuple.checks,names:tuple.cases.map(r=>r.name)}:(mode==='lifecycle'?R3_SPEC.tupleProtection:null)},fixtureNormalMainExecuted:false,deviceIo:false};
 const file=path.join(root,'coordinator-fixture-r3-'+mode+'-manifest.json');need(!fs.existsSync(file),'R3_FRESH_MANIFEST_REQUIRED');fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');const rel=path.relative(workspace,file);need(!rel.startsWith('..')&&!path.isAbsolute(rel),'R3_MANIFEST_WORKSPACE_SCOPE');return{manifest:rel.replaceAll('\\','/'),sha256:hash(file),value};
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const a=process.argv.slice(2),o={};need(a.length%2===0,'R3_BIND_ARGUMENTS');for(let i=0;i<a.length;i+=2){need(['--repo','--build','--mode','--behavior-proof'].includes(a[i])&&!Object.hasOwn(o,a[i]),'R3_BIND_ARGUMENT');o[a[i]]=a[i+1];}const r=bindR3Fixture({repoRoot:path.resolve(o['--repo']??path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..')),buildRoot:o['--build'],mode:o['--mode']??'pipe',behaviorProof:o['--behavior-proof']});console.log(JSON.stringify({status:'SOURCE_BOUND_COMPILED_NOT_BEHAVIORAL_APPROVAL',manifest:r.manifest,sha256:r.sha256,behaviorStatus:r.value.behaviorProof.status}));}catch(e){console.error(e.stack??e.message);process.exitCode=1;}
}

