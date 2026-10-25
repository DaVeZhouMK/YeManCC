"""Production Worker/WinHTTP checks; all writes and transport stay in a fresh fixture."""
import argparse, hashlib, json, pathlib, subprocess, sys, time
from urllib.parse import urlparse
from shared_game_media_parallel_resume_fixture import Fixture, HOST, INIT, payload, box

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--exe',type=pathlib.Path,required=True);ap.add_argument('--out',type=pathlib.Path,required=True);ap.add_argument('--captured-page',type=pathlib.Path);ap.add_argument('--case',action='append');args=ap.parse_args()
    if args.out.exists():raise ValueError('fresh output required')
    args.out.mkdir(parents=True);reports=[]
    cases=['same-game-formal','same-game-extras','different-game-trailer','different-game-extras','encoded-extras','quoted-formal','legacy-metadata','slow-bytes','slow-metadata','session']
    if args.captured_page:cases.append('captured-anvil')
    for mode in cases:
        if args.case and mode not in args.case:continue
        fixture=Fixture(mode);base=fixture.server.RequestHandlerClass;requests=[]
        body=INIT+box(b'mdat',payload(570)[:128*1024])
        class Handler(base):
            def dispatch(self):
                path=urlparse(self.path).path;requests.append(path)
                formal=HOST+'/store_trailers/570/preview.mp4'
                if path=='/api/appdetails':
                    if mode=='slow-metadata':time.sleep(.8)
                    movies=[{'mp4':{'max':HOST+'/steam/apps/256972298/movie_max.mp4'}}] if mode=='legacy-metadata' else [{'mp4':{'max':formal}}] if mode in ['slow-bytes','slow-metadata','session'] else []
                    data={'steam_appid':570,'type':'game','name':'verified fixture','background_raw':HOST+'/assets/570/poster.jpg','movies':movies}
                    return self.send_body(json.dumps({'570':{'success':True,'data':data}}).encode(),kind='metadata')
                if path.startswith('/app/'):
                    url=HOST+{'same-game-formal':'/store_trailers/570/preview.mp4','same-game-extras':'/store_item_assets/steam/apps/570/extras/loop.mp4','different-game-trailer':'/store_trailers/730/preview.mp4','different-game-extras':'/store_item_assets/steam/apps/730/extras/loop.mp4','encoded-extras':'/store_item_assets/steam/apps/570/%65xtras/loop.mp4','quoted-formal':'/store_trailers/570/preview.mp4'}[mode] if mode!='captured-anvil' else ''
                    html=args.captured_page.read_text(encoding='utf-8-sig') if mode=='captured-anvil' else ('{&quot;hlsManifest&quot;:&quot;'+url+'&quot;}') if mode=='quoted-formal' else '<video src="'+url+'"></video>'
                    return self.send_body(html.encode(),kind='page')
                if path.endswith('preview.mp4') or path.endswith('movie_max.mp4') or path.endswith('loop.mp4'):
                    return self.send_body(body,extra={'ETag':'"bound-video"'},delay=.15 if mode=='slow-bytes' else .3 if mode=='session' else 0,kind='video')
                return super().dispatch()
        fixture.server.RequestHandlerClass=Handler;fixture.start();started=time.monotonic()
        try:
            p=subprocess.run([str(args.exe.resolve()),str((args.out/mode).resolve()),str(fixture.server.server_port),mode],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=20)
            if p.returncode:raise RuntimeError(p.stderr.decode(errors='replace'))
            value=json.loads(p.stdout)
        finally:fixture.close()
        checks=[]
        def check(ok,label):checks.append({'ok':bool(ok),'label':label})
        ready=mode in ['same-game-formal','quoted-formal','legacy-metadata','slow-bytes']
        entry=value['entry'];job=value['jobs']['steam-570']
        check(bool(entry.get('dynamicReady'))==ready,'only correct formal source with productive transfer is published')
        if ready and entry.get('dynamicReady'):
            a=entry['assets']['dynamic'];file=args.out/mode/'artwork/video'/(a['id']+'.'+a['extension']);check(hashlib.sha256(file.read_bytes()).digest()==hashlib.sha256(body).digest(),'published bytes match exact declared source')
        elif mode not in ['slow-metadata','session']:
            check(not any(path.endswith('.mp4') or path.endswith('.webm') or path.endswith('.m3u8') for path in requests),'no decoration or other-game video is requested')
            check(job.get('lastError')=='no-dynamic-video','static remains usable with honest missing-video outcome')
        else:
            check(job.get('lastError')=='media-task-timeout','unproductive metadata and absolute session ceiling remain bounded')
            check(job.get('attempts')==1,'failed attempt retains retry budget')
        if mode=='slow-bytes':check(value['elapsedMs']>1000,'actual received bytes survive the original 500ms task timeout')
        check(value['state'].get('workerPid',0)==0 and value['state'].get('activeCount',0)==0,'finished task leaves no active lanes')
        report={'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks,'elapsedMs':value['elapsedMs'],'requests':requests};reports.append(report)
        print(json.dumps({'case':mode,'ok':report['ok'],'failed':[c['label'] for c in checks if not c['ok']]}),flush=True)
    report={'ok':all(r['ok'] for r in reports),'checks':sum(len(r['checks']) for r in reports),'results':reports,'scope':'Production worker with real loopback WinHTTP and publishing; explicit AC fixture and short task/session limits for timing cases; captured Steam page replay only, no live cache/Steam writes'}
    (args.out/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8');return 0 if report['ok'] else 1
if __name__=='__main__':raise SystemExit(main())
