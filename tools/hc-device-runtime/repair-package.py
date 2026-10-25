"""Create a runtime repair with an optional verified managed Host component build.

The executable build identity is inherited, never relabelled as a fresh source build.

Every payload entry and the regenerated export inventory are verified afterwards.

"""

from pathlib import Path, PurePosixPath

from zipfile import ZipFile, ZIP_DEFLATED

import argparse, copy, datetime, hashlib, json



ap=argparse.ArgumentParser(__doc__)

ap.add_argument('--base',required=True)

ap.add_argument('--base-sha256',required=True)

ap.add_argument('--runtime',required=True)

ap.add_argument('--output',required=True)

ap.add_argument('--host-payload')

ap.add_argument('--host-provenance')

ap.add_argument('--lighting-payload')

ap.add_argument('--lighting-provenance')

a=ap.parse_args()

sha=lambda b:hashlib.sha256(b).hexdigest()

readjson=lambda b:json.loads(b.decode('utf-8-sig'))

encoded=lambda obj:(json.dumps(obj,ensure_ascii=False,indent=2)+'\n').encode('utf-8')

base=Path(a.base).resolve(); runtime=Path(a.runtime).resolve(); output=Path(a.output).resolve()

if output.exists():raise RuntimeError('Refuse to overwrite an existing package')

if sha(base.read_bytes())!=a.base_sha256.lower():raise RuntimeError('BASE_ARCHIVE_SHA_MISMATCH')

manifest_bytes=(runtime/'HandheldCompanion.runtime.json').read_bytes();manifest=readjson(manifest_bytes)

repo=Path(__file__).resolve().parents[2]

profile_path=repo/'deps/ymcc-device-core-runtime/profile.json';profile_bytes=profile_path.read_bytes();profile=readjson(profile_bytes)

if manifest.get('deviceCoreProfileSha256')!=sha(profile_bytes) or manifest.get('runtimeId')!=profile.get('runtimeId'):raise RuntimeError('RUNTIME_PROFILE_BINDING_MISMATCH')

files={p.relative_to(runtime).as_posix():p.read_bytes() for p in runtime.rglob('*') if p.is_file()}

expected={r['path'].replace('\\','/'):r['sha256'].lower() for r in manifest['files']}

if {k:sha(v) for k,v in files.items() if k!='HandheldCompanion.runtime.json'}!=expected:raise RuntimeError('RUNTIME_EXACT_CLOSURE_MISMATCH')

def clean(name):

 name=name.replace('\\','/');path=PurePosixPath(name)

 if path.is_absolute() or '..' in path.parts or ':' in name:raise RuntimeError('UNSAFE_ARCHIVE_PATH')

 return name

# The profile authorizes replacements by exact size and SHA-256, regardless of DLL name.

upstream_index_bytes=(repo/'deps/ymcc-device-core-runtime/upstream.runtime.json').read_bytes()

if sha(upstream_index_bytes)!=profile['sourceManifestSha256']:raise RuntimeError('UNAPPROVED_UPSTREAM_INDEX')

upstream_index=readjson(upstream_index_bytes)

original_hashes={clean(x['path']):x['sha256'].lower() for x in upstream_index['files']}

source_expected=dict(original_hashes)

for row in profile['replacements']:

 name=clean(row['path'])

 if name not in source_expected:raise RuntimeError('UNKNOWN_PROFILE_REPLACEMENT: '+name)

 if name not in files or len(files[name])!=row['bytes']:raise RuntimeError('PROFILE_REPLACEMENT_SIZE_MISMATCH: '+name)

 source_expected[name]=row['sha256']

for row in profile['exclusions']:source_expected.pop(clean(row['path']))

if {name:sha(data) for name,data in files.items() if name!='HandheldCompanion.runtime.json'}!=source_expected:raise RuntimeError('RUNTIME_PROFILE_CLOSURE_MISMATCH')

prefix='PowerControl/handheldcompanion-runtime/'+manifest['runtimeId']+'/'

