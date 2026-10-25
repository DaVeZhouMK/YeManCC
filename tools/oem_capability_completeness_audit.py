"""Model capability completeness: source/metadata/selected paths, not physical acceptance.
Direction verification is deferred by user; never counted as a completeness gate here.
"""
import argparse,hashlib,json,pathlib,re
from oem_capability_evidence_matrix import parse_hc,effective
p=argparse.ArgumentParser();p.add_argument('--native-matrix',required=True);p.add_argument('--hc-devices',required=True);p.add_argument('--source-reachability',required=True);p.add_argument('--runtime-lighting',required=True);p.add_argument('--repo',required=True);p.add_argument('--output',required=True);p.add_argument('--controller-contract');a=p.parse_args()
repo=pathlib.Path(a.repo);out=pathlib.Path(a.output);out.mkdir(parents=True,exist_ok=True)
def text(p):return p.read_text(encoding='utf-8-sig')
def load(p):return json.loads(text(pathlib.Path(p)))
main=text(repo/'native/main.cpp');helper=text(repo/'FanLab/LightSetter/Program.cs');card=text(repo/'src/components/ControllerFeedbackCapabilitiesCard.vue');feedback=text(repo/'src/bridge/controllerFeedbackCapabilities.ts');host=text(repo/'InputHost/Program.cs');api=text(repo/'src/bridge/api.ts')
native=load(a.native_matrix);models={m['deviceClass']:m for m in native['models']};hc=parse_hc(pathlib.Path(a.hc_devices));source=load(a.source_reachability);sources={c['deviceClass']:c for c in source['classes']};runtime=load(a.runtime_lighting);bindings={c['deviceClass']:c for c in runtime['classes']}
# An OEM-chord/catalog audit cannot discover keys absent from the catalog.
# Accept the controller-layer supplement only when pinned-source provenance and
# both current producer/Host source identities are present; never merge stale claims.
controllerContract = load(a.controller_contract) if a.controller_contract else None
controllerFindings = {}
if controllerContract:
 assert controllerContract.get('committedSourcesVerified') is True, 'Controller contract needs verified committed HC sources'
 assert controllerContract.get('HCCommit') == '06c0b9544db2b1f39abf9cd3796225ccfb096103', 'Unexpected controller reference commit'
 contractBindings={b['role']:b for b in controllerContract['sourceBindings']}
 for role,path in [('nativeMain',repo/'native/main.cpp'),('inputHost',repo/'InputHost/Program.cs')]:
  assert contractBindings[role]['sha256']==hashlib.sha256(path.read_bytes()).hexdigest(), 'Stale controller source binding: '+role
 for finding in controllerContract['findings']:
  assert finding['deviceClass'] in models, 'Controller finding outside queried capability inventory'
  controllerFindings.setdefault(finding['deviceClass'],[]).append({k:v for k,v in finding.items() if k!='deviceClass'})
