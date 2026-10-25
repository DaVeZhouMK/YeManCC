"""Selected wallpaper reverse cases through production Queue/Worker/WinHTTP."""
import argparse,copy,json,os,pathlib,subprocess
from shared_game_media_popular_games_selftest import Fixture
TASK=(pathlib.Path(__file__).resolve().parents[4]/'Build/Tasks/YMCC-Decky-SharedWallpaper-20261017').resolve()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--exe',type=pathlib.Path,default=TASK/'implementation/reverse-chain-r65/worker.exe');ap.add_argument('--output',type=pathlib.Path,default=TASK/'implementation/reverse-chain-r65/selection-replay');ap.add_argument('--case',action='append');args=ap.parse_args()
 root=args.output.resolve();exe=args.exe.resolve()
 if TASK not in root.parents or root.exists() or not exe.is_file():raise ValueError('owned fresh evidence and compiled fixture required')
 root.mkdir(parents=True);profile0=json.loads((TASK/'implementation/popular-games-r45/replay-profile.json').read_text(encoding='utf-8'));reports=[]
 for mode in (args.case or ['selected-second','expired-choice','new-choice-after-queue','static-only-expired-choice','encoded-official-choice']):
  profile=copy.deepcopy(profile0);profile['items']=profile['items'][:1];item=profile['items'][0];app=item['appId'];alternate=f'https://video.steamstatic.com/store_trailers/{app}/selected/master.m3u8';profile['routes'][alternate]=copy.deepcopy(profile['routes'][item['master']]);profile['routes'][alternate]['text']='#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=6000000,RESOLUTION=1920x1080\n'+item['variant']+'\n';data=item['sourceDocument'][str(app)]['data'];data['movies']=[{'name':'Trailer A','hls_h264':item['master']},{'name':'Trailer B','hls_h264':alternate}]
  item['selectedVideo']={'appId':app,'url':alternate if mode=='selected-second' else item['master'] if mode=='new-choice-after-queue' else alternate+'?expired'}
  if mode=='encoded-official-choice':
   canonical=alternate+'?a=1&b=2';profile['routes'][canonical]=copy.deepcopy(profile['routes'][alternate]);data['movies'][1]['hls_h264']=canonical.replace('&','&amp;');item['selectedVideo']={'appId':app,'url':data['movies'][1]['hls_h264']}
  if mode=='new-choice-after-queue':item['selectionAfterQueue']={'appId':app,'url':alternate}
  if mode=='static-only-expired-choice':item['staticOnly']=True
  source=root/(mode+'.json');source.write_text(json.dumps(profile),encoding='utf-8');fixture=Fixture('normal20',profile);fixture.start();env=os.environ.copy();env['YMCC_POPULAR_TEST_PROFILE']=str(source)
  try:
   result=subprocess.run([str(exe),str(root/mode),str(fixture.server.server_port),'normal20'],env=env,capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=40)
   if result.returncode:raise RuntimeError(result.stderr.decode('utf-8',errors='replace'))
   value=json.loads(result.stdout)
  finally:fixture.close()
  checks=[]
  def check(v,label):checks.append({'ok':bool(v),'label':label})
  entry=value['entries'][str(app)];paths=[r['path'] for r in fixture.logs]
  if mode in ('selected-second','new-choice-after-queue','encoded-official-choice'):
   check(entry.get('dynamicReady'),'chosen trailer reaches published cache');check(any('/selected/master.m3u8' in p for p in paths),'download requests current persisted chosen trailer');check(not any(p==item['master'].split('.com',1)[-1] for p in paths),'stale queued/default trailer is never requested')
  elif mode=='static-only-expired-choice':
   check(entry.get('staticReady'),'expired video choice cannot block static-only completion');check(entry.get('dynamicReady'),'existing dynamic asset remains available');check(not any('/store_trailers/' in p for p in paths),'static-only work never downloads a video');check(entry.get('selectedVideo',{}).get('url')==item['selectedVideo']['url'],'static-only work preserves the independent video preference')
  else:
   check(not entry.get('dynamicReady'),'expired selection never downloads another trailer silently');check(value['jobs']['steam-'+str(app)].get('lastError')=='media-video-selection-expired','expired candidate gets explicit retry reason')
  check(value['state'].get('workerPid',0)==0,'all completion/rejection paths release worker ownership');reports.append({'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks});(root/mode/'http.json').write_text(json.dumps(fixture.logs,indent=2),encoding='utf-8')
 report={'ok':all(r['ok'] for r in reports),'checks':sum(len(r['checks']) for r in reports),'results':reports,'scope':'production Store/Queue/Worker/WinHTTP publish; durable choice, stale queued choice and independent static-only work; owned loopback only'};(root/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report));return 0 if report['ok'] else 1
if __name__=='__main__':raise SystemExit(main())
