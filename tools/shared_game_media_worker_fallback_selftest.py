"""Production Worker/WinHTTP checks; all writes and transport stay in a fresh fixture."""
import argparse, hashlib, json, pathlib, subprocess, sys, time
from urllib.parse import parse_qs, urlparse
from shared_game_media_parallel_resume_fixture import Fixture, HOST, INIT, payload, box

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--exe',type=pathlib.Path,required=True);ap.add_argument('--out',type=pathlib.Path,required=True);ap.add_argument('--captured-page',type=pathlib.Path);ap.add_argument('--case',action='append');args=ap.parse_args()
    if args.out.exists():raise ValueError('fresh output required')
    args.out.mkdir(parents=True);reports=[]
    cases=['same-game-formal','same-game-extras','different-game-trailer','different-game-extras','encoded-extras','quoted-formal','legacy-metadata','slow-bytes','slow-metadata','session']
    unavailable_cases=['slot-static-store-unavailable','slot-dynamic-store-unavailable','slot-static-not-game','slot-dynamic-not-game']
    slot_cases=['slot-static-page-fails','slot-static-no-videos','slot-dynamic-no-poster','slot-dynamic-bad-poster']
    partial_missing_cases=['pair-no-video-poster-fails','pair-no-poster-video-fails']
    pair_cases=['pair-page-fails','cached-pair-page-fails']
    demo_cases=['demo-static','demo-pair','demo-mismatched-id','demo-music','demo-dlc','demo-manual-retry','demo-region-fallback']
    poster_cases=['poster-empty-raw','poster-unused-malformed-background','poster-raw-priority','poster-raw-absent','poster-both-empty']
    directed=poster_cases+demo_cases+['cached-static-fail','cached-static-ok','cached-static-missing','cached-pair-fail','publication-guards']+slot_cases+unavailable_cases+pair_cases+partial_missing_cases+['pair-no-poster']
    if args.captured_page:cases.append('captured-anvil')
    if args.case:
        if len(set(args.case))!=len(args.case) or set(args.case)-set(cases+directed):raise ValueError('unknown or duplicate explicit case selection')
        cases=[case for case in cases+directed if case in args.case]
    for mode in cases:
        if args.case and mode not in args.case:continue
        fixture=Fixture(mode);base=fixture.server.RequestHandlerClass;requests=[];countries=[]
        body=INIT+box(b'mdat',payload(570)[:128*1024])
        class Handler(base):
            def dispatch(self):
                parsed=urlparse(self.path);path=parsed.path;requests.append(path)
                formal=HOST+'/store_trailers/570/preview.mp4'
                if path=='/api/appdetails':
                    country=parse_qs(parsed.query).get('cc',[''])[0];countries.append(country)
                    if mode.endswith('-store-unavailable') or (mode=='demo-region-fallback' and country!='jp'):
                        return self.send_body(json.dumps({'570':{'success':False}}).encode(),kind='metadata-unavailable')
                    if mode=='slow-metadata':time.sleep(.8)
                    movies=[{'mp4':{'max':HOST+'/steam/apps/256972298/movie_max.mp4'}}] if mode=='legacy-metadata' else [{'mp4':{'max':formal}}] if mode in ['slow-bytes','slow-metadata','session'] or mode in directed else []
                    if mode.startswith('slot-static-') or mode in pair_cases:movies=[]
                    if mode=='pair-no-video-poster-fails':movies=[]
                    data={'steam_appid':570,'type':'game','name':'verified fixture','background_raw':'' if mode in ['cached-static-missing','slot-dynamic-no-poster','pair-no-poster','pair-no-poster-video-fails'] else HOST+'/assets/570/poster.jpg','movies':movies}
                    if mode in poster_cases:
                        data['background']=HOST+'/assets/570/poster.jpg'
                        if mode=='poster-empty-raw':data['background_raw']=''
                        elif mode=='poster-unused-malformed-background':data['background']={'unused':'malformed optional field'}
                        elif mode=='poster-raw-priority':data['background']=HOST+'/assets/570/unselected.jpg'
                        elif mode=='poster-raw-absent':data.pop('background_raw')
                        else:data['background_raw']='';data['background']=''
                    if mode=='slot-dynamic-bad-poster':data['background_raw']={'unrelated':'malformed static metadata'}
                    if mode.startswith('demo-'):
                        data['type']='music' if mode=='demo-music' else 'dlc' if mode=='demo-dlc' else 'demo'
                        data['fullgame']={'appid':'730','name':'unrelated full game'}
                        if mode=='demo-static':data['movies']=[]
                        if mode=='demo-mismatched-id':data['steam_appid']=730
                    if mode.endswith('-not-game'):data['type']='music'
                    return self.send_body(json.dumps({'570':{'success':True,'data':data}}).encode(),kind='metadata')
                if path=='/assets/570/poster.jpg' and mode in ['cached-static-fail','cached-pair-fail','pair-no-video-poster-fails']:
                    return self.send_body(b'owned unavailable poster',status=404,kind='poster-failure')
                if path.startswith('/app/'):
                    if mode=='slot-static-page-fails' or mode in pair_cases:return self.send_body(b'video page unavailable',status=404,kind='page-failure')
                    if mode=='demo-static':return self.send_body(b'<html>no demo trailers</html>',kind='page')
                    if mode=='slot-static-no-videos':return self.send_body(b'<html>no trailers</html>',kind='page')
                    if mode=='pair-no-video-poster-fails':return self.send_body(b'<html>no trailers</html>',kind='page')
                    url=HOST+{'same-game-formal':'/store_trailers/570/preview.mp4','same-game-extras':'/store_item_assets/steam/apps/570/extras/loop.mp4','different-game-trailer':'/store_trailers/730/preview.mp4','different-game-extras':'/store_item_assets/steam/apps/730/extras/loop.mp4','encoded-extras':'/store_item_assets/steam/apps/570/%65xtras/loop.mp4','quoted-formal':'/store_trailers/570/preview.mp4'}[mode] if mode!='captured-anvil' else ''
                    html=args.captured_page.read_text(encoding='utf-8-sig') if mode=='captured-anvil' else ('{&quot;hlsManifest&quot;:&quot;'+url+'&quot;}') if mode=='quoted-formal' else '<video src="'+url+'"></video>'
                    return self.send_body(html.encode(),kind='page')
                if path.endswith('preview.mp4') or path.endswith('movie_max.mp4') or path.endswith('loop.mp4'):
                    if mode=='pair-no-poster-video-fails':return self.send_body(b'video unavailable',status=404,kind='video-failure')
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
        if mode in poster_cases:
            entry=value['entry'];job=value['jobs']['steam-570'];missing=mode=='poster-both-empty'
            check(value['workerCode']==0,'poster metadata fallback stays in the original Worker')
            check(bool(entry.get('staticReady'))==(not missing),'poster field preference preserves available static media')
            check(entry.get('dynamicReady'),'poster fields cannot prevent offered video publication')
            check(job.get('state')=='done' and not job.get('lastError') and not job.get('attempts'),'poster field choice does not create error debt or false failure')
            check(not entry.get('error') and not entry.get('refreshRequested'),'poster settlement leaves no stranded refresh or Store error')
            check(requests.count('/api/appdetails')==1 and requests.count('/assets/570/poster.jpg')==(0 if missing else 1),'one exact poster source follows one metadata read')
            check(not any(path.endswith('unselected.jpg') or path.startswith('/app/') for path in requests),'unused background and unnecessary fallback pages are never requested')
            check(bool(entry.get('staticUnavailableUntil'))==missing,'only genuinely absent poster gets a bounded unavailable result')
            check(not value['state'].get('activeCount') and not value['state'].get('workerPid'),'poster result releases every active lane')
            if not missing:
                asset=entry.get('assets',{}).get('static',{});file=args.out/mode/'artwork/video'/(asset.get('id','missing')+'.'+asset.get('extension','missing'))
                check(file.is_file() and hashlib.sha256(file.read_bytes()).digest()==hashlib.sha256(b'\xff\xd8\xff\xe0'+b'\0'*64).digest(),'poster fallback publishes exact declared response bytes')
            report={'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks,'elapsedMs':value['elapsedMs'],'requests':requests,'rawResult':str((args.out/mode/'result.json').resolve())};reports.append(report)
            print(json.dumps({'case':mode,'ok':report['ok'],'failed':[c['label'] for c in checks if not c['ok']]}),flush=True)
            if not report['ok']:break
            continue
        if mode in demo_cases:
            entry=value['entry'];job=value['jobs']['steam-570'];blocked=mode in ['demo-music','demo-dlc'];mismatch=mode=='demo-mismatched-id'
            check(value['workerCode']==0,'demo metadata settles through original Worker')
            expected_countries=['us','cn','jp'] if mode=='demo-region-fallback' else ['us']
            check(requests.count('/api/appdetails')==len(expected_countries) and countries==expected_countries,'only the expected ordered storefront queries are made')
            check(not value['state'].get('activeCount') and not value['state'].get('workerPid'),'demo result returns every owned active lane')
            if mismatch or blocked:
                check(not entry.get('staticReady') and not entry.get('dynamicReady'),'foreign or non-game metadata publishes no media')
                check(requests==['/api/appdetails'],'foreign and unsupported types launch no asset or parent-game request')
                check(job.get('lastError')==('media-identity-unconfirmed' if mismatch else 'not-game-or-no-video'),'AppID refusal and unsupported type remain distinct')
                check(job.get('attempts')==(1 if mismatch else 0),'metadata mismatch spends original failure attempt only')
            else:
                pair=mode!='demo-static'
                check(entry.get('staticReady') and bool(entry.get('dynamicReady'))==pair,'playable demo retains its poster and offered trailer')
                check(job.get('lastError')==('' if pair else 'no-dynamic-video'),'demo absence applies only to the unavailable video')
                check(not entry.get('error') and not job.get('attempts'),'playable demo does not consume failed-transfer debt')
                check(requests.count('/assets/570/poster.jpg')==1 and not any('/730' in path for path in requests),'demo assets retain the exact demo AppID rather than fullgame parent')
                asset=entry.get('assets',{}).get('static',{});file=args.out/mode/'artwork/video'/(asset.get('id','missing')+'.'+asset.get('extension','missing'))
                check(file.is_file() and hashlib.sha256(file.read_bytes()).digest()==hashlib.sha256(b'\xff\xd8\xff\xe0'+b'\0'*64).digest(),'published demo poster bytes equal the declared owned response')
                if pair:
                    asset=entry.get('assets',{}).get('dynamic',{});file=args.out/mode/'artwork/video'/(asset.get('id','missing')+'.'+asset.get('extension','missing'))
                    check(file.is_file() and hashlib.sha256(file.read_bytes()).digest()==hashlib.sha256(body).digest(),'published demo trailer bytes equal the exact response')
                else:check(entry.get('videoUnavailableUntil',0)>0 and not entry.get('staticUnavailableUntil'),'no-video demo leaves its usable poster available')
                if mode=='demo-manual-retry':
                    prior=json.loads((args.out/mode/'demo-stopped-before-retry.json').read_text(encoding='utf-8-sig'))
                    check(prior['queue']['jobs']['steam-570'].get('autoStopped') and prior['queue']['jobs']['steam-570'].get('lastError')=='not-game-or-no-video','manual retry begins from the actual old categorical stop')
                    check(prior['entry'].get('videoUnavailableUntil',0)>0 and prior['entry'].get('staticUnavailableUntil',0)>0,'old unavailable Store deadlines are retained in the before record')
                    check(value['jobs']['steam-777']==prior['queue']['jobs']['steam-777'],'explicit demo retry cannot refund or alter the unrelated stopped job')
                    check(entry.get('lastManualRetry') and entry.get('lastManualRetry')!=prior['entry'].get('lastManualRetry'),'original fresh manual request bypasses old missing deadlines once')
            report={'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks,'elapsedMs':value['elapsedMs'],'requests':requests,'rawResult':str((args.out/mode/'result.json').resolve())};reports.append(report)
            print(json.dumps({'case':mode,'ok':report['ok'],'failed':[c['label'] for c in checks if not c['ok']]}),flush=True)
            if not report['ok']:break
            continue
        if mode in partial_missing_cases:
            entry=value['entry'];job=value['jobs']['steam-570']
            absent='dynamic' if mode=='pair-no-video-poster-fails' else 'static'
            failed='static' if absent=='dynamic' else 'dynamic'
            field='videoUnavailableUntil' if absent=='dynamic' else 'staticUnavailableUntil'
            other='staticUnavailableUntil' if absent=='dynamic' else 'videoUnavailableUntil'
            check(value['workerCode']==0,'partial absence and source failure stay in the ordinary worker lane')
            check(job.get('state')=='pending' and job.get('attempts')==1 and job.get('lastError')=='media-http-404','actual failed transfer spends exactly one original queue attempt')
            check(entry.get('error')=='media-http-404' and entry.get('attempts')==1 and entry.get('retryAt',0)>0,'actual Store error and retry deadline remain intact')
            check(entry.get(field,0)>0,'conclusively absent requested slot retains its bounded unavailable deadline despite other failure')
            check(not entry.get(other),'failed transfer is not misclassified as confirmed missing media')
            check(not entry.get('staticReady') and not entry.get('dynamicReady'),'no failed or absent asset is published')
            check(not value['state'].get('activeCount') and not value['state'].get('workerPid'),'partial failure releases every active lane')
            recovered=None;recovery_requests=[]
            if all(c['ok'] for c in checks):
                healthy=Fixture('pair-page-recover');healthy_base=healthy.server.RequestHandlerClass
                class PartialRecovery(healthy_base):
                    def dispatch(self):
                        path=urlparse(self.path).path;recovery_requests.append(path)
                        if path=='/api/appdetails':
                            data={'steam_appid':570,'type':'game','name':'verified fixture','background_raw':'' if absent=='static' else HOST+'/assets/570/poster.jpg','movies':[] if absent=='dynamic' else [{'mp4':{'max':HOST+'/store_trailers/570/preview.mp4'}}]}
                            return self.send_body(json.dumps({'570':{'success':True,'data':data}}).encode(),kind='metadata')
                        if path.endswith('preview.mp4'):return self.send_body(body,extra={'ETag':'"bound-video"'},kind='video')
                        return super().dispatch()
                healthy.server.RequestHandlerClass=PartialRecovery;healthy.start()
                try:
                    resumed=subprocess.run([str(args.exe.resolve()),str((args.out/mode).resolve()),str(healthy.server.server_port),'pair-page-recover'],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=20)
                    if resumed.returncode:raise RuntimeError(resumed.stderr.decode(errors='replace'))
                    recovered=json.loads(resumed.stdout)
                finally:healthy.close()
                check(recovered['jobs']['steam-570'].get('state')=='done' and not recovered['jobs']['steam-570'].get('lastError'),'same request completes after only the failed source recovers')
                check(recovered['entry'].get(failed+'Ready') and not recovered['entry'].get(absent+'Ready'),'recovery publishes only the previously failed slot')
                check(recovered['entry'].get(field)==entry.get(field),'recovery retains the original absence deadline without extending it')
                check(not any(path.startswith('/app/') for path in recovery_requests),'known missing video never causes another fallback-page request')
                check(not any(path.endswith(('.mp4','.webm','.m3u8')) for path in recovery_requests) if absent=='dynamic' else not any(path.endswith('poster.jpg') for path in recovery_requests),'recovery transfers no confirmed missing slot')
                check(not recovered['entry'].get('error') and not recovered['entry'].get('refreshRequested') and not recovered['state'].get('activeCount') and recovered['state'].get('nextDueAt',0)<0,'completed recovery releases debt and returns to idle')
            report={'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks,'elapsedMs':value['elapsedMs'],'requests':requests,'rawResult':str((args.out/mode/('result-'+mode+'.json')).resolve()),'recoveryRequests':recovery_requests};reports.append(report)
            if recovered is not None:report['recoveryRaw']=str((args.out/mode/'result-pair-page-recover.json').resolve())
            print(json.dumps({'case':mode,'ok':report['ok'],'failed':[c['label'] for c in checks if not c['ok']]}),flush=True)
            if not report['ok']:break
            continue
        if mode=='pair-no-poster':
            entry=value['entry'];job=value['jobs']['steam-570']
            check(value['workerCode']==0,'valid video with no poster completes through the original worker')
            check(entry.get('dynamicReady') and not entry.get('staticReady'),'real dynamic asset is published without inventing a poster')
            check(entry.get('staticUnavailableUntil',0)>0 and not entry.get('videoUnavailableUntil'),'successful video publication preserves the requested static unavailability receipt')
            check(job.get('state')=='done' and not job.get('attempts') and not job.get('lastError'),'expected missing poster does not spend failure budget')
            check(not any(path.endswith('poster.jpg') or path.startswith('/app/') for path in requests),'missing poster and offered video start no doomed or fallback requests')
            check(not entry.get('error') and not entry.get('refreshRequested'),'completed pair has no outstanding error or user edit')
            check(not value['state'].get('activeCount') and not value['state'].get('workerPid') and value['state'].get('nextDueAt',0)<0,'completion returns to idle')
            report={'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks,'elapsedMs':value['elapsedMs'],'requests':requests,'rawResult':str((args.out/mode/'result.json').resolve())};reports.append(report)
            print(json.dumps({'case':mode,'ok':report['ok'],'failed':[c['label'] for c in checks if not c['ok']]}),flush=True)
            if not report['ok']:break
            continue
        if mode in unavailable_cases:
            entry=value['entry'];original=value['originalEntry'];job=value['jobs']['steam-570']
            slot='dynamic' if mode.startswith('slot-dynamic-') else 'static'
            requested='videoUnavailableUntil' if slot=='dynamic' else 'staticUnavailableUntil'
            unrequested='staticUnavailableUntil' if slot=='dynamic' else 'videoUnavailableUntil'
            reason='store-unavailable' if mode.endswith('-store-unavailable') else 'not-game-or-no-video'
            check(value['workerCode']==0,'explicit metadata absence settles through the original worker')
            check(entry.get('assets')==original.get('assets') and entry.get('staticReady') and entry.get('dynamicReady'),'explicit source absence retains both exact playable cached assets')
            check(entry.get(requested,0)>0,'the requested slot receives its bounded unavailable deadline')
            check(entry.get(unrequested,0)==original.get(unrequested,0),'the unrequested slot never receives a new unavailable deadline')
            check(job.get('state')=='unavailable' and job.get('autoStopped') and job.get('lastError')==reason and job.get('stopReason')==reason,'original explicit unavailable outcome is preserved without a false success')
            check(not job.get('attempts') and not job.get('automaticElapsedMs'),'explicit absence spends no failed network retry budget')
            check(not entry.get('refreshRequested') and not entry.get('error') and not entry.get('taskSlots') and not entry.get('leaseUntil'),'the owned request and lease settle without error or stranded intent')
            check(requests.count('/api/appdetails')==(3 if reason=='store-unavailable' else 1),'the existing bounded region fallback remains exact')
            check(all(path=='/api/appdetails' for path in requests),'confirmed source absence launches no page or asset transfer')
            check(not value['state'].get('activeCount') and not value['state'].get('workerPid') and value['state'].get('nextDueAt',0)<0,'unavailable settlement returns to idle')
            report={'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks,'elapsedMs':value['elapsedMs'],'requests':requests,'rawResult':str((args.out/mode/('result-'+mode+'.json')).resolve())};reports.append(report)
            print(json.dumps({'case':mode,'ok':report['ok'],'failed':[c['label'] for c in checks if not c['ok']]}),flush=True)
            if not report['ok']:break
            continue
        if mode in slot_cases:
            entry=value['entry'];original=value['originalEntry'];job=value['jobs']['steam-570']
            slot='dynamic' if mode.startswith('slot-dynamic-') else 'static';other='static' if slot=='dynamic' else 'dynamic'
            check(value['workerCode']==0,'slot-specific refresh is settled through the ordinary worker')
            check(entry.get(slot+'Ready') and entry.get('assets',{}).get(slot)!=original['assets'][slot],'the requested asset is actually replaced')
            check(entry.get('assets',{}).get(other)==original['assets'][other],'the unrequested cached asset stays byte-identical')
            check(job.get('state')=='done' and not job.get('autoStopped') and not job.get('lastError'),'successful one-slot work is not stopped as missing unrequested media')
            check(job.get('attempts')==0 and not job.get('automaticElapsedMs'),'unrelated metadata does not consume retry attempts or time')
            check(not entry.get('refreshRequested') and not entry.get('error'),'successful requested replacement clears its pending intent and error')
            check(entry.get('videoUnavailableUntil',0)==original.get('videoUnavailableUntil',0) and entry.get('staticUnavailableUntil',0)==original.get('staticUnavailableUntil',0),'unrequested missing metadata does not create an unavailable deadline')
            check(requests.count('/api/appdetails')==1,'one admitted metadata read supplies the requested asset')
            check(not any(path.startswith('/app/') for path in requests),'one-slot refresh does not fetch an unneeded video fallback page')
            check(not any(path.endswith(('.mp4','.webm','.m3u8')) for path in requests) if slot=='static' else not any(path.endswith('poster.jpg') for path in requests),'one-slot refresh never transfers the other asset')
            asset=entry.get('assets',{}).get(slot,{});file=args.out/mode/'artwork/video'/(asset.get('id','missing')+'.'+asset.get('extension','missing'))
            expected=body if slot=='dynamic' else b'\xff\xd8\xff\xe0'+b'\0'*64
            check(file.is_file() and hashlib.sha256(file.read_bytes()).digest()==hashlib.sha256(expected).digest(),'published bytes are the exact requested fixture asset')
            check(not value['state'].get('activeCount') and not value['state'].get('workerPid') and value['state'].get('nextDueAt',0)<0,'successful one-slot completion returns to event-driven idle')
            report={'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks,'elapsedMs':value['elapsedMs'],'requests':requests,'rawResult':str((args.out/mode/('result-'+mode+'.json')).resolve())};reports.append(report)
            print(json.dumps({'case':mode,'ok':report['ok'],'failed':[c['label'] for c in checks if not c['ok']]}),flush=True)
            if not report['ok']:break
            continue
        if mode in pair_cases:
            entry=value['entry'];job=value['jobs']['steam-570'];original=value['originalEntry']
            check(value['workerCode']==0,'video page failure remains an ordinary per-game result')
            check(entry.get('staticReady') and entry.get('assets',{}).get('static')!=original.get('assets',{}).get('static'),'a failed video-only page cannot prevent the requested valid poster publication')
            check(job.get('state')=='pending' and job.get('attempts')==1 and job.get('lastError')=='media-http-404','the actual video failure spends one original attempt and is never reported as no-video or success')
            check(entry.get('error')=='media-http-404' and entry.get('retryAt',0)>0,'the Store preserves the real failed slot and original retry deadline')
            check(not entry.get('videoUnavailableUntil') and not entry.get('staticUnavailableUntil'),'unreadable page is not authoritative missing-media metadata')
            check(requests.count('/assets/570/poster.jpg')==1 and requests.index('/assets/570/poster.jpg')<requests.index('/app/570/'),'poster completes before the unrelated video page is read')
            check(not any(path.endswith(('.mp4','.webm','.m3u8')) for path in requests),'unconfirmed video is never downloaded')
            if mode=='cached-pair-page-fails':
                check(entry.get('refreshRequested') and entry.get('refreshSlot')=='dynamic' and entry.get('selectedVideo')==original.get('selectedVideo',{'appId':570,'url':HOST+'/store_trailers/570/preview.mp4'}),'selected video request remains pending independently of the replaced poster')
                check(entry['assets']['dynamic']==original['assets']['dynamic'],'failed selected lookup retains the exact previous video')
            check(not value['state'].get('activeCount') and not value['state'].get('workerPid'),'failed partial request releases all active lanes')
            recovery_requests=[];recovered=None
            if all(c['ok'] for c in checks):
                healthy=Fixture('pair-page-recover');healthy_base=healthy.server.RequestHandlerClass
                class PairRecovery(healthy_base):
                    def dispatch(self):
                        path=urlparse(self.path).path;recovery_requests.append(path)
                        if path=='/api/appdetails':
                            data={'steam_appid':570,'type':'game','name':'verified fixture','background_raw':HOST+'/assets/570/poster.jpg','movies':[{'mp4':{'max':HOST+'/store_trailers/570/preview.mp4'}}]}
                            return self.send_body(json.dumps({'570':{'success':True,'data':data}}).encode(),kind='metadata')
                        if path.endswith('preview.mp4'):return self.send_body(body,extra={'ETag':'"bound-video"'},kind='video')
                        return super().dispatch()
                healthy.server.RequestHandlerClass=PairRecovery;healthy.start()
                try:
                    resumed=subprocess.run([str(args.exe.resolve()),str((args.out/mode).resolve()),str(healthy.server.server_port),'pair-page-recover'],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=20)
                    if resumed.returncode:raise RuntimeError(resumed.stderr.decode(errors='replace'))
                    recovered=json.loads(resumed.stdout)
                finally:healthy.close()
                check(recovered['jobs']['steam-570'].get('state')=='done' and not recovered['entry'].get('error') and not recovered['entry'].get('refreshRequested'),'same accepted request completes after video metadata recovers')
                check(recovered['entry']['assets']['static']==entry['assets']['static'] and not any(path.endswith('poster.jpg') for path in recovery_requests),'recovery reuses the completed poster instead of downloading it again')
                check(recovered['entry'].get('dynamicReady') and recovered['state'].get('nextDueAt',0)<0,'recovered video completes the pair and returns to idle')
            report={'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks,'elapsedMs':value['elapsedMs'],'requests':requests,'rawResult':str((args.out/mode/('result-'+mode+'.json')).resolve()),'recoveryRequests':recovery_requests};reports.append(report)
            if recovered is not None:report['recoveryRaw']=str((args.out/mode/'result-pair-page-recover.json').resolve())
            print(json.dumps({'case':mode,'ok':report['ok'],'failed':[c['label'] for c in checks if not c['ok']]}),flush=True)
            if not report['ok']:break
            continue
        if mode=='publication-guards':
            check(value.get('ok') and value.get('mode')==mode,'original Queue and Store publication guard controls pass')
            check(not requests,'guard controls perform no network requests')
            report={'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks,'nativeChecks':value.get('checks',0),'elapsedMs':round((time.monotonic()-started)*1000),'requests':requests,'rawResult':str((args.out/mode/'result.json').resolve())};reports.append(report)
            print(json.dumps({'case':mode,'ok':report['ok'],'nativeChecks':value.get('checks',0)}),flush=True)
            if not report['ok']:break
            continue
        if mode in directed:
            entry=value['entry'];job=value['jobs']['steam-570'];original=value['originalEntry'];failed=mode in ['cached-static-fail','cached-pair-fail']
            check(value['workerCode']==0,'one failed refresh remains an ordinary per-item result')
            check(entry.get('assets',{}).get('static')==original.get('assets',{}).get('static') if mode!='cached-static-ok' else entry.get('assets',{}).get('static')!=original.get('assets',{}).get('static'),'static failure/missing metadata retains fallback; success replaces it')
            check(job.get('state')=='pending' if failed else job.get('state')=='done','old usable static does not turn a failed refresh into a success receipt')
            check(job.get('attempts')==1 and job.get('lastError')=='media-http-404' if failed else job.get('attempts')==0,'failure spends exactly one original attempt while honest success spends none')
            check(bool(entry.get('refreshRequested'))==failed,'failed requested slot remains durable until a real result')
            check(entry.get('error')=='media-http-404' and entry.get('retryAt',0)>0 if failed else not entry.get('error'),'Store failure receipt and original retry deadline are retained')
            check(not value['state'].get('activeCount') and not value['state'].get('workerPid'),'settlement returns every active lane')
            if mode=='cached-pair-fail':
                check(value.get('publicationObserved') and value.get('inventoryBlockedUntilAcknowledgement') and value.get('concurrentInventoryCompleted'),'actual concurrent inventory waits until partial publication and queue receipt are atomic')
                check(entry.get('assets',{}).get('dynamic')!=original.get('assets',{}).get('dynamic'),'successful selected video publication is retained alongside static error')
                check(value['afterRepeatedInventory']['jobs']['steam-570'].get('attempts')==job.get('attempts') and value['afterRepeatedInventory']['jobs']['steam-570'].get('automaticElapsedMs')==job.get('automaticElapsedMs'),'partial publication plus repeated inventory cannot refund attempts or time')
            elif mode=='cached-static-missing':
                check(not any(path.endswith('poster.jpg') for path in requests),'empty poster metadata does not start a doomed transfer')
            recovered=None;recovery_requests=[]
            if failed and all(c['ok'] for c in checks):
                healthy=Fixture('cached-static-recover');healthy_base=healthy.server.RequestHandlerClass
                class Recovery(healthy_base):
                    def dispatch(self):
                        path=urlparse(self.path).path;recovery_requests.append(path)
                        if path=='/api/appdetails':
                            data={'steam_appid':570,'type':'game','name':'verified fixture','background_raw':HOST+'/assets/570/poster.jpg','movies':[{'mp4':{'max':HOST+'/store_trailers/570/preview.mp4'}}]}
                            return self.send_body(json.dumps({'570':{'success':True,'data':data}}).encode(),kind='metadata')
                        if path=='/assets/570/poster.jpg':return self.send_body(b'\xff\xd8\xff\xe0recovered-owned-poster',kind='poster')
                        return super().dispatch()
                healthy.server.RequestHandlerClass=Recovery;healthy.start()
                try:
                    resumed=subprocess.run([str(args.exe.resolve()),str((args.out/mode).resolve()),str(healthy.server.server_port),'cached-static-recover'],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=20)
                    if resumed.returncode:raise RuntimeError(resumed.stderr.decode(errors='replace'))
                    recovered=json.loads(resumed.stdout)
                finally:healthy.close()
                check(recovered['workerCode']==0 and recovered['jobs']['steam-570'].get('state')=='done','same accepted request completes after source recovery')
                check(recovered['entry']['assets']['static']!=entry['assets']['static'] and not recovered['entry'].get('refreshRequested') and not recovered['entry'].get('error'),'real replacement clears only completed pending intent/error')
                check(recovered['entry']['assets']['dynamic']==entry['assets']['dynamic'],'recovery does not replace a previously successful or protected video')
                check(not any(path.endswith(('.mp4','.webm','.m3u8')) for path in recovery_requests),'static recovery does not redownload completed dynamic media')
                check(recovered['state'].get('nextDueAt',0)<0 and not recovered['state'].get('activeCount') and not recovered['state'].get('workerPid'),'complete recovery returns to an idle queue without relaunch work')
            report={'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks,'elapsedMs':value['elapsedMs'],'requests':requests,'rawResult':str((args.out/mode/('result-'+mode+'.json')).resolve())};reports.append(report)
            if recovered is not None:report.update(recoveryRaw=str((args.out/mode/'result-cached-static-recover.json').resolve()),recoveryRequests=recovery_requests)
            print(json.dumps({'case':mode,'ok':report['ok'],'failed':[c['label'] for c in checks if not c['ok']]}),flush=True)
            if not report['ok']:break
            continue
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
    report={'ok':all(r['ok'] for r in reports),'checks':sum(len(r['checks']) for r in reports),'nativeChecks':sum(r.get('nativeChecks',0) for r in reports),'results':reports,'scope':'Production worker with real loopback WinHTTP and publishing; explicit AC fixture and short task/session limits for timing cases; captured Steam page replay only, no live cache/Steam writes'}
    with (args.out/'results.json').open('x',encoding='utf-8') as out:json.dump(report,out,indent=2)
    return 0 if report['ok'] else 1
if __name__=='__main__':raise SystemExit(main())