changes=[]

host_files={};host_provenance=None;host_build_inputs=[];host_manifest=None;host_prefix=None

if bool(a.host_payload)!=bool(a.host_provenance):raise RuntimeError('HOST_PAYLOAD_AND_BUILD_PROVENANCE_REQUIRED_TOGETHER')

if a.host_payload:

 host_root=Path(a.host_payload).resolve();proof_path=Path(a.host_provenance).resolve()

 host_provenance=readjson(proof_path.read_bytes())

 host_files={p.relative_to(host_root).as_posix():p.read_bytes() for p in host_root.rglob('*') if p.is_file()}

 host_manifest=readjson(host_files['YeManFanHost.payload.json'])

 if host_manifest['schemaVersion']!=2 or host_manifest['runtimeId']!=manifest['runtimeId']:raise RuntimeError('HOST_RUNTIME_IDENTITY_MISMATCH')

 target_manifest='../handheldcompanion-runtime/'+manifest['runtimeId']+'/HandheldCompanion.runtime.json'

 if host_manifest['runtimeManifest'].replace(chr(92),'/')!=target_manifest:raise RuntimeError('HOST_RUNTIME_MANIFEST_PATH_MISMATCH')

 host_records={clean(r['path']):r['sha256'].lower() for r in host_manifest['files']}

 if len(host_records)!=len(host_manifest['files']) or {n:sha(b) for n,b in host_files.items() if n!='YeManFanHost.payload.json'}!=host_records:raise RuntimeError('HOST_EXACT_CLOSURE_MISMATCH')

 build_outputs={r['file']:r['sha256'].lower() for r in host_provenance['outputHashes']}

 for name in ('YeManFanHost.dll','YeManFanBackend.dll','YeManFanHost.exe'):

  if sha(host_files[name])!=build_outputs.get(name):raise RuntimeError('HOST_BUILD_OUTPUT_HASH_MISMATCH: '+name)

 if host_provenance.get('forcedExit') or not host_provenance.get('currentSourceMatchesTested') or host_provenance.get('currentInputMismatches'):raise RuntimeError('HOST_BUILD_PROVENANCE_NOT_VERIFIED')

 source_index=proof_path.parent/host_provenance['sourceInputs']

 host_build_inputs=readjson(source_index.read_bytes())

 if not host_build_inputs:raise RuntimeError('EMPTY_HOST_BUILD_SOURCE_INDEX')

 for row in host_build_inputs:

  relative=clean(row['path']);current=repo/relative

  if sha(current.read_bytes())!=row['sha256'].lower():raise RuntimeError('HOST_BUILD_SOURCE_CHANGED: '+relative)

 if sha((repo/'FanLab/real-host/Program.cs').read_bytes())!=host_provenance['sourceSha256'].lower():raise RuntimeError('HOST_PROGRAM_SOURCE_CHANGED')

 lifecycle_path=Path(host_provenance['hostLifecycleEvidence']);lifecycle=readjson(lifecycle_path.read_bytes())
 if sha(lifecycle_path.read_bytes())!=host_provenance['hostLifecycleEvidenceSha256'] or lifecycle.get('status')!='PASS' or lifecycle.get('candidateHostSha256')!=sha(host_files['YeManFanHost.dll']) or lifecycle.get('hardwareCalls')!=0 or lifecycle.get('nativeAttempts')!=0 or lifecycle.get('hostMainStarted') is not False or len(lifecycle.get('cases',[]))<25 or any(x.get('status')!='PASS' for x in lifecycle['cases']):raise RuntimeError('HOST_LIFECYCLE_GATE_NOT_VERIFIED')
 selftest_path=Path(host_provenance['hostSelfTestEvidence']);selftest=readjson(selftest_path.read_bytes())
 if sha(selftest_path.read_bytes())!=host_provenance['hostSelfTestEvidenceSha256'] or selftest.get('status')!='PASS' or selftest.get('processExitCode')!=0 or selftest.get('checksPassed',0)<400 or selftest.get('hostDllSha256')!=sha(host_files['YeManFanHost.dll']) or selftest.get('sourceChanged'):raise RuntimeError('HOST_FULL_SELFTEST_NOT_VERIFIED')
 full_path=Path(host_provenance['fullGateEvidence']);full=readjson(full_path.read_bytes())
 required_suites={'fan_connection','tool_gate','invariants','extremes','regressions','gyro_core','signals','api_contract','cycle'}
 suites=full.get('extra',{}).get('suites',[])
 if sha(full_path.read_bytes())!=host_provenance['fullGateEvidenceSha256'] or full.get('verdict')!='ALL_PASS' or full.get('exitCode')!=0 or {x.get('id') for x in suites}!=required_suites or len(suites)!=9 or any(x.get('exitCode')!=0 for x in suites) or full.get('realSleepPerformed') is not False or full.get('hardwareActionPerformed') is not False:raise RuntimeError('HOST_NINE_SUITE_GATE_NOT_VERIFIED')
 fan_suite=next(x for x in suites if x['id']=='fan_connection');fan_args=fan_suite['args'];fan_root=Path(fan_args[fan_args.index('-EvidenceDir')+1]);fan_runs=list(fan_root.glob('sim-fan-*.json'))
 if len(fan_runs)!=1:raise RuntimeError('HOST_FAN_GATE_IDENTITY_MISSING')
 fan_gate=readjson(fan_runs[0].read_bytes())
 if fan_gate.get('verdict')!='ALL_PASS' or fan_gate['identity']['host']['sha256'].lower()!=sha(host_files['YeManFanHost.dll']) or fan_gate['identity']['runtimeManifest']['sha256'].lower()!=sha(manifest_bytes):raise RuntimeError('HOST_FAN_GATE_INPUT_IDENTITY_MISMATCH')
 host_build_record={'provenanceSha256':sha(proof_path.read_bytes()),'buildCommand':host_provenance['buildCommand'],'sourceSha256':host_provenance['sourceSha256'].lower(),'inputs':host_build_inputs,'outputs':host_provenance['outputHashes'],'hostPayloadManifestSha256':sha(host_files['YeManFanHost.payload.json']),'currentSourceBuildClaimed':False,'componentBuildMatchesInputsAtPackaging':True}

