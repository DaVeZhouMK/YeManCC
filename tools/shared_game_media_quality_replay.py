"""Production worker quality replay using captured metadata and owned loopback."""
import argparse,copy,hashlib,json,os,pathlib,subprocess
from urllib.parse import urljoin,urlparse
from shared_game_media_popular_games_selftest import Fixture,INIT,payload,pathkey
LANE=(pathlib.Path(__file__).resolve().parents[4]/'Build/Tasks/YMCC-Decky-SharedWallpaper-20261017/implementation/quality-r59').resolve()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--run',default='quality-replay');args=ap.parse_args()
 exe=LANE/'quality-worker.exe';out=(LANE/args.run).resolve()
 if out.parent!=LANE or not out.name.startswith('quality-replay'):raise ValueError('owned replay directory only')
 if not exe.is_file() or out.exists():raise ValueError('compiled owned worker and fresh output required')
 out.mkdir();original=json.loads((LANE.parent/'popular-games-r45/replay-profile.json').read_text(encoding='utf-8'));reports=[]
 for mode in ['hls-full-hd','max-direct','max-failure-fallback']:
  profile=copy.deepcopy(original)
  if mode=='hls-full-hd':
   for item in profile['items']:
    if not item.get('segments'):continue
    app=item['appId'];movies=item['sourceDocument'][str(app)]['data'].setdefault('movies',[]);movies.insert(0,{'mp4':{'480':f'https://video.steamstatic.com/store_trailers/{app}/movie480.mp4'}})
    lines=['#EXTM3U']
    for height,bandwidth in [(480,500000),(2160,20000000),(720,2000000),(1080,6000000)]:
     url=urljoin(item['variant'],f'quality-{height}.m3u8');profile['routes'][url]={'app':item['appId'],'kind':'playlist','text':item['playlist']};lines.extend([f'#EXT-X-STREAM-INF:BANDWIDTH={bandwidth},RESOLUTION=1920x{height}',url])
    profile['routes'][item['master']]['text']='\n'.join(lines)+'\n'
  else:
   profile['items']=profile['items'][:1];item=profile['items'][0];app=item['appId'];base=f'https://video.steamstatic.com/store_trailers/{app}/';item['sourceDocument'][str(app)]['data']['movies']=[{'mp4':{'480':base+'movie480.mp4','max':base+'movie_max.mp4'}}]
  profileFile=out/(mode+'-profile.json');profileFile.write_text(json.dumps(profile),encoding='utf-8');fixture=Fixture('normal20',profile);base=fixture.server.RequestHandlerClass
  class Handler(base):
   def dispatch(self):
    if self.path.endswith('/movie_max.mp4') or self.path.endswith('/movie480.mp4'):
     app=profile['items'][0]['appId'];kind='max-direct' if self.path.endswith('/movie_max.mp4') else 'low-fallback'
     if mode=='max-failure-fallback' and kind=='max-direct':return self.send(b'temporary failure',503,kind=kind,app=app)
     return self.send(INIT+payload(app,1),kind=kind,app=app)
    return super().dispatch()
  fixture.server.RequestHandlerClass=Handler;fixture.start();env=os.environ.copy();env['YMCC_POPULAR_TEST_PROFILE']=str(profileFile)
  try:
   process=subprocess.run([str(exe),str(out/mode),str(fixture.server.server_port),'normal20'],env=env,capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=60)
   if process.returncode:raise RuntimeError(process.stderr.decode(errors='replace'))
   value=json.loads(process.stdout)
  finally:fixture.close()
  checks=[]
  def check(ok,label):checks.append({'ok':bool(ok),'label':label})
  if mode=='hls-full-hd':
   for item in profile['items']:
    app=item['appId'];entry=value['entries'][str(app)];eligible=item.get('segments') and item['sourceDocument'][str(app)]['data'].get('type')=='game'
    if not eligible:continue
    check(entry.get('dynamicReady'),f'{app} full-HD asset published')
    asset=entry.get('assets',{}).get('dynamic',{});check(asset.get('qualityPolicy')==2 and asset.get('selectedHeight')==1080,f'{app} selected playlist resolution recorded')
    if entry.get('dynamicReady'):
     file=out/mode/'artwork/video'/(asset['id']+'.'+asset['extension']);expected=INIT+b''.join(payload(app,n) for n in range(1,len(item['segments'])));check(hashlib.sha256(file.read_bytes()).digest()==hashlib.sha256(expected).digest(),f'{app} exact media output retained')
   paths=[r['path'] for r in fixture.logs];check(any('quality-1080.m3u8' in p for p in paths),'worker actually requests full-HD variant');check(not any('quality-480.m3u8' in p or 'quality-2160.m3u8' in p or p.endswith('/movie480.mp4') for p in paths),'global source order avoids an earlier low-only trailer and the 4K alternatives')
  else:
   app=profile['items'][0]['appId'];entry=value['entries'][str(app)];check(entry.get('dynamicReady'),'real worker publishes preferred/fallback direct file');check(entry.get('assets',{}).get('dynamic',{}).get('qualityPolicy')==2,'successful selection is durable and will not loop upgrades');requests=[r['kind'] for r in fixture.logs if r['kind'] in ['max-direct','low-fallback']];check(requests and requests[0]=='max-direct','max MP4 is first direct request');check(('low-fallback' in requests)==(mode=='max-failure-fallback'),'480p used only after high-quality source fails')
  check(value['state'].get('workerPid',0)==0 and value['state'].get('activeCount',0)==0,'finished worker leaves no active process/lane');report={'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks};reports.append(report);(out/mode/'http.json').write_text(json.dumps(fixture.logs,indent=2));print(json.dumps({'case':mode,'ok':report['ok'],'failed':[c['label'] for c in checks if not c['ok']]}),flush=True)
 result={'ok':all(r['ok'] for r in reports),'checks':sum(len(r['checks']) for r in reports),'results':reports,'scope':'real production Worker/WinHTTP/publish; captured Steam metadata plus synthetic loopback variants/video; no live cache/Steam changes'};(out/'results.json').write_text(json.dumps(result,indent=2));print(json.dumps({'ok':result['ok'],'checks':result['checks']}));return 0 if result['ok'] else 1
if __name__=='__main__':raise SystemExit(main())
