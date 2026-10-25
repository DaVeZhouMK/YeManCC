"""Read-only real local EXEs; all scans, resolver output and caches stay in Build.
No expected AppID is ever passed to the worker. Games and Steam are never started.
"""
import argparse,concurrent.futures,datetime,hashlib,json,os,pathlib,shutil,subprocess,time,urllib.request
REPO=pathlib.Path(__file__).resolve().parents[1]
BUILD=(REPO/'../../Build/CustomSteamLibrary').resolve()
PRODUCTION=pathlib.Path('D:/YeMan/CustomSteamLibrary/data')
def load(p):return json.loads(p.read_text('utf-8-sig'))
def save(p,m):
 p.parent.mkdir(parents=True,exist_ok=True);t=p.with_suffix(p.suffix+'.tmp');t.write_text(json.dumps(m,ensure_ascii=False,indent=2),encoding='utf-8');t.replace(p)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run_command(worker,args,data,log,timeout=90):
 env=os.environ.copy();env['YEMAN_STEAM_BIG_PICTURE_DATA_ROOT']=str(data);start=time.monotonic()
 try:
  p=subprocess.run([str(worker),*map(str,args)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout);code=p.returncode;out=p.stdout
 except subprocess.TimeoutExpired as e:code='timeout';out=e.stdout or b''
 log.parent.mkdir(parents=True,exist_ok=True);log.write_bytes(out)
 return {'exit':code,'seconds':round(time.monotonic()-start,3),'log':str(log)}
def prepare(root,worker):
 assert not(root/'source.json').exists(),'Use a fresh evidence directory'
 (root/'bin').mkdir(parents=True,exist_ok=True);frozen=root/'bin/baseline-worker.exe';shutil.copy2(worker,frozen)
 files={};sources=[]
 for name in ['config/library-config.json','config/manual-overrides.json','state/library-scan.json','state/steam-add-plan.json']:
  p=PRODUCTION/name
  if p.exists():
   dest=root/'production-snapshot'/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest);files[name]=sha(p)
 for name in ['identity','steam-appdetails','steam-search']:
  p=PRODUCTION/'cache'/name
  if p.exists():shutil.copytree(p,root/'production-snapshot/cache'/name)
 save(root/'source.json',{'capturedAt':datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),'worker':str(frozen),'workerSha256':sha(frozen),'productionInputs':files,'scope':'configured D:/Game and G:/GAME plus all collection children; full actual PE metadata and local INI/AppID discovery; expected Steam IDs never injected'})
 config=load(root/'production-snapshot/config/library-config.json');data=root/'scan-data';save(data/'config/library-config.json',config)
 options=['--scan-library',root/'scan-jobs']
 for path in config.get('roots',[]):options+=['--root',path]
 row=run_command(frozen,options,data,root/'scan.log',180);save(root/'scan-run.json',row)
 scan=load(data/'state/library-scan.json');old=load(root/'production-snapshot/state/library-scan.json')
 subjects={}
 def add(g,origin):
  exe=g.get('primaryExecutable');
  if not exe or not pathlib.Path(exe).is_file():return
  key=str(pathlib.Path(exe)).replace('/','\\').lower()
  if key not in subjects:subjects[key]={'executable':exe,'gameDirectory':g.get('gameDirectory'),'directoryName':g.get('directoryName'),'primaryProductName':g.get('primaryProductName'),'nativeSteamAppId':g.get('nativeSteamAppId'),'origins':[]}
  subjects[key]['origins'].append(origin)
 for g in old.get('games',[]):add(g,'persisted-library')
 for g in scan.get('games',[]):add(g,'fresh-full-scan')
 # Audit every child of a collection even if the scanner collapsed it into one
 # card; its other games cannot disappear from the coverage denominator.
 collections=[]
 for g in old.get('games',[])+scan.get('games',[]):
  for c in g.get('candidates',[]):
   exe=c.get('path');
   if exe and c.get('role')=='main-game':
    local=pathlib.Path(exe).parent
    # This captures candidate product installations in collection directories.
    # Other launcher alternatives stay evidence, not separate game identities.
    if local.parent==pathlib.Path(g.get('gameDirectory','')) and local!=pathlib.Path(g.get('gameDirectory','')):
     collections.append(local)
 # Include the known real collection's immediate game folders independently.
 container=pathlib.Path('G:/GAME/BenBenGame')
 if container.is_dir():collections.extend(p for p in container.iterdir() if p.is_dir())
 seen=set()
 for directory in collections:
  key=str(directory).lower()
  if key in seen:continue
  seen.add(key)
  childdata=root/'child-scans'/str(len(seen))/'data';childjob=childdata.parent/'jobs'
  # Scan parent as a root so this one directory is enumerated normally; use
  # explicit manual directory registration for deeply nested collection leaves.
  save(childdata/'config/library-config.json',{'roots':[str(directory)],'trainerRoots':[],'manualGameDirectories':[{'path':str(directory),'source':'user','locked':True,'bypassAutomaticPlatformExclusion':True}],'firstRunDefaultScan':{'status':'completed'}})
  result=run_command(frozen,['--scan-library',childjob,'--root',directory],childdata,childdata.parent/'scan.log',90)
  output=childdata/'state/library-scan.json'
  if output.exists():
   for g in load(output).get('games',[]):add(g,'independent-collection-child-scan')
  else:sources.append({'directory':str(directory),'scan':result})
 rows=list(subjects.values());rows.sort(key=lambda r:r['executable'].lower())
 for i,r in enumerate(rows):r['key']=f'{i+1:03}-'+hashlib.sha256(r['executable'].lower().encode()).hexdigest()[:8]
 save(root/'subjects.json',rows);save(root/'coverage.json',{'originalGames':len(old.get('games',[])),'freshGames':len(scan.get('games',[])),'subjectCount':len(rows),'collectionChildrenChecked':len(seen),'childScanFailures':sources,'freshCollectionContainers':scan.get('collectionContainers',[])})
 print('PREPARED',len(rows),'actual EXEs; fresh scan',len(scan.get('games',[])),flush=True)
