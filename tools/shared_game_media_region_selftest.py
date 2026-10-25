"""Bounded region fallback replay. Known task binary, owned loopback, fresh output."""
import argparse,hashlib,json,pathlib,subprocess
from urllib.parse import urlparse,parse_qs
from shared_game_media_parallel_resume_fixture import Fixture,HOST,full
LANE=(pathlib.Path(__file__).resolve().parents[4]/'Build/Tasks/YMCC-Decky-SharedWallpaper-20261017/implementation/live-after-game-20261018').resolve()
CASES=['us-ready','cn-only','jp-only','all-unavailable','wrong-app','non-game','missing-key','jp-wrong-app','http429','malformed','missing-success','wrong-success-type']
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--exe',default='region-current.exe');ap.add_argument('--run',default='region-results');ap.add_argument('--case',action='append',choices=CASES);args=ap.parse_args()
 exe=(LANE/args.exe).resolve();root=(LANE/args.run).resolve()
 if exe.parent!=LANE or not exe.name.startswith('region-') or exe.suffix!='.exe' or root.parent!=LANE or root.exists():raise ValueError('fresh owned fixture only')
 root.mkdir();reports=[]
 for mode in CASES:
  if args.case and mode not in args.case:continue
  fixture=Fixture('parallel-twelve');base=fixture.server.RequestHandlerClass;calls={}
  class Handler(base):
   def dispatch(self):
    parsed=urlparse(self.path)
    if parsed.path!='/api/appdetails':return super().dispatch()
    args=parse_qs(parsed.query);app=int(args['appids'][0]);cc=args['cc'][0];calls.setdefault(app,[]).append(cc)
    if mode=='http429':return self.send_body(b'busy',429,kind='metadata')
    if mode=='malformed':return self.send_body(b'{bad',kind='metadata')
    unavailable=mode=='all-unavailable' or mode=='cn-only' and cc=='us' or mode in ['jp-only','jp-wrong-app'] and cc!='jp'
    if unavailable:return self.send_body(json.dumps({str(app):{'success':False}}).encode(),kind='metadata')
    if mode in ['us-ready','cn-only','jp-only']:return super().dispatch()
    data={'steam_appid':app+1 if mode in ['wrong-app','jp-wrong-app'] else app,'type':'advertising' if mode=='non-game' else 'game','name':'regional fixture','background_raw':HOST+f'/assets/{app}/poster.jpg','movies':[{'mp4':{'480':HOST+f'/assets/{app}/video.mp4'}}]}
    doc={str(app):{'success':'false' if mode=='wrong-success-type' else True,'data':data}}
    if mode=='missing-key':doc={str(app+100):doc[str(app)]}
    if mode=='missing-success':del doc[str(app)]['success']
    return self.send_body(json.dumps(doc).encode(),kind='metadata')
  fixture.server.RequestHandlerClass=Handler;fixture.start();target=root/mode
  try:
   proc=subprocess.run([str(exe),str(target),str(fixture.server.server_port),'parallel-twelve'],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=40)
   if proc.returncode:raise RuntimeError(proc.stderr.decode(errors='replace'))
   result=json.loads(proc.stdout)
  finally:fixture.close()
  checks=[]
  def check(v,label):checks.append({'ok':bool(v),'label':label})
  ready=mode in ['us-ready','cn-only','jp-only'];wanted=['us','cn','jp'] if mode in ['jp-only','jp-wrong-app','all-unavailable'] else ['us','cn'] if mode=='cn-only' else ['us']
  for app,e in result['entries'].items():
   check(calls.get(int(app))==wanted,app+' exact bounded region sequence')
   check(bool(e.get('dynamicReady'))==ready,app+' verified regional readiness')
   if ready and e.get('dynamicReady'):
    asset=e['assets']['dynamic'];file=target/'artwork/video'/(asset['id']+'.'+asset['extension']);check(hashlib.sha256(file.read_bytes()).digest()==hashlib.sha256(full(int(app))).digest(),app+' exact correct-game bytes')
   else:check(not e.get('staticReady'),app+' invalid identity never publishes static either')
  check(result['state'].get('workerPid',0)==0 and result['state'].get('activeCount',0)==0,'worker exits')
  check(not ready or result['maxActive']==5,'regional fallback retains five-lane pool')
  report={'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks,'regionCalls':calls};reports.append(report);print(json.dumps({'case':mode,'ok':report['ok'],'failed':[c['label'] for c in checks if not c['ok']]}),flush=True)
 report={'ok':all(r['ok'] for r in reports),'cases':len(reports),'checks':sum(len(r['checks']) for r in reports),'results':reports,'scope':'real production worker/queue; synthetic AppIDs and media, real WinHTTP loopback; no live application/cache writes'};(root/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps({k:report[k] for k in ['ok','cases','checks']}));return 0 if report['ok'] else 1
if __name__=='__main__':raise SystemExit(main())
