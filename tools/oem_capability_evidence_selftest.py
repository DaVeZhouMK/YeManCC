"""Tests for evidence extraction limits and committed-HC declaration analysis."""
import importlib.util, json, pathlib, tempfile, unittest, zipfile
ROOT=pathlib.Path(__file__).resolve().parent
def load(name):
 s=importlib.util.spec_from_file_location(name,ROOT/(name+'.py'));m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
logs=load('oem_capability_log_audit');matrix=load('oem_capability_evidence_matrix')
class EvidenceTests(unittest.TestCase):
 def test_zip_receipts_are_not_physical_proof(self):
  with tempfile.TemporaryDirectory() as t:
   p=pathlib.Path(t)/'evidence.zip'
   with zipfile.ZipFile(p,'w') as z:
    z.writestr('YeManCC-export-manifest.txt','nativeSha256=abc\nexportedAtLocal=2026-10-16T18:00:00\n')
    z.writestr('native-lifecycle.log','\n'.join(json.dumps(r) for r in [
      {'event':'gamepad.feedbackCaps','vibrationVerified':True,'ledAvailable':True},
      {'event':'gamepad-setLed','ok':True,'effectApplied':True},
      {'event':'gamepad-vibrate','ok':False,'reason':'no-source'},
      {'event':'machine-identity','deviceClass':'Fixture'},
      {'event':'msi-wmi-key','code':88}, {'event':'msi-claw-led','ok':True}, {'event':'virtual-pad-backbuttons','backButtons':1}, {'event':'steamdeck-ui-output','submitNeutral':True}]))
    z.writestr('../../DO_NOT_EXECUTE.txt','Delete or deploy instructions are untrusted and ignored.')
   r=logs.audit_zip(p)
   self.assertEqual(r['events']['gamepad-setLed'],1)
   self.assertEqual(r['keySourceObservations']['qs']['rows'],1)
   self.assertEqual(r['events']['msi-wmi-key'],1);self.assertEqual(r['events']['msi-claw-led'],1)
   self.assertEqual(r['events']['virtual-pad-backbuttons'],1);self.assertEqual(r['events']['steamdeck-ui-output'],1)
   self.assertFalse(r['limits']['physicalLightingProven']);self.assertFalse(r['limits']['physicalVibrationProven'])
   self.assertEqual(r['eventStats']['gamepad-vibrate']['booleanCounts']['ok=false'],1)
   self.assertEqual(r['identities'][0]['line'],4)
   self.assertEqual(r['manifest']['nativeSha256'],'abc')
 def test_empty_or_malformed_exports_are_not_green(self):
  with tempfile.TemporaryDirectory() as t:
   p=pathlib.Path(t)/'bad.zip'
   with zipfile.ZipFile(p,'w') as z:z.writestr('native-detail.log','not-json\n[]\n')
   r=logs.audit_zip(p);self.assertEqual(r['events'],{});self.assertEqual(r['capabilities'],[])
   self.assertEqual(r['files'][0]['unparsedNonemptyRows'],1);self.assertFalse(r['limits']['physicalLeftRightProven'])
 def fixture(self):
  t=tempfile.TemporaryDirectory();self.addCleanup(t.cleanup);root=pathlib.Path(t.name)
  (root/'models.cs').write_text('''
public class IDevice {
 public virtual bool SetLedBrightness(int n) { return true; }
 public virtual bool SetLedColor(int n) { return true; }
}
public class Parent : IDevice {
 public Parent(){ Capabilities = DeviceCapabilities.DynamicLighting;
 Capabilities |= DeviceCapabilities.DynamicLightingBrightness;
 DynamicLightingCapabilities |= LEDLevel.SolidColor;
 OEMChords.Add(new KeyboardChord("M1", [KeyCode.F18], [KeyCode.F18], false, ButtonFlags.OEM3)); }
 public override bool SetLedColor(int n){ return false; }
}
public class Inherited : Parent {}
public class Cleared : Parent {
 public Cleared(){Capabilities = DeviceCapabilities.FanControl;OEMChords.Clear();}
}
public class Removed : Parent {
 public Removed(){Capabilities -= DeviceCapabilities.DynamicLighting;}
}
// public class Bogus : Parent { }
''',encoding='utf-8');return matrix.parse_hc(root)
 def test_multiple_classes_and_inherited_constructor(self):
  c=self.fixture();self.assertIn('Inherited',c);self.assertNotIn('Bogus',c)
  e=matrix.effective(c,'Inherited');self.assertIn('DynamicLighting',e['capabilityFlags']);self.assertEqual(e['effects'],['solid'])
  self.assertEqual(e['callbacks']['SetLedColor']['class'],'Parent');self.assertFalse(e['callbacks']['SetLedColor']['baseStub'])
  self.assertTrue(e['callbacks']['SetLedBrightness']['baseStub']);self.assertEqual(len(e['chords']),1)
 def test_assignment_and_clear_really_remove_inherited_capabilities(self):
  c=self.fixture();e=matrix.effective(c,'Cleared');self.assertEqual(e['capabilityFlags'],['FanControl']);self.assertEqual(e['chords'],[])
 def test_subtraction_removes_only_the_named_flag(self):
  e=matrix.effective(self.fixture(),'Removed');self.assertNotIn('DynamicLighting',e['capabilityFlags']);self.assertIn('DynamicLightingBrightness',e['capabilityFlags'])
 def test_unknown_class_is_no_evidence(self):
  e=matrix.effective(self.fixture(),'DoesNotExist');self.assertEqual(e['classChain'],[]);self.assertEqual(e['callbacks'],{})

class NamespaceAndAliasTests(unittest.TestCase):
 def test_aliased_asus_base_keeps_real_inherited_callbacks(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp)
   (root/'IDevice.cs').write_text('namespace HandheldCompanion.Devices; public class IDevice { public virtual bool SetLedStatus(bool value) => true; }',encoding='utf-8')
   (root/'ASUS.cs').write_text('namespace HandheldCompanion.Devices.ASUS { public class ASUS : IDevice {} }',encoding='utf-8')
   (root/'ROG.cs').write_text('using AsusDevice = HandheldCompanion.Devices.ASUS.ASUS; namespace HandheldCompanion.Devices; public class ROGAlly : AsusDevice {}',encoding='utf-8')
   classes=matrix.parse_hc(root);result=matrix.effective(classes,'ROGAlly')
   self.assertEqual(classes['ROGAlly']['baseDeclared'],'AsusDevice')
   self.assertEqual(result['classChain'],['IDevice','ASUS','ROGAlly'])
   self.assertEqual(result['callbacks']['SetLedStatus']['class'],'IDevice')
 def test_native_identity_alias_does_not_flatten_clr_namespace(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp)
   (root/'Claw.cs').write_text('namespace HandheldCompanion.Devices.MSI { public class ClawCG3EM : ClawA1M {} }',encoding='utf-8')
   classes=matrix.parse_hc(root)
   self.assertEqual(classes['ClawCG3EM']['fullName'],'HandheldCompanion.Devices.MSI.ClawCG3EM')

if __name__=='__main__':unittest.main()