lighting_files={};lighting_build_record=None;lighting_prefix='PowerControl/light-setter/'

if bool(a.lighting_payload)!=bool(a.lighting_provenance):raise RuntimeError('LIGHTING_PAYLOAD_AND_BUILD_PROVENANCE_REQUIRED_TOGETHER')

if a.lighting_payload:

 lighting_root=Path(a.lighting_payload).resolve();lighting_proof_path=Path(a.lighting_provenance).resolve()

 lighting_proof=readjson(lighting_proof_path.read_bytes())

 lighting_names={'YeManLightDeviceCore.dll','YeManLightSetter.dll','YeManLightSetter.exe','YeManLightSetter.json','YeManLightSetter.deps.json','YeManLightSetter.runtimeconfig.json'}

 lighting_files={clean(p.relative_to(lighting_root).as_posix()):p.read_bytes() for p in lighting_root.rglob('*') if p.is_file() and p.suffix.lower()!='.pdb'}

 if set(lighting_files)!=lighting_names:raise RuntimeError('LIGHTING_EXACT_CLOSURE_MISMATCH')

 lighting_outputs={r['file']:r['sha256'].lower() for r in lighting_proof['outputHashes']}

 if {n:sha(b) for n,b in lighting_files.items()}!=lighting_outputs:raise RuntimeError('LIGHTING_BUILD_OUTPUT_HASH_MISMATCH')

 for row in lighting_proof['sourceInputs']:

  relative=clean(row['path'])

  if sha((repo/relative).read_bytes())!=row['sha256'].lower():raise RuntimeError('LIGHTING_BUILD_SOURCE_CHANGED: '+relative)

 if len(lighting_proof['sourceInputs'])<9 or not any(x.get('path')=='FanLab/LightSetter/OneXLightingSession.cs' for x in lighting_proof['sourceInputs']):raise RuntimeError('LIGHTING_COMPILED_INPUTS_MISSING')

 evidence_path=Path(lighting_proof['validationEvidence']);evidence_bytes=evidence_path.read_bytes();evidence=readjson(evidence_bytes)

 if sha(evidence_bytes)!=lighting_proof['validationEvidenceSha256']:raise RuntimeError('LIGHTING_EVIDENCE_HASH_MISMATCH')

 behavior=evidence['compatibility']['productionHelper']['artifactBehavior']

 if not evidence.get('boundHidLightingContract',{}).get('passed') or not evidence.get('onexRequestCacheContract',{}).get('passed') or not evidence.get('onexLightingContract',{}).get('passed') or not evidence.get('msiLightingContract',{}).get('passed') or evidence['status']!='PASS' or evidence['hardwareIo'] is not False or evidence['compatibility']['differences'] or evidence['compatibility']['changedDuringRun'] or not behavior['passed'] or behavior['checksPassed']<177 or behavior['mainEntryInvoked'] is not False or behavior['nativeAttempts'] or behavior['dependencyAttempts']:raise RuntimeError('LIGHTING_ARTIFACT_BEHAVIOR_NOT_VERIFIED')

 if not any(x['name']=='actual Run is wired to the lighting-only boundary' and x['passed'] for x in behavior['checks']):raise RuntimeError('LIGHTING_OPEN_ISOLATION_NOT_VERIFIED')

 if not all(any(c.get('name')==name and c.get('passed') for c in behavior['checks']) for name in ['actual Run prepares the OneX lighting transport session','actual Run releases the OneX transport in finally','actual action consumer prepares requested serial cache before dispatch','actual preset consumer prepares explicit brightness cache before dispatch']):raise RuntimeError('ONEX_LIGHTING_SESSION_NOT_VERIFIED')

 if behavior['actualSha256']!=sha(lighting_files['YeManLightSetter.dll']) or lighting_proof['helperSha256']!=behavior['actualSha256']:raise RuntimeError('LIGHTING_TESTED_HELPER_MISMATCH')

 lighting_build_record={**lighting_proof,'provenanceSha256':sha(lighting_proof_path.read_bytes()),'componentBuildMatchesInputsAtPackaging':True,'currentSourceBuildClaimed':False}

