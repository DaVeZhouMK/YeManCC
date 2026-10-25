"""AST-extracted Intel static evidence tests. No module import, DLL or hardware.
Compare original transaction call order, arguments, return codes and exceptions
against the observation-only implementation using deterministic fake backends.
"""
import argparse, ast, copy, hashlib, json, pathlib, time, types, os
ROOT=pathlib.Path(__file__).resolve().parents[1]
FUNCTIONS=['_intel_single_target_limits','apply_tdp','_clamp_w','_rapl_power_unit_w','_intel_power_limit_value','_tdp_observe_update','_tdp_observe_raw','intel_set_pawn','_intel_set_pawn_observed_core','_daemon_dispatch_request','_daemon_dispatch_request_observed_core']
def environment(source, original=False):
    tree=ast.parse(source);names=set(FUNCTIONS if not original else ['_clamp_w','_rapl_power_unit_w','_intel_power_limit_value','intel_set_pawn','_daemon_dispatch_request'])
    nodes=[n for n in tree.body if isinstance(n,(ast.FunctionDef,ast.AsyncFunctionDef)) and n.name in names]
    logs=[];env={'time':time,'os':os,'json':json,'log':lambda *v:logs.append(v),'INTEL_MAX_W':300,'MSR_PKG_POWER_LIMIT':0x610,'MSR_RAPL_POWER_UNIT':0x606,'MSR_PKG_POWER_LOCK':1<<63,
        'detect_intel_capability':lambda:{'name':'fake Model CC','rapl_supported':False},'_TDP_OBSERVATION_CURRENT':None,'_TDP_OBSERVATION_LAST':None,'_TDP_OBSERVATION_FAILURES':0,'_TDP_OBSERVATION_CONTEXT':{}}
    exec(compile(ast.Module(body=nodes,type_ignores=[]),'<extracted-existing-transactions>','exec'),env)
    return env,logs
class Fake:
    def __init__(self,case):self.case=case;self.msr=object();self._tx_depth=0;self.calls=[];self.value=0x123400001234
    def _enter_tx(self):self.calls.append(('enter',));self._tx_depth=1;return self.case!='mutex'
    def _leave_tx(self):self.calls.append(('leave',));self._tx_depth=0
    def read_msr_raw(self,reg):
        self.calls.append(('read',reg))
        if reg==0x606:return None if self.case=='unit-failed' else 3
        reads=sum(c==('read',0x610) for c in self.calls)
        if self.case=='read-before-failed' and reads==1:return None
        if self.case=='locked' and reads==1:return self.value|(1<<63)
        if self.case=='readback-failed' and reads>1:return None
        if self.case=='mismatch' and reads>1:return self.value^1
        return self.value
    def write_msr_raw(self,reg,value):
        self.calls.append(('write',reg,value))
        if self.case=='write-failed':return False
        self.value=value;return True

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--baseline',type=pathlib.Path,required=True);parser.add_argument('--output',type=pathlib.Path,required=True);args=parser.parse_args()
    source=(ROOT/'PowerControl/pawnio/YeManTdpCtl.py').read_text(encoding='utf-8-sig');baseline=args.baseline.read_text(encoding='utf-8-sig');checks=[]
    def check(name,ok):checks.append({'name':name,'pass':bool(ok)})
    before,_=environment(baseline,True)
    for case in ['ok','mutex','locked','read-before-failed','unit-failed','write-failed','readback-failed','mismatch']:
        after,logs=environment(source);a,b=Fake(case),Fake(case)
        rc_old=before['intel_set_pawn'](a,29,30,False);rc_new=after['intel_set_pawn'](b,29,30,False);record=after['_TDP_OBSERVATION_LAST']
        check(case+'-same-rc-call-order-and-exact-payload',rc_old==rc_new and a.calls==b.calls)
        check(case+'-terminal-static-observation',record and record['rc']==rc_new and record['verificationScope']=='MSR_0x610_target_fields_only' and record['physicalPowerVerified'] is False)
        if case=='ok':
            check('existing-unit-before-intended-after-without-new-reads',record['unitExponent']==3 and record['targetPL1W']==29 and record['targetPL2W']==30 and record['beforeRaw']!='not-observed' and record['intendedRaw']==record['afterRaw'] and record['readbackMatched'] is True and [c for c in b.calls if c[0]=='read']==[('read',0x610),('read',0x606),('read',0x610)])
        if case=='unit-failed':check('failed-unit-remains-unknown-no-guessed-values','unitW' not in record and record['intendedRaw']=='not-observed' and not any(c[0]=='write' for c in b.calls))
        if case=='mismatch':check('mismatch-not-physical-success',record['readbackMatched'] is False and record['rc']==6)
        if case=='locked':check('locked-no-write-or-unlock',record['stage']=='lock' and not any(c[0]=='write' for c in b.calls))
    after,_=environment(source);fake=Fake('ok');response,stop=after['_daemon_dispatch_request']('intel',{'intel':fake},{'version':1,'requestId':'fixture-causal-id','op':'set','args':{'watts':30}})
    check('daemon-existing-request-id-transmitted-in-structured-result',not stop and response['rc']==0 and response['result']['msrObservation']['requestId']=='fixture-causal-id' and response['result']['msrObservation']['route']=='daemon')
    observation=response['result']['msrObservation']
    check('daemon-single-target-30W-means-PL1-30-PL2-31',observation['requestedPL1W']==30 and observation['requestedPL2W']==31 and observation['targetPL1W']==30 and observation['targetPL2W']==31 and observation['readbackMatched'] is True)
    check('single-target-upper-app-domain-preserves-PL1',after['_intel_single_target_limits'](200)==(200,201))
    after,_=environment(source);fake=Fake('ok');rc=after['intel_set_pawn'](fake,29,30,True)
    check('dry-run-zero-hardware-calls-not-physical-proof',rc==0 and not fake.calls and after['_TDP_OBSERVATION_LAST']['readbackMatched']=='not-observed')
    after,_=environment(source);after['log']=lambda *v:(_ for _ in ()).throw(OSError('fixture-sink-failed')) if v[0]=='YMCC_TDP_OBSERVATION' else None;fake=Fake('ok');rc=after['intel_set_pawn'](fake,29,30,False)
    check('logging-failure-does-not-alter-control-rc-or-release',rc==0 and fake.calls[-1]==('leave',) and after['_TDP_OBSERVATION_FAILURES']>0)
    report={'checks':checks,'failures':sum(not c['pass'] for c in checks),'sourceSha256':hashlib.sha256(source.encode()).hexdigest(),'realProductOrHardwareExecuted':False,'scope':'AST_EXTRACTED_REAL_MSR_TRANSACTIONS_FAKE_BACKEND_ONLY'}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,indent=2));return 1 if report['failures'] else 0
if __name__=='__main__':raise SystemExit(main())
