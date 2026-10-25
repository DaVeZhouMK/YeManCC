import argparse,copy,hashlib,json,os,pathlib,subprocess,time
from urllib.parse import urlparse
from shared_game_media_popular_games_selftest import Fixture,INIT,payload
LANE=(pathlib.Path(__file__).resolve().parents[4]/'Build/Tasks/YMCC-Decky-SharedWallpaper-20261017/implementation/catalog-order-r47').resolve()
PROFILE=LANE.parent/'popular-games-r45/replay-profile.json'
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--exe',default='catalog-current.exe');ap.add_argument('--run',default='catalog-matrix');ap.add_argument('--case',action='append',choices=['catalog-empty','catalog-locals','catalog-failure','catalog-legacy']);args=ap.parse_args()
 exe=(LANE/args.exe).resolve();root=(LANE/args.run).resolve()
 if exe.parent!=LANE or not exe.name.startswith('catalog-') or exe.suffix!='.exe' or root.parent!=LANE or root.exists():raise ValueError('fresh owned fixture only')
 root.mkdir();profile=json.loads(PROFILE.read_text(encoding='utf-8'));reports=[]
 for mode in ['catalog-empty','catalog-locals','catalog-failure','catalog-legacy']:
  if args.case and mode not in args.case:continue
  use=copy.deepcopy(profile)
  if mode=='catalog-locals':
   for app,name in [(1091500,'Cyberpunk'),(1361510,'TMNT')]:
    doc={str(app):{'success':True,'data':{'steam_appid':app,'name':name,'type':'game','movies':[]}}};use['items'].append({'appId':app,'rank':100,'sourceDocument':doc,'name':name,'lastPlayed':0})
  fixture=Fixture('normal20',use);base=fixture.server.RequestHandlerClass;catalogCalls=[]
  class Handler(base):
   def dispatch(self):
    if urlparse(self.path).path=='/ISteamChartsService/GetMostPlayedGames/v1/':
     catalogCalls.append(time.monotonic())
     if mode=='catalog-failure' and len(catalogCalls)==1:return self.send(b'busy',503,kind='catalog')
     return self.send(json.dumps({'response':{'ranks':[{'rank':i['rank'],'appid':i['appId']} for i in profile['items']]}}).encode(),kind='catalog')
    return super().dispatch()
  fixture.server.RequestHandlerClass=Handler;fixture.start();target=root/mode
  try:
   proc=subprocess.run([str(exe),str(target),str(fixture.server.server_port),mode],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=65)
   if proc.returncode:raise RuntimeError(proc.stderr.decode(errors='replace'))
   value=json.loads(proc.stdout)
  finally:fixture.close()
  checks=[]
  def check(v,label):checks.append({'ok':bool(v),'label':label})
  check(value['pausedProtected'],'game pause creates no child while retaining catalog request')
  check(value['recovered'],'resume starts discovery and completes eligible downloads')
  if mode=='catalog-legacy':
   check(not catalogCalls and value['state'].get('workerPid',0)==0,'old worker never launched merely to discover catalog')
   check(value['state'].get('catalogError')=='download-worker-upgrade-required' and value['state'].get('catalogRetryAt',0)>value['state'].get('updatedAt',0)+50000,'old component has explicit bounded upgrade retry')
   reports.append({'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks,'catalogCalls':0});print(json.dumps({'case':mode,'ok':reports[-1]['ok']}),flush=True);continue
  check(value['state'].get('catalogCount')==20,'all20 official ranks entered production queue')
  check(value['state'].get('catalogStatus')=='ready' and not value['state'].get('catalogPending'),'discovery status returns ready')
  check(value['maxActive']==5,'one real controller launches five lanes')
  check(value['state'].get('workerPid',0)==0 and value['state'].get('activeCount',0)==0,'complete/known unavailable leaves no child')
  check(len(catalogCalls)==(2 if mode=='catalog-failure' else 1),'catalog is fetched once with bounded failure retry')
  for item in profile['items']:
   app=item['appId'];job=value['jobs'].get('steam-'+str(app),{});check(job.get('sourceTier')==0 and job.get('chartRank')==item['rank'],f'{app} popular priority and rank persisted')
   entry=value['entries'].get('steam-'+str(app),{});check(bool(entry.get('dynamicReady'))==bool(item.get('segments')),f'{app} automatic catalog output matches availability')
   if entry.get('dynamicReady'):
    asset=entry['assets']['dynamic'];file=target/'artwork/video'/(asset['id']+'.'+asset['extension']);expected=INIT+b''.join(payload(app,n) for n in range(1,len(item['segments'])))
    check(hashlib.sha256(file.read_bytes()).digest()==hashlib.sha256(expected).digest(),f'{app} exact auto-discovered media hash')
  if mode=='catalog-failure':check(value['firstIdle'] and value['firstState'].get('catalogRetryAt',0)>value['firstState'].get('updatedAt',0)+50000,'first failure exits and retains minute retry; test only advances due timestamp')
  if mode=='catalog-locals':
   for app,tier in [(1091500,1),(1361510,2)]:check(value['jobs']['steam-'+str(app)].get('sourceTier')==tier,'local catalog tier preserved')
   api=[int(row['path'].split('appids=')[1].split('&')[0]) for row in fixture.logs if row['kind']=='metadata' and '/api/appdetails?' in row['path']]
   check(all(a not in [1091500,1361510] for a in api[:5]),'first download batch belongs to popular chart, ahead of local catalog')
  report={'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks,'catalogCalls':len(catalogCalls)};reports.append(report);(target/'http.json').write_text(json.dumps(fixture.logs,indent=2));print(json.dumps({'case':mode,'ok':report['ok'],'failed':[c['label'] for c in checks if not c['ok']]}),flush=True)
 result={'ok':all(r['ok'] for r in reports),'checks':sum(len(r['checks']) for r in reports),'results':reports,'scope':'actual native Controller/Supervisor/Worker/Queue, real owned subprocesses and WinHTTP; official snapshot loopback/synthetic media; no live cache or Steam mutation; catalog failure due advanced only in test'};(root/'results.json').write_text(json.dumps(result,indent=2));print(json.dumps({'ok':result['ok'],'checks':result['checks']}));return 0 if result['ok'] else 1
if __name__=='__main__':raise SystemExit(main())
