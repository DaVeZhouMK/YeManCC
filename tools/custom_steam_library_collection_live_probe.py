"""Read-only Steam capture plus isolated cold/warm worker validation.
All worker writes go to a new Mainline Build directory. No game is executed,
no Steam account/config is changed, and production caches are never cleared.
"""
import argparse, hashlib, json, os, pathlib, subprocess, time, urllib.request, urllib.parse, shutil

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--worker',required=True)
    parser.add_argument('--baseline-worker')
    parser.add_argument('--output',required=True)
    args=parser.parse_args()
    repo=pathlib.Path(__file__).resolve().parent.parent
    build=(repo/'../../Build').resolve()
    output=pathlib.Path(args.output).resolve()
    if not output.is_relative_to(build) or output.exists():raise RuntimeError('Use a fresh Mainline Build directory')
    output.mkdir(parents=True)
    baseline=pathlib.Path(args.baseline_worker).resolve() if args.baseline_worker else None
    worker=pathlib.Path(args.worker).resolve()
    # Shared mainline build/package outputs can change during another task's
    # publish. Freeze both executable inputs before measuring any request.
    bins=output/'input-bin';bins.mkdir()
    currentCopy=bins/'current.exe';shutil.copy2(worker,currentCopy);worker=currentCopy
    if baseline:
        baselineCopy=bins/'baseline.exe';shutil.copy2(baseline,baselineCopy);baseline=baselineCopy
    report={'worker':str(worker),'workerSha256':hashlib.sha256(worker.read_bytes()).hexdigest(),
        'baselineWorker':str(baseline) if baseline else None,'baselineSha256':hashlib.sha256(baseline.read_bytes()).hexdigest() if baseline else None,'network':'real official Steam plus worker providers',
        'realSteamFilesModified':False,'realGamesExecuted':False,'realUserCacheCleared':False,'cases':[],'assets':[]}
    def call(label,exe,options,data,timeout=45):
        env=os.environ.copy();env['YEMAN_STEAM_BIG_PICTURE_DATA_ROOT']=str(data)
        start=time.monotonic()
        try:
            p=subprocess.run([str(exe),*map(str,options)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout)
            code=p.returncode;text=p.stdout.decode('utf-8','replace')
        except subprocess.TimeoutExpired as e:code='timeout';text=(e.stdout or b'').decode('utf-8','replace')
        file=output/(label+'.log');file.parent.mkdir(parents=True,exist_ok=True);file.write_text(text,encoding='utf-8')
        return {'exit':code,'seconds':round(time.monotonic()-start,3),'log':str(file)},text
    cases=[('portal','Portal',400),('portal2','Portal 2',620),('cyberpunk','Cyberpunk 2077',1091500),
        ('sgre','STEINS;GATE RE:BOOT',4012810),('eu4','Europa Universalis IV',236850),
        ('cyberpunk-cn','赛博朋克2077',1091500)]
    for key,title,expected in cases:
        r=output/key;library=r/'Library';game=library/('STEINS.GATE.RE.BOOT-P2P' if key=='sgre' else title);game.mkdir(parents=True)
        exe=game/(key+'.exe');exe.write_bytes(b'Isolated fixture. Never executed.')
        # Both revisions see the identical exe/name/config inputs. Only data
        # and cache directories differ. These are exact-match examples, not a
        # claim about overall Steam catalog recall.
        for revision,program in ([('baseline',baseline)] if baseline else [])+[('current',worker)]:
            data=r/revision/'data';(data/'config').mkdir(parents=True)
            (data/'config/library-config.json').write_text(json.dumps({'roots':[str(library)],'trainerRoots':[],'firstRunDefaultScan':{'status':'completed'}}),encoding='utf-8')
            for mode in ['cold','warm']:
                jobs=r/revision/'jobs'
                row,text=call(key+'-'+revision+'-'+mode,program,[exe,jobs,'--metadata-only','--steam-comprehensive-identity-only'],data)
                matches=list(jobs.rglob('manifest.json'))
                match=json.loads(matches[0].read_text('utf-8-sig')).get('match',{}) if matches else {}
                row.update({'case':key,'name':title,'revision':revision,'cache':mode,'expectedAppId':expected,'actualAppId':match.get('appId'),
                    'passed':row['exit']==0 and match.get('appId')==expected and match.get('steamVerificationStatus')=='steam-verified'})
                report['cases'].append(row);print(json.dumps(row,ensure_ascii=True),flush=True)
        if key=='cyberpunk-cn':continue
        data=r/'assets-data'
        row,text=call(key+'-long-search',worker,['--search-artwork',title,'--type','long','--steam-app-id',expected],data)
        marker='ARTWORK_SEARCH_JSON: '
        candidates=json.loads(text.split(marker,1)[1].splitlines()[0]).get('candidates',[]) if marker in text else []
        headers=[c for c in candidates if c.get('sourceRole') in ['library-assets-header_2x','library-assets-header','store-header']]
        asset={'case':key,'steamAppId':expected,'searchExit':row['exit'],'headers':headers,'passed':bool(headers)}
        if headers:
            image=r/('downloaded-official-long'+pathlib.Path(urllib.parse.urlsplit(headers[0]['url']).path).suffix)
            got,_=call(key+'-long-download',worker,['--download-artwork','--url',headers[0]['url'],'--type','long','--output',image],data)
            asset.update({'download':got,'downloadedFile':str(image),'downloadedBytes':image.stat().st_size if image.exists() else 0})
            asset['passed']=asset['passed'] and got['exit']==0 and image.exists()
        report['assets'].append(asset);print('ARTWORK '+json.dumps({'case':key,'passed':asset['passed'],'headers':len(headers)}),flush=True)
    report['allCurrentIdentitiesPassed']=all(c['passed'] for c in report['cases'] if c['revision']=='current')
    report['allOfficialLongDownloadsPassed']=all(c['passed'] for c in report['assets'])
    report['allPassed']=report['allCurrentIdentitiesPassed'] and report['allOfficialLongDownloadsPassed']
    (output/'summary.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    if not report['allPassed']:raise RuntimeError('See live-probe summary for actual failing cases')
if __name__=='__main__':main()
