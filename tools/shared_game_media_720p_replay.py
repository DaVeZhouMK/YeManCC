import copy,hashlib,json,os,pathlib,re,subprocess,sys
from urllib.parse import urljoin
lane=pathlib.Path(sys.argv[1]).resolve() if len(sys.argv)>1 else None
if lane is None:raise ValueError('existing evidence directory required')
root=pathlib.Path('G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC');sys.path.insert(0,str(root/'tools'))
from shared_game_media_popular_games_selftest import Fixture,INIT,payload
profilePath=lane.parent/'popular-games-r45/replay-profile.json'
original=json.loads(profilePath.read_text(encoding='utf-8'))
source=next(x for x in original['items'] if x.get('segments') and x['sourceDocument'][str(x['appId'])]['data'].get('type')=='game')
run=lane/('quality-replay-'+os.urandom(4).hex());run.mkdir();reports=[]
for mode in ['automatic720','missing720','manual1080','size480','direct720','direct-fallback480']:
 profile=copy.deepcopy(original);item=copy.deepcopy(source);profile['items']=[item];app=item['appId'];base='https://video.steamstatic.com/store_trailers/'+str(app)+'/'
 if mode.startswith('direct'):
  item['sourceDocument'][str(app)]['data']['movies']=[{'mp4':{'720':base+'q720.mp4','480':base+'q480.mp4','max':base+'q1080.mp4'}}]
 else:
  rows=[(2160,20000000),(1080,6000000),(720,2000000),(480,500000)]
  if mode=='missing720':rows=[(1080,6000000),(540,2000000),(480,500000)]
  if mode=='size480':rows=[(1080,12000000),(720,5800000),(480,1400000)]
  lines=['#EXTM3U']
  for height,bandwidth in rows:
   url=urljoin(item['variant'],f'q{height}.m3u8');leaf=item['playlist']
   if mode=='size480':
    leaf=re.sub(r'#EXTINF:[^\n]+','#EXTINF:1,',leaf);leaf=leaf.replace('#EXTINF:1,','#EXTINF:660,',1)
   profile['routes'][url]={'app':app,'kind':'playlist','text':leaf}
   lines.extend([f'#EXT-X-STREAM-INF:BANDWIDTH={bandwidth},RESOLUTION=1920x{height}',url])
  profile['routes'][item['master']]['text']='\n'.join(lines)+'\n'
  if mode=='manual1080':item['selectedVideo']={'appId':app,'url':item['master'],'preferredHeight':1080}
 file=run/(mode+'-profile.json');file.write_text(json.dumps(profile),encoding='utf-8');fixture=Fixture('normal20',profile);Base=fixture.server.RequestHandlerClass
 class Handler(Base):
  def dispatch(self):
   if self.path.endswith(('/q720.mp4','/q480.mp4','/q1080.mp4')):
    if mode=='direct-fallback480' and self.path.endswith('/q720.mp4'):return self.send(b'unavailable',503,kind='direct',app=app)
    return self.send(INIT+payload(app,1),kind='direct',app=app)
   return super().dispatch()
 fixture.server.RequestHandlerClass=Handler;fixture.start();env=os.environ.copy();env['YMCC_POPULAR_TEST_PROFILE']=str(file)
 try:
  result=subprocess.run([str(lane/'quality-worker.exe'),str(run/mode),str(fixture.server.server_port),'normal20'],env=env,capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=45)
  if result.returncode:raise RuntimeError(result.stderr.decode(errors='replace'))
  value=json.loads(result.stdout)
 finally:fixture.close()
 checks=[]
 def check(ok,label):checks.append({'ok':bool(ok),'label':label})
 entry=value['entries'][str(app)];asset=entry.get('assets',{}).get('dynamic',{});paths=[row['path'] for row in fixture.logs]
 check(entry.get('dynamicReady'),'usable video published');check(asset.get('qualityPolicy')==3,'new policy is durable');check(asset.get('targetHeight')==(1080 if mode=='manual1080' else 720),'explicit manual height or automatic default survives Store lease')
 if not mode.startswith('direct'):
  height=1080 if mode=='manual1080' else 480 if mode in ['missing720','size480'] else 720
  check(asset.get('selectedHeight')==height,'published selected quality matches requested/fallback');check(any(f'q{height}.m3u8' in path for path in paths),'actual WinHTTP requests the correct variant');check(not any('q2160.m3u8' in path for path in paths),'4K variant is not requested')
  if mode=='size480':check(any('q720.m3u8' in path for path in paths),'size fallback first assesses preferred 720p')
 else:
  direct=[p for p in paths if re.search(r'/q(?:720|480|1080)\.mp4',p)]
  check(direct and direct[0].endswith('/q720.mp4'),'720p direct source is attempted first');check(any(p.endswith('/q480.mp4') for p in direct)==(mode=='direct-fallback480'),'480p direct source used only when preferred fails');check(not any(p.endswith('/q1080.mp4') for p in direct),'automatic direct route does not jump to available 1080p')
 check(value['state'].get('activeCount',0)==0 and value['state'].get('workerPid',0)==0,'completed job retains no active worker/lane')
 row={'mode':mode,'ok':all(c['ok'] for c in checks),'checks':checks};reports.append(row);(run/(mode+'-http.json')).write_text(json.dumps(fixture.logs,indent=2),encoding='utf-8');print(json.dumps({'mode':mode,'ok':row['ok'],'failed':[c['label'] for c in checks if not c['ok']]}),flush=True)
summary={'ok':all(row['ok'] for row in reports),'cases':len(reports),'checks':sum(len(row['checks']) for row in reports),'reports':reports,'run':str(run),'scope':'actual production Worker, real WinHTTP over owned loopback, captured identity metadata and synthetic media, no live user data/settings writes'};(lane/'quality-replay-results.json').write_text(json.dumps(summary,indent=2),encoding='utf-8');sys.exit(0 if summary['ok'] else 1)
