"""Real WinHTTP video failure matrix. Loopback and fresh test roots only."""
import argparse, json, pathlib, socket, struct, subprocess, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

def box(kind, payload=b''):
    return struct.pack('>I',len(payload)+8)+kind+payload
INIT=box(b'ftyp',b'isom'+b'\0'*12)+box(b'moov',b'\0'*16)
MEDIA=box(b'mdat',b'\x7f'*32768)
MP4=INIT+MEDIA
FRAGMENT=box(b'moof',b'\0'*16)+MEDIA
WEBM=b'\x1a\x45\xdf\xa3\x87\x42\x82\x84webm'+b'\x18\x53\x80\x67'+b'\x20\x80\x03'+b'\x15\x49\xa9\x66\x80'+b'\x16\x54\xae\x6b\x80'+b'\x1f\x43\xb6\x75'+b'\x20\x7f\xf4'+b'\0'*32756
# Correct the finite Segment size; its child structure is synthetic, no decoder claim.
children=b'\x15\x49\xa9\x66\x80'+b'\x16\x54\xae\x6b\x80'+b'\x1f\x43\xb6\x75'+b'\x20\x80\x00'+b'\0'*32768
WEBM=b'\x1a\x45\xdf\xa3\x87\x42\x82\x84webm'+b'\x18\x53\x80\x67'+(len(children)|0x200000).to_bytes(3,'big')+children
JPG=b'\xff\xd8\xff\xe0'+b'\0'*64
HOST='https://cdn.akamai.steamstatic.com'
REAL_WEBM=None;REAL_MP4=None
CASES={
 'hls-adaptive-estimate':True,'hls-adaptive-actual':True,'hls-adaptive-480':True,'hls-adaptive-all-large':False,'hls-adaptive-no-lower':False,'direct-adaptive-480':True,
 'slow-mp4':True,'chunked-mp4':True,'no-length-mp4':True,
 'truncated-length-mp4':False,'no-length-truncated-mp4':False,
 'empty-video':False,'html-video':False,'poster-returns-video':False,
 'video-returns-image':False,'wrong-appid':False,'not-game':False,
 'api-503':False,'video-429':False,'slow-response-headers':True,'read-reset':False,
 'redirect-denied':False,'redirect-loop':False,'hls-good':True,
 'hls-truncated':False,'hls-map-change':False,'hls-missing-end':False,
 'webm-good':True,'webm-truncated':False,'unverified-target':False,
 'unverified-exe':False,'wrong-response-key':False,'oversized-header':False,
 'fallback-first-fails':True,'no-video':False,'supervised-recovery':True,
 'real-webm':True,'real-webm-truncated':False,'real-mp4':True,'real-mp4-truncated':False,
}

