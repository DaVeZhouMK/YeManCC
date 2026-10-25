"""Independent completeness classification fixtures, no hardware or inspected code."""
import json,pathlib,subprocess,sys,tempfile,unittest,shutil
TOOL=pathlib.Path(__file__).with_name('oem_capability_completeness_audit.py')
class CapabilityCompletenessTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();root=pathlib.Path(cls.tmp.name);cls.root=root;repo=root/'repo'
  files={
   'native/main.cpp':'const bool brightnessAvailable = rogLightingFamily; // YMCC renders a static two-zone split.',
   'FanLab/LightSetter/Program.cs':'["preset"] = 128; contract.SetColor.Invoke;',
   'src/components/ControllerFeedbackCapabilitiesCard.vue':'',
   'src/bridge/api.ts':'',
   'src/bridge/controllerFeedbackCapabilities.ts':'outputCallbackBound: false, physicalRouteClosed: false',
   'InputHost/Program.cs':'class Host {}'}
  for n,v in files.items():p=repo/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(v,encoding='utf-8')
  hc=root/'HC';hc.mkdir();(hc/'IDevice.cs').write_text('namespace HandheldCompanion.Devices; public class IDevice {}',encoding='utf-8')
  native=[];sources=[];bindings=[]
  for name,family,keys,light,effects,backend in [('SteamDeck','ValveSteamDeck',['special','special2'],False,[],'none'),('OneXPlayerX1','OneXPlayer',[],True,['solid','preset'],'HC-LightSetter'),('AynLoki','Ayn',[],True,['solid'],'HC-LightSetter'),('ClawA1M','MsiClaw',[],True,['solid','ambilight'],'native-msi-hid')]:
   full='HandheldCompanion.Devices.'+name;(hc/(name+'.cs')).write_text('namespace HandheldCompanion.Devices; public class '+name+' : IDevice {}',encoding='utf-8')
   native.append({'deviceClass':full,'family':family,'keys':[{'keyId':k,'keyboardRules':[]} for k in keys],'lightingDeclared':light,'ledEffects':effects,'secondColorDeclared':False})
   sources.append({'deviceClass':full,'keys':[{'keyId':k,'sourceCandidateDeclared':False} for k in keys]})
   bindings.append({'deviceClass':full,'backendCandidate':backend,'bindingIssues':[],'callbacks':[{'method':'SetLedBrightness','runtimeOwner':'HandheldCompanion.Devices.IDevice','constantBooleanBody':'true'},{'method':'SetLEDPreset','runtimeOwner':'HandheldCompanion.Devices.OneXPlayerX1','constantBooleanBody':None}]})
  paths={}
  for name,data in [('native',{'modelCases':4,'models':native}),('sources',{'classes':sources}),('binding',{'classes':bindings})]:p=root/(name+'.json');p.write_text(json.dumps(data),encoding='utf-8');paths[name]=p
  result=subprocess.run([sys.executable,str(TOOL),'--native-matrix',str(paths['native']),'--hc-devices',str(hc),'--source-reachability',str(paths['sources']),'--runtime-lighting',str(paths['binding']),'--repo',str(repo),'--output',str(root/'out')],capture_output=True,text=True,encoding='utf-8')
  if result.returncode:raise RuntimeError(result.stdout+result.stderr)
  cls.report=json.loads((root/'out/oem-capability-completeness.json').read_text(encoding='utf-8'))
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def test_catalog_binding_without_source_stays_missing(self):self.assertEqual(self.report['summaryByFinding']['catalog-key-no-source']['classes'],['HandheldCompanion.Devices.SteamDeck'])
 def test_named_preset_does_not_pass_for_color_enum(self):self.assertEqual(self.report['summaryByFinding']['lighting-preset-no-call-route']['classes'],['HandheldCompanion.Devices.OneXPlayerX1'])
 def test_native_msi_brightness_is_not_confused_with_helper_base_stub(self):self.assertNotIn('HandheldCompanion.Devices.ClawA1M',self.report['summaryByFinding']['brightness-ui-to-noop']['classes']);self.assertIn('HandheldCompanion.Devices.AynLoki',self.report['summaryByFinding']['brightness-ui-to-noop']['classes'])
 def test_manual_pulse_does_not_close_game_feedback(self):self.assertEqual(self.report['summaryByFinding']['game-feedback-bridge-missing']['classCount'],4)
 def test_static_ambilight_is_partial(self):self.assertEqual(self.report['summaryByFinding']['ambilight-static-only']['classes'],['HandheldCompanion.Devices.ClawA1M'])
 def test_direction_deferred_not_a_capability_completion_claim(self):self.assertTrue(all(r['directionAudit']=='deferred-by-user' for r in self.report['classes']));self.assertFalse(self.report['goalComplete']);self.assertFalse(self.report['hardwareIo'])
 def test_compiled_operation_evidence_changes_status_without_claiming_physical_acceptance(self):
  root=self.root;repo=root/'fixed-repo';shutil.copytree(root/'repo',repo)
  (repo/'native/main.cpp').write_text('const bool brightnessAvailable = rogLightingFamily && ymcc::lighting::brightnessImplemented(fbDeviceClass); // YMCC renders a static two-zone split. validPreset --action preset --preset-id ledPresets',encoding='utf-8')
  (repo/'FanLab/LightSetter/Program.cs').write_text('["preset"] = 128; contract.SetColor.Invoke; ExecuteLedPreset --preset-id SetLEDPreset NameKey',encoding='utf-8')
  (repo/'src/components/ControllerFeedbackCapabilitiesCard.vue').write_text('presetId v-if="caps.brightnessAvailable"',encoding='utf-8');(repo/'src/bridge/api.ts').write_text('presetId',encoding='utf-8')
  native=json.loads((root/'native.json').read_text(encoding='utf-8'))
  for model in native['models']:
   model['lightingOperations']={'brightnessImplemented':not model['deviceClass'].endswith('AynLoki'),'presetImplemented':model['deviceClass'].endswith('OneXPlayerX1'),'presetDeclaredButUnavailable':False}
  (root/'fixed-native.json').write_text(json.dumps(native),encoding='utf-8')
  binding=json.loads((root/'binding.json').read_text(encoding='utf-8'))
  for item in binding['classes']:
   if item['deviceClass'].endswith('OneXPlayerX1'):
    callback=next(c for c in item['callbacks'] if c['method']=='SetLedBrightness');callback.update(runtimeOwner='HandheldCompanion.Devices.OneXPlayerX1',constantBooleanBody=None)
  (root/'fixed-binding.json').write_text(json.dumps(binding),encoding='utf-8')
  result=subprocess.run([sys.executable,str(TOOL),'--native-matrix',str(root/'fixed-native.json'),'--hc-devices',str(root/'HC'),'--source-reachability',str(root/'sources.json'),'--runtime-lighting',str(root/'fixed-binding.json'),'--repo',str(repo),'--output',str(root/'fixed-out')],capture_output=True,text=True,encoding='utf-8')
  self.assertEqual(result.returncode,0,result.stdout+result.stderr)
  report=json.loads((root/'fixed-out/oem-capability-completeness.json').read_text(encoding='utf-8'))
  self.assertNotIn('lighting-preset-no-call-route',report['summaryByFinding']);self.assertNotIn('brightness-ui-to-noop',report['summaryByFinding'])
  self.assertEqual(len(report['implementedOperationsAwaitingHardware']),1);self.assertEqual(len(report['unsupportedOperations']),1)
  self.assertFalse(report['goalComplete']);self.assertIn('game-feedback-bridge-missing',report['summaryByFinding'])
 def controller_supplement_run(self, *, verified=True, stale=False):
  import hashlib
  with tempfile.TemporaryDirectory() as tmp:
   output=pathlib.Path(tmp);repo=self.root/'repo'
   supplement={'schema':1,'HCCommit':'06c0b9544db2b1f39abf9cd3796225ccfb096103','committedSourcesVerified':verified,'missingCatalogInputCount':4,
    'sourceBindings':[{'role':role,'sha256':('stale' if stale and role=='nativeMain' else hashlib.sha256((repo/path).read_bytes()).hexdigest())} for role,path in [('nativeMain','native/main.cpp'),('inputHost','InputHost/Program.cs')]],
    'findings':[{'deviceClass':'HandheldCompanion.Devices.SteamDeck','id':'hc-controller-rear-keys-absent-from-catalog','status':'missing','referenceInputs':['L4','R4','L5','R5'],'missingInputCount':4}]}
   path=output/'controller-contract.json';path.write_text(json.dumps(supplement),encoding='utf-8')
   result=subprocess.run([sys.executable,str(TOOL),'--native-matrix',str(self.root/'native.json'),'--hc-devices',str(self.root/'HC'),'--source-reachability',str(self.root/'sources.json'),'--runtime-lighting',str(self.root/'binding.json'),'--repo',str(repo),'--controller-contract',str(path),'--output',str(output/'out')],capture_output=True,text=True,encoding='utf-8')
   reportPath=output/'out/oem-capability-completeness.json'
   return result,json.loads(reportPath.read_text(encoding='utf-8')) if reportPath.exists() else None
 def test_controller_layer_missing_keys_survive_completeness_join(self):
  result,report=self.controller_supplement_run()
  self.assertEqual(result.returncode,0,result.stdout+result.stderr)
  self.assertEqual(report['summaryByFinding']['hc-controller-rear-keys-absent-from-catalog']['classCount'],1)
  self.assertTrue(report['controllerLayerAuditIncluded']);self.assertEqual(report['controllerLayerMissingCatalogInputs'],4)
  self.assertFalse(report['catalogCoverageIsWholeControllerCoverage']);self.assertFalse(report['goalComplete']);self.assertFalse(report['hardwareIo'])
 def test_stale_controller_contract_is_rejected(self):
  result,report=self.controller_supplement_run(stale=True)
  self.assertNotEqual(result.returncode,0);self.assertIn('Stale controller source binding',result.stderr);self.assertIsNone(report)
 def test_unverified_controller_source_is_rejected(self):
  result,report=self.controller_supplement_run(verified=False)
  self.assertNotEqual(result.returncode,0);self.assertIn('verified committed HC sources',result.stderr);self.assertIsNone(report)
if __name__=='__main__':unittest.main()