def rescan(root,worker,revision):
 out=root/revision;out.mkdir(parents=True,exist_ok=True);data=out/'full-scan-data'
 config=load(root/'production-snapshot/config/library-config.json');save(data/'config/library-config.json',config)
 save(data/'state/library-scan.json',load(root/'production-snapshot/state/library-scan.json'))
 args=['--scan-library',out/'scan-jobs']
 for path in config.get('roots',[]):args+=['--root',path]
 result=run_command(worker,args,data,out/'full-scan.log',180)
 state=load(data/'state/library-scan.json');save(out/'full-scan-summary.json',dict(result,gameCount=len(state.get('games',[])),collectionContainers=state.get('collectionContainers'),games=state.get('games',[]),skippedDirectories=state.get('skippedDirectories',[])))
 print('FULL_SCAN',revision,len(state.get('games',[])),'games',state.get('collectionContainers'),flush=True)
def probe(root,revision,worker,mode,workers,seed_from=None):
 subjects=load(root/'subjects.json');out=root/revision;out.mkdir(exist_ok=True);frozen=out/'frozen-worker.exe'
 if not frozen.exists():shutil.copy2(worker,frozen)
 programsha=sha(frozen)
 def one(subject):
  dest=out/mode/subject['key'];result=dest/'result.json'
  if result.exists():return load(result)
  data=dest/'data';data.mkdir(parents=True,exist_ok=True)
  config=load(root/'production-snapshot/config/library-config.json');save(data/'config/library-config.json',config)
  # A warm run reproduces durable identity caches and manual choices; cold
  # recognition gets the same actual local executable and Steam metadata only.
  if mode=='warm':
   for folder in ['config','state','cache']:
    p=root/'production-snapshot'/folder
    if p.exists():shutil.copytree(p,data/folder,dirs_exist_ok=True)
  if seed_from:
   # Replay exact URL/body-keyed provider responses without changing their ages;
   # no expected AppID or identity manifest is supplied to recognition.
   seed=root/seed_from/mode/subject['key']/'data/cache'
   for name in ['steam-appdetails','steam-search','igdb-responses']:
    if (seed/name).is_dir():shutil.copytree(seed/name,data/'cache'/name,dirs_exist_ok=True)
  row=run_command(frozen,[subject['executable'],dest/'jobs','--metadata-only','--steam-comprehensive-identity-only'],data,dest/'worker.log',55)
  manifests=list((dest/'jobs').rglob('manifest.json'));failures=list((dest/'jobs').rglob('rounds-failed.json'));m=load(manifests[0]) if manifests else load(failures[0]) if failures else {}
  match=m.get('match') or {};local=m.get('localSteamIdentity') or {};official=local.get('officialVerification') or {}
  r=dict(subject,revision=revision,mode=mode,workerSha256=programsha,**row,match=match,localSteamIdentity=local,failure=m.get('failure'),steamAccepted=row['exit']==0 and match.get('identityStatus')=='verified' and match.get('steamVerificationStatus')=='steam-verified',officialLocalAppId=local.get('candidateAppId'),localIdRejected=official.get('accepted') is False)
  save(result,r);print(json.dumps({'key':r['key'],'exe':r['executable'],'mode':mode,'exit':r['exit'],'appId':match.get('appId'),'name':match.get('formalName'),'localAppId':r['officialLocalAppId'],'localRejected':r['localIdRejected']},ensure_ascii=True),flush=True);return r
 with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:results=list(pool.map(one,subjects))
 summary={'revision':revision,'mode':mode,'cacheSeedFrom':seed_from,'workerSha256':programsha,'count':len(results),'accepted':sum(r['steamAccepted'] for r in results),'localIdRejected':sum(r['localIdRejected'] for r in results),'timeouts':sum(r['exit']=='timeout' for r in results),'cases':results}
 save(out/(mode+'-summary.json'),summary);print('COMPLETED',revision,mode,summary['count'],summary['accepted'],'Steam verified',flush=True)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',required=True);p.add_argument('--worker',required=True);p.add_argument('--prepare',action='store_true');p.add_argument('--probe',action='store_true');p.add_argument('--rescan',action='store_true');p.add_argument('--revision',default='baseline');p.add_argument('--mode',choices=['cold','warm'],default='cold');p.add_argument('--workers',type=int,default=2);p.add_argument('--seed-from');a=p.parse_args();root=pathlib.Path(a.root).resolve();assert root.is_relative_to(BUILD),'Write only below isolated Build';root.mkdir(parents=True,exist_ok=True)
 if a.prepare:prepare(root,pathlib.Path(a.worker))
 if a.rescan:rescan(root,pathlib.Path(a.worker),a.revision)
 if a.probe:probe(root,a.revision,pathlib.Path(a.worker),a.mode,a.workers,a.seed_from)