def run(exe, root, mode, expected):
    requests=[];video_attempts=0
    class Handler(BaseHTTPRequestHandler):
        protocol_version='HTTP/1.1'
        def log_message(self,*a): pass
        def body(self,data,kind='application/octet-stream',length=True,status=200):
            self.send_response(status);self.send_header('Content-Type',kind)
            self.send_header('Connection','close')
            if length:self.send_header('Content-Length',str(len(data)))
            self.end_headers();self.wfile.write(data);self.wfile.flush();self.close_connection=True
        def do_GET(self):
            requests.append(self.path)
            try:self.dispatch()
            except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError,OSError):pass
        def dispatch(self):
            nonlocal video_attempts
            if self.path.startswith('/api/appdetails'):
                if mode=='api-503': return self.body(b'unavailable',status=503)
                videos=[] if mode in ['no-video','poster-returns-video'] else [{'mp4':{'480':HOST+'/assets/video.mp4'}}]
                if mode.startswith('hls-'):videos=[{'hls_h264':HOST+'/assets/master.m3u8'}]
                if mode.startswith('webm-') or mode.startswith('real-webm'):videos=[{'webm':{'480':HOST+'/assets/video.webm'}}]
                if mode=='fallback-first-fails':videos=[{'mp4':{'480':HOST+'/assets/first.mp4','max':HOST+'/assets/video.mp4'}}]
                if mode=='direct-adaptive-480':videos=[{'mp4':{q:HOST+'/assets/'+q+'.mp4' for q in ['1080','max','720','480']},'webm':{q:HOST+'/assets/'+q+'.webm' for q in ['1080','max','720']}}]
                data={'570':{'success':True,'data':{'steam_appid':999 if mode=='wrong-appid' else 570,'type':'dlc' if mode=='not-game' else 'game','name':'fixture game','background_raw':HOST+'/assets/poster.jpg','movies':videos}}}
                if mode=='wrong-response-key':data={'999':data['570']}
                return self.body(json.dumps(data).encode(),'application/json')
            if self.path.startswith('/app/'):return self.body(b'<html>No trailer</html>','text/html')
            if self.path.endswith('/poster.jpg'):return self.body(MP4 if mode=='poster-returns-video' else JPG,'image/jpeg')
            if self.path.endswith('/first.mp4'):return self.body(b'offline',status=503)
            if mode=='direct-adaptive-480' and self.path.rsplit('/',1)[-1] in ['1080.mp4','1080.webm','max.mp4','max.webm','720.mp4','720.webm']:
                self.send_response(200);self.send_header('Content-Length',str(301*1024*1024));self.send_header('Connection','close');self.end_headers();self.close_connection=True;return
            if mode.startswith('hls-adaptive-') and self.path.endswith('/master.m3u8'):
                variants=[(1080,5800000),(720,2600000),(480,1400000)]
                if mode=='hls-adaptive-no-lower':variants=variants[:1]
                return self.body(('#EXTM3U\n'+''.join(f'#EXT-X-STREAM-INF:BANDWIDTH={b},RESOLUTION=1920x{h}\n{h}.m3u8\n' for h,b in variants)).encode())
            if mode.startswith('hls-adaptive-') and self.path.endswith('.m3u8'):
                height=int(self.path.rsplit('/',1)[-1].split('.')[0]);duration=660.1 if mode=='hls-adaptive-estimate' else 2
                return self.body(f'#EXTM3U\n#EXT-X-MAP:URI="{height}-init.mp4"\n#EXTINF:{duration},\n{height}.m4s\n#EXT-X-ENDLIST\n'.encode())
            if mode.startswith('hls-adaptive-') and self.path.endswith('.m4s'):
                height=int(self.path.rsplit('/',1)[-1].split('.')[0])
                oversized=mode in ['hls-adaptive-actual','hls-adaptive-no-lower'] and height==1080 or mode=='hls-adaptive-480' and height in [1080,720] or mode=='hls-adaptive-all-large'
                if oversized:
                    self.send_response(200);self.send_header('Content-Length',str(301*1024*1024));self.send_header('Connection','close');self.end_headers();self.close_connection=True;return
                return self.body(FRAGMENT)
            if self.path.endswith('/master.m3u8'):
                text='#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=200000,RESOLUTION=854x480\nvariant.m3u8\n'
                return self.body(text.encode())
            if self.path.endswith('/variant.m3u8'):
                text='#EXTM3U\n#EXT-X-MAP:URI="init.mp4"\n#EXTINF:2,\nsegment.m4s\n'
                if mode=='hls-map-change':text+='#EXT-X-MAP:URI="other-init.mp4"\n#EXTINF:2,\nsegment.m4s\n'
                if mode!='hls-missing-end':text+='#EXT-X-ENDLIST\n'
                return self.body(text.encode())
            if self.path.endswith('init.mp4'):return self.body(INIT)
            if self.path.endswith('.m4s'):
                if mode=='hls-truncated':return self.body(FRAGMENT[:-500],length=False)
                return self.body(FRAGMENT)
            data=WEBM if mode.startswith('webm-') else MP4
            if mode.startswith('real-webm'):data=REAL_WEBM
            if mode.startswith('real-mp4'):data=REAL_MP4
            if mode.endswith('-truncated') and mode.startswith('real-'):return self.body(data[:-500],length=False)
            if mode=='supervised-recovery':
                video_attempts+=1
                if video_attempts==1:
                    self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data[:20000]);self.wfile.flush();time.sleep(6);self.close_connection=True;return
            if mode=='webm-truncated':return self.body(data[:-500],length=False)
            if mode=='no-length-truncated-mp4':return self.body(data[:-500],length=False)
            if mode=='no-length-mp4':return self.body(data,length=False)
            if mode=='empty-video':return self.body(b'')
            if mode=='html-video':return self.body(b'<html>proxy error</html>','text/html')
            if mode=='video-returns-image':return self.body(JPG,'image/jpeg')
            if mode=='video-429':return self.body(b'rate limited',status=429)
            if mode=='slow-response-headers':time.sleep(3);return self.body(data)
            if mode=='redirect-denied':
                self.send_response(302);self.send_header('Location','https://example.invalid/untrusted.mp4');self.send_header('Content-Length','0');self.end_headers();return
            if mode=='redirect-loop':
                self.send_response(302);self.send_header('Location',HOST+'/assets/video.mp4');self.send_header('Content-Length','0');self.end_headers();return
            if mode=='oversized-header':
                self.send_response(200);self.send_header('Content-Length',str(301*1024*1024));self.send_header('Connection','close');self.end_headers();self.close_connection=True;return
            if mode in ['truncated-length-mp4','read-reset']:
                self.send_response(200);self.send_header('Content-Length',str(len(data)));self.send_header('Connection','close');self.end_headers();self.wfile.write(data[:1024]);self.wfile.flush()
                if mode=='read-reset':self.connection.setsockopt(socket.SOL_SOCKET,socket.SO_LINGER,struct.pack('ii',1,0))
                self.close_connection=True;return
            if mode=='chunked-mp4':
                self.send_response(200);self.send_header('Transfer-Encoding','chunked');self.end_headers()
                for n in range(0,len(data),1024):
                    block=data[n:n+1024];self.wfile.write(('%x\r\n'%len(block)).encode()+block+b'\r\n');self.wfile.flush()
                self.wfile.write(b'0\r\n\r\n');self.wfile.flush();return
            if mode=='slow-mp4':
                self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers()
                for n in range(0,len(data),1024):self.wfile.write(data[n:n+1024]);self.wfile.flush();time.sleep(.015)
                return
            return self.body(data)
    class Server(ThreadingHTTPServer):
        def handle_error(self,*args):pass  # expected resets from negative cases
    server=Server(('127.0.0.1',0),Handler);server.daemon_threads=True
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    destination=root/mode
    start=time.monotonic()
    try:
        proc=subprocess.run([str(exe),str(destination),str(server.server_port),mode],capture_output=True,timeout=110 if mode=='supervised-recovery' else 18,creationflags=subprocess.CREATE_NO_WINDOW)
        if proc.returncode:raise RuntimeError(proc.stderr.decode(errors='replace'))
        value=json.loads(proc.stdout)
        (destination/'native-stderr.log').write_bytes(proc.stderr)
    except subprocess.TimeoutExpired as error:
        destination.mkdir(parents=True,exist_ok=True)
        (destination/'native-timeout-stderr.log').write_bytes(error.stderr or b'')
        (destination/'timeout.json').write_text(json.dumps({'seconds':time.monotonic()-start,'timeout':error.timeout}),encoding='utf-8')
        raise
    finally:
        server.shutdown();server.server_close()
        if destination.exists():(destination/'requests.json').write_text(json.dumps(requests,indent=2),encoding='utf-8')
    checks=[]
    def check(condition,label):checks.append({'ok':bool(condition),'label':label})
    entry=value['entry'];state=value['state'];job=value['jobs'].get('steam-570',{})
    check(bool(entry.get('dynamicReady'))==expected,'dynamic readiness reflects complete valid video only')
    check(value['partialFiles']==0,'failed and completed transfers release temporary files')
    check(state.get('workerPid',0)==0,'background worker ends its session')
    if expected:check(job.get('state')=='done','complete variant marks actual task done')
    elif mode in ['unverified-target','unverified-exe']:
        check(not requests and not value['jobs'],'unconfirmed identity never initiates network work')
    elif mode in ['no-video','not-game']:check(job.get('state')=='unavailable','genuine no-video response has bounded unavailable state')
    else:
        check(job.get('state')=='pending' and job.get('retryAt',0)>int(time.time()*1000),'failed video retains retryable queue instead of false completion')
    if mode.startswith('hls-adaptive-') and expected:
        height=480 if mode=='hls-adaptive-480' else 720
        check(entry.get('assets',{}).get('dynamic',{}).get('selectedHeight')==height,'published asset records the actual official fallback resolution')
        if mode=='hls-adaptive-estimate':check(not any('/1080-init.mp4' in v or '/1080.m4s' in v for v in requests),'oversized 1080p estimate does not download its payload')
    if mode=='direct-adaptive-480':check(any('/480.mp4' in v for v in requests),'direct 480p remains reachable beyond the first four quality URLs')
    if mode=='supervised-recovery':
        recovery=value['recovery'];check(recovery.get('completed') and recovery.get('firstFailureMs',0)>0,'real stalled worker is killed then next owned worker completes automatically')
        check(recovery.get('totalMs',0)>=60000 and video_attempts==2,'production one-minute retry backoff is respected without repeated helper churn')
    if mode=='poster-returns-video':check(not entry.get('staticReady') and not entry.get('dynamicReady'),'poster response cannot publish into the other slot')
    if mode in ['truncated-length-mp4','no-length-truncated-mp4','hls-truncated','webm-truncated','video-returns-image','real-webm-truncated','real-mp4-truncated']:
        check(entry.get('staticReady',False),'failed video preserves the successfully cached static wallpaper')
    if mode in ['wrong-appid','not-game','wrong-response-key']:
        check(len(requests)==1 and not entry.get('staticReady'),'metadata mismatch stops before asset download')
    (destination/'requests.json').write_text(json.dumps(requests,indent=2),encoding='utf-8')
    return {'case':mode,'ok':all(c['ok'] for c in checks),'checks':checks,'seconds':round(time.monotonic()-start,3),'requests':len(requests),'reason':job.get('lastError','')}

