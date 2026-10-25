// Explicit build-only batch entry. No target execution option is accepted.
import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
import {beginB49CompileBatch,requireB49CompileBatch,finishB49CompileBatch} from './ymcc_sim/global/controller-coordinator-pipe/r3-b49-compile-batch.mjs';
import {buildManagedFixture} from './ymcc_sim/global/controller-coordinator-pipe/r3-managed-builder.mjs';
import {verifyManagedSourceRows,verifyManagedBuilderAdapters} from './ymcc_sim/global/controller-coordinator-pipe/r3-source-equivalence.mjs';
import {buildOrRunB49Ordered} from './input_coordinator_lifecycle_b49_ordered_selftest.mjs';
import {inspectNativeTools,resolveLinkGraph,fileIdentity,buildToolRun} from './ymcc_sim/global/controller-coordinator-pipe/r3-b49-build-observation.mjs';
const need=(v,m)=>{if(!v)throw Error('B49_COMPILE_ONLY_'+m);};
function allFiles(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>{need(!e.isSymbolicLink(),'NO_OUTPUT_LINKS');return e.isDirectory()?allFiles(path.join(dir,e.name)):[path.join(dir,e.name)];});}
export function compileB49Prerequisites({configuration}){
 need(configuration?.compileOnly===true&&configuration.executeTargets===false,'EXPLICIT_COMPILE_ONLY_REQUIRED');
 const config=structuredClone(configuration),b51=config.sourceProfile==='B51_PRIVATE_POSITIVE',repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),out=path.resolve(config.out);
 need(path.isAbsolute(config.out)&&!fs.existsSync(out),'FRESH_ABSOLUTE_OUTPUT_REQUIRED');fs.mkdirSync(out,{recursive:true});
 need(config.sourceProfile===undefined||config.sourceProfile==='B49'||b51,'UNKNOWN_EXPLICIT_COMPILE_PROFILE');
 if(b51)need(config.privateTerminalClosure===true&&config.layout?.sourceAuthority?.mode==='B51_FROZEN_V7_PRIVATE_COMPILE_ONLY','B51_PRIVATE_CLOSURE_AND_FULL_AUTHORITY_REQUIRED');
 const result={schemaVersion:1,status:'FAIL',scope:'Build-only prerequisites, not controller behavior acceptance',compileOnly:true,fixtureExecuted:false,nativeExecuted:false,normalHostMainExecuted:false,realSdkLoaded:false,hardwareIo:false,businessFaultDetected:false,behaviorPass:null,globalActive:!b51,joint:false,cpuPowerBenefit:null,commands:[],nativeBuilds:[],managedBuild:null,metadata:null,sourceAuthority:null,stage:'admission'};
 let token;const write=(name,j)=>fs.writeFileSync(path.join(out,name),JSON.stringify(j,null,2)+'\n');
 try{
  if(b51){need(Array.isArray(config.variants)&&config.variants.length===1&&config.variants[0].name==='positive'&&config.variants[0].mutation===null,'B51_POSITIVE_ONLY');}
  else {
  need(Array.isArray(config.variants)&&config.variants.length===4&&config.variants.map(v=>v.name).join(',')==='positive,cancellation,psOwnership,psBinding','EXPLICIT_FOUR_PREREQUISITE_VARIANTS_REQUIRED');
  need(config.variants.map(v=>v.mutation).join(',')===',rollback-cancellation-guards,rollback-ps-endpoint-ownership,rollback-ps-binding-revision','ORIGINAL_DIRECTED_MUTATIONS_REQUIRED');
  }
  const actualToolFiles=allFiles(repo).filter(p=>/\.(?:mjs|json|cs|cpp)$/.test(p)||(b51&&/\.h$/.test(p)));
  token=beginB49CompileBatch({layout:config.layout,actualToolFiles,receiptDirectory:path.join(out,'authority')});
  result.stage='managed-compile';const managedOut=path.join(out,'managed');const managed=buildManagedFixture(managedOut,{batchAuthority:token,sourceRoot:config.layout.sourceRoot,dotnet:config.dotnet});result.managedBuild=managed;
  const managedManifest=JSON.parse(fs.readFileSync(managed.manifestPath,'utf8'));
  result.managedSources=verifyManagedSourceRows(managedManifest,config.layout.sourceRoot,{batchAuthority:token});result.managedAdapter=verifyManagedBuilderAdapters(managedManifest,repo);
  need(managedManifest.fixtureExecuted===false&&managedManifest.realSdkLoaded===false&&managedManifest.hardwareIo===false,'MANAGED_BUILD_ONLY_RECEIPT_REQUIRED');
  result.stage='metadata-tool-compile';const metadataDir=path.join(out,'metadata-tool');fs.mkdirSync(metadataDir);const toolSource=path.join(repo,'tools/fixtures/input_coordinator_lifecycle/B49CompileMetadataReview.cs'),project=path.join(metadataDir,'MetadataReview.csproj');
  fs.copyFileSync(toolSource,path.join(metadataDir,'Program.cs'));
  fs.writeFileSync(path.join(metadataDir,'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>');
  fs.writeFileSync(project,'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><UseAppHost>false</UseAppHost><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><NuGetAudit>false</NuGetAudit><UseSharedCompilation>false</UseSharedCompilation><ImportDirectoryBuildProps>false</ImportDirectoryBuildProps><ImportDirectoryBuildTargets>false</ImportDirectoryBuildTargets></PropertyGroup><Import Project="MetadataInputs.targets"/></Project>');
  fs.writeFileSync(path.join(metadataDir,'MetadataInputs.targets'),'<Project><Target Name="RecordMetadataInputs" AfterTargets="CoreCompile"><ItemGroup><_AllImports Include="$(MSBuildAllProjects)"/></ItemGroup><WriteLinesToFile File="$(MSBuildProjectDirectory)/evaluated-inputs.txt" Lines="@(Compile-&gt;\'Compile&#x9;%(FullPath)\');@(ReferencePath-&gt;\'Reference&#x9;%(FullPath)\');@(Analyzer-&gt;\'Analyzer&#x9;%(FullPath)\');@(_AllImports-&gt;\'Import&#x9;%(FullPath)\')" Overwrite="true" Encoding="UTF-8"/></Target></Project>');
  const dotnet=config.dotnet;need(path.isAbsolute(dotnet)&&fs.statSync(dotnet).isFile(),'EXPLICIT_DOTNET_TOOL_REQUIRED');
  result.commands.push(buildToolRun(dotnet,['build',project,'-c','Release','--nologo','-v:minimal'],{cwd:metadataDir,log:path.join(metadataDir,'build.log')}).command);
  const metadataDll=path.join(metadataDir,'bin/Release/net10.0/MetadataReview.dll');const metadataInputs=fs.readFileSync(path.join(metadataDir,'evaluated-inputs.txt'),'utf8').replace(/^\uFEFF/,'').split(/\r?\n/).filter(Boolean).map(line=>{const [kind,...rest]=line.split('\t');return {kind,...fileIdentity(rest.join('\t'))};});
  result.metadataTool={source:fileIdentity(toolSource),compiledSource:fileIdentity(path.join(metadataDir,'Program.cs')),project:fileIdentity(project),dll:fileIdentity(metadataDll),dotnet:fileIdentity(dotnet),evaluatedCompilerInputs:metadataInputs,outputs:allFiles(metadataDir).map(fileIdentity),targetAssemblyLoading:false};
  result.stage='native-tool-inspection';const tooling=inspectNativeTools(path.join(out,'native-tools'));result.nativeTooling=tooling;
  for(const variant of config.variants){result.stage='native-compile:'+variant.name;write('progress.json',result);requireB49CompileBatch(token,{sourceRoot:config.layout.sourceRoot});const dir=path.join(out,variant.name),args=['--out',dir,'--source-root',config.layout.sourceRoot,'--managed-dll',managed.fixtureDll,'--compile-only'];if(variant.mutation)args.push('--mutation',variant.mutation);
   const native=buildOrRunB49Ordered({argv:args,batchAuthority:token,sourceProfile:b51?'B51_PRIVATE_POSITIVE':'B49',privateTerminalClosure:config.privateTerminalClosure??false});need(native.status==='COMPILED_NOT_EXECUTED'&&native.runBranchReached===false&&native.results.length===0,'NATIVE_STOP_BEFORE_RUNTIME_REQUIRED');const graph=resolveLinkGraph(native,tooling,config.layout.sourceRoot);write(variant.name+'-compile-graph.json',graph);result.nativeBuilds.push({name:variant.name,mutation:variant.mutation,receipt:native,graph:fileIdentity(path.join(out,variant.name+'-compile-graph.json'))});write('progress.json',result);
  }
  result.stage='read-only-target-PE-metadata';const metadataOut=path.join(out,'PE-TERMINAL-REVIEW.json');result.commands.push(buildToolRun(dotnet,[metadataDll,'--out',metadataOut,managed.fixtureDll,...result.nativeBuilds.map(b=>b.receipt.exe.path)],{cwd:out,log:path.join(out,'metadata-tool.log')}).command);
  const metadata=JSON.parse(fs.readFileSync(metadataOut,'utf8'));need(metadata.targetAssembliesLoaded===0&&metadata.targets[0].bclReferencesOnly===true,'SDK_FREE_RAW_METADATA_REQUIRED');need(metadata.targets[0].terminalMethodBodies.length>0,'ACTUAL_TERMINAL_METHOD_IL_REQUIRED');result.metadata=fileIdentity(metadataOut);
  result.managedReferenceProof=metadata.targets[0].assemblyReferences.map(reference=>{const rows=managedManifest.evaluatedCompilerInputs.filter(r=>r.kind==='Reference'&&path.basename(r.path,'.dll')===reference.name);need(rows.length===1&&fileIdentity(rows[0].path).sha256===rows[0].sha256,'EXACT_MANAGED_REFERENCE_REQUIRED:'+reference.name);return {assembly:reference,kind:rows[0].kind,path:rows[0].path,sha256:rows[0].sha256};});
  if(b51)result.sourceProfileEvidence={profile:config.sourceProfile,privateTerminalClosure:config.privateTerminalClosure,actualStaticCalls:101,actualUniqueNames:100,originalContractCalls:100,originalUniqueNames:99,actualPrefixBoundary:96,originalMsiWitnessOrdinal:95,coreExecutedShaMeans:'Compiled source body identity; target not executed'};
  result.stage='output-and-input-closure';
  for(const row of [...managedManifest.inputBindings.generatedInputs,...managedManifest.inputBindings.toolchainAllowlist,...managedManifest.runtimeArtifacts])need(fileIdentity(row.path).sha256===row.sha256,'MANAGED_COMPILE_INPUT_OR_OUTPUT_DRIFT:'+row.path);
  for(const row of managedManifest.evaluatedCompilerInputs)need(fileIdentity(row.path).sha256===row.sha256,'MANAGED_EVALUATED_INPUT_DRIFT:'+row.path);
  result.managedSourcesAfter=verifyManagedSourceRows(managedManifest,config.layout.sourceRoot,{batchAuthority:token});
  const outputs=allFiles(out);need(outputs.length<=3072,'ORIGINAL_GLOBAL_3072_LIMIT');result.outputManifest={path:path.join(out,'OUTPUTS.json'),count:outputs.length};write('OUTPUTS.json',outputs.map(fileIdentity));
  result.status='COMPILED_NOT_EXECUTED';result.stage='complete-build-only';
 }catch(error){result.status='FAIL';result.failure={stage:result.stage,message:error.message,stack:error.stack,command:error.command??null};}
 finally{if(token){try{result.sourceAuthority=finishB49CompileBatch(token);}catch(error){result.status='FAIL';result.authorityFailure={message:error.message,stack:error.stack};}}write('COMPILE-BATCH-RESULT.json',result);}
 return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const args=process.argv.slice(2);need(args.length===3&&args[0]==='--compile-only'&&args[1]==='--config'&&path.isAbsolute(args[2]),'Use --compile-only --config ABS_JSON; no default target execution');
 const result=compileB49Prerequisites({configuration:JSON.parse(fs.readFileSync(args[2],'utf8'))});console.log(JSON.stringify({status:result.status,stage:result.stage,failure:result.failure??result.authorityFailure??null,sourceAuthorityCalls:result.sourceAuthority?.fullSourceVerificationCalls,nativeBuilds:result.nativeBuilds.length,targetsExecuted:0}));process.exitCode=result.status==='COMPILED_NOT_EXECUTED'?0:1;
}