# These findings require current path anchors, not just historical prose.
legacyBrightnessGate='const bool brightnessAvailable = rogLightingFamily;' in main
operationGate='const bool brightnessAvailable = rogLightingFamily && ymcc::lighting::brightnessImplemented(fbDeviceClass);' in main
assert legacyBrightnessGate or operationGate, 'Review changed brightness availability path'
presetRouteExists=all(x in helper for x in ['ExecuteLedPreset','--preset-id','SetLEDPreset','NameKey']) and all(x in main for x in ['validPreset','--action preset --preset-id','ledPresets']) and 'presetId' in card and 'presetId' in api
if operationGate:assert all('lightingOperations' in m for m in models.values()), 'New operation gate needs current compiled per-model evidence'
assert '["preset"] = 128' in helper and 'contract.SetColor.Invoke' in helper
assert 'outputCallbackBound: false' in feedback and 'physicalRouteClosed: false' in feedback
assert not re.search(r'\b(?:Rumble|Vibration|Feedback|OutputReport|outputCallback)\b',host,re.I), 'Inspect new Host feedback implementation before declaring bridge absent'
assert 'YMCC renders a static two-zone split.' in main
bits={n:1<<int(v) for n,v in re.findall(r'kVk(\w+)\s*=\s*1u\s*<<\s*(\d+)',main)}
canon={'LControl':'Ctrl','RControl':'Ctrl','LControlKey':'Ctrl','RControlKey':'Ctrl','LShift':'Shift','RShift':'Shift','LShiftKey':'Shift','RShiftKey':'Shift','LMenu':'Alt','RMenu':'Alt','RAlt':'Alt','LMenuKey':'Alt','RMenuKey':'Alt','LWin':'Win','RWin':'Win','LWinKey':'Win','RWinKey':'Win','Escape':'Esc','Delete':'Del','Snapshot':'Snap'}
rows=[];findings=[];encodings=0;unmatched=[];implementedAwaitingHardware=[];unsupportedOperations=[]
for name,m in sorted(models.items()):
 h=effective(hc,name.split('.')[-1]);src=sources[name];bound=bindings[name];key_gaps=[k['keyId'] for k in src['keys'] if not k['sourceCandidateDeclared']];issues=list(controllerFindings.get(name,[]))
 if key_gaps:issues.append({'id':'catalog-key-no-source','keys':key_gaps,'status':'missing','scope':'no selected keyboard/vendor publisher; declaration alone is insufficient'})
 rules=[{'keyId':k['keyId'],**r} for k in m['keys'] for r in k['keyboardRules']];seen=set();encoding_rows=[]
 for c in h['chords']:
  if c['silenced']:continue
  keys={canon.get(k,k) for k in c['keys']};identity=(c['buttonFlag'],tuple(sorted(keys)))
  if identity in seen:continue
  seen.add(identity);encodings+=1
  if not keys:encoding_rows.append({'name':c['name'],'buttonFlag':c['buttonFlag'],'kind':'non-keyboard-declaration','keyboardCoverage':'not-applicable'});continue
  mouse=bool(keys&{'LButton','XButton2','OemClear'});unknown=keys-set(bits)-{'LButton','XButton2','OemClear'};mask=sum(bits.get(k,0) for k in keys)
  match=[r for r in rules if r['mouseChord'] and mouse or not mouse and not unknown and not r['mouseChord'] and r['keysMask'] and mask&r['keysMask']==r['keysMask']]
  entry={'name':c['name'],'buttonFlag':c['buttonFlag'],'keys':sorted(keys),'matchedCanonicalCandidates':sorted(set(r['keyId'] for r in match)),'kind':'mouse' if mouse else 'keyboard','coverage':'selected-profile-subset-rule' if match else 'missing','physicalActionVerified':False};encoding_rows.append(entry)
  if not match:unmatched.append({'deviceClass':name,**entry})
 if m['lightingDeclared'] and 'preset' in m['ledEffects']:
  cb=next(c for c in bound['callbacks'] if c['method']=='SetLEDPreset')
  if presetRouteExists and m.get('lightingOperations',{}).get('presetImplemented'):
   implementedAwaitingHardware.append({'deviceClass':name,'operation':'named-lighting-preset','status':'implemented-offline-verified-physical-pending','scope':'compiled per-class admission + native stable-ID command + actual HC list object invocation + bounded queued-write observation'})
  else:issues.append({'id':'lighting-preset-no-call-route','status':'missing','runtimeCallback':cb,'scope':'HC preset declaration lacks usable transport for this type' if m.get('lightingOperations',{}).get('presetDeclaredButUnavailable') else 'HC declares named presets but no complete per-model ID/SetLEDPreset route'})
 brightness_cb=next(c for c in bound['callbacks'] if c['method']=='SetLedBrightness')
 if bound['backendCandidate']=='HC-LightSetter' and m['lightingDeclared'] and brightness_cb['runtimeOwner']=='HandheldCompanion.Devices.IDevice' and brightness_cb['constantBooleanBody']=='true':
  if operationGate and m.get('lightingOperations',{}).get('brightnessImplemented') is False and 'v-if="caps.brightnessAvailable"' in card:
   unsupportedOperations.append({'deviceClass':name,'operation':'hardware-brightness','status':'not-implemented-in-HC-current-hardware-contract','declarationCorrected':True,'scope':'no-op method is no longer exposed or sent as a real brightness operation; no new protocol invented'})
  else:issues.append({'id':'brightness-ui-to-noop','status':'overstated','scope':'capability query/UI offers brightness but actual helper resolves constant-true base SetLedBrightness; no hardware brightness command'})
 if 'ambilight' in m['ledEffects']:
  issues.append({'id':'ambilight-static-only','status':'partial','scope':'manual two-color update exists; HC screen-driven color sampling/update lifecycle has no YMCC feedback-card route'})
 issues.append({'id':'game-feedback-bridge-missing','status':'missing','scope':'manual pulse route exists separately; current virtual Host has no game feedback callback/physical forwarding/release path'})
 for issue in issues:findings.append({'deviceClass':name,**issue})
 rows.append({'deviceClass':name,'family':m['family'],'keyInventory':{'catalogCount':len(m['keys']),'staticSourceCandidateCount':sum(k['sourceCandidateDeclared'] for k in src['keys']),'sourceGaps':key_gaps,'keys':src['keys'],'HCVisibleEncodingInventory':encoding_rows},'lighting':{'declared':m['lightingDeclared'],'effects':m['ledEffects'],'supportsSecondColor':m['secondColorDeclared'],'backendCandidate':bound['backendCandidate'],'sourceRuntimeBindingIssues':bound['bindingIssues'],'physicalEffectsVerified':False},'vibration':{'manualBackendCandidate':'managed-MSI-HID-or-admitted-physical-XInput' if m['family']=='MsiClaw' else 'admitted-physical-XInput','manualRuntimeReadinessVerified':False,'manualPhysicalEffectVerified':False,'gameFeedbackBridge':'missing'},'findings':issues,'directionAudit':'deferred-by-user'})
