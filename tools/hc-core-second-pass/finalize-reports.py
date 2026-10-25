"""Offline evidence finalization only. Never writes outside this tool directory and its fixed scratch core."""
import hashlib,json,datetime
from pathlib import Path
import xml.etree.ElementTree as ET
TOOLS=Path(__file__).resolve().parent
REPO=TOOLS.parents[1]
WORK=Path(r'D:/Codex/2026-10-18/kai-s/work/hc-third-trim/core')
def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,x):Path(p).write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
base=read(WORK/'baseline-copy.json');inventory=read(WORK/'candidate-inventory.json');roots=read(WORK/'roots-v3.xml.json')
load=read(WORK/'load-unsigned-final.json');il=read(WORK/'retained-IL-unsigned-final.json');devices=read(WORK/'device-IL-unsigned-final.json');closure=read(WORK/'closure-unsigned-final.json');lifecycle=read(WORK/'lifecycle-unsigned-final/verification.json');callbacks=read(WORK/'callback-unsigned-final.json');signatures=read(WORK/'signature-baseline-candidate.json')
assert all(r['status']=='passed' for r in [load,il,devices,closure,lifecycle,callbacks])
assert inventory['identity']=='HandheldCompanion, Version=0.32.4.0, Culture=neutral, PublicKeyToken=null'
assert signatures['candidateOnlyFailures']==[]
source=REPO/'PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1'
source_diffs=[]
for row in base['records']:
 p=source/row['path'];actual=sha(p)
 if actual!=row['sha256']:source_diffs.append({'path':row['path'],'baselineSha256':row['sha256'],'currentSha256':actual,'dll':p.suffix.lower()=='.dll'})
assert not any(x['dll'] for x in source_diffs),'Production DLL drift must be reviewed, not silently accepted'
assert sha(source/'HandheldCompanion.dll')==base['hc']['sha256']
files=[p for p in (WORK/'candidate').rglob('*') if p.is_file()]
assert len(files)==86
size=sum(p.stat().st_size for p in files);candidate=WORK/'candidate/HandheldCompanion.dll';saved=base['hc']['bytes']-candidate.stat().st_size
live_size=sum(p.stat().st_size for p in source.rglob('*') if p.is_file())
assert size==base['bytes']-saved
# Resource payloads and all retained opaque entries are intentionally unchanged this round.
bi=read(WORK/'baseline-inventory-resources.json');resources=[]
for old in bi['resources']:
 new=next(x for x in inventory['resources'] if x['name']==old['name'])
 assert old==new
 resources.append({'name':old['name'],'bytes':old['bytes'],'entries':len(old['entries'] or []),'identical':True})
save(WORK/'resource-preservation.json',{'status':'passed','resources':resources,'removedResources':[],'ledThumbnailsRetained':11,'potentialUiThumbnailRawBytes':107169})
# Every XML descriptor entry has explicit provenance, including metadata-derived manager types and App fields.
provenance=[]
for t in ET.parse(WORK/'roots-v3.xml').getroot().find('assembly').findall('type'):
 n=t.attrib['fullname'];p=[x for x in roots['provenance'] if x['type']==n]
 if not p:p=[{'type':n,'reason':'Explicit conservative headless consumer root in CoreTrim.Roots','source':'tools/hc-core-second-pass/Program.cs; actual Host and callback/pure-constructor sources frozen below'}]
 provenance.append({'type':n,'preserve':t.attrib.get('preserve'),'sources':p})
source_sites=[];source_hashes={};drift=[]
for site in roots['consumerReflectionSites']:
 p=REPO/site['path'];lines=p.read_text(encoding='utf-8-sig').splitlines()
 if site['line']>len(lines) or lines[site['line']-1]!=site['text']:drift.append(site)
 source_hashes[site['path']]=sha(p);source_sites.append(site)