with ZipFile(base) as source:

 infos=source.infolist();lookup={clean(i.filename):i for i in infos if not i.is_dir()}

 if len(lookup)!=sum(not i.is_dir() for i in infos):raise RuntimeError('DUPLICATE_ARCHIVE_MEMBER')

 export_name='YeManCC/export-identity.json';update_name='YeManCC/update-manifest.json'

 original_export=readjson(source.read(lookup[export_name]));original_update=readjson(source.read(lookup[update_name]))

 base_inventory={n:{'path':n,'bytes':lookup[n].file_size,'sha256':sha(source.read(lookup[n]))} for n in lookup if n!=export_name}

 if {r['path']:r for r in original_export['payload']}!=base_inventory or original_export['payloadFileCount']!=len(base_inventory):raise RuntimeError('BASE_EXPORT_INVENTORY_INVALID')

 replacements={prefix+k:v for k,v in files.items()}

 update=copy.deepcopy(original_update);binding=update['rules']['fanHostPayload']

 if binding['runtimeDirectory'].replace('\\','/')!=prefix[len('PowerControl/'):].rstrip('/'):raise RuntimeError('UNEXPECTED_RUNTIME_DIRECTORY')

 binding['runtimeManifestSha256']=sha(manifest_bytes)

 payload_path='PowerControl/'+binding['directory']+'/YeManFanHost.payload.json'

 if sha(source.read(lookup[payload_path]))!=binding['manifestSha256'].lower():raise RuntimeError('BASE_HOST_BINDING_INVALID')

 if host_manifest is not None:

  host_prefix='PowerControl/'+clean(binding['directory']).rstrip('/')+'/'

  old_host_manifest=readjson(source.read(lookup[host_prefix+'YeManFanHost.payload.json']))

  old_host_records={r['path']:r['sha256'].lower() for r in old_host_manifest['files']}

  if set(old_host_records)!=set(host_records):raise RuntimeError('HOST_COMPONENT_FILE_SET_CHANGED')

  allowed_changes={'YeManFanHost.dll','YeManFanBackend.dll','YeManFanHost.authorization.md'}

  for name,old_hash in old_host_records.items():

   if sha(source.read(lookup[host_prefix+name]))!=old_hash:raise RuntimeError('BASE_HOST_MEMBER_INVALID: '+name)

   if name not in allowed_changes and sha(host_files[name])!=old_hash:raise RuntimeError('UNAPPROVED_HOST_COMPONENT_CHANGE: '+name)

  authorization=host_files['YeManFanHost.authorization.md'].decode('utf-8-sig')

  for marker,value in [('approvedHostExeSha256',sha(host_files['YeManFanHost.exe'])),('approvedHostDllSha256',sha(host_files['YeManFanHost.dll'])),('approvedHcSha256',expected['HandheldCompanion.dll'])]:

   if marker+': '+value.upper() not in authorization:raise RuntimeError('HOST_AUTHORIZATION_IDENTITY_MISMATCH: '+marker)

  replacements.update({host_prefix+k:v for k,v in host_files.items()})

  binding['manifestSha256']=sha(host_files['YeManFanHost.payload.json']);binding['fileCount']=len(host_records)

 if lighting_build_record is not None:

  base_lighting={n.removeprefix(lighting_prefix) for n in lookup if n.startswith(lighting_prefix)}

  if base_lighting!=set(lighting_files):raise RuntimeError('BASE_LIGHTING_FILE_SET_CHANGED')

  for name,data in lighting_files.items():

   if name!='YeManLightSetter.dll' and sha(source.read(lookup[lighting_prefix+name]))!=sha(data):raise RuntimeError('UNAPPROVED_LIGHTING_COMPONENT_CHANGE: '+name)

  replacements.update({lighting_prefix+k:v for k,v in lighting_files.items()})

 replacements[update_name]=encoded(update)

 selected=set(lookup)-{n for n in lookup if n.startswith(prefix) or (host_prefix and n.startswith(host_prefix))}

 selected.update(replacements)

 inventory=[]

 for name in sorted(selected):

  if name==export_name:continue

  data=replacements[name] if name in replacements else source.read(lookup[name])

  inventory.append({'path':name,'bytes':len(data),'sha256':sha(data)})

 exported=copy.deepcopy(original_export)

 exported['exportedAtUtc']=datetime.datetime.now(datetime.timezone.utc).isoformat()

 exported['payloadFileCount']=len(inventory);exported['payload']=inventory

 exported['runtimeRepair']={'kind':'dependency-only-verified-full-package-derivation','baseArchiveSha256':a.base_sha256.lower(),'inheritedExecutableBuildId':original_export['buildId'],'inheritedSourceTreeSha256':original_export['sourceTreeSha256'],'executablesRecompiled':False,'currentSourceBuildClaimed':False,'runtimeManifestSha256':sha(manifest_bytes),'deviceCoreProfileSha256':sha(profile_bytes),'restoredUpstreamFiles':profile.get('interopAbiRepair',{}).get('restoredFiles',[])}

 if host_manifest is not None:

  exported['runtimeRepair']['kind']='runtime-and-managed-host-verified-full-package-derivation'

  exported['runtimeRepair']['managedHostRecompiled']=True

  exported['runtimeRepair']['nativeAndFrontendRecompiled']=False

  exported['runtimeRepair']['hostBuild']=host_build_record

 if lighting_build_record is not None:

  exported['runtimeRepair']['managedLightSetterRecompiled']=True

  exported['runtimeRepair']['lightingBuild']=lighting_build_record

 replacements[export_name]=encoded(exported)

 output.parent.mkdir(parents=True,exist_ok=True)

 with ZipFile(output,'w',compression=ZIP_DEFLATED,compresslevel=6,allowZip64=True) as target:

  for info in infos:

   name=clean(info.filename)

   if info.is_dir():target.writestr(info,b'');continue

   if name not in selected:changes.append({'path':name,'action':'remove','beforeSha256':sha(source.read(info))});continue

   before=source.read(info);data=replacements.get(name,before)

   if data!=before:changes.append({'path':name,'action':'replace','beforeBytes':len(before),'afterBytes':len(data),'beforeSha256':sha(before),'afterSha256':sha(data)})

   target.writestr(info,data)

  for name in sorted(selected-set(lookup)):

   data=replacements[name];target.writestr(name,data);changes.append({'path':name,'action':'add','afterBytes':len(data),'afterSha256':sha(data)})

