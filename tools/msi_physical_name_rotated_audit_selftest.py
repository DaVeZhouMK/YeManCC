import json,subprocess,sys,tempfile,unittest,zipfile
from pathlib import Path
SCRIPT=Path(__file__).with_name('msi_physical_name_zip_audit.py')
class RotatedAudit(unittest.TestCase):
 def run_audit(self,extra=''):
  with tempfile.TemporaryDirectory() as temp:
   root=Path(temp);p=root/'sample.zip';out=root/'report.json'
   row={'time':'2026-10-12T21:00:04.000','event':'msi-dinput-enumeration','nativePid':17,
        'metadataCaptureId':'run:17:1','candidates':[{'product':'؉','productUtf16':[1545],
        'hidProductString':'؉','hidProductUtf16':[1545],'hidProductStringKnown':True,
        'interfacePath':'HID#VID_0DB0&PID_1902&MI_00&COL02','hidUsagePage':65520,'hidUsage':64,
        'selectedMain':False,'axes':0,'buttons':0}]}
   boot={'time':'2026-10-12T21:00:01.000','event':'boot-single-instance-acquired','pid':17}
   old={'time':'2026-10-12T20:00:01.000','event':'thread-role','pid':17}
   other=dict(row,nativePid=18)
   with zipfile.ZipFile(p,'w') as z:
    z.writestr('YeManCC-export-manifest.txt','processId=17\nexportedAtLocal=2026-10-12T21:00:05\nnativeSha256=test\n')
    z.writestr('native-lifecycle.log',json.dumps(boot)+'\n')
    z.writestr('native-lifecycle.log.1',json.dumps(old)+'\n')
    z.writestr('virtual-gamepad.log','')
    z.writestr('virtual-gamepad.log.1',json.dumps(row)+'\n'+json.dumps(other)+'\n'+extra)
    z.writestr('steam/logs/controller.txt','[2026-10-12 21:00:04] Local Device Found\n type: 0db0 1902 path: sdl://2\n Product: ؉\n')
   run=subprocess.run([sys.executable,str(SCRIPT),'--zip',str(p),'--output',str(out)],text=True,encoding='utf8',capture_output=True)
   self.assertEqual(run.returncode,0,run.stderr)
   return json.loads(out.read_text(encoding='utf8'))
 def test_rotated_metadata_not_reported_absent(self):
  r=self.run_audit();self.assertEqual(len(r['metadata']),1)
  self.assertEqual(r['metadata'][0]['file'],'virtual-gamepad.log.1')
 def test_pid_and_latest_boot_bound(self):
  r=self.run_audit();self.assertEqual(r['nativeWindow'][0],'2026-10-12T21:00:01.000')
  self.assertFalse(r['nativeWindowLeftCensored']);self.assertEqual(len(r['metadata']),1)
 def test_raw_utf16_matches_without_causal_claim(self):
  r=self.run_audit();c=r['metadata'][0]['candidates'][0]
  self.assertTrue(c['namesRawEqual']);self.assertTrue(c['productTextMatchesRaw']);self.assertTrue(c['hidTextMatchesRaw'])
  self.assertEqual(r['unknownBoundaries']['SDLToMI_COLMapping'],'not-observed')
 def test_partial_tail_is_counted_not_crash_or_success(self):
  r=self.run_audit('{truncated');self.assertEqual(r['malformedMetadataRows'],1)
  self.assertEqual(len(r['metadata']),1)
if __name__=='__main__':unittest.main(verbosity=2)