assert not drift,'Consumer source lines changed since root generation'
save(WORK/'REFLECT-ROOT-SOURCES.json',{'status':'lexical-census-and-explicit-roots-recorded','fullRootTypes':len(roots['rootTypes']),'descriptorTypes':len(provenance),'entries':provenance,'consumerReflectionSiteCount':len(source_sites),'consumerReflectionSites':source_sites,'sourceSha256':source_hashes,'sourceLinesStableSinceRoots':True,'unchangedSharedLogManager':{'source':'FanLab/real-host/Program.cs:8695-8700; FanLab/LightSetter/Program.cs:477-483','assemblySha256':sha(WORK/'baseline/Shared.dll'),'preservation':'entire Shared.dll unchanged'},'initializerClosureReceipt':'closure-unsigned-final.json','retainedReflectionAndSerializerSites':len(closure['reflectionAndSerializerSites']),'semanticReflectionClosureProven':False})
deleted=load['evidence'][0]['deletedTypes'];save(WORK/'DELETED-TYPES.json',{'originalRuntimeGetTypes':1768,'candidateRuntimeGetTypes':1557,'deletedCount':len(deleted),'deletedTypes':deleted,'removedMethods':closure['removedMethods'],'remainingDanglingILReferences':closure['missingInternalReferences']})
checks=[
 ('original-baseline-copy','passed','baseline-copy.json'),('all-original-device-IL-fields','passed','device-IL-unsigned-final.json'),('all-retained-method-IL-native-layouts','passed','retained-IL-unsigned-final.json'),('factory-initializer-static-forward-closure-and-negative-IL-references','passed','closure-unsigned-final.json'),('required-types-full-GetTypes-77-primary-and-91-total-devices','passed','load-unsigned-final.json'),('22-in-memory-ACPI-callback-checks','passed','callback-unsigned-final.json'),('production-shared-original-lifecycle','passed','lifecycle-baseline/verification.json'),('unsigned-production-shared-candidate-lifecycle','passed','lifecycle-unsigned-final/verification.json'),('all-method-signatures-diagnostic',signatures['status'],'signature-baseline-candidate.json'),('resource-payload-preservation','passed','resource-preservation.json')]
