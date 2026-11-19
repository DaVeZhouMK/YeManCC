from pathlib import Path
import copy,json,sys,tempfile,unittest,zipfile
sys.path.insert(0,str(Path(__file__).resolve().parent));import log_lane_audit as a
class ShortcutReadTests(unittest.TestCase):
 def raw(self,**extra):
  fields=dict(sourceReadOnly=True,readAttempted=True,**extra);return {'shortcutRead':dict(schema=1,scope='original-shortcut-inventory-read-facts;not-path-or-owner-proof',reportedFields=len(fields),pathsAndAccountsOmitted=True,filesystemErrorCategoryKnown=False,**fields)}
 def test_missing_null_bad_type_schema_and_scope(self):
  for v,s in [({},'missing'),({'shortcutRead':None},'null'),({'shortcutRead':True},'invalid-type')]:self.assertEqual(a.shortcut_read_record(v)['state'],s)
  for x in [True,2,1.0,'1']:
   v=self.raw();v['shortcutRead']['schema']=x;self.assertEqual(a.shortcut_read_record(v)['state'],'unsupported-schema')
  v=self.raw();v['shortcutRead']['scope']='future';self.assertEqual(a.shortcut_read_record(v)['state'],'unsupported-scope')
 def test_original_normal_missing_and_probe_facts_separate(self):
  r=a.shortcut_read_record(self.raw(filePresent=True,regularFile=True,fileBytes=13,fileSizeErrorCode=0));self.assertEqual(r['state'],'reported-original-shortcut-read-facts');self.assertEqual(r['reported']['fileBytes'],13)
  r=a.shortcut_read_record(self.raw(filePresent=False,fileStatusErrorCode=2));self.assertNotIn('fileBytes',r['reported'])
  r=a.shortcut_read_record(self.raw(afterFailureReadProbe=True,afterFailureReadErrorCode=32,readErrorTag='media-shortcuts-unavailable'));self.assertEqual(r['reported']['afterFailureReadErrorCode'],32);self.assertFalse(r['currentPathProven'])
 def test_uint_and_signed_bounds_remain_exact(self):
  r=a.shortcut_read_record(self.raw(fileBytes=(1<<64)-1,fileStatusErrorCode=-(1<<31),fileSizeErrorCode=(1<<31)-1,afterFailureReadErrorCode=(1<<32)-1));self.assertEqual(r['reported']['fileBytes'],(1<<64)-1)
  for k,v in [('fileBytes',-1),('fileBytes',True),('fileBytes',1<<64),('afterFailureReadErrorCode',1<<32),('fileStatusErrorCode',1<<31),('fileSizeErrorCode',0.0)]:self.assertEqual(a.shortcut_read_record(self.raw(**{k:v}))['state'],'invalid-fields')
 def test_private_values_and_unknown_fields_do_not_survive(self):
  for v in [self.raw(file='PRIVATE'),self.raw(account='PRIVATE'),self.raw(readErrorTag='media-shortcut-PRIVATE')]:r=a.shortcut_read_record(v);self.assertEqual(r['state'],'invalid-fields');self.assertNotIn('PRIVATE',json.dumps(r))
 def test_missing_flags_counts_and_category_cannot_be_fabricated(self):
  for key,value in [('sourceReadOnly',1),('readAttempted',None),('reportedFields',True),('reportedFields',10),('pathsAndAccountsOmitted',False),('filesystemErrorCategoryKnown',True)]:
   v=self.raw();v['shortcutRead'][key]=value;self.assertEqual(a.shortcut_read_record(v)['state'],'invalid-fields')
 def test_no_mutation_or_cross_event_join(self):
  v=self.raw(filePresent=True,fileBytes=13);before=copy.deepcopy(v);a.shortcut_read_record(v);self.assertEqual(v,before)
  r=a.decky_shortcut_read_evidence([('decky-sidebar.log',[(1,dict(event='wallpaper-prepare',**v)),(2,dict(event='runtime',**v))],{})]);self.assertEqual(r['rowsObserved'],0)
 def test_fifty_thousand_rows_keep_fixed_witnesses_and_all_counts(self):
  v=dict(event='wallpaper-inventory',processPid=37,**self.raw(fileBytes=13));r=a.decky_shortcut_read_evidence([('decky-sidebar.log',[(n,v) for n in range(50000)],{'archiveEntryOccurrence':1})],'37');self.assertEqual(r['rowsObserved'],50000);self.assertEqual(len(r['witnesses']),16);self.assertEqual(r['rowsNotListed'],49984);self.assertEqual(r['countsByState']['reported-original-shortcut-read-facts'],50000);self.assertFalse(r['samePidMeansSameLifetime'])
 def test_exact_carrier_and_payload_subject_not_writer(self):
  v=dict(event='wallpaper-inventory',pid=37,hostPid=37,**self.raw());r=a.decky_shortcut_read_evidence([('decky-sidebar.log',[(1,v)],{}),('steam/logs/decky-sidebar.log',[(1,v)],{}),('not-decky-sidebar.log',[(1,v)],{})],'37');self.assertEqual(r['rowsObserved'],1);self.assertIsNone(r['witnesses'][0]['writerPid']);self.assertIsNone(r['witnesses'][0]['currentPidMatch'])
 def test_original_common_summary_and_actual_audit_zip(self):
  v=dict(event='wallpaper-inventory',processPid=37,**self.raw(fileBytes=13));direct=a.common_evidence_summary({},[('decky-sidebar.log',[(1,v)],{})],'37');self.assertEqual(direct['shortcutReadEvidence']['rowsObserved'],1)
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'one.zip'
   with zipfile.ZipFile(p,'w') as z:z.writestr('decky-sidebar.log',json.dumps(v)+'\n')
   before=p.read_bytes();r=a.audit_zip(p,{'baseEvents':[]});self.assertEqual(r['commonEvidence']['shortcutReadEvidence']['rowsObserved'],1);self.assertEqual(p.read_bytes(),before)
if __name__=='__main__':unittest.main()
