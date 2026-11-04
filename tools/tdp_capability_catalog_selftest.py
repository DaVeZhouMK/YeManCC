"""Focused unified TDP consumer replay. Never loads PawnIO or invokes a product/helper."""
from pathlib import Path
import argparse,ast,hashlib,json,struct,subprocess
from log_frequency_selftest import ROOT,extract

def read(p): return json.loads(p.read_text(encoding='utf-8-sig'))
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def module(p):
 b=p.read_bytes();prefix=4+struct.unpack_from('<I',b)[0];assert prefix==516
 x=b[prefix:];keys=['size','magic','file_version','amx_version','flags','defsize','cod','dat','hea','stp','cip','publics','natives','libraries','pubvars','tags','nametable'];h=dict(zip(keys,struct.unpack_from('<IHBBHH12I',x)))
 assert h['size']==len(x) and h['magic']==0xf1e1 and h['file_version']==h['amx_version']==11 and h['defsize']==8
 result={}
 for key,end in [('publics','natives'),('natives','libraries')]:
  rows={}
  for at in range(h[key],h[end],8):
   addr,name=struct.unpack_from('<II',x,at);s=x[name:x.index(b'\0',name)].decode('ascii');assert s not in rows;rows[s]=addr
  result[key]=rows
 return result

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--output-dir',type=Path,required=True);args=parser.parse_args();out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
 policy=read(ROOT/'capabilities/uxtu-tdp-policy.json');catalog=read(ROOT/'capabilities/hc-device-catalog.json');lock=read(ROOT/'tools/release-assets.lock.json')
 tool=read(ROOT/'tools/ymcc_sim/global/tdp-protocol-consumer/toolchain-inputs.json');checks=[]
 def check(name,condition):
  assert condition,name
  checks.append(name)
 check('unified-catalog-TDP-domain-references-one-policy-and-existing-engines',catalog['domains']['tdp']['policyFile']=='capabilities/uxtu-tdp-policy.json' and catalog['domains']['tdp']['applyHcDeclaredLimits'] is False)
 check('core-and-voltage-production-activation-still-false',policy['activation']=={'tdp':True,'coreSwitch':False,'voltageScheduling':False} and 'LEGACY_CCD_UNDERVOLT_ACTIVE = false' in (ROOT/'src/bridge/legacyCpuFeaturePolicy.ts').read_text(encoding='utf-8'))
 check('manual-CCD-and-CPU-offset-enabled-without-legacy-startup-or-GPU-consumers',policy['ccdControl']['activation'] is True and policy['voltageOffset']['activation'] is True and policy['manualControls']=={'mode':'explicit_settings_apply','restoreSavedOnStartup':False,'automaticReapply':False,'voltageConsumerPlanes':['core','cache'],'physicalCcdPowerOff':False} and 'CCD_MANUAL_CONTROL_ACTIVE = true' in (ROOT/'src/bridge/cpuCapabilityPolicy.generated.ts').read_text(encoding='utf-8') and 'CPU_VOLTAGE_OFFSET_ACTIVE = true' in (ROOT/'src/bridge/cpuCapabilityPolicy.generated.ts').read_text(encoding='utf-8'))
 check('tdp-generic-capability-metadata-is-cpu-only-not-model-admission',catalog['domains']['tdp']['modelBinding']['source'].startswith('diagnostics-only') and 'handheldCoverage' not in catalog['domains']['tdp'] and catalog['domains']['tdp']['applyHcDeclaredLimits'] is False and policy['dispatchContract']['hardwareAcceptanceFromCatalog'] is False and policy['dispatchContract']['applyHcDeclaredLimits'] is False and not any('whitelist' in key.lower() or 'modelcandidate' in key.lower() for key in policy.keys()))
 module_reports=[]
 for m in policy['moduleCandidates']:
  p=ROOT/'PowerControl/pawnio'/m['name'];abi=module(p);upgraded=False;expected=m['runtimeSha256']
  if m.get('sourceOnlyCandidatePath'):
   candidate=ROOT/m['sourceOnlyCandidatePath'];candidate_abi=module(candidate)
   check('source-only-candidate-preserved-without-runtime-replacement-'+m['name'],digest(candidate)==m['sha256'] and set(m['requiredExistingPublics'])<=set(candidate_abi['publics']) and expected==m['previousSha256'])
  check('module-exact-locked-identity-'+m['name'],digest(p)==expected and set(m['requiredExistingPublics'])<=set(abi['publics']))
  row=next(x for x in lock['files'] if x['releasePath']=='pawnio/'+m['name']);check('release-lock-bound-'+m['name'],row['sha256'].lower()==expected)
  check('root-internal-module-copies-match-'+m['name'],digest(ROOT/'PowerControl/pawnio/_internal'/m['name'])==expected)
  module_reports.append({'name':m['name'],'upgraded':upgraded,'sha256':expected,'publics':list(abi['publics']),'natives':list(abi['natives']),'scope':'passive container/public ABI identity; no VM, signature/driver load or hardware acceptance'})
 check('all-21-current-PawnIO-assets-match-release-lock',sum(row['component']=='PawnIO' for row in lock['files'])==21 and all(digest(ROOT/row['fallbackPath'])==row['sha256'].lower() for row in lock['files'] if row['component']=='PawnIO'))
 # Compile the actual native descriptor and its pure CPU/board predicates with inert identity ports.
 source=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig')
 signatures=['static std::string ymccDmiNormalize(std::string value) {','static constexpr bool tdpCpuTokenEquals(std::string_view token, std::string_view expected) {','static constexpr std::string_view tdpRegistryIdentityText(std::string_view value) {','static constexpr bool tdpIsG3eCpu(std::string_view vendor, std::string_view processor) {','static constexpr bool tdpG3eOemRecipeBoard(std::string_view board) {','static json tdpExecutionCapability() {']
 slices={name:extract(source,name) for name in signatures}
 prelude=r'''#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "json.hpp"
#include "TdpCapabilityCatalog.generated.h"
using json=nlohmann::json;
struct Machine {bool attempted=true;std::string processor,product,deviceClass;} g_machineIdentity;
static std::string vendor="AuthenticAMD";
static constexpr int HKEY_LOCAL_MACHINE=0;
static std::string sgRegSZ(int,const wchar_t*,const wchar_t*){return vendor;}
static unsigned long long currentPowerGeneration(){return 7;}
static unsigned long long GetTickCount64(){return 1000;}
static constexpr int tdpIntelDefaultPl2Offset=ymcc::capabilities::tdp::intelPl2Offset;
'''
 test=r'''int main(int argc,char**){std::vector<json> rows;auto check=[&](const char* name,bool pass){rows.push_back({{"name",name},{"passed",pass}});};
 g_machineIdentity={true,"AMD Ryzen 7 7840U","AYANEO 2S","HandheldCompanion.Devices.AYANEO2S"};
 const auto first=ymcc::capabilities::aliases.front();g_machineIdentity.deviceClass=first.name;
 auto cap=tdpExecutionCapability();check("default-domain-sourced-from-library",cap["minPL1"]==2&&cap["maxPL1"]==200&&!cap["selected"].get<bool>());
 check("actual-native-consumer-joins-existing-model-alias",cap["capabilityCatalog"]["modelId"]==std::string(ymcc::capabilities::devices[first.device].deviceId));
 if(argc==1){
  for(const auto& alias:ymcc::capabilities::aliases){g_machineIdentity.deviceClass=alias.name;auto c=tdpExecutionCapability();check("every-original-model-alias-retains-unified-id",c["capabilityCatalog"]["modelRegistered"]==true&&c["capabilityCatalog"]["modelId"]==std::string(ymcc::capabilities::devices[alias.device].deviceId));}
  g_machineIdentity.deviceClass="unknown-device";cap=tdpExecutionCapability();check("unknown-model-never-invents-identity-or-watt-maximum",cap["capabilityCatalog"]["modelRegistered"]==false&&cap["capabilityCatalog"]["modelId"]==""&&cap["machineMaximumKnown"]==false&&cap["minPL1"]==2&&cap["maxPL1"]==200);
  g_machineIdentity.product="ms-1t91";g_machineIdentity.processor="Intel(R) Arc(TM) G3 Extreme";vendor="GenuineIntel";cap=tdpExecutionCapability();check("existing-precise-board-and-CPU-provider-ready-contract-retained",cap["selected"]==true&&cap["ready"]==true&&cap["provider"]=="msi-g3e-wmi"&&cap["minPL1"]==8&&cap["maxPL1"]==200&&cap["generation"]==7&&cap["deadlineTick"]==9000&&cap["pl2Offset"]==1);
  vendor.push_back('\0');cap=tdpExecutionCapability();check("existing-vendor-terminal-NUL-normalization-retained",cap["ready"]==true);
  vendor="AuthenticAMD";cap=tdpExecutionCapability();check("wrong-CPU-on-selected-board-is-not-ready-and-not-default-fallback",cap["selected"]==true&&cap["ready"]==false&&cap["provider"]=="msi-g3e-wmi");
  vendor="GenuineIntel";g_machineIdentity.product="OTHER-BRAND";cap=tdpExecutionCapability();check("same-CPU-other-board-keeps-default-transport",cap["selected"]==false&&cap["provider"]=="default"&&cap["minPL1"]==2&&cap["cpuFamily"]=="intel-arc-g3-extreme");
  check("registration-does-not-activate-other-capabilities-or-claim-hardware",!ymcc::capabilities::tdp::coreSwitchActive&&!ymcc::capabilities::tdp::voltageSchedulingActive&&!ymcc::capabilities::tdp::applyHcDeclaredLimits&&cap["capabilityCatalog"]["sourceLimitsApplied"]==false&&cap["capabilityCatalog"]["hardwareAcceptance"]==false);
  check("native-and-generated-library-version-match",cap["capabilityCatalog"]["libraryId"]=="ymcc.uxtu.tdp"&&cap["capabilityCatalog"]["version"]=="26.3.1"&&cap["capabilityCatalog"]["policySha256"]==std::string(ymcc::capabilities::tdp::policySha256));
  g_machineIdentity.attempted=false;bool blocked=false;try{tdpExecutionCapability();}catch(const std::runtime_error&){blocked=true;}check("original-uninitialized-identity-gate-retained",blocked);
  g_machineIdentity={true,"Intel(R) Arc(TM) G3 Extreme","OTHER-BRAND","unknown-device"};vendor="GenuineIntel";const json unknownModelCap=tdpExecutionCapability();
  g_machineIdentity.deviceClass=first.name;const json knownModelCap=tdpExecutionCapability();
  check("same-CPU-known-and-unknown-model-keep-identical-generic-capability",unknownModelCap["selected"]==false&&knownModelCap["selected"]==false&&unknownModelCap["provider"]==knownModelCap["provider"]&&unknownModelCap["minPL1"]==knownModelCap["minPL1"]&&unknownModelCap["maxPL1"]==knownModelCap["maxPL1"]&&unknownModelCap["cpuFamily"]==knownModelCap["cpuFamily"]&&unknownModelCap["ready"]==knownModelCap["ready"]&&knownModelCap["capabilityCatalog"]["modelRegistered"]==true&&unknownModelCap["capabilityCatalog"]["modelRegistered"]==false&&unknownModelCap["capabilityCatalog"]["modelId"]==""&&knownModelCap["capabilityCatalog"]["modelId"]!=""&&unknownModelCap["capabilityCatalog"]["sourceLimitsApplied"]==false);
  g_machineIdentity={true,"Intel(R) Arc(TM) G3 Extreme","ms-1t91","unknown-device"};vendor="GenuineIntel";const json oemUnknownModelCap=tdpExecutionCapability();
  check("OEM-CPU-readiness-does-not-depend-on-model-registration",oemUnknownModelCap["selected"]==true&&oemUnknownModelCap["ready"]==true&&oemUnknownModelCap["provider"]=="msi-g3e-wmi"&&oemUnknownModelCap["capabilityCatalog"]["modelRegistered"]==false);
 }
 int failed=0;for(const auto& r:rows)if(!r["passed"].get<bool>())++failed;
 std::cout<<json({{"checks",rows},{"failedCount",failed},{"allPassed",failed==0},{"realHardwareOperations",0}}).dump();return failed?1:0;
}'''
 vc=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat');native_reports=[]
 sdk_root=Path(r'C:\Program Files (x86)\Windows Kits\10');sdk=next((q for q in sorted((sdk_root/'Include').iterdir(),reverse=True) if (q/'um/Windows.h').is_file() and (q/'ucrt/stdio.h').is_file()),None);assert sdk is not None,'Existing Windows SDK required'
 sdk_flags=''.join(' /I"'+str(sdk/name)+'"' for name in ('um','shared','ucrt','winrt'))
 sdk_libs=' /link'+''.join(' /LIBPATH:"'+str(sdk_root/'Lib'/sdk.name/name/'x64')+'"' for name in ('um','ucrt'))
 for label,mutation in [('positive',None),('negative-range',('{"minPL1", requestDomain.minimumW}','{"minPL1", 0}')),('negative-model-binding',('modelId(g_machineIdentity.deviceClass)','modelId("unknown-device")'))]:
  folder=out/label;folder.mkdir();body='\n'.join(slices.values())
  if mutation:assert body.count(mutation[0])==1;body=body.replace(*mutation)
  cpp=folder/'fixture.cpp';cpp.write_text(prelude+'\n'+body+'\n'+test,encoding='utf-8');exe=folder/'fixture.exe'
  command='@echo off\ncall "'+str(vc)+'" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "'+str(cpp)+'" /I"'+str(ROOT/'deps/json')+'" /I"'+str(ROOT/'capabilities')+'"'+sdk_flags+' /Fo"'+str(folder/'fixture.obj')+'" /Fe"'+str(exe)+'"'+sdk_libs+'\n'
  batch=folder/'build.cmd';batch.write_text(command);compiled=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=folder,capture_output=True,encoding='utf-8',errors='replace',timeout=30);(folder/'compile.log').write_text(compiled.stdout+compiled.stderr,encoding='utf-8');assert compiled.returncode==0,compiled.stdout+compiled.stderr
  run=subprocess.run([str(exe)]+(['negative-selected-cases'] if mutation else []),cwd=folder,capture_output=True,encoding='utf-8',errors='replace',timeout=5);(folder/'stdout.json').write_text(run.stdout,encoding='utf-8');value=json.loads(run.stdout)
  check('actual-native-'+label,(run.returncode==1 and value['failedCount']>0) if mutation else (run.returncode==0 and value['allPassed']))
  native_reports.append({'label':label,'checkCount':len(value['checks']),'failedCount':value['failedCount'],'report':str(folder/'stdout.json'),'actualSourceSlices':{k:hashlib.sha256(v.encode()).hexdigest() for k,v in slices.items()}})
 node_check=out/'policy-negatives.mjs'
 node_check.write_text("import fs from 'node:fs';import {validateTdpPolicy} from "+json.dumps((ROOT/'tools/lib/tdp_capability_catalog.mjs').as_uri())+";const p=JSON.parse(fs.readFileSync("+json.dumps(str(ROOT/'capabilities/uxtu-tdp-policy.json'))+",'utf8'));const c=JSON.parse(fs.readFileSync("+json.dumps(str(ROOT/'capabilities/hc-device-catalog.json'))+",'utf8'));let caught=0;for(const mutate of [x=>{x.activation.coreSwitch=true},x=>{x.dispatchContract.applyHcDeclaredLimits=true},x=>{x.familyRecipes.FAM_APU_FP6.mirror_fast=0x32},x=>{x.familyRecipes.FAM_DRAGON_FIRE.mirror_transport='rsmu';x.familyRecipes.FAM_DRAGON_FIRE.mirror_slow=0xCB},x=>{x.familyRecipes.FAM_DESKTOP_AM5.mirror_transport='rsmu';x.familyRecipes.FAM_DESKTOP_AM5.mirror_slow=0xCB},x=>{x.moduleCandidates[0].name='../other.bin'},x=>{x.familyRecipes.FAM_DESKTOP_AM5.co_enc='u32'},x=>{x.familyRecipes.FAM_HSMP_FP10_AM5.co_enc='u20'},x=>{x.source.upstreamFiles.find(f=>f.path==='Views/Windows/MainWindow.xaml.cs').sha256='0'.repeat(64)},x=>{x.voltageOffset.amd.encodingProducer.nonHsmp='u32'}]){const x=structuredClone(p);mutate(x);let rejected=false;try{validateTdpPolicy(x,c)}catch(e){if(!String(e.message).startsWith('TDP_CATALOG_'))throw e;rejected=true}if(!rejected)throw Error('negative accepted');caught++}console.log(JSON.stringify({caught}));",encoding='utf-8')
 rejected=subprocess.run([tool['nodeExecutable'],str(node_check)],cwd=ROOT,capture_output=True,encoding='utf-8',timeout=10);(out/'policy-negatives.log').write_text(rejected.stdout+rejected.stderr,encoding='utf-8');check('actual-policy-builder-rejects-ten-boundary-protocol-and-producer-mutations',rejected.returncode==0 and json.loads(rejected.stdout)['caught']==10)
 generated=subprocess.run([tool['nodeExecutable'],str(ROOT/'tools/tdp_capability_catalog_generate.mjs'),'--check'],cwd=ROOT,capture_output=True,encoding='utf-8',timeout=10);(out/'generator-check.log').write_text(generated.stdout+generated.stderr,encoding='utf-8');check('generator-is-current',generated.returncode==0)
 manifest_out=out/'runtime-manifest-delivery';manifest_run=subprocess.run([tool['powershellExecutable'],'-NoProfile','-NonInteractive','-File',str(ROOT/'tools/tdp_runtime_manifest_selftest.ps1'),'-EvidenceDir',str(manifest_out)],cwd=ROOT,capture_output=True,encoding='utf-8',errors='replace',timeout=20);(out/'runtime-manifest-delivery.log').write_text(manifest_run.stdout+manifest_run.stderr,encoding='utf-8');manifest_result=json.loads((manifest_out/'RESULT.json').read_text(encoding='utf-8-sig'));check('actual-package-and-deploy-runtime-sidecar-consumers-with-eight-negatives',manifest_run.returncode==0 and manifest_result['allPassed'] and len(manifest_result['checks'])==10)
 report={'schemaVersion':1,'status':'PASS','passed':True,'checks':checks,'native':native_reports,'modules':module_reports,'inactivePolicyPreserved':True,'modelSourceClasses':len(catalog['devices']),'noModuleLoadOrHardware':True,'binaryCompatibilityScope':'Module bytes/container/public exports plus actual native capability consumer replay. Target VM behavior, module signature/driver admission and hardware acceptance NOT_RUN.','sourceSlicesBound':True,'fullAcceptance':False}
 (out/'tdp-catalog-result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('TDP catalog consumer: PASS '+str(len(checks)));return 0
if __name__=='__main__':raise SystemExit(main())
