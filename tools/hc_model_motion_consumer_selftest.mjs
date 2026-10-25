// Offline execution of exact HC catalog and production motion consumers.
// Writes evidence only outside the repository; never starts the app or devices.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import {runBoundedTasks,runLoggedChild} from './lib/hc_motion_mutation_pool.mjs';
import {TRACE_HELPER,TRACE_VERIFIER,writeMotionGenerated,captureTraceAuthority,captureMotionProcess,beginMotionCompile,finishMotionCompile,requireMotionNativeProcess} from './lib/hc_motion_compiler_trace.mjs';
import { closeBrace } from './fixtures/hc_remaining_model_capabilities/expectations.mjs';
import { oracle } from './fixtures/hc_model_motion_consumer/oracle.mjs';
import { extract } from './fixtures/legion_motion_consumer/extract_main.mjs';
import { calibrationToolchainPreamble } from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const runner=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(runner),'..');
const fixture=path.join(repo,'tools/fixtures/hc_model_motion_consumer');
const sha=b=>createHash('sha256').update(b).digest('hex');
const sourceTextEqual=(a,b)=>a.replaceAll('\r\n','\n')===b.replaceAll('\r\n','\n');
const generatedInputsAtCreation=[];
const writeGenerated=(name,bytes)=>writeMotionGenerated(out,name,bytes,generatedInputsAtCreation);
let workspace=repo;
while(!fs.existsSync(path.join(workspace,'Docs/Research/PcCapabilities-HC-CTW-20261014/upstream/source-manifest.json'))) {
  const next=path.dirname(workspace);if(next===workspace)throw Error('Fixed HC inputs unavailable');workspace=next;
}
const args=process.argv.slice(2),opt={};
for(let i=0;i<args.length;i++) {
  if(['--baseline','--new9-baseline','--mutation-checks','--generate-only','--expectations-only','--collect-failures','--consumer-only','--reviewed-mutation-checks','--reviewed-baseline'].includes(args[i])) opt[args[i].slice(2)]=true;
  else if(['--source','--before','--historical-before','--capability-before','--catalog','--legacy-catalog','--out','--vcvars','--binding-header','--binding-catalog','--mutation-jobs'].includes(args[i])&&args[i+1]) opt[args[i].slice(2)]=args[++i];
  else throw Error(`Unknown/incomplete option ${args[i]}`);
}
const mutationJobs=opt['mutation-jobs']===undefined?1:Number(opt['mutation-jobs']);
if(!Number.isSafeInteger(mutationJobs)||mutationJobs<1||mutationJobs>4)throw Error('Mutation jobs must be an integer1..4');
const evidenceRoot=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion');
const capabilityBeforePath=path.resolve(opt['capability-before']||path.join(evidenceRoot,'main.before.cpp'));
const historicalPath=path.resolve(opt['historical-before']||path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/MotionConsumer/main.before.cpp'));
const isBaseline=!!(opt.baseline||opt['new9-baseline']||opt['reviewed-baseline']);
if(opt['reviewed-baseline']&&(!opt.source||!opt.catalog))throw Error('Reviewed baseline needs explicit frozen source and catalog');
const beforePath=path.resolve(opt.before||path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/LegacyMotion/main.before.cpp'));
const sourcePath=path.resolve(opt.source||(opt['new9-baseline']?historicalPath:opt.baseline?beforePath:path.join(repo,'native/main.cpp')));
const catalogPath=path.resolve(opt.catalog||path.join(repo,'native/hc_model_motion_catalog.h'));
const legacyCatalogPath=path.resolve(opt['legacy-catalog']||path.join(repo,'native/hc_legacy_motion_catalog.h'));
const out=path.resolve(opt.out||path.join(evidenceRoot,'capability-tests',`${opt['new9-baseline']?'new9-historical-baseline':opt.baseline?'legacy-before-baseline':'current'}-${new Date().toISOString().replace(/[:.]/g,'-')}`));
const relative=path.relative(repo,out);
if(relative===''||(!relative.startsWith(`..${path.sep}`)&&!path.isAbsolute(relative)))throw Error('Evidence --out must be outside the repository');
if(fs.existsSync(out)&&fs.readdirSync(out).length)throw Error('Use a fresh empty --out directory to avoid stale evidence');
for(const input of [sourcePath,beforePath,historicalPath,capabilityBeforePath,catalogPath,legacyCatalogPath])if(path.dirname(input)===out)throw Error('Evidence cannot overwrite input snapshots');
fs.mkdirSync(out,{recursive:true});
const save=(name,data)=>fs.writeFileSync(path.join(out,name),JSON.stringify(data,null,2)+'\n');
const expected=oracle(workspace,repo);save('expectations.json',expected);
if(opt['expectations-only']){console.log(`Independent oracle: ${expected.identities.length} new aliases, ${expected.legacy.recipes.length} legacy tuples, ${expected.legacy.cases.length} fixed legacy identities, ${expected.legacy.sourceChecks.length} C# files verified; ${out}`);process.exit(0);}
const sourceBytes=fs.readFileSync(sourcePath),beforeBytes=fs.readFileSync(beforePath),historicalBytes=fs.readFileSync(historicalPath);
const legacyCatalogBytes=fs.readFileSync(legacyCatalogPath),capabilityBeforeBytes=fs.readFileSync(capabilityBeforePath);
const catalogBytes=fs.readFileSync(catalogPath),msiPath=path.join(repo,'native/msi_motion_capabilities.h'),msiBytes=fs.readFileSync(msiPath);
writeGenerated('hc_model_motion_catalog.snapshot.h',catalogBytes);
writeGenerated('hc_legacy_motion_catalog.snapshot.h',legacyCatalogBytes);
writeGenerated('msi_motion_capabilities.snapshot.h',msiBytes);
const defaultCatalogPath=path.join(repo,'native/hc_default_motion_catalog.h'),defaultCatalogBytes=fs.readFileSync(defaultCatalogPath);
const genericCatalogPath=path.join(repo,'native/hc_generic_motion_catalog.h'),genericCatalogBytes=fs.readFileSync(genericCatalogPath);
writeGenerated('hc_generic_motion_catalog.h',genericCatalogBytes);
writeGenerated('hc_default_motion_catalog.h',defaultCatalogBytes);
const bindings=[];
function extractor(text,label,allowMissingNew=false,allowMissingLegacy=false,allowMissingCapability=false) {
  const entries=[];
  function bind(name,start,end,kind='compiled-definition') {
    const code=text.slice(start,end),record={snapshot:label,name,kind:label==='capability-before'?'source-only-control-comparison':kind,line:text.slice(0,start).split('\n').length,
      byteOffset:Buffer.byteLength(text.slice(0,start)),bytes:Buffer.byteLength(code),sha256:sha(code)};
    entries.push({name,code});bindings.push(record);return code;
  }
  function fn(name,optional=false) {
    for(const m of text.matchAll(new RegExp(`^static [^\\n]*\\b${name}\\(`,'gm'))) {
      const open=text.indexOf('{',m.index),semi=text.indexOf(';',m.index);
      if(semi>=0&&semi<open)continue;
      return bind(name,m.index,closeBrace(text,open)+1);
    }
    if(optional)return '';
    throw Error(`${label}: missing production definition ${name}`);
  }
  function decl(marker) {
    const at=text.indexOf(marker);if(at<0)throw Error(`${label}: missing ${marker}`);
    const end=closeBrace(text,text.indexOf('{',at))+1;
    return bind(marker,at,end+(text[end]===';'?1:0));
  }
  function global(name) {
    const m=text.match(new RegExp(`^static [^\\n;]*\\b${name}\\b[^;]*;`,'m'));
    if(!m)throw Error(`Missing global ${label}:${name}`);return bind(name,m.index,m.index+m[0].length);
  }
  const code=[`static constexpr bool defaultWindowsAvailable=${text.includes('static bool inputCaptureDefaultHcMotionIdentity(')};`];
  code.push(fn('ascii_lower'),fn('trim_ascii'),decl('enum class YmccFamily : int {'),decl('struct MachineIdentity {'),
    decl('struct HcMotionSelectorReceipt {'),decl('struct InputCaptureSensorMeta {'),decl('struct InputCaptureLegacySensorFields {'));
  code.push(fs.readFileSync(path.join(fixture,'mocks.cpp'),'utf8'));
  code.push(global('g_machineIdentity'),global('g_hcMotionSelector'),global('g_sensorMeta'));
  if(text.includes('t_windowsProcessingMeta'))code.push(global('t_windowsProcessingMeta'));
  for(const name of ['ymccFamilyName','ymccDmiNormalize','ymccResolveFamily','normalizeHcDmi','inputCaptureLegacySensorFieldsSnapshot'])code.push(fn(name));
  if(text.includes('inputCaptureGenericHcMotionRecipe('))for(const name of ['inputCaptureGenericHcMotionRecipe','inputCaptureResolveGenericHcMotionSelector','inputCaptureGenericHcMotionSelectorBound','inputCaptureMotionDeviceClass'])code.push(fn(name));
  if(text.includes('static bool inputCaptureDefaultHcMotionIdentity('))for(const name of ['inputCaptureDefaultHcMotionIdentity','inputCaptureResolveDefaultHcMotionSelector','inputCaptureDefaultHcMotionSelectorBound'])code.push(fn(name));
  const newResolver=fn('inputCaptureResolveNewHcMotionSelector',allowMissingNew);
  const newBound=fn('inputCaptureNewHcMotionSelectorBound',allowMissingNew);
  const legacyResolver=fn('inputCaptureResolveLegacyHcMotionSelector',allowMissingLegacy);
  const legacyBound=fn('inputCaptureLegacyHcMotionSelectorBound',allowMissingLegacy);
  code.push(newResolver,newBound,legacyResolver,legacyBound);
  if(text.includes('static const ymcc::hc_model_motion::Recipe* inputCapturePrivateHcMotionSelectorRecipe('))code.push(fn('inputCapturePrivateHcMotionSelectorRecipe'));
  for(const name of ['inputCaptureResolveMsiMotionSelector','inputCaptureMotionSelectorBound','inputCaptureResolveHcMotionSelector',
    'ymccParseAxisToken','ymccSplitAxisRecipe','ymccApplyNamedImuMatrix','inputCaptureApplyHcMatrixCandidate',
    'inputCaptureMotionPairAdmitted','inputCaptureCandidateMatrixReceipt'])code.push(fn(name));
  const capability=fn('inputCaptureMotionCapabilityReceipt',allowMissingCapability);
  const evidenceDue=fn('inputCaptureMotionEvidenceDue',allowMissingCapability);
  code.push(capability,evidenceDue);
  code.push(`static constexpr bool capabilityAvailable=${!!capability};
static json capabilityForTest(const InputCaptureSensorMeta& meta,bool observed) {${capability?'return inputCaptureMotionCapabilityReceipt(meta,observed);':'(void)meta;(void)observed;return json{{"unavailable",true}};'}}`);
  if(capability) {
    code.push(fn('inputCaptureSensorMetaSnapshot'));
    code.push(global('g_motionCapabilityLoggedPower'));
    const gateAt=text.indexOf('    const auto capabilityPower = currentPowerGeneration();');
    const gateIf=text.indexOf('    if (inputCaptureMotionEvidenceDue(',gateAt);
    if(gateAt<0||gateIf<gateAt)throw Error('Capability evidence generation gate moved');
    const evidenceGate=bind('LOG:motion-evidence-generation-gate',gateAt,closeBrace(text,text.indexOf('{',gateIf))+1,'compiled-production-log-fragment');
    code.push(`static constexpr bool evidenceDueAvailable=true;static bool evidenceDueForTest(uint64_t lp,uint64_t lb,uint64_t p,uint64_t b){return inputCaptureMotionEvidenceDue(lp,lb,p,b);}
static void capabilityEvidenceLogForTest(){const UINT64 now=1000;${evidenceGate}}`);
    const initialMarker='{"motionCapability", inputCaptureMotionCapabilityReceipt(capabilityMeta, false)}';
    const detailMarker='{"motionCapability",inputCaptureMotionCapabilityReceipt(sensorMeta, controllerMotion.has_value())}';
    const snapshotMarker='    const auto controllerMotion = legionMotionSnapshot(now);';
    const capture=(name,marker)=>{const at=text.indexOf(marker);if(at<0)throw Error(`${label}: missing capability caller ${name}`);return bind(name,at,at+marker.length,'compiled-production-log-fragment');};
    const initial=capture('LOG:initial-motion-capability',initialMarker);
    const detail=capture('LOG:sample-motion-capability',detailMarker);
    const leaseSnapshot=capture('LOG:controller-observation-snapshot',snapshotMarker);
    code.push(`static json capabilityInitialLogForTest() {const auto capabilityMeta=inputCaptureSensorMetaSnapshot();return json{${initial}};}
static json capabilitySampleLogForTest(const InputCaptureSensorMeta& sensorMeta) {const UINT64 now=1000;${leaseSnapshot} return json{${detail}};}`);
  } else code.push('static constexpr bool evidenceDueAvailable=false;static uint64_t g_motionCapabilityLoggedPower=0,g_motionCapabilityLoggedBinding=0;static bool evidenceDueForTest(uint64_t,uint64_t,uint64_t,uint64_t){return false;}static void capabilityEvidenceLogForTest(){} static json capabilityInitialLogForTest(){return json{{"unavailable",true}};} static json capabilitySampleLogForTest(const InputCaptureSensorMeta&){return json{{"unavailable",true}};}');

  // Exact BUS consumer's provider/generation/selector gate, bounded by adjacent declarations.
  const start=text.indexOf('    const bool sameProvider = !gyroProvider.empty() && gyroProvider == accelProvider;');
  const final='    const bool hcMatrixAdmitted = pairProven && selectorBound;';
  const end=text.indexOf(final,start);
  if(start<0||end<start)throw Error(`${label}: production consumer boundary moved`);
  const consumer=bind('BUS:motion-pair-admission',start,end+final.length,'compiled-production-fragment');
  const prepareAt=text.indexOf('    double mgx = gx, mgy = gy, mgz = gz, maxa = ax, may = ay, maz = az;');
  const prepareEnd=text.indexOf('    double cgx = mgx, cgy = mgy, cgz = mgz;',prepareAt);
  if(prepareAt<0||prepareEnd<prepareAt)throw Error('Production candidate preparation moved');
  const prepare=bind('BUS:matrix-candidate-preparation',prepareAt,prepareEnd,'compiled-production-fragment');
  const activateAt=text.indexOf('    if (hcMatrixAdmitted',end);
  if(activateAt<0)throw Error('Production six-axis activation missing');
  const activate=bind('BUS:six-axis-activation',activateAt,closeBrace(text,text.indexOf('{',activateAt))+1,'compiled-production-fragment');
  const effectAt=text.indexOf('    if (g_gm && g_gmProcess && g_gmGetCal && gyro && accel &&',activateAt);
  const effectLast=text.includes('        g_gmProcessedThisTick = windowsFrameProcessed;')?'        g_gmProcessedThisTick = windowsFrameProcessed;':'        g_gmProcessedThisTick = true;';
  const effectEnd=text.indexOf(effectLast,effectAt);
  if(effectAt<0||effectEnd<effectAt)throw Error('Production GamepadMotion condition/call boundary moved');
  const effect=bind('BUS:GamepadMotion-condition-and-call-prefix',effectAt,effectEnd+effectLast.length,'compiled-production-prefix-effect-sink');
  code.push(`struct ProbeMotionResult {bool admitted;std::array<double,6> active;std::string matrixIdentity;unsigned processCalls;std::array<float,6> processAxes;};
static ProbeMotionResult probeActive(bool live,bool gyro,bool accel,const std::string& gyroProvider,const std::string& accelProvider,UINT64 gyroBindingGeneration,UINT64 accelBindingGeneration,std::array<double,6> raw={2,-3,5,11,13,-17}) {
const double gx=raw[0],gy=raw[1],gz=raw[2],ax=raw[3],ay=raw[4],az=raw[5];
// Controlled snapshot boundary; coherence and final lifetime gates execute in windows_consumer_view_selftest.
struct FixtureSnapshot {struct Capture {const std::atomic<UINT64>* activeBinding;} capture;};
const FixtureSnapshot windowsSensorView{{g_inputCaptureSink?&g_inputCaptureSink->activeBindingGeneration:nullptr}};
const bool windowsSensorViewCurrent=true;
${prepare}
${consumer}
${activate}
// Controlled CPU acceptance/effect sink; real processing ownership executes in calibration DLL fixtures.
const bool windowsProcessorOwnerReady=true,privateMotionPairProven=false;
const auto inputCaptureWindowsProcessingFrameCurrent=[](const auto&){return true;};
const auto inputCaptureWindowsProcessingProcess=[](const auto&,float gx,float gy,float gz,float ax,float ay,float az,float dt){g_gmProcess(g_gm,gx,gy,gz,ax,ay,az,dt);return true;};
const bool calibrationPairProven=false;const float motionDeltaSeconds=0.008f;
mockProcessCalls=0;mockProcessAxes={};g_gmProcessedThisTick=false;
${effect}
}
return {hcMatrixAdmitted,{mgx,mgy,mgz,maxa,may,maz},matrixIdentity,mockProcessCalls,mockProcessAxes};
}
static bool probeConsumer(bool live,bool gyro,bool accel,const std::string& gp,const std::string& ap,UINT64 gg,UINT64 ag) {return probeActive(live,gyro,accel,gp,ap,gg,ag).admitted;}`);
  code.push(`static constexpr bool resolveNewAvailable=${!!newResolver};\nstatic bool resolveNewForTest(const MachineIdentity& id,HcMotionSelectorReceipt& receipt) {${newResolver?'return inputCaptureResolveNewHcMotionSelector(id,receipt);':'(void)id;(void)receipt;return false;'}}`);
  code.push(`static constexpr bool resolveLegacyAvailable=${!!legacyResolver};\nstatic bool resolveLegacyForTest(const MachineIdentity& id,HcMotionSelectorReceipt& receipt) {${legacyResolver?(newResolver?'if(ymcc::hc_model_motion::find(id.deviceClass))return inputCaptureResolveNewHcMotionSelector(id,receipt);return inputCaptureResolveLegacyHcMotionSelector(id,receipt);':'return inputCaptureResolveLegacyHcMotionSelector(id,receipt);'):'(void)id;(void)receipt;return false;'}}`);
  code.push(`static bool resolveOldOnlyForTest(const MachineIdentity& id,HcMotionSelectorReceipt& receipt) {${legacyResolver?'return inputCaptureResolveLegacyHcMotionSelector(id,receipt);':'(void)id;(void)receipt;return false;'}}`);
  code.push(fs.readFileSync(path.join(fixture,'bridge.cpp'),'utf8'));
  return {code:code.join('\n\n'),entries,fn};
}
const frozen=extractor(beforeBytes.toString('utf8'),'before',false,true,true);
const historical=extractor(historicalBytes.toString('utf8'),'historical',true,true,true);
const capabilityBefore=extractor(capabilityBeforeBytes.toString('utf8'),'capability-before',false,false,true);
const actual=extractor(sourceBytes.toString('utf8'),'current',!!opt['new9-baseline'],isBaseline,isBaseline);
const unifiedFiles=['capabilities/HcMotionCapabilityCatalog.h','capabilities/HcMotionCapabilityCatalog.generated.h'];
const privateCatalogPath=path.join(repo,'native/hc_oem_windows_motion_catalog.h');
writeGenerated('hc_oem_windows_motion_catalog.h',fs.readFileSync(privateCatalogPath));
writeGenerated('unified_binding.cpp',fs.readFileSync(path.join(fixture,'unified_binding.cpp')));
const unifiedInput=file=>file==='capabilities/HcMotionCapabilityCatalog.h'&&opt['binding-header']?path.resolve(opt['binding-header']):
 file==='capabilities/HcMotionCapabilityCatalog.generated.h'&&opt['binding-catalog']?path.resolve(opt['binding-catalog']):path.join(repo,file);
for(const file of unifiedFiles)writeGenerated(path.basename(file),fs.readFileSync(unifiedInput(file)));
const preamble=`#include <algorithm>\n#include <array>\n#include <atomic>\n#include <cctype>\n#include <cmath>\n#include <cstdint>\n#include <fstream>\n#include <iostream>\n#include <map>\n#include <mutex>\n#include <optional>\n#include <set>\n#include <stdexcept>\n#include <string>\n#include <string_view>\n#include <utility>\n#include <vector>\n#include "json.hpp"\n#include "hc_model_motion_catalog.snapshot.h"\n#include "hc_legacy_motion_catalog.snapshot.h"\n#include "msi_motion_capabilities.snapshot.h"\n#include "hc_default_motion_catalog.h"\n#include "hc_generic_motion_catalog.h"\n#include "HcMotionCapabilityCatalog.h"\n#include "hc_oem_windows_motion_catalog.h"\nusing json=nlohmann::json;using UINT32=std::uint32_t;using UINT64=std::uint64_t;\n`;
const generated=preamble+`static constexpr bool consumerOnlyControl=${!!opt['consumer-only']};\n`+`namespace before {\n${frozen.code}\n}\nnamespace historical {\n${historical.code}\n}\nnamespace candidate {\n${actual.code}\n}\n`+fs.readFileSync(path.join(fixture,'harness.cpp'),'utf8');
const cpp=path.join(out,'motion_consumer.cpp');writeGenerated('motion_consumer.cpp',generated);
const fixtureInputs=['private-binding-contract.json','unified_binding.cpp','unified-binding-contract.json','oracle.mjs','reviewed_resources.mjs','mocks.cpp','bridge.cpp','harness.cpp','README.md'].map(name=>({path:path.join(fixture,name),sha256:sha(fs.readFileSync(path.join(fixture,name)))}));
fixtureInputs.push({path:path.join(repo,'tools/lib/hc_motion_mutation_pool.mjs'),sha256:sha(fs.readFileSync(path.join(repo,'tools/lib/hc_motion_mutation_pool.mjs')))});
for(const file of [TRACE_HELPER,TRACE_VERIFIER])fixtureInputs.push({path:path.join(repo,file),sha256:sha(fs.readFileSync(path.join(repo,file)))});
if(opt['binding-header']){const file=path.resolve(opt['binding-header']);fixtureInputs.push({path:file,sha256:sha(fs.readFileSync(file))});}
fixtureInputs.push({path:privateCatalogPath,sha256:sha(fs.readFileSync(privateCatalogPath))});
fixtureInputs.push(...unifiedFiles.map(file=>{const input=unifiedInput(file);return {path:input,sha256:sha(fs.readFileSync(input))};}));
fixtureInputs.push({path:genericCatalogPath,sha256:sha(genericCatalogBytes)},{path:defaultCatalogPath,sha256:sha(defaultCatalogBytes)},{path:path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'),sha256:sha(fs.readFileSync(path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs')))});
const sameBodies=['inputCaptureResolveMsiMotionSelector','normalizeHcDmi','ymccApplyNamedImuMatrix','inputCaptureMotionPairAdmitted','BUS:matrix-candidate-preparation','BUS:six-axis-activation','BUS:GamepadMotion-condition-and-call-prefix'];
const legacySourceChecks=sameBodies.map(name=>{
 const a=frozen.entries.find(e=>e.name===name)?.code,b=actual.entries.find(e=>e.name===name)?.code;
 return {name,beforeSha256:sha(a||''),currentSha256:sha(b||''),unchanged:sourceTextEqual(a,b),comparison:'CRLF-to-LF-only'};
});
const msiMatrixTail=s=>s.slice(s.indexOf('    const auto legacyFields=inputCaptureLegacySensorFieldsSnapshot();'));const oldMatrix=frozen.entries.find(e=>e.name==='inputCaptureApplyHcMatrixCandidate').code,newMatrix=actual.entries.find(e=>e.name==='inputCaptureApplyHcMatrixCandidate').code;legacySourceChecks.push({name:'MSI legacy field override tail',beforeSha256:sha(msiMatrixTail(oldMatrix)),currentSha256:sha(msiMatrixTail(newMatrix)),unchanged:sourceTextEqual(msiMatrixTail(oldMatrix),msiMatrixTail(newMatrix)),comparison:'CRLF-to-LF-only'});
const beforeEntry=frozen.entries.find(e=>e.name==='inputCaptureResolveHcMotionSelector').code;
const currentEntry=actual.entries.find(e=>e.name==='inputCaptureResolveHcMotionSelector').code;
const fallback=s=>s.slice(s.indexOf('        const std::string manufacturer = normalizeHcDmi(sgRegSZ('));
legacySourceChecks.push({name:'ROG registry fallback and exception handler',beforeSha256:sha(fallback(beforeEntry)),currentSha256:sha(fallback(currentEntry)),unchanged:sourceTextEqual(fallback(beforeEntry),fallback(currentEntry)),comparison:'CRLF-to-LF-only'});
const legacyBoundSuffix=s=>s.slice(s.indexOf('    if(g_hcMotionSelector.selectorId=="asus-rc73xa"'));
const oldBound=frozen.entries.find(e=>e.name==='inputCaptureMotionSelectorBound').code;
const nowBound=actual.entries.find(e=>e.name==='inputCaptureMotionSelectorBound').code;
legacySourceChecks.push({name:'Original ASUS fast path and MSI-bound comparison tail',beforeSha256:sha(legacyBoundSuffix(oldBound)),currentSha256:sha(legacyBoundSuffix(nowBound)),unchanged:sourceTextEqual(legacyBoundSuffix(oldBound),legacyBoundSuffix(nowBound)),comparison:'CRLF-to-LF-only'});
const boundCode=actual.entries.find(e=>e.name==='inputCaptureNewHcMotionSelectorBound')?.code;
const cacheCalls=boundCode?[...boundCode.matchAll(/\b([\w:]+)\s*\(/g)].map(m=>m[1]).filter(n=>!['inputCaptureNewHcMotionSelectorBound','if'].includes(n)):[];
const legacyCache=actual.entries.find(e=>e.name==='inputCaptureLegacyHcMotionSelectorBound')?.code;
const legacyCacheCalls=legacyCache?[...legacyCache.matchAll(/\b([\w:]+)\s*\(/g)].map(m=>m[1]).filter(n=>!['inputCaptureLegacyHcMotionSelectorBound','if'].includes(n)):[];
const reviewedChanges=new Set(['inputCaptureResolveNewHcMotionSelector','inputCaptureNewHcMotionSelectorBound','inputCaptureResolveLegacyHcMotionSelector','inputCaptureResolveHcMotionSelector','inputCaptureApplyHcMatrixCandidate','inputCaptureCandidateMatrixReceipt']);
const capabilityControlChecks=capabilityBefore.entries.filter(e=>e.name!=='inputCaptureMotionCapabilityReceipt'&&!e.name.startsWith('LOG:')&&!reviewedChanges.has(e.name)).map(e=>({name:e.name,unchanged:actual.entries.some(a=>a.name===e.name&&sourceTextEqual(a.code,e.code)),beforeSha256:sha(e.code),currentSha256:sha(actual.entries.find(a=>a.name===e.name)?.code||'')}));
for(const check of capabilityControlChecks){
 const allowed=expected.genericFallbackPolicy.changedMethods.find(row=>row.symbol===check.name);
 const current=actual.entries.find(row=>row.name===check.name);
 if(allowed&&current&&sha(current.code.replaceAll('\r\n','\n'))===allowed.after){check.controlledRefactorVerified=true;check.refactorScope='frozen-generic-source-ownership-contract';check.expectedAfterSha256=allowed.after;}
}
const genericContinuation=fs.readFileSync(expected.genericFallbackPolicy.continuationSource.path,'utf8');
for(const name of ['inputCaptureLegacySensorFieldsSnapshot','inputCaptureMotionSelectorBound','BUS:motion-pair-admission']) {
 const check=capabilityControlChecks.find(row=>row.name===name),current=actual.entries.find(row=>row.name===name);if(!check||!current)throw Error('Missing continuation source check '+name);
 let archived;
 if(name==='BUS:motion-pair-admission'){const start=genericContinuation.indexOf('    const bool sameProvider = !gyroProvider.empty() && gyroProvider == accelProvider;'),last='    const bool hcMatrixAdmitted = pairProven && selectorBound;',end=genericContinuation.indexOf(last,start);if(start<0||end<start)throw Error('Frozen continuation pair boundary moved');archived=genericContinuation.slice(start,end+last.length);}
 else archived=extract(genericContinuation,name).code;
 const declared=expected.genericFallbackPolicy.changedMethods.find(row=>row.symbol===name);
 if(declared&&sha(archived)!==declared.after)throw Error('Documented generic method differs from its frozen source');
 if(sourceTextEqual(archived,current.code)){check.controlledRefactorVerified=true;check.refactorScope='frozen-generic-continuation-source-with-CRLF-only-normalization';check.continuationSource=expected.genericFallbackPolicy.continuationSource;check.continuationRawBodySha256=sha(archived);check.continuationNormalizedBodySha256=sha(archived.replaceAll('\r\n','\n'));}
}
fixtureInputs.push(...expected.genericFallbackPolicy.inputs);
// These three intended route/view changes have an exact, immutable phase preimage;
// they remain audited rather than pretending they are unchanged since the old capability phase.
if(sourceBytes.includes(Buffer.from('static WindowsSensorView inputCaptureWindowsSensorViewSnapshot()'))){
 const prePath=path.join(evidenceRoot,'WindowsConsumerView-20261018/before/native/main.cpp'),preBytes=fs.readFileSync(prePath);
 const plan=JSON.parse(fs.readFileSync(path.join(evidenceRoot,'WindowsConsumerView-20261018/PLAN.json')));
 if(sha(preBytes)!==plan.preimages.find(x=>x.path==='native/main.cpp').sha256)throw Error('Frozen view preimage drift');
 fixtureInputs.push({path:prePath,sha256:sha(preBytes)});
 const pre=preBytes.toString('utf8').replace(/\r\n/g,'\n');
 const legacy=extract(pre,'inputCaptureLegacySensorFieldsSnapshot').code;
 const expectedLegacy=legacy.replace('{\n','{\n    if(t_windowsProcessingMeta)return {t_windowsProcessingMeta->gyroFamily=="sensorapi"&&t_windowsProcessingMeta->gyroHcLegacyFields,\n        t_windowsProcessingMeta->accelFamily=="sensorapi"&&t_windowsProcessingMeta->accelHcLegacyFields};\n');
 const begin=pre.indexOf('    const bool sameProvider = !gyroProvider.empty() && gyroProvider == accelProvider;');
 const last='    const bool hcMatrixAdmitted = pairProven && selectorBound;';const end=pre.indexOf(last,begin);
 if(begin<0||end<begin)throw Error('Frozen pair boundary');
 const expectedPair=pre.slice(begin,end+last.length).replace('const UINT64 activeBindingGeneration=g_inputCaptureSink\n        ? g_inputCaptureSink->activeBindingGeneration.load(std::memory_order_acquire) : 0;','const UINT64 activeBindingGeneration=windowsSensorView.capture.activeBinding\n        ? windowsSensorView.capture.activeBinding->load(std::memory_order_acquire) : 0;').replace('const bool pairProven = inputCaptureMotionPairAdmitted(','const bool pairProven = windowsSensorViewCurrent && inputCaptureMotionPairAdmitted(');
 const expected=new Map([['inputCaptureLegacySensorFieldsSnapshot',expectedLegacy],['inputCaptureMotionSelectorBound',extract(pre,'inputCaptureMotionSelectorBound').code],['BUS:motion-pair-admission',expectedPair]]);
 for(const check of capabilityControlChecks)if(expected.has(check.name)){
  check.controlledRefactorVerified=actual.entries.find(x=>x.name===check.name)?.code.replace(/\r\n/g,'\n')===expected.get(check.name);
  check.preimagePath=prePath;check.preimageSha256=sha(preBytes);check.expectedRefactorSha256=sha(expected.get(check.name));
 }
}
// Keep the one intentional worker refactor under an exact immutable preimage audit.
if(sourceBytes.includes(Buffer.from('const bool windowsFrameProcessed=inputCaptureWindowsProcessingProcess('))){
 const prePath=path.join(evidenceRoot,'WindowsProcessorOwnership-20261018/before/native/main.cpp'),bytes=fs.readFileSync(prePath),plan=JSON.parse(fs.readFileSync(path.join(evidenceRoot,'WindowsProcessorOwnership-20261018/PLAN.json')));
 if(sha(bytes)!==plan.preimages.find(x=>x.path==='native/main.cpp').sha256)throw Error('Frozen processor preimage drift');fixtureInputs.push({path:prePath,sha256:sha(bytes)});
 const pre=bytes.toString('utf8').replace(/\r\n/g,'\n'),from=pre.indexOf('    if (g_gm && g_gmProcess && g_gmGetCal && gyro && accel &&'),last='        g_gmProcessedThisTick = true;',end=pre.indexOf(last,from);
 if(from<0||end<from)throw Error('Frozen processor effect boundary');
 const expected=pre.slice(from,end+last.length)
  .replace('        ((live && pairProven) || calibrationPairProven)) {','        windowsProcessorOwnerReady&&inputCaptureWindowsProcessingFrameCurrent(windowsSensorView)&&\n        ((live && pairProven) || calibrationPairProven || privateMotionPairProven)) {')
  .replace('const bool calibrationOnlyProcess = calibrationPairProven &&','const bool calibrationOnlyProcess = (calibrationPairProven||privateMotionPairProven) &&')
  .replace('        g_gmProcess(g_gm,','        const bool windowsFrameProcessed=inputCaptureWindowsProcessingProcess(windowsSensorView,')
  .replace('        g_gmProcessedThisTick = true;','        g_gmProcessedThisTick = windowsFrameProcessed;');
 const check=capabilityControlChecks.find(x=>x.name==='BUS:GamepadMotion-condition-and-call-prefix');if(!check)throw Error('Processor effect preservation check missing');
 check.controlledRefactorVerified=actual.entries.find(x=>x.name===check.name)?.code.replace(/\r\n/g,'\n')===expected;check.preimagePath=prePath;check.preimageSha256=sha(bytes);check.expectedRefactorSha256=sha(expected);
 const legacyCheck=legacySourceChecks.find(x=>x.name===check.name);if(legacyCheck){legacyCheck.controlledRefactorVerified=check.controlledRefactorVerified;legacyCheck.preimagePath=prePath;legacyCheck.preimageSha256=sha(bytes);legacyCheck.expectedRefactorSha256=sha(expected);}
}
// Only the exact reviewed binding changes are admitted; arbitrary selector changes still fail.
const unifiedContract=JSON.parse(fs.readFileSync(path.join(fixture,'unified-binding-contract.json'),'utf8'));
const receiptControl=capabilityControlChecks.find(e=>e.name==='struct HcMotionSelectorReceipt {');
const receiptCurrent=actual.entries.find(e=>e.name==='struct HcMotionSelectorReceipt {');
if(receiptControl&&receiptCurrent&&sha(receiptCurrent.code.replaceAll('\r\n','\n'))===unifiedContract.receiptSha256){receiptControl.controlledRefactorVerified=true;receiptControl.refactorScope='unified-binding-receipt-additive-field';}
for(const method of unifiedContract.methods) {
 const current=actual.entries.find(e=>e.name===method.symbol);
 if(!current || sha(current.code.replaceAll('\r\n','\n'))!==method.afterSha256)continue;
 const control=capabilityControlChecks.find(e=>e.name===method.symbol);
 if(control){control.controlledRefactorVerified=true;control.refactorScope='unified-immutable-native-recipe-binding';control.expectedRefactorSha256=method.afterSha256;}
}
const privateContract=JSON.parse(fs.readFileSync(path.join(fixture,'private-binding-contract.json'),'utf8'));
for(const method of privateContract.methods) {
 const current=actual.entries.find(e=>e.name===method.symbol);
 if(!current||sha(current.code.replaceAll('\r\n','\n'))!==method.afterSha256)continue;
 for(const control of [...capabilityControlChecks,...legacySourceChecks])if(control.name===method.symbol||method.legacyChecks?.includes(control.name)){
  control.controlledRefactorVerified=true;control.refactorScope='exact-six-private-original-recipe-binding';control.expectedAfterSha256=method.afterSha256;
 }
}
// Admit only the exact reviewed primary-owner refactor, never arbitrary source changes.
if(sourceBytes.includes(Buffer.from('ymcc::capabilities::motion::primaryKind('))){
 const file=path.join(fixture,'primary-owner-contract.json'),bytes=fs.readFileSync(file),contract=JSON.parse(bytes);fixtureInputs.push({path:file,sha256:sha(bytes)});
 if(contract.schemaVersion!==1||contract.methods.length!==3)throw Error('Motion primary-owner contract schema');
 if((!opt['collect-failures']||opt['generate-only'])&&(contract.preservedMethods.length!==5||contract.preservedMethods.some(method=>{const body=actual.entries.find(x=>x.name===method.symbol);return !body||sha(body.code.replaceAll('\r\n','\n'))!==method.sha256;})))throw Error('Motion primary-owner changed preserved source/identity gates');
 for(const method of contract.methods){
  if(sha(method.before)!==method.beforeSha256)throw Error('Motion primary-owner preimage changed');
  const current=actual.entries.find(entry=>entry.name===method.symbol);if(!current||sha(current.code.replaceAll('\r\n','\n'))!==method.afterSha256)continue;
  for(const check of [...capabilityControlChecks,...legacySourceChecks])if(check.name===method.symbol||method.legacyChecks?.includes(check.name)){check.controlledRefactorVerified=true;check.refactorScope='exact-generated-primary-owner-consumer';check.expectedAfterSha256=method.afterSha256;}
 }
}
const evidenceGateCode=actual.entries.find(e=>e.name==='LOG:motion-evidence-generation-gate')?.code||'';
const evidenceKeyDeclaration=actual.entries.find(e=>e.name==='g_motionCapabilityLoggedPower')?.code||'';
const evidenceWriterChecks={
  ordinaryUint64Pair:/^static uint64_t g_motionCapabilityLoggedPower = 0, g_motionCapabilityLoggedBinding = 0;$/.test(evidenceKeyDeclaration),
  powerUsesCurrentGeneration:evidenceGateCode.includes('const auto capabilityPower = currentPowerGeneration();'),
  bindingUsesAtomicActiveGeneration:evidenceGateCode.includes('activeBindingGeneration.load(std::memory_order_acquire)'),
  noNonatomicSensorGeneration:!evidenceGateCode.includes('g_inputCaptureSensorGeneration'),
  powerAssignments:[...sourceBytes.toString('utf8').matchAll(/\bg_motionCapabilityLoggedPower\s*=/g)].length,
  bindingAssignments:[...sourceBytes.toString('utf8').matchAll(/\bg_motionCapabilityLoggedBinding\s*=/g)].length,
};
const sourceChecks={evidenceWriterChecks,capabilityControlChecks,legacySourceChecks,cacheBoundCalls:cacheCalls,cacheBoundHasOnlyCatalogFind:!!boundCode&&cacheCalls.every(n=>n==='ymcc::hc_model_motion::find'||n==='ymcc::capabilities::motion::consume'||n==='recipe->deviceClass.find'||n==='find'||n==='empty'||n==='ymcc::hc_model_motion::identityGyroId'||n==='ymcc::hc_model_motion::identityAccelId'),legacyCacheCalls,legacyCacheNoResolutionOrIO:!!legacyCache&&legacyCacheCalls.every(n=>n==='ymcc::hc_legacy_motion::find'||n==='ymcc::capabilities::motion::consume')};
const report={schema:'hc-model-motion-consumer-evidence-v4',status:'prepared',consumerOnlyControl:!!opt['consumer-only'],mode:opt['reviewed-baseline']?'reviewed-before-expected-failure':opt['new9-baseline']?'new9-historical-expected-failure':opt.baseline?'legacy-before-expected-failure':'candidate',out,
  source:{path:sourcePath,sha256:sha(sourceBytes)},before:{path:beforePath,sha256:sha(beforeBytes)},
  capabilityBefore:{path:capabilityBeforePath,sha256:sha(capabilityBeforeBytes)},historical:{path:historicalPath,sha256:sha(historicalBytes)},legacyCatalog:{path:legacyCatalogPath,sha256:sha(legacyCatalogBytes)},legacyOracle:expected.legacy.provenance,catalog:{path:catalogPath,sha256:sha(catalogBytes)},msi:{path:msiPath,sha256:sha(msiBytes)},
  runner:{path:runner,sha256:sha(fs.readFileSync(runner))},fixtures:fixtureInputs,
  oracleSha256:sha(fs.readFileSync(path.join(out,'expectations.json'))),generatedCppSha256:sha(generated),
  generatedInputsAtCreation,
  bindings,sourceChecks,configurations:[],mutations:[],boundaries:{fullBusExecuted:false,realSleepExecuted:false,realDevicesAccessed:false,
    executed:'Actual capability receipt/pure evidence-due helper, actual initial/detail capability JSON fields, lease-snapshot statement and generation-gated receipt block with memory event sink. Actual new9/legacy catalogs and full tuple lookup, identities, new9/legacy cold/bound helpers, original MSI/Xbox selectors, once-only entry, matrix application, provenance receipt, pair function and exact BUS admission/activation/GamepadMotion call prefixes with finite memory effect sink.',
    mocked:'Current power generation, controller lease snapshot and event sink are memory-only. Registry strings/read count, sensor metadata/readiness, enabled/live inputs, provider strings, atomic active binding generation. No OS registry, sensors, Host, hooks, input injection or power operations.',
    limitation:'Extracted standalone C++ process; no full BUS, backend callback lifetime, real sensor axes or Windows sleep/resume validation.'}};
const persist=()=>save('source-bindings.json',report);persist();
function run(command,argv,log,env=process.env,cwd=out) {
  const startedAt=Date.now(),raw=spawnSync(command,argv,{cwd,env,windowsHide:true,timeout:180000,maxBuffer:32*1024*1024});
  const r={...raw,stdout:(raw.stdout??Buffer.alloc(0)).toString('utf8'),stderr:(raw.stderr??Buffer.alloc(0)).toString('utf8')};
  if(/^(?:Release|Checked)\/(?:compile|run)\.log$/.test(log))r.motionProcess=captureMotionProcess({out,log,command,argv,env,cwd,raw,startedAt});
  fs.mkdirSync(path.dirname(path.join(out,log)),{recursive:true});
  fs.writeFileSync(path.join(out,log),(r.stdout||'')+(r.stderr||'')+(r.error?.message||''));
  return r;
}
function required(r,label) {if(r.error||r.status!==0)throw Error(`${label} failed (${r.status}); see evidence log`);return r.stdout;}
try {
  if(opt['generate-only']) {report.status='generated-only';persist();console.log(out);process.exit(0);}
  if(process.platform!=='win32')throw Error('MSVC Windows toolchain required');
  let vcvars=opt.vcvars;
  if(!vcvars) {
    const vswhere=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft Visual Studio/Installer/vswhere.exe');
    const install=required(run(vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],'toolchain-discovery.log'),'VS discovery').trim();
    vcvars=path.join(install,'VC/Auxiliary/Build/vcvars64.bat');
  }
  if(!fs.existsSync(vcvars)||/[\r\n"%]/.test(vcvars))throw Error('Unavailable/unsupported vcvars path');
  const envCmd=path.join(out,'compiler-environment.cmd');
  fs.writeFileSync(envCmd,calibrationToolchainPreamble(vcvars)+'set\r\n');
  // Environment output is parsed in memory; never persist credentials or the full environment.
  const er=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',envCmd],{encoding:'utf8',windowsHide:true,timeout:30000});
  if(er.error||er.status!==0)throw Error('MSVC environment setup failed');
  const env={...process.env};
  for(const line of er.stdout.split(/\r?\n/)) {const at=line.indexOf('=');if(at>0){const k=line.slice(0,at),existing=Object.keys(env).find(x=>x.toLowerCase()===k.toLowerCase());env[existing||k]=line.slice(at+1);}}
  fs.writeFileSync(path.join(out,'compiler-environment.log'),`vcvars=${vcvars}\nVCToolsVersion=${env.VCToolsVersion}\nWindowsSDKVersion=${env.WindowsSDKVersion}\n`);
  const traceAuthority=captureTraceAuthority(repo,out,report,generatedInputsAtCreation);
  report.compilerTrace={schema:'hc-motion-actual-compiler-inputs-v1',declaration:traceAuthority.ref,requiredForEveryConfiguration:true,osModuleLoadingProven:false};persist();
  for(const [name,flags] of [['Release',['/O2','/DNDEBUG','/MD']],['Checked',['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MDd']]]) {
    const dir=path.join(out,name);fs.mkdirSync(dir,{recursive:true});
    const exe=path.join(dir,'motion_consumer.exe');
    const argv=['/nologo','/std:c++20','/EHsc','/utf-8','/W4','/permissive-',...flags,`/I${path.join(repo,'deps/json')}`,cpp,
      `/Fo${path.join(dir,'harness.obj')}`,`/Fd${path.join(dir,'harness.pdb')}`,`/Fe${exe}`,'/sourceDependencies',path.join(dir,'source-dependencies.json'),'/link','/INCREMENTAL:NO','/verbose:lib'];
    const traceBefore=beginMotionCompile({out,name,argv,env,authority:traceAuthority});
    const compile=run('cl.exe',argv,`${name}/compile.log`,env,dir);
    required(compile,`${name} compilation`);
    const compilerTrace=finishMotionCompile({out,name,argv,before:traceBefore,processRecord:compile.motionProcess,authority:traceAuthority});
    const result=run(exe,[path.join(out,'expectations.json')],`${name}/run.log`,env,dir);
    const nativeProcess=requireMotionNativeProcess(out,result.motionProcess,exe);
    let parsed;try{parsed=JSON.parse(result.stdout.trim());}catch{throw Error(`${name}: no semantic test report`);}
    const new9Deficit=parsed.new9FeatureAvailable===false&&parsed.legacyFeatureAvailable===false&&parsed.aliasCoverage&&Object.values(parsed.aliasCoverage).every(c=>c.bound===0&&c.admitted===0)&&
      parsed.failures.some(f=>f.group==='aliases')&&!parsed.failures.some(f=>['preserved-routes','legacy-static','legacy-controller'].includes(f.group));
    const legacyDeficit=parsed.new9FeatureAvailable===true&&parsed.legacyFeatureAvailable===false&&parsed.failures.some(f=>f.group==='legacy-admission')&&!parsed.failures.some(f=>['preserved-routes','legacy-static','legacy-controller'].includes(f.group));
    const reviewedFailureGroups=new Set(['catalog','legacy-active-axes','legacy-matrices','legacy-receipt','legacy-unbound-receipt','reviewed-no-fallback','reviewed-receipt','reviewed-selection','reviewed-tuple']);
    const reviewedDeficit=parsed.reviewedResourceModels===18&&parsed.new9FeatureAvailable===true&&parsed.legacyFeatureAvailable===true&&parsed.failures.some(f=>f.group==='reviewed-selection')&&parsed.failures.some(f=>f.group==='legacy-matrices')&&parsed.failures.every(f=>reviewedFailureGroups.has(f.group));
    const accepted=isBaseline?result.status===1&&parsed.status==='fail'&&(opt['reviewed-baseline']?reviewedDeficit:opt['new9-baseline']?new9Deficit:legacyDeficit):result.status===0&&parsed.status==='pass';
    report.configurations.push({name,flags,compileCommand:['cl.exe',...argv],executableSha256:sha(fs.readFileSync(exe)),exitCode:result.status,
      accepted,report:parsed,compilerTrace,nativeProcess});persist();
    if(!accepted&&!opt['collect-failures'])throw Error(`${name}: ${isBaseline?'baseline did not demonstrate missing consumer':'semantic assertions failed'}`);
    console.log(`${name}: ${accepted?(isBaseline?'EXPECTED FAILURE':'PASS'):'FAIL'} (${parsed.checks} assertions, ${parsed.failures.length} failures)`);
  }
  if((opt['mutation-checks']||opt['reviewed-mutation-checks'])&&!isBaseline) {
    const source=sourceBytes.toString('utf8');
    const mutateFn=(name,change)=>{const original=actual.entries.find(e=>e.name===name)?.code;if(!original)throw Error(`Mutation definition unavailable: ${name}`);const modified=change(original);if(modified===original)throw Error(`Mutation did not alter ${name}`);return source.replace(original,modified);};
    const mutateConsumer=change=>mutateFn('BUS:motion-pair-admission',change);
    const mutants=[
      ['catalog-only-no-entry',()=>mutateFn('inputCaptureResolveHcMotionSelector',s=>s.replace('(void)inputCaptureResolveNewHcMotionSelector(g_machineIdentity, g_hcMotionSelector);','/* selection deleted */'))],
      ['catalog-only-no-bound',()=>mutateFn('inputCaptureMotionSelectorBound',s=>s.replace('return inputCaptureNewHcMotionSelectorBound();','return false;'))],
      ['resolver-trusts-forged-identity',()=>mutateFn('inputCaptureResolveNewHcMotionSelector',s=>s.replace('ymccResolveFamily(resolved);','/* raw DMI recheck deleted */'))],
      ['cache-wrong-source-sha',()=>mutateFn('inputCaptureNewHcMotionSelectorBound',s=>s.replace('g_hcMotionSelector.matrixSourceSha256 == recipe->sourceSha256','true'))],
      ['cache-family-unchecked',()=>mutateFn('inputCaptureNewHcMotionSelectorBound',s=>s.replace('if (g_machineIdentity.family != expectedFamily) return false;','/* family gate deleted */'))],
      ['cache-identity-gyro-unchecked',()=>mutateFn('inputCaptureNewHcMotionSelectorBound',s=>s.replace('g_machineIdentity.gyroMatrixId == ymcc::hc_model_motion::identityGyroId(*recipe)','true'))],
      ['new-model-asus-cache-shortcut',()=>mutateFn('inputCaptureMotionSelectorBound',s=>s.replace('if (ymcc::hc_model_motion::find(g_machineIdentity.deviceClass)) return inputCaptureNewHcMotionSelectorBound();','').replace('if(!g_hcMotionSelector.resolved) return false;','if(!g_hcMotionSelector.resolved) return false;\nif(g_hcMotionSelector.selectorId=="asus-rc73xa") return true;\nif (ymcc::hc_model_motion::find(g_machineIdentity.deviceClass)) return inputCaptureNewHcMotionSelectorBound();'))],
      ['pair-old-current-generation',()=>mutateFn('inputCaptureMotionPairAdmitted',s=>s.replace('gyroGeneration==currentGeneration','true'))],
      ['pair-zero-generation',()=>mutateFn('inputCaptureMotionPairAdmitted',s=>s.replace('gyroGeneration!=0','true'))],
      ['pair-mixed-generations',()=>mutateFn('inputCaptureMotionPairAdmitted',s=>s.replace('gyroGeneration==accelGeneration','true'))],
      ['pair-mixed-provider',()=>mutateFn('inputCaptureMotionPairAdmitted',s=>s.replace('&& sameProvider','&& true'))],
      ['pair-gyro-without-accel',()=>mutateFn('inputCaptureMotionPairAdmitted',s=>s.replace('&& accel','&& true'))],
      ['pair-disabled-or-suspended',()=>mutateFn('inputCaptureMotionPairAdmitted',s=>s.replace('enabled && live','true'))],
      ['actual-consumer-bypasses-pair',()=>mutateConsumer(s=>s.replace(/const bool pairProven = [^;]*inputCaptureMotionPairAdmitted\([^;]*\);/,'const bool pairProven = true;'))],
      ['accel-uses-gyro-matrix',()=>mutateFn('inputCaptureApplyHcMatrixCandidate',s=>s.replace('ymccApplyNamedImuMatrix(accelId,','ymccApplyNamedImuMatrix(gyroId,'))],
      ['receipt-reports-unbound-source',()=>mutateFn('inputCaptureCandidateMatrixReceipt',s=>s.replace('newRecipe && inputCaptureNewHcMotionSelectorBound()','newRecipe != nullptr'))],
      ['bound-does-registry-io',()=>mutateFn('inputCaptureNewHcMotionSelectorBound',s=>s.replace('{','{\n(void)sgRegSZ(HKEY_LOCAL_MACHINE,L"MOCK",L"BaseBoardProduct");'))],
      ['activation-drops-accel-z',()=>mutateFn('BUS:six-axis-activation',s=>s.replace('maz = hcCandidateAz;','/* dropped accel Z assignment */'))],
      ['gamepadmotion-bypasses-admission',()=>mutateFn('BUS:GamepadMotion-condition-and-call-prefix',s=>s.replace('((live && pairProven) || calibrationPairProven || privateMotionPairProven)','true'))],
      ['catalog-resource-sha-wrong',null,s=>s.replace(/"[0-9a-f]{64}"/,`"${'0'.repeat(64)}"`)],
      ['capability-evidence-ignores-binding',()=>mutateFn('inputCaptureMotionEvidenceDue',s=>s.replace('lastPower != power || lastBinding != binding','lastPower != power'))],
      ['capability-deletes-windows-consumer',()=>mutateFn('inputCaptureMotionCapabilityReceipt',s=>s.replace('{"inputConsumerImplemented", windows || tablet || legionS}','{"inputConsumerImplemented", false}'))],
      ['capability-observation-promotes-controller',()=>mutateFn('inputCaptureMotionCapabilityReceipt',s=>s.replace('{"inputConsumerImplemented", windows || tablet || legionS}','{"inputConsumerImplemented", windows || tablet || legionS || (controller && controllerObserved)}'))],
      ['legacy-cache-processor-unchecked',()=>mutateFn('inputCaptureLegacyHcMotionSelectorBound',s=>s.replace('g_hcMotionSelector.dmiProcessor == g_machineIdentity.processor','true'))],
      ['legacy-cache-version-unchecked',()=>mutateFn('inputCaptureLegacyHcMotionSelectorBound',s=>s.replace('g_hcMotionSelector.dmiSystemVersion == g_machineIdentity.systemVersion','true'))],
      ['legacy-crosspair-lookup',null,null,s=>s.replace('recipe.accelMatrixId == accelMatrixId','true')],
      ['legacy-controller-windows-admitted',()=>{let t=source;for(const name of ['inputCaptureResolveLegacyHcMotionSelector','inputCaptureLegacyHcMotionSelectorBound']){const s=actual.entries.find(e=>e.name===name).code;t=t.replace(s,s.replace(' || recipe->controllerPreferred',''));}return t;}],
      ['catalog-steammachine-added',null,s=>s.replace('inline constexpr Recipe recipes[] = {','inline constexpr Recipe recipes[] = {\n{"HandheldCompanion.Devices.SteamMachine","forged","gyro","accel","wrong","wrong"},')],
      ['generic-claims-exact-machine',()=>mutateFn('inputCaptureMotionCapabilityReceipt',v=>v.replace('{"machineIdentityResolved",identity.resolved}','{"machineIdentityResolved",true}'))],
      ['generic-cache-product-unchecked',()=>mutateFn('inputCaptureGenericHcMotionSelectorBound',v=>v.replace('g_hcMotionSelector.dmiProduct==g_machineIdentity.product','true'))],
      ['generic-cache-source-digest-unchecked',()=>mutateFn('inputCaptureGenericHcMotionSelectorBound',v=>v.replace('g_hcMotionSelector.matrixSourceSha256==recipe->sourceSha256','true'))],

    ];
    mutants.push(
      ['private-total-binding-missing',()=>mutateFn('inputCaptureResolveMsiMotionSelector',s=>s.replace('receipt.unifiedBinding=binding;','receipt.unifiedBinding={};'))],
      ['private-original-reference-unchecked',()=>mutateFn('inputCapturePrivateHcMotionSelectorRecipe',s=>s.replace(/const auto\* recipe=ymcc::capabilities::motion::consume\([\s\S]*?ymcc::hc_oem_windows_motion::recipes\);/,'const auto* recipe=ymcc::hc_oem_windows_motion::find(g_machineIdentity.deviceClass);'))],
      ['private-source-digest-unchecked',()=>mutateFn('inputCapturePrivateHcMotionSelectorRecipe',s=>s.replace('g_hcMotionSelector.matrixSourceSha256!=recipe->sourceSha256','false'))],
      ['private-current-model-unchecked',()=>{
        // Both consumers now enforce the same current-class constraint. Remove both
        // copies for this existing semantic fault; all other identity gates remain.
        let changed=mutateFn('inputCapturePrivateHcMotionSelectorRecipe',s=>s.replace('recipe->deviceClass!=g_machineIdentity.deviceClass','false'));
        const bound=actual.entries.find(e=>e.name==='inputCaptureMotionSelectorBound')?.code;
        if(!bound)throw Error('Current-model fault Bound definition unavailable');
        const call='ymcc::capabilities::motion::primaryKind(g_hcMotionSelector.unifiedBinding,g_machineIdentity.deviceClass)';
        if(bound.includes('ymcc::capabilities::motion::primaryKind(')){
          if(bound.split(call).length!==2)throw Error('Current-model fault primary-owner guard changed');
          changed=changed.replace(bound,bound.replace(call,'ymcc::capabilities::motion::primaryKind(g_hcMotionSelector.unifiedBinding,g_hcMotionSelector.deviceClass)'));
        }
        return changed;
      }]
    );
    const reviewedMutants=[
      ['reviewed-activation-omitted',()=>mutateFn('inputCaptureApplyHcMatrixCandidate',v=>v.replace('const bool selectedResource = resource && !resource->identityGyroMatrixId.empty() && inputCaptureNewHcMotionSelectorBound();','const bool selectedResource = false;'))],
      ['reviewed-processor-unchecked',()=>mutateFn('inputCaptureNewHcMotionSelectorBound',v=>v.replace('g_hcMotionSelector.dmiProcessor != g_machineIdentity.processor','false'))],
      ['reviewed-canonical-gyro-unchecked',()=>mutateFn('inputCaptureNewHcMotionSelectorBound',v=>v.replace('g_machineIdentity.gyroMatrixId == ymcc::hc_model_motion::identityGyroId(*recipe)','true'))],
      ['reviewed-provenance-static-ids',()=>mutateFn('inputCaptureCandidateMatrixReceipt',v=>v.replace('(gyro ? g_hcMotionSelector.gyroMatrixId : g_hcMotionSelector.accelMatrixId)','(gyro ? g_machineIdentity.gyroMatrixId : g_machineIdentity.accelMatrixId)'))],
    ];
    const selectedMutants=opt['reviewed-mutation-checks']?reviewedMutants:[...mutants,...reviewedMutants];
    const mutationRecords=new Array(selectedMutants.length),mutationEvents=[];
    report.mutationScheduling={concurrency:mutationJobs,total:selectedMutants.length,kind:'bounded-independent-owned-children'};
    const updateMutationSchedule=event=>{mutationEvents.push(event);save('mutation-scheduling.json',{schemaVersion:1,concurrency:mutationJobs,total:selectedMutants.length,events:mutationEvents});};
    await runBoundedTasks(selectedMutants,{concurrency:mutationJobs,onEvent:updateMutationSchedule,task:async([name,change,headerChange,legacyHeaderChange],mutationIndex)=> {
      const dir=path.join(out,'mutants',name);fs.mkdirSync(dir,{recursive:true});
      const mutantSource=change?change():source,mutantCatalog=headerChange?headerChange(catalogBytes.toString('utf8')):catalogBytes.toString('utf8'),mutantLegacy=legacyHeaderChange?legacyHeaderChange(legacyCatalogBytes.toString('utf8')):legacyCatalogBytes.toString('utf8');
      if(mutantSource===source&&mutantCatalog===catalogBytes.toString('utf8')&&mutantLegacy===legacyCatalogBytes.toString('utf8'))throw Error(`No mutation applied: ${name}`);
      const input=path.join(dir,'main.mutant.cpp'),header=path.join(dir,'catalog.mutant.h'),legacyHeader=path.join(dir,'legacy-catalog.mutant.h'),childOut=path.join(dir,'evidence');
      fs.writeFileSync(input,mutantSource);fs.writeFileSync(header,mutantCatalog);fs.writeFileSync(legacyHeader,mutantLegacy);
      // The controller-preferred barrier now has two consumers. This fault removes both
      // original and total-library copies so it still injects the same semantic defect.
      const bindingFault=[];
      if(name==='legacy-controller-windows-admitted'){const headerPath=path.join(dir,'binding.mutant.h');const raw=fs.readFileSync(path.join(repo,'capabilities/HcMotionCapabilityCatalog.h'),'utf8');const changed=raw.replace(' || recipe->controllerPreferred','');if(changed===raw)throw Error('Controller binding fault marker absent');fs.writeFileSync(headerPath,changed);bindingFault.push('--binding-header',headerPath);}
      const argv=[runner,...bindingFault,'--collect-failures','--source',input,'--catalog',header,'--before',beforePath,'--historical-before',historicalPath,'--capability-before',capabilityBeforePath,'--legacy-catalog',legacyHeader,'--out',childOut,'--vcvars',vcvars];
      if(!name.startsWith('capability-')&&!name.startsWith('generic-')&&!name.startsWith('private-'))argv.push('--consumer-only');
      const child=await runLoggedChild(process.execPath,argv,{cwd:out,env:process.env,timeoutMs:180000,outputLimit:32*1024*1024,onSpawn:submission=>save(`mutants/${name}/submitted.json`,submission)});
      fs.writeFileSync(path.join(dir,'runner.log'),(child.stdout||'')+(child.stderr||'')+(child.error?.message||''));
      save(`mutants/${name}/process.json`,{pid:child.pid,status:child.status,signal:child.signal,timedOut:child.timedOut,launchError:child.launchError,outputTruncated:child.outputTruncated,naturalExitObserved:child.naturalExitObserved,durationMs:child.durationMs,error:child.error?.message??null});
      let evidence;try{evidence=JSON.parse(fs.readFileSync(path.join(childOut,'source-bindings.json')));}catch{}
      // A killed mutant requires BOTH successful compilations and executable assertion failures.
      // --collect-failures runs the second build even when the first semantic report fails.
      const configs=evidence?.configurations||[];
      const detected=child.naturalExitObserved&&child.status===1&&configs.length===2&&configs.every(c=>c.exitCode===1&&c.report.status==='fail'&&c.report.failures.length>0);
      const mutationRecord={name,detected,childExitCode:child.status,evidence:childOut,
        configurations:configs.map(c=>({name:c.name,exitCode:c.exitCode,checks:c.report.checks,failedAssertions:c.report.failures.length,compilerTrace:c.compilerTrace,nativeProcess:c.nativeProcess,groups:[...new Set(c.report.failures.map(f=>f.group))]}))};
      mutationRecords[mutationIndex]=mutationRecord;report.mutations=mutationRecords.filter(Boolean);
      persist();console.log(`Mutation ${name}: ${detected?'DETECTED in Release + Checked':'NOT VERIFIED'}`);
      if(!detected)throw Error(`Mutation ${name} was not semantically detected in both configurations`);
      return mutationRecord;
    }});
    report.mutationScheduling={...report.mutationScheduling,completed:report.mutations.length,eventsFile:path.join(out,'mutation-scheduling.json'),eventsSha256:sha(fs.readFileSync(path.join(out,'mutation-scheduling.json')))};persist();
  }
  const finishBytes=fs.readFileSync(sourcePath),finish=finishBytes.toString('utf8');
  report.sourceAtFinish={sha256:sha(finishBytes),extractedDefinitionsUnchanged:actual.entries.every(e=>finish.includes(e.code)),
    legacyCatalogUnchanged:sha(fs.readFileSync(legacyCatalogPath))===sha(legacyCatalogBytes),legacyProvenanceUnchanged:sha(fs.readFileSync(expected.legacy.provenance.path))===expected.legacy.provenance.sha256,legacyPinnedSourcesUnchanged:expected.legacy.sourceChecks.every(f=>sha(fs.readFileSync(path.join(repo,f.path)))===f.sha256)&&[expected.legacy.baseline,expected.legacy.sourceIndex,expected.legacy.identityInputs].every(f=>sha(fs.readFileSync(f.path))===f.sha256),capabilityBeforeUnchanged:sha(fs.readFileSync(capabilityBeforePath))===sha(capabilityBeforeBytes),historicalUnchanged:sha(fs.readFileSync(historicalPath))===sha(historicalBytes),catalogUnchanged:sha(fs.readFileSync(catalogPath))===sha(catalogBytes),msiUnchanged:sha(fs.readFileSync(msiPath))===sha(msiBytes),beforeUnchanged:sha(fs.readFileSync(beforePath))===sha(beforeBytes),fixturesUnchanged:fixtureInputs.every(f=>sha(fs.readFileSync(f.path))===f.sha256),runnerUnchanged:sha(fs.readFileSync(runner))===report.runner.sha256};
  if(!report.sourceAtFinish.capabilityBeforeUnchanged||!report.sourceAtFinish.legacyPinnedSourcesUnchanged||!report.sourceAtFinish.legacyCatalogUnchanged||!report.sourceAtFinish.legacyProvenanceUnchanged||!report.sourceAtFinish.historicalUnchanged||!report.sourceAtFinish.extractedDefinitionsUnchanged||!report.sourceAtFinish.catalogUnchanged||!report.sourceAtFinish.msiUnchanged||!report.sourceAtFinish.beforeUnchanged||!report.sourceAtFinish.fixturesUnchanged||!report.sourceAtFinish.runnerUnchanged)throw Error('Relevant production input changed during test; rerun against final snapshot');
  if(report.configurations.some(c=>!c.accepted)&&!isBaseline)throw Error('Semantic failures in candidate');
  if(!isBaseline&&!opt['collect-failures']&&(!evidenceWriterChecks.ordinaryUint64Pair||!evidenceWriterChecks.powerUsesCurrentGeneration||!evidenceWriterChecks.bindingUsesAtomicActiveGeneration||!evidenceWriterChecks.noNonatomicSensorGeneration||evidenceWriterChecks.powerAssignments!==2||evidenceWriterChecks.bindingAssignments!==2))throw Error('Capability evidence writer ownership/atomic-read source audit failed');
  if(!isBaseline&&!opt['collect-failures']&&!capabilityControlChecks.every(c=>c.unchanged||c.controlledRefactorVerified))throw Error('Capability diagnostic changed an existing consumer control definition');
  if(!opt['collect-failures']&&(!legacySourceChecks.every(c=>c.unchanged||c.controlledRefactorVerified)||(!isBaseline&&(!sourceChecks.cacheBoundHasOnlyCatalogFind||!sourceChecks.legacyCacheNoResolutionOrIO))))throw Error('Legacy verbatim body/cache-bound call audit failed');
  report.status=isBaseline?'expected-failure-confirmed':'pass';persist();console.log(`Evidence: ${out}`);
} catch(error) {report.status='fail';report.error=error.message;persist();console.error(error.message);console.error(`Evidence: ${out}`);process.exitCode=1;}
