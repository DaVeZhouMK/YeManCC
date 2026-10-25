"""Official top20 metadata/playlist replay; bounded loopback and known acceptance binary only."""
import argparse,copy,hashlib,json,os,pathlib,re,select,socket,struct,subprocess,threading,time
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from urllib.parse import urlparse,parse_qs
LANE=(pathlib.Path(__file__).resolve().parents[4]/'Build/Tasks/YMCC-Decky-SharedWallpaper-20261017/implementation/popular-games-r45').resolve()
CASES=['normal20','wrong-identities20','crossgame20','cross-playlist20','cross-redirect20','cross-fallback20','transient20','rate20','disconnect20','changed20','plan20','stale-edit20','auto20']
def box(name,data=b''):return struct.pack('>I',len(data)+8)+name+data
INIT=box(b'ftyp',b'isom'+b'\0'*12)+box(b'moov',b'\0'*16)
def payload(app,index,version=1):
 b=hashlib.sha256(f'{app}:{index}:{version}'.encode()).digest();size=128*1024 if index==1 else 16*1024
 return box(b'moof',b'\0'*16)+box(b'mdat',(b*((size+31)//32))[:size])
def pathkey(url):
 p=urlparse(url);return p.path+('?' + p.query if p.query else '')
class Fixture:
 def __init__(self,mode,profile):
  self.mode=mode;self.profile=profile;self.items={x['appId']:x for x in profile['items']};self.routes={pathkey(url):value for url,value in profile['routes'].items()};self.calls={};self.lock=threading.Lock();self.logs=[];self.active=0;self.peak=0;self.gate=threading.Event();self.first=set();self.cut=set();self.crossRequests=[]
  self.movieApps=[x['appId'] for x in profile['items'] if x.get('segments')];fixture=self
  class Handler(BaseHTTPRequestHandler):
   protocol_version='HTTP/1.1'
   def log_message(self,*args):pass
   def release_transfer(self):
    if getattr(self,'counted',False):
     with fixture.lock:fixture.active-=1
     self.counted=False
   def send(self,data,status=200,headers=None,cut=None,delay=0,kind='metadata',app=0):
    self.send_response(status);self.send_header('Connection','close');self.send_header('Content-Length',str(len(data)))
    for k,v in (headers or {}).items():self.send_header(k,v)
    self.end_headers();sent=0
    try:
     actual=data[:cut] if cut is not None else data
     for n in range(0,len(actual),8192):
      if n+8192>=len(actual):self.release_transfer()
      self.wfile.write(actual[n:n+8192]);self.wfile.flush();sent+=len(actual[n:n+8192])
      if delay and n+8192<len(actual):time.sleep(delay)
    finally:
     self.close_connection=True
     with fixture.lock:fixture.logs.append({'app':app,'kind':kind,'path':self.path,'status':status,'bytes':sent,'range':self.headers.get('Range',''),'ifRange':self.headers.get('If-Range','')})
   def do_GET(self):
    try:self.dispatch()
    except (OSError,BrokenPipeError,ConnectionResetError):pass
   def dispatch(self):
    parsed=urlparse(self.path)
    if parsed.path=='/api/appdetails':
     app=int(parse_qs(parsed.query)['appids'][0]);item=fixture.items[app]
     with fixture.lock:fixture.calls[app]=fixture.calls.get(app,0)+1;phase=fixture.calls[app]
     if mode in ['transient20','rate20'] and phase==1 and app in fixture.movieApps[:3]:return self.send(b'busy',429 if mode=='rate20' else 503,app=app)
     doc=copy.deepcopy(item['sourceDocument']);data=doc[str(app)]['data']
     if mode=='wrong-identities20':data['steam_appid']=fixture.profile['items'][(item['rank'])%20]['appId']
     if mode=='crossgame20' and app==730:data['movies']=[{'hls_h264':fixture.items[570]['master']}]
     if mode=='cross-fallback20' and app==730:data['movies']=[]
     return self.send(json.dumps(doc).encode(),app=app)
    if mode=='cross-fallback20' and parsed.path=='/app/730/':return self.send(('<video src="'+fixture.items[570]['master']+'"></video>').encode(),kind='fallback',app=730)
    route=fixture.routes.get(self.path)
    if not route:
     if parsed.path.startswith('/app/'):return self.send(b'<!doctype html><title>Empty fallback fixture</title>',kind='fallback')
     return self.send(b'not captured',404,kind='uncaptured')
    app=route['app'];kind=route['kind'];phase=fixture.calls.get(app,0)
    if mode=='crossgame20' and app==570 and fixture.calls.get(730,0) and phase==0:fixture.crossRequests.append(self.path)
    if kind in ['master','playlist','fallback']:
     text=route['text']
     if mode=='cross-redirect20' and app==730 and kind=='master':return self.send(b'',302,{'Location':fixture.items[570]['master']},kind='redirect',app=app)
     if mode=='plan20' and phase>=2 and app in fixture.movieApps[:3] and kind=='playlist':text+='\n#YMCC-TEST-PLAN-REVISION-2\n'
     if mode=='cross-playlist20' and app==730 and kind=='playlist':text=fixture.items[570]['playlist'];base=fixture.items[570]['variant'];text='\n'.join(re.sub(r'URI="([^"]+)"',lambda m:'URI="'+__import__('urllib.parse',fromlist=['urljoin']).urljoin(base,m[1])+'"',line) if line.startswith('#') else __import__('urllib.parse',fromlist=['urljoin']).urljoin(base,line.strip()) if line.strip() else '' for line in text.splitlines())
     return self.send(text.encode(),kind=kind,app=app)
    if kind=='poster':return self.send(b'\xff\xd8\xff\xe0'+hashlib.sha256(str(app).encode()).digest(),kind=kind,app=app)
    index=route.get('index',0);version=2 if mode in ['changed20','plan20'] and phase>=2 and app in fixture.movieApps[:3] else 1;data=INIT if kind=='init' else payload(app,index,version);tag=f'"{app}-{index}-v{version}"';headers={'ETag':tag};raw=self.headers.get('Range','')
    if raw:
     start=int(re.fullmatch(r'bytes=(\d+)-',raw)[1]);headers['Content-Range']=f'bytes {start}-{len(data)-1}/{len(data)}'
     if self.headers.get('If-Range')!=tag:return self.send(data,headers=headers,kind=kind,app=app)
     if start>=len(data):headers['Content-Range']=f'bytes */{len(data)}';return self.send(b'',416,headers,kind=kind,app=app)
     return self.send(data[start:],206,headers,kind=kind,app=app)
    first=kind=='segment' and index==1
    if first:
     with fixture.lock:
      self.counted=True;fixture.active+=1;fixture.peak=max(fixture.peak,fixture.active);fixture.first.add(app)
      if len(fixture.first)>=5:fixture.gate.set()
     try:
      fixture.gate.wait(1)
      if mode in ['disconnect20','changed20','plan20'] and phase==1 and app in fixture.movieApps[:3]:fixture.cut.add(app);return self.send(data,headers=headers,cut=32768,kind=kind,app=app)
      if mode=='auto20' and app==730 and phase==1:
       self.send_response(200);self.send_header('Content-Length',str(len(data)));self.send_header('ETag',tag);self.send_header('Connection','close');self.end_headers();self.wfile.write(data[:32768]);self.wfile.flush()
       deadline=time.monotonic()+6
       # Retire the simulated hung transfer when its owned client is killed.
       # A sleeping server handler after FIN is no longer an active download.
       while time.monotonic()<deadline:
        readable,_,_=select.select([self.connection],[],[],.05)
        if readable and not self.connection.recv(1,socket.MSG_PEEK):break
       self.close_connection=True;return
      return self.send(data,headers=headers,delay=.015 if mode=='auto20' and phase==1 else .001,kind=kind,app=app)
     finally:
      self.release_transfer()
    return self.send(data,headers=headers,kind=kind,app=app)
  class Server(ThreadingHTTPServer):
   daemon_threads=True
   def handle_error(self,*args):pass
  self.server=Server(('127.0.0.1',0),Handler)
 def start(self):threading.Thread(target=self.server.serve_forever,daemon=True).start()
 def close(self):self.server.shutdown();self.server.server_close()
def run(exe,root,mode,profile):
 fixture=Fixture(mode,profile);fixture.start();env=os.environ.copy();env['YMCC_POPULAR_TEST_PROFILE']=str(LANE/'replay-profile.json');target=root/mode;start=time.monotonic()
 try:
  proc=subprocess.run([str(exe),str(target),str(fixture.server.server_port),mode],env=env,capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=125 if mode=='auto20' else 55)
  if proc.returncode:raise RuntimeError(proc.stderr.decode(errors='replace'))
  value=json.loads(proc.stdout)
 finally:fixture.close()
 checks=[]
 def check(v,label):checks.append({'ok':bool(v),'label':label})
 for item in profile['items']:
  app=item['appId'];entry=value['entries'][str(app)];eligible=item['sourceDocument'][str(app)]['data'].get('type')=='game';ready=eligible and bool(item.get('segments')) and mode!='wrong-identities20' and not(mode in ['crossgame20','cross-playlist20','cross-redirect20','cross-fallback20','stale-edit20'] and app==730)
  check(bool(entry.get('dynamicReady'))==ready,f'{app} dynamic readiness is identity/availability correct')
  if ready and entry.get('dynamicReady'):
   asset=entry['assets']['dynamic'];file=target/'artwork/video'/(asset['id']+'.'+asset['extension']);version=2 if mode in ['changed20','plan20'] and app in fixture.movieApps[:3] else 1
   expected=INIT+b''.join(payload(app,n,version) for n in range(1,len(item['segments'])))
   check(hashlib.sha256(file.read_bytes()).digest()==hashlib.sha256(expected).digest(),f'{app} exact per-game and per-version media hash')
  if mode=='wrong-identities20':check(not entry.get('staticReady'),f'{app} wrong identity cannot publish static either')
  elif not eligible:check(value['jobs']['steam-'+str(app)].get('lastError')=='media-identity-unconfirmed' and not entry.get('staticReady'),f'{app} official non-game type is rejected and deferred')
  elif not item.get('segments'):check(value['jobs']['steam-'+str(app)].get('state')=='unavailable',f'{app} no-movie is bounded unavailable')
 check(value['state'].get('workerPid',0)==0 and value['state'].get('activeCount',0)==0,'worker and lanes exit')
 check(value['maxActive']==5,'five-lane hard limit exercised')
 if mode!='wrong-identities20':check(fixture.peak<=5,'at most five outstanding first-segment body transfers (not server cleanup handlers)')
 if mode in ['transient20','rate20','disconnect20','changed20','plan20']:
  for app in fixture.movieApps[:3]:check(not value['firstEntries'][str(app)].get('dynamicReady'),f'{app} failed first round never publishes partial')
  check(all(value['jobs']['steam-'+str(i['appId'])].get('state') in ['done','unavailable'] for i in profile['items'] if i['sourceDocument'][str(i['appId'])]['data'].get('type')=='game'),'public redownload recovers eligible failed subset')
 if mode in ['disconnect20','auto20']:check(value['resumedBytes']>0 and any(r['range'] and r['ifRange'] for r in fixture.logs),'actual validated Range recovery')
 if mode=='plan20':check(not any(r['range'] for r in fixture.logs if r['app'] in fixture.movieApps[:3]),'changed exact playlist prevents all old-prefix reuse')
 if mode=='changed20':check(any(r['range'] and r['status']==200 for r in fixture.logs),'changed ETag falls back to full response')
 if mode=='stale-edit20':check(value['edited'] and value['entries']['730'].get('deletedSlots',{}).get('dynamic'),'delete wins over late old download')
 if mode=='auto20':check(value['recovery'].get('complete') and 0<value['recovery'].get('firstFailureMs',0)<5000 and value['recovery'].get('elapsedMs',0)>=60000,'automatic watchdog then actual minute backoff recovery')
 (target/'http-requests.json').write_text(json.dumps(fixture.logs,indent=2),encoding='utf-8');result={'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks,'seconds':round(time.monotonic()-start,3),'requests':len(fixture.logs),'peakConnections':fixture.peak,'maxActive':value['maxActive'],'resumedBytes':value['resumedBytes'],'recovery':value['recovery']}
 (target/'assertions.json').write_text(json.dumps(result,indent=2),encoding='utf-8');return result
def main():
 ap=argparse.ArgumentParser();ap.add_argument('exe',type=pathlib.Path);ap.add_argument('root',type=pathlib.Path);ap.add_argument('--case',action='append');args=ap.parse_args();exe=args.exe.resolve();root=args.root.resolve()
 if exe.parent!=LANE or not exe.name.startswith('popular-games-') or exe.suffix!='.exe':raise ValueError('known bounded test binary only')
 if not root.is_relative_to(LANE) or root==LANE or root.exists():raise ValueError('fresh evidence within own task only')
 if args.case and any(c not in CASES for c in args.case):raise ValueError('unknown fixture')
 profile=json.loads((LANE/'replay-profile.json').read_text(encoding='utf-8'));root.mkdir();results=[]
 for mode in CASES:
  if args.case and mode not in args.case:continue
  try:row=run(exe,root,mode,profile)
  except Exception as e:row={'case':mode,'ok':False,'checks':[],'error':str(e)}
  results.append(row);print(json.dumps({'case':mode,'ok':row['ok'],'failed':[c['label'] for c in row['checks'] if not c['ok']],'error':row.get('error','')}),flush=True)
 result={'ok':all(r['ok'] for r in results),'cases':len(results),'checks':sum(len(r['checks']) for r in results),'results':results,'scope':'official top20 AppIDs/names/metadata and exact HLS plans; deterministic per-game synthetic media, real WinHTTP loopback; real production queue/worker/supervisor; no installed Steam/game changes'}
 (root/'results.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps({k:result[k] for k in ['ok','cases','checks']}));return 0 if result['ok'] else 1
if __name__=='__main__':raise SystemExit(main())