report={'schema':1,'scope':'special/rear key inventory and selected source, lighting capability-to-operation path, manual vibration vs virtual game feedback; direction deferred by user','modelCases':native['modelCases'],'modelClasses':len(rows),'catalogKeys':sum(r['keyInventory']['catalogCount'] for r in rows),'staticSourceCandidateKeys':sum(r['keyInventory']['staticSourceCandidateCount'] for r in rows),'HCVisibleEncodingCount':encodings,'unmatchedSelectedProfileEncodings':unmatched,'lightingClasses':sum(r['lighting']['declared'] for r in rows),'implementedOperationsAwaitingHardware':implementedAwaitingHardware,'unsupportedOperations':unsupportedOperations,'lightingBackendCounts':{backend:sum(r['lighting']['declared'] and r['lighting']['backendCandidate']==backend for r in rows) for backend in ['native-msi-hid','native-rog-aura','HC-LightSetter']},'classes':rows,'findings':findings,'summaryByFinding':{code:{'classCount':len({f['deviceClass'] for f in findings if f['id']==code}),'classes':sorted({f['deviceClass'] for f in findings if f['id']==code})} for code in sorted({f['id'] for f in findings})},'sourceBindings':[{'file':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in [repo/'native/main.cpp',repo/'FanLab/LightSetter/Program.cs',repo/'src/components/ControllerFeedbackCapabilitiesCard.vue',repo/'src/bridge/controllerFeedbackCapabilities.ts',repo/'InputHost/Program.cs']+[p for p in [repo/'native/oem_lighting_operations.h',repo/'FanLab/LightSetter/OneXQueuedLighting.cs'] if p.exists()]],'limits':['static source candidate is not runtime ready or verified action','metadata method existence is not hardware write/effect','encoding total includes non-keyboard empty-array declarations; no key counts inferred from encodings','existing physical exports cover three capability classes only'],'hardwareIo':False,'HCAssemblyLoaded':False,'goalComplete':False}
report['controllerLayerAuditIncluded']=controllerContract is not None
report['controllerLayerMissingCatalogInputs']=controllerContract.get('missingCatalogInputCount',0) if controllerContract else None
report['catalogCoverageIsWholeControllerCoverage']=False
if controllerContract:
 report['sourceBindings'].append({'file':str(pathlib.Path(a.controller_contract).resolve()),'sha256':hashlib.sha256(pathlib.Path(a.controller_contract).read_bytes()).hexdigest()})
 report['limits'].append('HC controller-layer dedicated/rear/wheel inputs supplement OEMChords; the native catalog count does not include missing reference inputs')
(out/'oem-capability-completeness.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:report[k] for k in ['modelCases','modelClasses','catalogKeys','staticSourceCandidateKeys','HCVisibleEncodingCount','unmatchedSelectedProfileEncodings','lightingClasses','lightingBackendCounts','summaryByFinding']},ensure_ascii=False,indent=2))
