"""Ten real WinHTTP lanes and a failing tail in an owned loopback fixture."""
import argparse,json,pathlib,struct,subprocess,threading,time
from urllib.parse import urlsplit,parse_qs
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer

def box(name,data=b''):return struct.pack('>I',len(data)+8)+name+data
INIT=box(b'ftyp',b'isom'+b'\0'*12)+box(b'moov',b'\0'*16)
MP4=INIT+box(b'mdat',b'\x7f'*131072)
JPG=b'\xff\xd8\xff\xe0'+b'\0'*64

def main():
 ap=argparse.ArgumentParser();ap.add_argument('exe',type=pathlib.Path);ap.add_argument('root',type=pathlib.Path);args=ap.parse_args();requests=[];gate=threading.Condition();initial=set()
 class Handler(BaseHTTPRequestHandler):
  protocol_version='HTTP/1.1'
  def log_message(self,*a):pass
  def body(self,data,kind,status=200,slow=False,etag=False):
   start=0;match=self.headers.get('Range','')
   if match.startswith('bytes='):
    start=int(match[6:].split('-')[0]);status=206
   payload=data[start:];self.send_response(status);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(payload)));self.send_header('Connection','close')
   if etag:self.send_header('ETag','"fixture-v1"');self.send_header('Accept-Ranges','bytes')
   if status==206:self.send_header('Content-Range',f'bytes {start}-{len(data)-1}/{len(data)}')
   self.end_headers()
   if slow:
    for n in range(0,len(payload),4096):self.wfile.write(payload[n:n+4096]);self.wfile.flush();time.sleep(.12)
   else:self.wfile.write(payload);self.wfile.flush()
   self.close_connection=True
  def do_GET(self):
   try:
    path=urlsplit(self.path);record={'path':self.path,'range':self.headers.get('Range','')};requests.append(record)
    if path.path=='/api/appdetails':
     app=int(parse_qs(path.query)['appids'][0])
     with gate:
      if app<=14:
       initial.add(app);gate.notify_all()
       if app<=10 and len(initial)<10:gate.wait_for(lambda:len(initial)>=10,timeout=2)
     if app==1:return self.body(b'busy','text/plain',503)
     data={str(app):{'success':True,'data':{'steam_appid':app,'name':f'Recent {app}','type':'game','background_raw':f'https://cdn.akamai.steamstatic.com/assets/p{app}.jpg','movies':[] if app==2 else [{'mp4':{'720':f'https://cdn.akamai.steamstatic.com/assets/v{app}.mp4'}}]}}}
     return self.body(json.dumps(data).encode(),'application/json')
    if path.path.startswith('/app/'):return self.body(b'<html>No movie</html>','text/html')
    if path.path.endswith('.jpg'):return self.body(JPG,'image/jpeg')
    if path.path.endswith('.mp4'):return self.body(MP4,'video/mp4',slow=path.path.endswith('/v3.mp4'),etag=True)
    return self.body(b'not found','text/plain',404)
   except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError,OSError):pass
 server=ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
 try:
  proc=subprocess.run([str(args.exe.resolve()),str(args.root.resolve()),str(server.server_port)],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=35)
  if proc.returncode:raise RuntimeError(proc.stderr.decode(errors='replace'))
  result=json.loads(proc.stdout);checks=[]
  def check(value,label):checks.append({'ok':bool(value),'label':label})
  check(result['maximumActive']==10,'ten real production worker lanes were active')
  check(not any('GetMostPlayedGames' in r['path'] for r in requests),'no popular-feed requests')
  jobs=result['jobs'];entries=result['entries']
  check(all(jobs[f'steam-{i}']['state']=='done' for i in range(4,15)),'eleven healthy games finished despite failing lanes')
  check(jobs['steam-1'].get('autoStopped') and jobs['steam-1']['attempts']==3,'repeated HTTP failure exhausts that game only')
  check(jobs['steam-2'].get('autoStopped') and jobs['steam-2']['attempts']==0,'confirmed no-video result stops immediately')
  check(jobs['steam-3'].get('autoStopped') and jobs['steam-3']['attempts']==3,'slow video has a finite task budget across attempts')
  check(any(r['range'].startswith('bytes=') and r['range']!='bytes=0-' for r in requests if '/v3.mp4' in r['path']),'timed-out slow video resumes with a nonzero byte range')
  check(jobs['steam-99']['state']=='done' and entries['steam-99'].get('dynamicReady'),'new game after exhausted tail still downloads and publishes')
  check(result['state']['remaining']==0 and result['state']['workerPid']==0 and result['idleDelay']<0,'no retained work or endless tail wakeups')
  check(all(r['code']==0 for r in result['rounds']),'individual failure does not crash the worker')
  result.update({'checks':checks,'requests':requests,'ok':all(c['ok'] for c in checks)})
  (args.root/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
  print(json.dumps({'ok':result['ok'],'checks':len(checks),'failed':[c['label'] for c in checks if not c['ok']],'maximumActive':result['maximumActive'],'elapsedMs':result['elapsedMs'],'cpuMs':result['cpuMs'],'cpuPercentAllProcessors':result['cpuPercentAllProcessors'],'peakWorkingSetBytes':result['peakWorkingSetBytes']}));return 0 if result['ok'] else 1
 finally:server.shutdown();server.server_close();thread.join(timeout=2)
if __name__=='__main__':raise SystemExit(main())
