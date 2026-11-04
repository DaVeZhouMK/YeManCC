import {buildLightingArtifacts} from './lib/hc_lighting_catalog.mjs';
import {validateSuperJoyExtension} from './lib/hc_lighting_source_adapters.mjs';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {MANIFEST_FILE,LEGACY_ARTIFACT_FLOOR,INTEGRITY_CASES,sha,validateArtifactManifest,verifyBaseline,
 copyBaseline,applyIntegrityFault,auditVariant,classifyIntegrityRun,snapshotArtifacts} from './hc_capability_catalog_integrity.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
if(![4,5].includes(process.argv.length)||process.argv[2]!=='--out'||process.argv.length===5&&process.argv[4]!=='--lighting-only')throw Error('Usage: --out isolated-evidence-directory [--lighting-only]');
const lightingOnly=process.argv[4]==='--lighting-only';
const out=path.resolve(process.argv[3]),rel=path.relative(repo,out);
if(!rel||(!rel.startsWith('..'+path.sep)&&!path.isAbsolute(rel)))throw Error('Evidence must be outside production source');
if(fs.existsSync(out)&&fs.readdirSync(out).length)throw Error('Evidence directory must be new or empty');
fs.mkdirSync(out,{recursive:true});
const realOut=fs.realpathSync(out),realRepo=fs.realpathSync(repo),realRel=path.relative(realRepo,realOut);
if(!realRel||(!realRel.startsWith('..'+path.sep)&&!path.isAbsolute(realRel)))throw Error('Evidence resolves inside production source');
const write=(file,value)=>{fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,typeof value==='string'?value:JSON.stringify(value,null,2)+'\n');};
if(lightingOnly){
 const read=file=>JSON.parse(fs.readFileSync(path.join(repo,file),'utf8').replace(/^\uFEFF/,''));
 const catalog=read('capabilities/hc-device-catalog.json'),policy=read('capabilities/hc-lighting-policy.json'),main=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8'),checks=[];
 const check=(ok,id)=>{checks.push({id,ok});if(!ok)throw Error('LIGHTING_SOURCE_CHECK_FAILED: '+id);};
 const build=selected=>buildLightingArtifacts({repo,catalog,policy:selected,main});
 const actual=build(policy),oxp=actual.metadata.extensionRows.find(r=>r.name==='OneXPlayer3');
 const mini=actual.metadata.rows.find(r=>r.name==='OneXPlayerX1Mini'),frozenMini=catalog.devices.find(d=>d.name==='OneXPlayerX1Mini');
 check(frozenMini.declared.Capabilities.minimum.includes('DynamicLightingSecondLEDColor')&&mini.sourceDeclaredSecondColor===true&&mini.consumerSecondColor===false&&mini.secondColor===false,'exact-X1Mini-source-declaration-and-effective-consumer-separated');
 check(mini.brightness&&mini.declaredEffects.join(',')==='solid,rainbow,preset'&&!mini.presetAvailable&&!mini.statusAvailable&&mini.statusTransportDisabled,'X1Mini-existing-actions-and-transport-exclusions-preserved');
 check(JSON.stringify(actual.metadata.disabledActions)===JSON.stringify(catalog.domains.lighting.disabledActions),'existing-preset-status-policy-preserved');
 check(JSON.stringify(actual.metadata.secondColorExclusions)===JSON.stringify(policy.secondColorExclusions),'source-bound-exclusion-retained-in-same-library-metadata');
 check(actual.metadata.rows.filter(r=>r.name!=='OneXPlayerX1Mini').every(r=>JSON.stringify(r)===JSON.stringify(catalog.domains.lighting.rows.find(old=>old.name===r.name))),'all-other-frozen-lighting-rows-unchanged');
 check(JSON.stringify(actual.metadata.extensionRows)===JSON.stringify(catalog.domains.lighting.extensionRows),'reviewed-extension-rows-match-published-catalog');
 check(actual.artifacts['capabilities/HcLightingCapabilityCatalog.generated.cs'].includes('new("hc.device.OneXPlayerX1Mini", true, false,'),'effective-exclusion-consumed-by-managed-descriptor');
 const native=actual.artifacts['native/main.cpp'].replace(/\r\n/g,'\n'),signature='static const std::vector<std::string>& ymccDeviceClassSecondColorSet()',start=native.indexOf(signature),end=native.indexOf('\n}',start),secondSet=native.slice(start,end+2);
 check(start>=0&&end>start&&!secondSet.includes('"HandheldCompanion.Devices.OneXPlayerX1Mini"'),'effective-exclusion-consumed-by-native-second-color-function');
 check(['OneXPlayerX1AMD','OneXPlayerX1Pro','OneXPlayer3'].every(n=>secondSet.includes('"HandheldCompanion.Devices.'+n+'"')),'X1AMD-X1Pro-OXP3-native-secondary-consumers-retained');
 check(catalog.devices.length===92&&actual.metadata.rows.length===47,'frozen92-class-and47-lighting-roots-preserved');
 check(oxp.presets.length===10&&oxp.presets.some(p=>p.value===1000)&&oxp.declaredEffects.join(',')==='solid,breathing,preset','original-OXP3-presets-effects-and-classic-preserved');
 check(actual.metadata.extensionRows.length===4&&actual.metadata.extensionRows.filter(r=>r.sourceAdapter==='superjoy-cec-v1').length===2&&
  actual.metadata.extensionRows.map(r=>r.name).sort().join(',')==='AYANEO3,AYANEONEXT2,OneXPlayer3,OneXPlayerX2MiniPro','exact-four-source-bound-extensions-and-two-SuperJoy-types');
 const x2=actual.metadata.extensionRows.find(r=>r.name==='OneXPlayerX2MiniPro');
 check(x2.sourceAdapter==='onex-x2mini-hid-v1'&&x2.brightness&&x2.secondColor&&x2.declaredEffects.join(',')==='solid,breathing','X2MiniPro-effective-five-zone-actions');
 check(x2.presetDeclared&&!x2.presetAvailable&&!x2.statusAvailable&&x2.statusTransportDisabled&&x2.fixedSpeed,'X2MiniPro-inherited-claims-not-transferred-to-unimplemented-writers');
 check(x2.independent?.off===true&&x2.independent.brightness===true&&JSON.stringify(x2.independent.color)==='["solid","breathing"]','X2MiniPro-same-library-independent-consumer-admission');
 check(x2.sourceDependencies.length===4&&x2.consumerSources.length===4,'X2MiniPro-full-inherited-source-and-actual-consumer-closure');
 check(actual.artifacts['capabilities/HcLightingCapabilityCatalog.generated.cs'].includes('SourceAdapter = LightingSourceAdapter.OneXX2MiniHid'),'X2MiniPro-adapter-consumed-by-managed-catalog');
 check(actual.artifacts['native/lighting_device_core_binding.h'].includes('HandheldCompanion.Devices.OneXPlayerX2MiniPro')&&actual.artifacts['native/lighting_device_core_binding.h'].includes('action == "off"'),'X2MiniPro-ordinary-native-independent-route-present');
 function reject(id,mutate,code){const candidate=structuredClone(policy);mutate(candidate);let reason;try{build(candidate);}catch(error){reason=error.message;}
  check(typeof reason==='string'&&reason.startsWith(code),id);checks.at(-1).rejection=reason;}
 reject('missing-source-bound-second-color-exclusion',p=>delete p.secondColorExclusions,'LIGHTING_SECOND_COLOR_EXCLUSION_REQUIRED');
 reject('empty-source-bound-second-color-exclusion',p=>p.secondColorExclusions=[],'LIGHTING_SECOND_COLOR_EXCLUSION_REQUIRED');
 reject('duplicate-second-color-exclusion',p=>p.secondColorExclusions.push(structuredClone(p.secondColorExclusions[0])),'LIGHTING_SECOND_COLOR_EXCLUSION_REQUIRED');
 for(const name of ['OneXPlayerX1AMD','OneXPlayerX1Pro','OneXPlayer3','OneXPlayer*'])reject('secondary-exclusion-does-not-generalize:'+name,p=>p.secondColorExclusions[0].device=name,'LIGHTING_SECOND_COLOR_EXCLUSION_SCOPE');
 reject('cannot-enable-secondary-through-exclusion',p=>p.secondColorExclusions[0].consumerSecondColor=true,'LIGHTING_SECOND_COLOR_EXCLUSION_SCOPE');
 reject('nonboolean-secondary-exclusion',p=>p.secondColorExclusions[0].consumerSecondColor='false','LIGHTING_SECOND_COLOR_EXCLUSION_SCOPE');
 reject('source-declaration-cannot-be-cleared',p=>p.secondColorExclusions[0].sourceDeclaredSecondColor=false,'LIGHTING_SECOND_COLOR_EXCLUSION_SCOPE');
 reject('inherited-owner-cannot-replace-exact-writer',p=>p.secondColorExclusions[0].methodOwner='OneXPlayerX1','LIGHTING_SECOND_COLOR_EXCLUSION_SCOPE');
 reject('wrong-secondary-method',p=>p.secondColorExclusions[0].method='SetLedBrightness','LIGHTING_SECOND_COLOR_EXCLUSION_SOURCE');
 reject('wrong-secondary-source-path',p=>p.secondColorExclusions[0].sourceFile='deps/handheldcompanion-runtime/source/Devices/OneXPlayer/OneXPlayerX1.cs','LIGHTING_SECOND_COLOR_EXCLUSION_SOURCE');
 reject('wrong-secondary-source-pin',p=>p.secondColorExclusions[0].sourceSha256='0'.repeat(64),'LIGHTING_SECOND_COLOR_EXCLUSION_SOURCE');
 reject('wrong-secondary-method-pin',p=>p.secondColorExclusions[0].methodSha256='0'.repeat(64),'LIGHTING_SECOND_COLOR_EXCLUSION_SOURCE');
 reject('wrong-independent-secondary-consumer',p=>p.secondColorExclusions[0].independentConsumer.type='PinnedOneXProtocol','LIGHTING_SECOND_COLOR_EXCLUSION_CONSUMER');
 reject('changed-independent-secondary-source',p=>p.secondColorExclusions[0].independentConsumer.sourceSha256='0'.repeat(64),'LIGHTING_SECOND_COLOR_EXCLUSION_CONSUMER_BYTES');
 reject('unknown-adapter-type',p=>p.reviewedExtensionModels[0].sourceAdapter='unimplemented','LIGHTING_EXTENSION_ADAPTER_UNREVIEWED');
 reject('duplicate-extension-identity',p=>p.reviewedExtensionModels.push(structuredClone(p.reviewedExtensionModels[0])),'LIGHTING_EXTENSION_IDENTITY_CONFLICT');
 reject('OXP3-original-zone-check',p=>p.reviewedExtensionModels[0].semanticSourceFacts.zones.gkey=4,'LIGHTING_EXTENSION_ZONES');
 reject('OXP3-original-delay-check',p=>p.reviewedExtensionModels[0].semanticSourceFacts.delaysMs.stick=599,'LIGHTING_EXTENSION_DELAYS');
 reject('OXP3-original-classic-check',p=>p.reviewedExtensionModels[0].semanticSourceFacts.classicPreset=999,'LIGHTING_EXTENSION_CLASSIC');
 for(const model of ['AYANEO3','AYANEONEXT2']){
  const row=p=>p.reviewedExtensionModels.find(r=>r.name===model);
  for(const [id,mutate,code] of [
   ['wrong-adapter',r=>r.sourceAdapter='oxp3-hid-v1','LIGHTING_EXTENSION_FIXED_SOURCE_AUTHORITY'],
   ['wrong-source-commit',r=>r.sourcePin='0'.repeat(40),'LIGHTING_EXTENSION_FIXED_SOURCE_AUTHORITY'],
   ['source-and-contract-hash-change',r=>r.sourceSha256='0'.repeat(64),'LIGHTING_EXTENSION_FIXED_SOURCE_AUTHORITY'],
   ['wrong-class',r=>r.deviceClass+='Alias','LIGHTING_EXTENSION_FIXED_SOURCE_AUTHORITY'],
   ['wrong-family',r=>r.family='Other','LIGHTING_SUPERJOY_MODEL_UNREVIEWED'],
   ['missing-dependency',r=>r.sourceDependencies.pop(),'LIGHTING_SUPERJOY_DEPENDENCY_SET'],
   ['duplicate-dependency-role',r=>r.sourceDependencies[1].role='cec','LIGHTING_SUPERJOY_DEPENDENCY_SET'],
   ['wrong-cec-pin',r=>r.sourceDependencies[0].sha256='0'.repeat(64),'LIGHTING_SUPERJOY_DEPENDENCY_PIN'],
   ['AYA-channel-order',r=>r.semanticSourceFacts.ayaChannels=['R','G','B'],'LIGHTING_SUPERJOY_SEMANTIC_FACTS'],
   ['wrong-confirmation',r=>r.semanticSourceFacts.confirmation=!r.semanticSourceFacts.confirmation,'LIGHTING_SUPERJOY_SEMANTIC_FACTS'],
   ['old-owner-ABI',r=>r.semanticSourceFacts.ownerAbi=1,'LIGHTING_SUPERJOY_SEMANTIC_FACTS'],
   ['hidden-color-cache',r=>r.semanticSourceFacts.explicitColorContext=false,'LIGHTING_SUPERJOY_SEMANTIC_FACTS'],
   ['preset-activation',r=>r.presetAvailable=true,'LIGHTING_SUPERJOY_CAPABILITIES_UNREVIEWED'],
   ['unsupported-effect',r=>r.ledEffects.push('Rainbow'),'LIGHTING_SUPERJOY_CAPABILITIES_UNREVIEWED'],
   ['unenabled-capability',r=>r.capabilities.push('SecondaryDisplay'),'LIGHTING_SUPERJOY_CAPABILITIES_UNREVIEWED'],
   ['nonboolean-policy',r=>r.statusAvailable='true','LIGHTING_SUPERJOY_CAPABILITIES_UNREVIEWED']
  ])reject(model+':'+id,p=>mutate(row(p)),code);
 }
 const x2row=p=>p.reviewedExtensionModels.find(r=>r.name==='OneXPlayerX2MiniPro');
 for(const [id,mutate,code] of [
  ['wrong-adapter',r=>r.sourceAdapter='oxp3-hid-v1','LIGHTING_EXTENSION_FIXED_SOURCE_AUTHORITY'],
  ['wrong-commit',r=>r.sourcePin='0'.repeat(40),'LIGHTING_EXTENSION_FIXED_SOURCE_AUTHORITY'],
  ['wrong-source-pin',r=>r.sourceSha256='0'.repeat(64),'LIGHTING_EXTENSION_FIXED_SOURCE_AUTHORITY'],
  ['brand-alias',r=>r.deviceClass='HandheldCompanion.Devices.OneXPlayerX2','LIGHTING_EXTENSION_FIXED_SOURCE_AUTHORITY'],
  ['missing-inherited-source',r=>r.sourceDependencies.pop(),'LIGHTING_X2MINI_DEPENDENCY_SET'],
  ['duplicate-inherited-role',r=>r.sourceDependencies[1].role='x1','LIGHTING_X2MINI_DEPENDENCY_SET'],
  ['wrong-common-packet-source',r=>r.sourceDependencies[2].sha256='0'.repeat(64),'LIGHTING_X2MINI_DEPENDENCY_PIN'],
  ['missing-actual-consumer',r=>r.consumerSources.pop(),'LIGHTING_X2MINI_CONSUMER_SET'],
  ['changed-actual-consumer',r=>r.consumerSources[0].sha256='0'.repeat(64),'LIGHTING_X2MINI_CONSUMER_SOURCE_CHANGED'],
  ['aggregate-zone',r=>r.semanticSourceFacts.aggregateZoneAllowed=true,'LIGHTING_X2MINI_SEMANTIC_FACTS'],
  ['secondary-zone-loss',r=>r.semanticSourceFacts.secondaryZones=[5,5],'LIGHTING_X2MINI_SEMANTIC_FACTS'],
  ['delay-drift',r=>r.semanticSourceFacts.zoneDelayMs=0,'LIGHTING_X2MINI_SEMANTIC_FACTS'],
  ['status-activation',r=>r.statusAvailable=true,'LIGHTING_X2MINI_CAPABILITIES_UNREVIEWED'],
  ['preset-activation',r=>r.presetAvailable=true,'LIGHTING_X2MINI_CAPABILITIES_UNREVIEWED'],
  ['unsupported-effect',r=>r.ledEffects.push('Rainbow'),'LIGHTING_X2MINI_CAPABILITIES_UNREVIEWED'],
  ['nonboolean-capability',r=>r.secondColor='true','LIGHTING_X2MINI_CAPABILITIES_UNREVIEWED'],
  ['unconsumed-domain',r=>r.capabilities.push('SecondaryDisplay'),'LIGHTING_X2MINI_CAPABILITIES_UNREVIEWED']
 ])reject('X2MiniPro:'+id,p=>mutate(x2row(p)),code);
 reject('X2MiniPro-no-registration-without-independent-consumer',p=>p.independentCore.models=p.independentCore.models.filter(r=>r.device!=='OneXPlayerX2MiniPro'),'LIGHTING_X2MINI_INDEPENDENT_ADMISSION_REQUIRED');
 reject('independent-unknown-model',p=>p.independentCore.models[1].device='OneXPlayerX2','UNREVIEWED_INDEPENDENT_LIGHTING_MODEL_ACTIONS');
 reject('independent-extra-action',p=>p.independentCore.models[1].actions.status=true,'UNREVIEWED_INDEPENDENT_LIGHTING_MODEL_ACTIONS');
 reject('independent-duplicate-model',p=>p.independentCore.models[1]=structuredClone(p.independentCore.models[0]),'DUPLICATE_INDEPENDENT_LIGHTING_MODEL');
 const fixtureRoot=path.join(out,'dependency-fault');
 for(const dep of policy.reviewedExtensionModels.find(r=>r.name==='AYANEO3').sourceDependencies){const target=path.join(fixtureRoot,dep.path);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(path.join(repo,dep.path),target);}
 const cec=path.join(fixtureRoot,'capabilities/upstream/f2e58468/AYANEODeviceCEc.cs');fs.writeFileSync(cec,fs.readFileSync(cec,'utf8').replace('* 192','* 191'));
 const aya=policy.reviewedExtensionModels.find(r=>r.name==='AYANEO3'),modelText=fs.readFileSync(path.join(repo,aya.sourceFile),'utf8');let reason;
 try{validateSuperJoyExtension(fixtureRoot,aya,modelText);}catch(error){reason=error.message;}
 check(reason==='LIGHTING_SUPERJOY_DEPENDENCY_CHANGED','actual-CEC-body-corruption-detected');
 const changed=structuredClone(aya);changed.sourceDependencies[0].sha256=sha(fs.readFileSync(cec));reason=undefined;
 try{validateSuperJoyExtension(fixtureRoot,changed,modelText);}catch(error){reason=error.message;}
 check(reason==='LIGHTING_SUPERJOY_DEPENDENCY_PIN','mutated-source-plus-contract-cannot-replace-independent-pin');
 write(path.join(out,'result.json'),{status:'PASS_LIGHTING_SOURCE_SCOPE',checks,passed:checks.length,frozenClasses:92,frozenLightingRows:47,reviewedExtensions:4,defaultFullIntegritySuiteExecuted:false,hardwareIo:false,assemblyLoaded:false});
 console.log(JSON.stringify({status:'PASS_LIGHTING_SOURCE_SCOPE',checks:checks.length,hardwareIo:false}));process.exit(0);
}
const toolFiles=['tools/hc_capability_catalog_generate.mjs','tools/hc_capability_catalog_selftest.mjs','tools/hc_capability_catalog_integrity.mjs'];
const toolSnapshot=()=>toolFiles.map(file=>{const bytes=fs.readFileSync(path.join(repo,file));return {path:file,size:bytes.length,sha256:sha(bytes)};});
const toolBefore=toolSnapshot(),cases=[];
function runCheck(dir,fixture){
 fs.mkdirSync(dir,{recursive:true});
 const args=['tools/hc_capability_catalog_generate.mjs','--check',...(fixture?['--generated-root',fixture]:[]),'--out',path.join(dir,'extractor')];
 const startedAt=new Date().toISOString();
 const run=spawnSync(process.execPath,args,{cwd:repo,windowsHide:true,encoding:'utf8',timeout:120000,maxBuffer:4*1024*1024});
 const endedAt=new Date().toISOString(),stdout=run.stdout??'',stderr=run.stderr??'';
 write(path.join(dir,'stdout.log'),stdout);write(path.join(dir,'stderr.log'),stderr);
 write(path.join(dir,'check.log'),stdout+stderr+(run.error?String(run.error):''));
 const receipt={executable:process.execPath,args,cwd:repo,pid:run.pid??null,startedAt,endedAt,status:run.status,signal:run.signal,error:run.error?{code:run.error.code,message:run.error.message}:null,
  stdout:{path:'stdout.log',size:Buffer.byteLength(stdout),sha256:sha(stdout)},stderr:{path:'stderr.log',size:Buffer.byteLength(stderr),sha256:sha(stderr)}};
 write(path.join(dir,'process.json'),receipt);return {...run,receipt};
}
let manifest,manifestBytes,manifestPath,before,after,failure;
try{
 // Only --check of the real repository may establish a baseline; generation never repairs the input.
 const baselineRun=runCheck(path.join(out,'baseline'));
 if(baselineRun.status!==0||baselineRun.error||baselineRun.signal)throw Error('CATALOG_BASELINE_STRICT_CHECK_FAILED');
 let report;try{report=JSON.parse(baselineRun.stdout.trim());}catch{}
 if(report?.status!=='CURRENT'||report.hardwareIo!==false||report.assemblyLoaded!==false)throw Error('CATALOG_BASELINE_REPORT_INVALID');
 manifestPath=path.join(out,'baseline','extractor',MANIFEST_FILE);manifestBytes=fs.readFileSync(manifestPath);
 manifest=validateArtifactManifest(JSON.parse(manifestBytes),{requiredPaths:LEGACY_ARTIFACT_FLOOR});
 before=verifyBaseline(repo,manifest);
 write(path.join(out,'baseline','verified.json'),{status:'PASS',strictCheck:true,manifest:{path:manifestPath,sha256:sha(manifestBytes)},artifacts:before,process:baselineRun.receipt});
 for(const spec of INTEGRITY_CASES){
  const dir=path.join(out,spec.id),fixture=path.join(dir,'generated');
  try{
   copyBaseline(repo,fixture,manifest);
   const variant=applyIntegrityFault(fixture,manifest,spec);write(path.join(dir,'mutation.json'),variant);
   const run=runCheck(dir,fixture),oracle=classifyIntegrityRun(run,manifest,spec);
   const afterVariant=auditVariant(fixture,manifest,spec,{expectedSnapshot:variant.snapshot});write(path.join(dir,'mutation-after.json'),afterVariant);
   if(spec.kind==='current'&&oracle.accepted){
    const actual=JSON.parse(fs.readFileSync(path.join(dir,'extractor',MANIFEST_FILE),'utf8'));
    validateArtifactManifest(actual,{expectedManifest:manifest,requiredPaths:LEGACY_ARTIFACT_FLOOR});
   }
   cases.push({id:spec.id,status:oracle.accepted?'PASS':'FAIL',actualExitCode:run.status,expectedRejection:spec.id!=='current',...oracle,
    mutationEvidence:{before:'mutation.json',after:'mutation-after.json',unchangedArtifacts:variant.unchangedArtifacts,targetPaths:variant.targetPaths},process:'process.json'});
  }catch(error){cases.push({id:spec.id,status:'FAIL',expectedRejection:spec.id!=='current',error:error.message});}
 }
 after=verifyBaseline(repo,manifest);
 if(!fs.readFileSync(manifestPath).equals(manifestBytes))throw Error('CATALOG_BASELINE_MANIFEST_CHANGED');
 validateArtifactManifest(JSON.parse(fs.readFileSync(manifestPath)),{expectedManifest:manifest,requiredPaths:LEGACY_ARTIFACT_FLOOR});
}catch(error){failure={message:error.message};if(manifest)try{after=snapshotArtifacts(repo,manifest);}catch(snapshotError){failure.snapshotError=snapshotError.message;}}
const toolsAfter=toolSnapshot();
const productionUnchanged=Boolean(before&&after&&JSON.stringify(before)===JSON.stringify(after));
const toolSourcesUnchanged=JSON.stringify(toolBefore)===JSON.stringify(toolsAfter);
const result={status:!failure&&productionUnchanged&&toolSourcesUnchanged&&cases.length===INTEGRITY_CASES.length&&cases.every(c=>c.status==='PASS')?'PASS':'FAIL',
 cases,productionUnchanged,toolSourcesUnchanged,sourceInputs:after??before??[],sourceInputsBefore:before??[],toolSourcesBefore:toolBefore,toolSourcesAfter:toolsAfter,
 baselineManifest:manifestBytes?{path:manifestPath,sha256:sha(manifestBytes),artifactCount:manifest.artifactCount}:null,
 failure:failure??null,hardwareIo:false,assemblyLoaded:false};
write(path.join(out,'result.json'),result);console.log(JSON.stringify(result));if(result.status!=='PASS')process.exitCode=1;
