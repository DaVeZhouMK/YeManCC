"""Official concurrent Top100: fixed inventory, ABC inputs, isolated real worker runs.
Expected IDs are evaluation-only: never supplied to the resolver or its caches.
"""
import argparse, concurrent.futures, datetime, hashlib, json, os, pathlib, re, shutil, subprocess, time, urllib.parse, urllib.request

def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp=path.with_suffix(path.suffix+'.tmp')
    temp.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')
    temp.replace(path)

def read(path): return json.loads(path.read_text('utf-8-sig'))
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def get(url):
    for attempt in range(3):
        try:
            with urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'YMCC-CustomLibrary-validation/1.0'}),timeout=30) as r:
                return json.loads(r.read())
        except Exception:
            if attempt==2: raise
            time.sleep(2*(attempt+1))

def prepare(root):
    rankpath=root/'GetGamesByConcurrentPlayers.json'
    if not rankpath.exists():save(rankpath,get('https://api.steampowered.com/ISteamChartsService/GetGamesByConcurrentPlayers/v1/'))
    ranking=read(rankpath)['response']; rows=ranking['ranks'][:100]
    assert len(rows)==100 and len({r['appid'] for r in rows})==100
    localized={}
    for language in ('english','schinese','japanese'):
        localized[language]={}
        for start in range(0,100,20):
            p=root/'capture'/f'store-items-{language}-{start:03}.json'
            if not p.exists():
                request={'ids':[{'appid':r['appid']} for r in rows[start:start+20]],'context':{'country_code':'US','language':language},'data_request':{'include_assets':True,'include_basic_info':True}}
                save(p,get('https://api.steampowered.com/IStoreBrowseService/GetItems/v1/?input_json='+urllib.parse.quote(json.dumps(request,separators=(',',':')))))
                time.sleep(.25)
            for item in read(p).get('response',{}).get('store_items',[]): localized[language][item.get('appid',item.get('id'))]=item
            print(f'inventory {language} {start+20}/100',flush=True)
    aliases={730:'CS2',570:'DOTA2',578080:'PUBG',1172470:'APEX',252490:'Rust',4000:'GMod',271590:'GTA V',2715900:'GTA V',550:'L4D2',1086940:'BG3',1174180:'RDR2',1245620:'ELDEN RING',1091500:'赛博朋克2077',105600:'Terraria',236850:'EU4',108600:'PZ',431960:'Wallpaper Engine',440:'TF2',413150:'星露谷物语',892970:'Valheim',2399830:'ARK',1623730:'幻兽帕鲁'}
    inventory=[]; cases=[]
    def safe(title):return re.sub(r'[<>:"/\\|?*\x00-\x1f]',' ',title).strip(' .') or 'Unknown'
    for row in rows:
        app=row['appid']; en=localized['english'].get(app,{})
        name=en.get('name')
        if not name: raise RuntimeError(f'Official title unavailable for ranked app {app}: keep evidence and resolve inventory before benchmarking')
        names={lang:localized[lang].get(app,{}).get('name',name) for lang in localized}
        game=en.get('type')==0 and app!=228980
        r=dict(row,name=name,storeType=en.get('type'),expectedGame=game,names=names,assets=en.get('assets',{}))
        inventory.append(r)
        exact=safe(name)
        variants=[exact.replace(' ','.')+'-P2P',exact+' [FitGirl Repack]',exact+'-Goldberg',exact.replace(' ','_')+'-CODEX',exact+' v1.2.3-GOG']
        local=names['schinese'] if names['schinese']!=name else names['japanese']
        c=aliases.get(app,local if local!=name else ''.join(w[0] for w in re.findall(r'[A-Za-z0-9]+',name)))
        if not c or len(c)<2:c=name.lower().replace(' ','')
        for group,title,nested,kind in [('A',exact,False,'exact-official-title'),('B',variants[(row['rank']-1)%len(variants)],row['rank']%2==0,'installation-release-directory'),('C',safe(c),row['rank']%3==0,'localized-or-ambiguous-alias')]:
            cases.append({'key':f'{row["rank"]:03}-{app}-{group}','group':group,'rank':row['rank'],'expectedAppId':app,'expectedGame':game,'officialName':name,'inputDirectory':title,'nestedExecutable':nested,'kind':kind,'aliasEvidence':names if group=='C' else None})
    meta={'definition':'Steam ISteamChartsService.GetGamesByConcurrentPlayers first 100 unique entries; software retained and evaluated as expected abstention','capturedAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),'rankingLastUpdate':ranking.get('last_update'),'rankingSha256':digest(rankpath),'inventory':inventory,'captures':[{'path':str(p),'sha256':digest(p)} for p in sorted((root/'capture').glob('*.json'))]}
    save(root/'inventory.json',meta); save(root/'cases.json',cases)
    print('PREPARED',len(inventory),'ranked entries',len(cases),'ABC inputs',flush=True)

def run(root,worker,revision,workers,timeout,groups,seed_revision=None):
    cases=[c for c in read(root/'cases.json') if c['group'] in groups]
    out=root/revision;out.mkdir(parents=True,exist_ok=True)
    frozen=out/'frozen-worker.exe'
    if not frozen.exists():shutil.copy2(worker,frozen)
    expectedhash=digest(frozen)
    def one(case):
        p=out/case['key']; result=p/'result.json'
        if result.exists():return read(result)
        library=p/'Library';game=library/case['inputDirectory']; exe=game/('bin/Win64/Game-Win64-Shipping.exe' if case['nestedExecutable'] else 'Game.exe')
        exe.parent.mkdir(parents=True,exist_ok=True);exe.write_bytes(b'YMCC isolated fixture, never executed')
        data=p/'data';save(data/'config/library-config.json',{'roots':[str(library)],'trainerRoots':[],'firstRunDefaultScan':{'status':'completed'}})
        if seed_revision:
            # Reuse only actual URL-keyed candidate/AppDetails captures. The
            # expected answer and identity manifests never enter resolver input.
            seed=root/seed_revision/case['key']/'data/cache'
            for name in ['steam-search','steam-appdetails']:
                if (seed/name).is_dir():shutil.copytree(seed/name,data/'cache'/name,dirs_exist_ok=True)
        env=os.environ.copy();env['YEMAN_STEAM_BIG_PICTURE_DATA_ROOT']=str(data)
        start=time.monotonic()
        try:
            proc=subprocess.run([str(frozen),str(exe),str(p/'jobs'),'--metadata-only','--steam-comprehensive-identity-only'],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout)
            code=proc.returncode;log=proc.stdout
        except subprocess.TimeoutExpired as error:code='timeout';log=error.stdout or b''
        (p/'worker.log').write_bytes(log)
        manifests=list((p/'jobs').rglob('manifest.json'))
        m=read(manifests[0]) if manifests else {};match=m.get('match') or {}
        ids=set();attempts=[]
        def walk(x):
            if isinstance(x,dict):
                for k,v in x.items():
                    if k in ('appId','storefrontAppId') and isinstance(v,int) and v>0:ids.add(v)
                    if k=='networkAttempts' and isinstance(v,list):attempts.extend(v)
                    walk(v)
            elif isinstance(x,list):
                for v in x:walk(v)
        for path in (p/'jobs').rglob('*.json'):
            try:walk(read(path))
            except Exception:pass
        accepted=match.get('steamVerificationStatus')=='steam-verified' and match.get('identityStatus')=='verified' and isinstance(match.get('appId'),int) and match['appId']>0 and code==0
        actual=match.get('appId') if accepted else None
        wrong=accepted and (not case['expectedGame'] or actual!=case['expectedAppId'])
        r=dict(case,revision=revision,workerSha256=expectedhash,seconds=round(time.monotonic()-start,3),exit=code,actualAppId=actual,match=match,correct=accepted and actual==case['expectedAppId'] and case['expectedGame'],wrong=wrong,safeAbstention=not accepted,candidateRecall=case['expectedAppId'] in ids,networkAttempts=len(attempts),rateLimited=any(a.get('status')==429 or a.get('httpStatus')==429 for a in attempts),manifestCount=len(manifests))
        save(result,r)
        print(json.dumps({k:r[k] for k in ('key','seconds','actualAppId','correct','wrong','safeAbstention','exit')},ensure_ascii=True),flush=True)
        return r
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        results=list(pool.map(one,cases))
    summaries={}
    for g in 'ABC':
        values=[r for r in results if r['group']==g]
        if not values:continue
        seconds=sorted(r['seconds'] for r in values)
        summaries[g]={'cases':len(values),'correct':sum(r['correct'] for r in values),'wrong':sum(r['wrong'] for r in values),'safeAbstention':sum(r['safeAbstention'] for r in values),'candidateRecall':sum(r['candidateRecall'] for r in values),'timeouts':sum(r['exit']=='timeout' for r in values),'secondsMedian':seconds[len(seconds)//2],'secondsP95':seconds[min(len(seconds)-1,int(len(seconds)*.95))]}
    save(out/'summary.json',{'revision':revision,'workerSha256':expectedhash,'scope':'production worker; actual captured URL cache with real network on misses' if seed_revision else 'real-network production worker; synthetic filesystem inputs only; all data isolated; expected IDs never supplied as input','cacheSeedRevision':seed_revision,'groups':summaries,'cases':results})
    print(json.dumps(summaries,ensure_ascii=True),flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--root',required=True);p.add_argument('--prepare',action='store_true');p.add_argument('--worker');p.add_argument('--revision',default='baseline');p.add_argument('--workers',type=int,default=2);p.add_argument('--timeout',type=int,default=28);p.add_argument('--groups',default='ABC');p.add_argument('--seed-cache-revision');a=p.parse_args()
    root=pathlib.Path(a.root).resolve();build=(pathlib.Path(__file__).resolve().parents[1]/'../../Build/CustomSteamLibrary').resolve()
    if not root.is_relative_to(build):raise ValueError('Evidence must stay under isolated Mainline Build/CustomSteamLibrary')
    root.mkdir(parents=True,exist_ok=True)
    if a.prepare:prepare(root)
    if a.worker:run(root,pathlib.Path(a.worker).resolve(),a.revision,a.workers,a.timeout,a.groups,a.seed_cache_revision)