receipts=[{'check':n,'status':s,'path':str(WORK/p),'sha256':sha(WORK/p)} for n,s,p in checks]
blockers=[{'id':'REFLECTION_SEMANTIC_CLOSURE_UNPROVEN','detail':'348 retained HC reflection/serialization sites and 649 consumer lexical sites have complete locations and context but are not all classified into live Host/LightSetter targets versus excluded UI/profile flows. Root preservation and GetTypes/fixtures do not prove that arbitrary runtime-selected member names or serialized model types survive.'},{'id':'FACTORY_LIFECYCLE_EXECUTION_OUT_OF_SCOPE','detail':'13 Factory field manager types, Status, profile events/CloneProfile and the 771-method constructor/implicit-cctor closure are retained and independently IL-compared. Real ManagerFactory/device/OpenEvents lifecycle was not executed under the hardware prohibition; static closure is reported as such.'}]
report={'schemaVersion':1,'status':'FINAL_NO_PROMOTION','decision':'REJECT_PROMOTION_THIS_ROUND','promotionApproved':False,'candidate':{'path':str(candidate),'bytes':candidate.stat().st_size,'sha256':sha(candidate),'assemblyIdentity':inventory['identity'],'strongNameAdded':False,'typeCount':1557,'primaryDevices':77,'allConcreteDeviceSubclasses':91,'trialOnly':True},'baseline':{'runtimeBytes':base['bytes'],'runtimeFiles':base['files'],'hcBytes':base['hc']['bytes'],'hcSha256':base['hc']['sha256'],'snapshot':str(WORK/'baseline')},'net':{'hcSavedBytes':saved,'hcSavedPercent':round(saved/base['hc']['bytes']*100,4),'candidateRuntimeFiles':len(files),'candidateRuntimeBytesWithFrozenOriginalJson':size,'currentParentRuntimeBytes':live_size,'projectedRuntimeBytesWithParentJsonOnly':live_size-saved,'projectedBytesAreNotPromoted':True,'previousUnpromotedDevicesV2HcBytes':2241536,'additionalBytesVsOldUnpromotedHc':2241536-candidate.stat().st_size},'requiredChecks':receipts,'independentComparison':{'originalDeviceMetadataTypes':devices['deviceTypes'],'deviceMethods':devices['methods'],'deviceFields':devices['fields'],'retainedMethods':il['results'][0]['methods'],'nativeLayouts':il['results'][0]['nativeLayouts'],'initializerClosureMethods':closure['initializerClosureCount'],'layoutAuditTypes':len(closure['layoutRows']),'missingKeptILReferences':len(closure['missingInternalReferences']),'resourcesUnchanged':True},'signatureDiagnostic':{'status':signatures['status'],'baselineFailedMethods':signatures['results'][0]['failureCount'],'candidateFailedMethods':signatures['results'][1]['failureCount'],'candidateOnlyFailures':0,'existingWindowsDisplayApiMissing':True,'dependenciesAdded':False,'fullGetTypesNotRelaxed':True},'signedTrial':{'path':str(WORK/'rejected-signed/HandheldCompanion.dll'),'bytes':2239488,'sha256':'2ede83a9727a8e500994a22ca537b42feee76e907c63c3bbc60a5204f5c41e63','rejected':True,'reason':'Introduced strong-name token 538ea1f68f629a04 where original HC has null. Latest parent instruction requires unchanged HC identity. Only a YMCC development derivative was ever claimed.'},'blockers':blockers,'productionSourceDriftByOtherTasks':source_diffs,'wroteProductionRuntimeProfileHostNativeDeployment':False,'productHostDeviceDriverSdlNativeSteamExecuted':False,'networkUsed':False,'metadataTokenRebase':'zero-touch-token-rebase.json; exact original method identities and original fixed tokens checked; candidate full retained IL equality pinned before replacing only three fixture token constants','noFifthExploration':True,'nextSpecificCuts':['Classify the recorded 348 retained reflection/serializer sites against actual Host/LightSetter receiver types and string-value sources, especially profile CloneProfile/Newtonsoft schemas and manager event/status reflection.','Use DELETED-TYPES.json and zero incoming retained IL evidence to assess UI-only regions; preserve actual model getter reflection such as LEDPreset.NameKey/Value and all implicit type-initializer side effects.','The 11 OneX LED thumbnail payloads total 107169 raw bytes. LEDPreset constructor stores Icon as a string, while LightSetter uses NameKey/Value; prove that no actual headless resource stream/render consumer reaches them before a future resource cut. They remain intact now.','No application library AssemblyRef disappeared in this candidate, so no leaf DLL removal is approved from this work. Other agents keep ownership of SDK/WinRT/UI libraries.']}
for p in [WORK/'STAGE-REPORT.json',TOOLS/'STAGE-REPORT.json']:save(p,report)
promotion={'schemaVersion':1,'status':'NO_PROMOTION','promotionApproved':False,'replace':[],'remove':[],'deploy':[],'experimentalOnly':[report['candidate']],'blockers':blockers,'identityConstraint':'HandheldCompanion, Version=0.32.4.0, Culture=neutral, PublicKeyToken=null','sourceBeforeSha256':base['hc']['sha256'],'parentJsonCompactWorkIndependent':True,'manifestRule':'Never deploy the candidate scratch overlay or its old runtime manifest. This list authorizes no replacement.'}
for p in [WORK/'PROMOTION-LIST.json',TOOLS/'PROMOTION-LIST.json']:save(p,promotion)
save(WORK/'VALIDATION-RECEIPTS.json',{'finalUnsigned':receipts,'rawFailuresPreserved':['linker-v1.log','resource-reader-failure.log','load-v2.json','load-v2b.json','closure-v2.json','pure-constructors-baseline.json','build-PassiveTimerFixture-initial-failure.log','development-signature-initial-failure.log','retained-IL-signed-rejected.json','lifecycle-candidate/SdlTouchpadProbe-candidate-zero.json','signature-baseline-candidate.json'],'failedAttemptsAreNotFinalPassReceipts':True})
print(json.dumps({'status':report['status'],'candidateBytes':report['candidate']['bytes'],'candidateSha256':sha(candidate),'runtimeBytes':size,'saved':saved,'sourceChangedPaths':[x['path'] for x in source_diffs]},ensure_ascii=False))