def main():
    global REAL_WEBM,REAL_MP4
    ap=argparse.ArgumentParser();ap.add_argument('exe',type=pathlib.Path);ap.add_argument('root',type=pathlib.Path);ap.add_argument('--case',action='append');ap.add_argument('--webm-fixture',type=pathlib.Path);ap.add_argument('--mp4-fixture',type=pathlib.Path);args=ap.parse_args()
    if args.webm_fixture:REAL_WEBM=args.webm_fixture.read_bytes()
    if args.mp4_fixture:REAL_MP4=args.mp4_fixture.read_bytes()
    args.root.mkdir(parents=True,exist_ok=False)
    results=[]
    for mode,expected in CASES.items():
        if args.case and mode not in args.case:continue
        if not args.case and (mode.startswith('real-webm') and REAL_WEBM is None or mode.startswith('real-mp4') and REAL_MP4 is None):continue
        try:row=run(args.exe,args.root,mode,expected)
        except Exception as ex:row={'case':mode,'ok':False,'error':str(ex),'checks':[]}
        results.append(row);print(json.dumps({'case':mode,'ok':row['ok'],'failed':[v['label'] for v in row.get('checks',[]) if not v['ok']],'error':row.get('error','')}),flush=True)
    report={'ok':all(r['ok'] for r in results),'cases':len(results),'checks':sum(len(r['checks']) for r in results),'results':results,'scope':'unmodified production Worker/Queue/Store; real WinHTTP HTTP framing, 1s accelerated per-operation timeouts and AC fixture; only test executable routes allowed upstream names to owned loopback server; synthetic media structures, no decoder validity claim'}
    (args.root/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({'ok':report['ok'],'cases':report['cases'],'checks':report['checks']}));return 0 if report['ok'] else 1
if __name__=='__main__':raise SystemExit(main())
