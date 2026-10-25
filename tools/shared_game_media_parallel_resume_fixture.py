"""Synthetic media HTTP fixtures. Binds only owned loopback; no user files served."""
import hashlib, json, re, struct, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

def box(name, data=b''):
    return struct.pack('>I', len(data)+8)+name+data
INIT=box(b'ftyp', b'isom'+b'\0'*12)+box(b'moov', b'\0'*16)

def payload(app, version=1, fragment=None):
    size=512*1024 if fragment is not None else 1024*1024
    block=hashlib.sha256(f'{app}:{version}:{fragment}'.encode()).digest()
    data=(block*((size+31)//32))[:size]
    return (box(b'moof', b'\0'*16) if fragment is not None else INIT)+box(b'mdat', data)

def full(app, version=1, hls=False):
    return INIT+payload(app, version, 0)+payload(app, version, 1) if hls else payload(app, version)

HOST='https://cdn.akamai.steamstatic.com'

class Fixture:
    def __init__(self, mode):
        self.mode=mode
        self.lock=threading.Lock()
        self.gate=threading.Event()
        self.logs=[]
        self.api_calls={}
        self.attempts={}
        self.active=0
        self.peak=0
        self.duplicate=False
        self.live=set()
        self.parallel=mode.startswith('parallel') or mode=='auto-five-resume'
        self.hls=mode.startswith('hls-')
        fixture=self
        class Handler(BaseHTTPRequestHandler):
            protocol_version='HTTP/1.1'
            def log_message(self, *args): pass
            def date_time_string(self, timestamp=None): return 'Sun, 18 Oct 2026 01:02:00 GMT'
            def send_body(self, data, status=200, extra=None, cut=None, delay=0, kind='asset'):
                self.send_response(status)
                self.send_header('Connection', 'close')
                self.send_header('Content-Length', str(len(data)))
                for key, value in (extra or {}).items(): self.send_header(key, value)
                self.end_headers()
                sent=0
                try:
                    actual=data[:cut] if cut is not None else data
                    for start in range(0, len(actual), 16384):
                        chunk=actual[start:start+16384]
                        self.wfile.write(chunk)
                        self.wfile.flush()
                        sent+=len(chunk)
                        if delay: time.sleep(delay)
                finally:
                    self.close_connection=True
                    with fixture.lock:
                        fixture.logs.append({'path':self.path, 'range':self.headers.get('Range',''), 'ifRange':self.headers.get('If-Range',''), 'status':status, 'bytes':sent, 'kind':kind})
            def do_GET(self):
                try: self.dispatch()
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError): pass
            def dispatch(self):
                path=urlparse(self.path).path
                if path=='/api/appdetails':
                    app=int(parse_qs(urlparse(self.path).query)['appids'][0])
                    with fixture.lock: fixture.api_calls[app]=fixture.api_calls.get(app,0)+1
                    movie={'hls_h264':HOST+f'/assets/{app}/playlist.m3u8'} if fixture.hls else {'mp4':{'480':HOST+('/shared/video.mp4' if mode=='resume-app-isolation' else f'/assets/{app}/video.mp4')}}
                    document={str(app):{'success':True,'data':{'steam_appid':app,'type':'game','name':f'fixture {app}','background_raw':HOST+f'/assets/{app}/poster.jpg','movies':[movie]}}}
                    return self.send_body(json.dumps(document).encode(), kind='metadata')
                app=(571 if fixture.api_calls.get(571,0) else 570) if path=='/shared/video.mp4' else int(path.split('/')[2])
                phase=fixture.api_calls.get(app,0)
                if path.endswith('poster.jpg'): return self.send_body(b'\xff\xd8\xff\xe0'+b'\0'*64, kind='static')
                version=2 if phase>=2 and mode in ['resume-changed-200','resume-changed-206','hls-plan-changed'] else 1
                if path.endswith('.m3u8'):
                    text=f'#EXTM3U\n#EXT-X-VERSION:{5 if version==2 else 4}\n#EXT-X-MAP:URI="init.mp4"\n#EXTINF:1,\nsegment-a.m4s\n#EXTINF:1,\nsegment-b.m4s\n#EXT-X-ENDLIST\n'
                    return self.send_body(text.encode(), kind='playlist')
                data=INIT if path.endswith('init.mp4') else payload(app,version,0) if path.endswith('segment-a.m4s') else payload(app,version,1) if path.endswith('segment-b.m4s') else payload(app,version)
                with fixture.lock:
                    key=(app,path)
                    fixture.attempts[key]=fixture.attempts.get(key,0)+1
                    attempt=fixture.attempts[key]
                tag=f'"video-{app}-v{1 if mode=="hls-plan-changed" else version}"'
                extra={'ETag':tag}
                if mode.startswith('resume-last-modified'): extra={'Last-Modified':'Sun, 18 Oct 2026 01:00:00 GMT'}
                if mode=='resume-last-modified-recent': extra={'Last-Modified':'Sun, 18 Oct 2026 01:01:59 GMT'}
                if mode=='resume-last-modified-invalid': extra={'Last-Modified':'not-a-date'}
                if mode=='resume-last-modified-weak-etag': extra={'Last-Modified':'Sun, 18 Oct 2026 01:00:00 GMT','ETag':'W/'+tag}
                if mode=='resume-no-validator': extra={}
                if mode=='resume-weak': extra={'ETag':'W/'+tag}
                if mode=='parallel-one-fails' and app==570: return self.send_body(b'unavailable',503,kind='video')
                if fixture.parallel:
                    with fixture.lock:
                        fixture.active+=1
                        fixture.peak=max(fixture.peak,fixture.active)
                        if app in fixture.live: fixture.duplicate=True
                        fixture.live.add(app)
                        if fixture.active>=(2 if mode=='parallel-two' else 5): fixture.gate.set()
                    try:
                        fixture.gate.wait(2)
                        if mode=='auto-five-resume' and attempt==1:
                            if app==570:
                                self.send_response(200)
                                self.send_header('ETag',tag)
                                self.send_header('Content-Length',str(len(data)))
                                self.send_header('Connection','close')
                                self.end_headers()
                                self.wfile.write(data[:131072])
                                self.wfile.flush()
                                with fixture.lock: fixture.logs.append({'path':self.path,'range':'','ifRange':'','status':200,'bytes':131072,'kind':'video'})
                                time.sleep(6)
                                self.close_connection=True
                                return
                            return self.send_body(data,extra=extra,delay=.04,kind='video')
                        return self.ranged(data,extra,phase,attempt,app)
                    finally:
                        with fixture.lock:
                            fixture.active-=1
                            fixture.live.discard(app)
                return self.ranged(data,extra,phase,attempt,app)
            def ranged(self,data,extra,phase,attempt,app):
                path=urlparse(self.path).path
                cut=not fixture.parallel and (mode!='resume-app-isolation' or app==570) and phase==1 and attempt==1 and not path.endswith('init.mp4') and not path.endswith('segment-a.m4s')
                if cut: return self.send_body(data,extra=extra,cut=131072,kind='video')
                raw=self.headers.get('Range','')
                if raw:
                    start=int(re.fullmatch(r'bytes=(\d+)-',raw)[1])
                    if mode in ['resume-ignored','resume-changed-200']: return self.send_body(data,extra=extra,kind='video')
                    if start>=len(data): return self.send_body(b'',416,{**extra,'Content-Range':f'bytes */{len(data)}'},kind='video')
                    first=start+1 if mode=='resume-bad-start' else start
                    total=len(data)+1 if mode=='resume-bad-total' else len(data)
                    headers={**extra,'Content-Range':f'bytes {first}-{len(data)-1}/{total}'}
                    if mode=='resume-encoding': headers['Content-Encoding']='gzip'
                    return self.send_body(data[start:],206,headers,delay=.001,kind='video')
                return self.send_body(data,extra=extra,delay=.001 if fixture.parallel else 0,kind='video')
        class Server(ThreadingHTTPServer):
            daemon_threads=True
            def handle_error(self,*args): pass
        self.server=Server(('127.0.0.1',0),Handler)
    def start(self): threading.Thread(target=self.server.serve_forever,daemon=True).start()
    def close(self): self.server.shutdown(); self.server.server_close()
