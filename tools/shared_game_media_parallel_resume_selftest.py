"""Bounded acceptance runner: fresh task-owned output, known test binaries only."""
import argparse, hashlib, json, os, pathlib, subprocess, time
from shared_game_media_parallel_resume_fixture import Fixture, full, payload

WORKSPACE=pathlib.Path(__file__).resolve().parents[4]
ALLOWED=(WORKSPACE/'Build/Tasks/YMCC-Decky-SharedWallpaper-20261017/implementation/parallel-resume-r41').resolve()
CASES=['parallel-twelve','parallel-two','parallel-clamp','parallel-one-fails','parallel-cancel','resume-etag','resume-last-modified','resume-last-modified-recent','resume-last-modified-invalid','resume-last-modified-weak-etag','resume-ignored','resume-changed-200','resume-changed-206','resume-bad-start','resume-bad-total','resume-encoding','resume-416','resume-no-validator','resume-weak','resume-corrupt-meta','resume-missing-part','resume-app-isolation','hls-resume','hls-plan-changed','auto-five-resume']

def run(exe,root,mode):
    destination=root/mode
    fixture=Fixture(mode)
    seed=root/'seed.mp4'
    if not seed.exists(): seed.write_bytes(payload(570))
    env=os.environ.copy()
    env['YMCC_RESUME_SEED_VIDEO']=str(seed)
    started=time.monotonic()
    fixture.start()
    try:
        result=subprocess.run([str(exe),str(destination),str(fixture.server.server_port),mode],capture_output=True,env=env,creationflags=subprocess.CREATE_NO_WINDOW,timeout=125 if mode=='auto-five-resume' else 40)
        if result.returncode: raise RuntimeError(result.stderr.decode(errors='replace'))
        value=json.loads(result.stdout)
        (destination/'native-stderr.log').write_bytes(result.stderr)
    finally: fixture.close()
    checks=[]
    def check(ok,label): checks.append({'ok':bool(ok),'label':label})
    entries=value['entries']
    state=value['state']
    count=5 if mode in ['parallel-cancel','auto-five-resume'] else 12 if fixture.parallel else 1
    for app,entry in entries.items():
        ready=not(mode=='parallel-one-fails' and app=='570')
        check(bool(entry.get('dynamicReady'))==ready,f'{app}: readiness matches completed transfer')
        if ready and entry.get('dynamicReady'):
            asset=entry['assets']['dynamic']
            file=destination/'artwork/video'/(asset['id']+'.'+asset['extension'])
            version=2 if mode in ['resume-changed-200','resume-changed-206','hls-plan-changed'] else 1
            check(hashlib.sha256(file.read_bytes()).digest()==hashlib.sha256(full(int(app),version,fixture.hls)).digest(),f'{app}: exact bytes, no mixed or duplicate prefix')
    check(state.get('workerPid',0)==0 and state.get('activeCount',0)==0,'worker and active lanes released')
    if fixture.parallel:
        wanted=2 if mode=='parallel-two' else 5
        check(fixture.peak==wanted and value['maxActive']==wanted,f'transport and queue reach {wanted} lanes')
        check(fixture.peak<=5 and value['maxActive']<=5 and not fixture.duplicate,'hard cap5 and no duplicate game claim')
        if mode=='parallel-one-fails': check(value['jobs']['steam-570'].get('attempts')==1 and state.get('completed')==count-1,'failure does not penalize siblings')
    else:
        check(not any(entry.get('dynamicReady') for entry in value['firstEntries'].values()),'incomplete first transfer never publishes')
    ranges=[row for row in fixture.logs if row['range']]
    if mode in ['resume-etag','resume-last-modified','resume-416','hls-resume','parallel-cancel','auto-five-resume']:
        check(bool(ranges) and value['resumedBytes']>0,'validated cache produces actual Range and visible resume state')
        check(all(row['ifRange'] for row in ranges),'every resume carries a validator')
    if mode in ['resume-last-modified-recent','resume-last-modified-invalid','resume-last-modified-weak-etag','resume-no-validator','resume-weak','resume-corrupt-meta','resume-missing-part','resume-app-isolation','hls-plan-changed']:
        check(not ranges,'unverified, changed-plan or different-game cache is never appended')
    if mode in ['resume-ignored','resume-changed-200','resume-bad-start','resume-bad-total','resume-changed-206','resume-encoding']:
        check(bool(ranges),'server mismatch exercised after a genuine partial transfer')
    if mode=='parallel-cancel': check(value['firstState'].get('enabled') is False and value['firstState'].get('activeCount',0)==0,'OFF stops all lanes before re-enable')
    if mode=='auto-five-resume':
        recovery=value['recovery']
        check(recovery.get('complete') and 0<recovery.get('firstFailureMs',0)<3000,'hung lane detected while siblings send progress')
        check(recovery.get('elapsedMs',0)>=60000 and len(state.get('history',[]))>=2,'real minute backoff then automatic pool recovery')
    (destination/'http-requests.json').write_text(json.dumps(fixture.logs,indent=2),encoding='utf-8')
    return {'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks,'peakConnections':fixture.peak,'maxActive':value['maxActive'],'rangeRequests':len(ranges),'resumedBytes':value['resumedBytes'],'seconds':round(time.monotonic()-started,3)}

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('exe',type=pathlib.Path)
    ap.add_argument('root',type=pathlib.Path)
    ap.add_argument('--case',action='append')
    args=ap.parse_args()
    args.exe=args.exe.resolve()
    args.root=args.root.resolve()
    if args.exe.parent!=ALLOWED or not args.exe.name.startswith('parallel-resume-') or args.exe.suffix.lower()!='.exe': raise ValueError('Only the owned acceptance binary is allowed')
    if not args.root.is_relative_to(ALLOWED) or args.root==ALLOWED: raise ValueError('Evidence must stay in the owned task lane')
    if args.case and any(mode not in CASES for mode in args.case): raise ValueError('Unknown declarative fixture')
    args.root.mkdir(parents=True,exist_ok=False)
    reports=[]
    for mode in CASES:
        if args.case and mode not in args.case: continue
        try: row=run(args.exe,args.root,mode)
        except Exception as error: row={'case':mode,'ok':False,'checks':[],'error':str(error)}
        reports.append(row)
        print(json.dumps({'case':mode,'ok':row['ok'],'failed':[c['label'] for c in row['checks'] if not c['ok']],'error':row.get('error','')}),flush=True)
    report={'ok':all(r['ok'] for r in reports),'cases':len(reports),'checks':sum(len(r['checks']) for r in reports),'results':reports,'scope':'production queue/worker/supervisor; real WinHTTP routed to owned loopback only in test binary; explicit retry for fast matrix, real minute backoff in auto case; exact hashes of synthetic media; no installed application or input action'}
    (args.root/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({'ok':report['ok'],'cases':report['cases'],'checks':report['checks']}))
    return 0 if report['ok'] else 1
if __name__=='__main__': raise SystemExit(main())
