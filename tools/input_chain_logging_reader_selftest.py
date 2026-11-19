"""Explicit offline tests for the input-chain local count reader."""
from pathlib import Path
import copy,json,sys,tempfile,unittest,zipfile
sys.path.insert(0,str(Path(__file__).resolve().parent))
import log_lane_audit as audit

class InputChainFrameTests(unittest.TestCase):
    def row(self,event='wake-input-chain-first-nonneutral'):
        return dict(event=event,inputChainEvidenceSchema=3,nonNeutralEvidenceSchema=1,countPowerGeneration=20,sampledHostEpoch=3,
            countScope='current-power-generation-and-sampled-host-epoch',hostEpochScope='sampled-at-count-not-frame-tuple-proof',
            acceptedInCurrentGeneration=51,nonNeutralAcceptedInCurrentGeneration=1,semanticNeutral=False,semanticScope=audit.INPUT_CHAIN_SEMANTIC_SCOPE,
            evidenceScope='host-only',gameInputUnverified=True,ackIdentityStatus='not-projected;count-scope-only',
            ackIdentity=dict(schema=1,authority='native-local-ack-diagnostic',status='verified-local-ack',nativeAccepted=True,tupleVerified=True,
                protocolPowerGeneration=19,hostEpoch=2,wireSequence=51,runId='run',hostInstanceId='host',requestId='frame-51'))
    def test_old_missing_is_unknown(self):
        r=audit.input_chain_frame_record({'event':'wake-input-chain-first-frame'})
        self.assertEqual(r['state'],'legacy-or-unsupported');self.assertIsNone(r['accepted']);self.assertIsNone(r['nonNeutralAccepted'])
    def test_neutral_first_and_sustained_are_not_active_controls(self):
        for event in ('wake-input-chain-first-frame','wake-input-chain-sustained'):
            row=self.row(event);row['nonNeutralAcceptedInCurrentGeneration']=0
            if event.endswith('first-frame'):row['semanticNeutral']=True
            else:row.pop('semanticNeutral')
            r=audit.input_chain_frame_record(row);self.assertEqual(r['state'],'reported-local-counts');self.assertEqual(r['nonNeutralAccepted'],0);self.assertFalse(r['controlsNonzeroProven']);self.assertFalse(r['gameInputProven'])
    def test_valid_edge_is_count_scope_without_frame_tuple(self):
        r=audit.input_chain_frame_record(self.row());self.assertEqual(r['state'],'reported-local-counts');self.assertEqual(r['countPowerGeneration'],20);self.assertEqual(r['sampledHostEpoch'],3);self.assertIsNone(r['reportedLocalAckTuple']);self.assertFalse(r['physicalActivityProven'])
    def test_existing_ack_does_not_conflate_power_or_epoch(self):
        r=audit.input_chain_frame_record(self.row('wake-input-chain-first-frame'));self.assertEqual(r['reportedLocalAckTuple']['protocolPowerGeneration'],19);self.assertEqual(r['reportedLocalAckTuple']['hostEpoch'],2);self.assertEqual(r['countPowerGeneration'],20);self.assertEqual(r['sampledHostEpoch'],3)
        self.assertEqual(r['ackProtocolPowerRelationToCount'],'different');self.assertEqual(r['ackHostEpochRelationToSample'],'different')
    def test_ack_comparisons_require_same_row_values_and_valid_tuple(self):
        row=self.row('wake-input-chain-first-frame')
        row['ackIdentity'].update(protocolPowerGeneration=20,hostEpoch=3)
        view=audit.input_chain_frame_record(row)
        self.assertEqual((view['ackProtocolPowerRelationToCount'],view['ackHostEpochRelationToSample']),('same','same'))
        self.assertFalse(view['gameInputProven'])
        zero_sample=dict(row,sampledHostEpoch=0)
        invalid=dict(row,ackIdentity=dict(row['ackIdentity'],tupleVerified=False))
        report=audit.input_chain_frame_evidence([(1,row),(2,zero_sample),(3,invalid),(4,self.row())])
        self.assertEqual(report['ackProtocolPowerRelationsToCount'],{'same':2,'not-observed':2})
        self.assertEqual(report['ackHostEpochRelationsToSample'],{'same':1,'not-observed':3})
        self.assertEqual(report['ackRelationScope'],'same-row-value-comparison-only;not-current-tuple-or-delivery-proof')
        self.assertEqual(audit.input_chain_frame_evidence([])['ackProtocolPowerRelationsToCount'],{})
    def test_invalid_types_remain_unknown(self):
        for key in ('countPowerGeneration','sampledHostEpoch','acceptedInCurrentGeneration','nonNeutralAcceptedInCurrentGeneration'):
            for bad in (None,True,-1,1.0,'1',1<<64):
                row=self.row();row[key]=bad;r=audit.input_chain_frame_record(row);self.assertEqual(r['state'],'invalid-fields');self.assertIsNone(r['accepted'])
        for key,bad in [('nonNeutralAcceptedInCurrentGeneration',0),('nonNeutralAcceptedInCurrentGeneration',2),('semanticNeutral',True),('semanticScope','other'),('gameInputUnverified',False)]:
            row=self.row();row[key]=bad;self.assertEqual(audit.input_chain_frame_record(row)['state'],'invalid-fields')
    def test_schema_and_unrelated_events(self):
        for value in (None,True,3,'1'):
            row=self.row();row['nonNeutralEvidenceSchema']=value;self.assertEqual(audit.input_chain_frame_record(row)['state'],'unsupported-edge-schema')
        row=self.row();row.pop('nonNeutralEvidenceSchema');self.assertEqual(audit.input_chain_frame_record(row)['state'],'missing-edge-schema');self.assertIsNone(audit.input_chain_frame_record({'event':'unrelated'}))
    def test_ack_not_backfilled_from_samples(self):
        for bad in (None,{},dict(schema=True),dict(schema=1,tupleVerified=True)):
            row=self.row();row['ackIdentity']=bad;r=audit.input_chain_frame_record(row);self.assertEqual(r['state'],'reported-local-counts');self.assertIsNone(r['reportedLocalAckTuple'])
    def test_bounded_witnesses_and_no_absence_inference(self):
        r=audit.input_chain_frame_evidence([(i,self.row()) for i in range(100)])
        self.assertEqual(r['rows'],100);self.assertEqual(len(r['witnesses']),16);self.assertEqual(r['omittedWitnesses'],84);self.assertFalse(r['missingIsZeroOrFailureProof']);self.assertEqual(audit.input_chain_frame_evidence([])['rows'],0)
    def test_schema_two_ack_is_same_row_only(self):
        fast='strict-fast-validator-equality-and-parsed-host';dom='existing-validator-and-parsed-dom-reply'
        row=self.row();row.update(nonNeutralEvidenceSchema=2,ackIdentityStatus='verified-local-ack',ackIdentityOrigin=fast,
            wireSequence=51,ackWireSequence=51,ackProtocolPowerGeneration=19,ackHostEpoch=2,ackRunId='run',ackHostInstanceId='host',ackRequestId='frame-51')
        for origin,wire in ((fast,51),(dom,0)):
            r=audit.input_chain_frame_record(dict(row,ackIdentityOrigin=origin,wireSequence=wire))
            self.assertEqual(r['reportedLocalAckTuple'],dict(protocolPowerGeneration=19,hostEpoch=2,wireSequence=51,runId='run',hostInstanceId='host',requestId='frame-51'))
            self.assertEqual((r['countPowerGeneration'],r['sampledHostEpoch']),(20,3));self.assertFalse(r['gameInputProven'])
            self.assertEqual((r['ackProtocolPowerRelationToCount'],r['ackHostEpochRelationToSample']),('different','different'))
        for bad in (dict(wireSequence=52),dict(wireSequence=0),dict(ackHostEpoch=True),dict(ackRequestId=''),dict(ackIdentityOrigin='other'),dict(ackIdentityStatus='unknown')):
            r=audit.input_chain_frame_record(dict(row,**bad))
            self.assertEqual(r['state'],'reported-local-counts');self.assertEqual(r['reportedAckStatus'],'invalid-fields');self.assertIsNone(r['reportedLocalAckTuple'])
        unknown=dict(self.row(),nonNeutralEvidenceSchema=2,ackIdentityStatus='unknown')
        r=audit.input_chain_frame_record(unknown);self.assertEqual(r['reportedAckStatus'],'unknown');self.assertIsNone(r['reportedLocalAckTuple'])
    def test_actual_archive_audit_wires_native_rotations(self):
        source=(Path(__file__).resolve().parents[1]/'native/main.cpp').read_text('utf-8-sig');policy=audit.source_policy(source)
        with tempfile.TemporaryDirectory(prefix='ymcc-input-chain-reader-') as d:
            p=Path(d)/'test.zip'
            with zipfile.ZipFile(p,'w') as z:
                z.writestr('native-lifecycle.log.1',json.dumps(self.row())+'\n')
            report=audit.audit_zip(p,policy)
        entries=[r for r in report['files'] if r.get('file')=='native-lifecycle.log.1']
        self.assertEqual(len(entries),1);self.assertEqual(entries[0]['inputChainFrameEvidence']['rows'],1);self.assertEqual(entries[0]['inputChainFrameEvidence']['counts'],{'reported-local-counts':1})

if __name__=='__main__':unittest.main()