with ZipFile(output) as target:

 if target.testzip() is not None:raise RuntimeError('ARCHIVE_CRC_FAILED')

 actual={clean(i.filename):{'path':clean(i.filename),'bytes':i.file_size,'sha256':sha(target.read(i))} for i in target.infolist() if not i.is_dir() and clean(i.filename)!=export_name}

 stored=readjson(target.read(export_name if export_name in target.namelist() else next(i.filename for i in target.infolist() if clean(i.filename)==export_name)))

 if actual!={r['path']:r for r in stored['payload']} or len(actual)!=stored['payloadFileCount']:raise RuntimeError('FINAL_EXPORT_INVENTORY_INVALID')

 if stored['runtimeRepair']['baseArchiveSha256']!=a.base_sha256.lower():raise RuntimeError('DERIVATION_BINDING_INVALID')

report={'status':'PASS','scope':'dependency-only repair of verified normal full package; executable build lineage inherited','archive':str(output),'archiveSha256':sha(output.read_bytes()),'archiveBytes':output.stat().st_size,'fileCount':len(actual)+1,'runtimeFiles':len(files),'runtimeBytes':sum(map(len,files.values())),'runtimeManifestSha256':sha(manifest_bytes),'profileSha256':sha(profile_bytes),'changedFiles':changes,'exportInventoryExact':True,'currentSourceBuildClaimed':False,'baseBuildId':original_export['buildId'],'hostAndExecutablesUnchanged':all(c['path'].startswith(prefix) or c['path'] in (update_name,export_name) for c in changes),'installedDeploymentPerformed':False,'physicalHardwareVerified':False}

if host_manifest is not None:

 report.update(scope='runtime plus explicitly rebuilt managed Host component; native/frontend build lineage inherited',managedHostRecompiled=True,nativeAndFrontendRecompiled=False,hostBuild=host_build_record,hostManifestSha256=sha(host_files['YeManFanHost.payload.json']))

 report['nativeAndFrontendUnchanged']=all(c['path'].startswith(prefix) or c['path'].startswith(host_prefix) or (lighting_build_record is not None and c['path'].startswith(lighting_prefix)) or c['path'] in (update_name,export_name) for c in changes)

if lighting_build_record is not None:

 report.update(managedLightSetterRecompiled=True,lightingBuild=lighting_build_record,scope='runtime and verified managed component repairs; native/frontend build lineage inherited')

 report['nativeAndFrontendUnchanged']=all(c['path'].startswith(prefix) or (host_prefix is not None and c['path'].startswith(host_prefix)) or c['path'].startswith(lighting_prefix) or c['path'] in (update_name,export_name) for c in changes)

output.with_name('PACKAGE-AUDIT.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

print(json.dumps({k:report[k] for k in ['status','archiveSha256','fileCount','runtimeBytes','exportInventoryExact']},indent=2))

