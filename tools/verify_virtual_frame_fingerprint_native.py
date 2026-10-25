"""Extract real before/current pure hash functions; run private CryptoAPI oracle and benchmarks."""
from pathlib import Path
import argparse, json, hashlib, subprocess, re, sys
ap=argparse.ArgumentParser();ap.add_argument('--baseline',required=True);ap.add_argument('--output',required=True);args=ap.parse_args()
repo=Path(__file__).resolve().parent.parent;before=Path(args.baseline);out=Path(args.output)
if out.exists():raise SystemExit('Refusing existing output')
out.mkdir(parents=True)
def extract(s,name):
    start=s.index('static ',s.index(name)-30);opening=s.index('{',s.index(name,start));depth=0;quote=None;comment=None;i=opening
    while i<len(s):
        c=s[i];n=s[i:i+2]
        if comment=='line':
            if c=='\n':comment=None
        elif comment=='block':
            if n=='*/':comment=None;i+=1
        elif quote:
            if c=='\\':i+=1
            elif c==quote:quote=None
        elif n=='//':comment='line';i+=1
        elif n=='/*':comment='block';i+=1
        elif c in ('"',"'"):quote=c
        elif c=='{':depth+=1
        elif c=='}':
            depth-=1
            if depth==0:return s[start:i+1]
        i+=1
    raise ValueError(name)
old=before.read_bytes().decode('utf-8-sig');current=(repo/'native/main.cpp').read_bytes().decode('utf-8-sig')
names=['inputHostCryptoProvider','inputHostSha256','inputHostCanonicalFrameHash']
a='\n'.join(extract(old,n) for n in names);b='\n'.join(extract(current,n) for n in names)
assert 'CanonicalFrameFingerprintMemo' in b
preamble=r"""
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include "canonical_frame_fingerprint_memo.h"
"""
body=r"""
struct Frame { int buttons; std::array<float,6> axis; };
static std::uint64_t assertions=0;static volatile std::uint64_t sink=0;
#define CHECK(x) do {++assertions;if(!(x)){std::cerr<<"FAIL line "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(0)
float fromBits(std::uint32_t b){float f;std::memcpy(&f,&b,4);return f;}
std::string oldHash(const Frame& f){return oldpath::inputHostCanonicalFrameHash(f.buttons,f.axis[0],f.axis[1],f.axis[2],f.axis[3],f.axis[4],f.axis[5]);}
std::string newHash(const Frame& f){return newpath::inputHostCanonicalFrameHash(f.buttons,f.axis[0],f.axis[1],f.axis[2],f.axis[3],f.axis[4],f.axis[5]);}
template<class Fn> double measure(const std::vector<Frame>& frames,Fn fn){
    std::uint64_t total=0;const auto begin=std::chrono::steady_clock::now();
    for(const auto& f:frames){const auto h=fn(f);total+=h.size()+static_cast<unsigned char>(h[0]);}
    const auto elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();sink=total;return elapsed/frames.size();
}
int main(){
    std::mt19937 rng(0x20261014);Frame frame{};
    CHECK(oldHash(frame)=="40e92f4db7776375ba59bdf4576e638c224fe53973ec73576501947c573c5089");
    for(unsigned i=0;i<30000;++i){frame.buttons=static_cast<int>(rng());for(auto& f:frame.axis)f=fromBits(rng());const auto expect=oldHash(frame);CHECK(newHash(frame)==expect);CHECK(newHash(frame)==expect);}
    const std::array<std::uint32_t,12> edge{0,0x80000000u,1,0x80000001u,0x007fffffu,0x00800000u,0x7f800000u,0xff800000u,0x7fc00001u,0x7fc00002u,0x7f800001u,0xffffffffu};
    for(auto x:edge)for(auto y:edge){Frame f{static_cast<int>(x),{fromBits(x),fromBits(y),fromBits(x),fromBits(y),fromBits(x),fromBits(y)}};CHECK(newHash(f)==oldHash(f));CHECK(newHash(f)==oldHash(f));}
    ymcc::input::CanonicalFrameFingerprintMemo memo;unsigned calls=0;
    auto hash=[&](const std::string& text){++calls;return oldpath::inputHostSha256(text);};
    for(unsigned i=0;i<10000;++i)CHECK(memo.compute(0,0,0,0,0,0,0,hash)==oldHash(Frame{}));CHECK(calls==1);
    CHECK(memo.compute(0,fromBits(0x80000000u),0,0,0,0,0,hash)==oldHash(Frame{0,{fromBits(0x80000000u),0,0,0,0,0}}));CHECK(calls==2);
    auto fail=[&](const std::string&){++calls;return std::string{};};
    CHECK(memo.compute(1,0,0,0,0,0,0,fail).empty());CHECK(memo.compute(1,0,0,0,0,0,0,fail).empty());CHECK(calls==4);
    CHECK(memo.compute(1,0,0,0,0,0,0,hash)==oldHash(Frame{1,{}}));CHECK(calls==5);
    auto throwing=[](const std::string&)->std::string{throw std::runtime_error("injected");};bool threw=false;
    try{memo.compute(2,0,0,0,0,0,0,throwing);}catch(const std::runtime_error&){threw=true;}CHECK(threw);
    std::atomic<unsigned> bad{0};std::vector<std::thread> workers;
    for(unsigned t=0;t<4;++t)workers.emplace_back([&,t]{for(unsigned i=0;i<500;++i){Frame f{static_cast<int>(i+t*1000),{fromBits(i),fromBits(t),0,0,0,0}};if(newHash(f)!=oldHash(f))++bad;}});
    for(auto& t:workers)t.join();CHECK(bad.load()==0);
    constexpr unsigned count=40000;std::vector<Frame> samples(count);std::vector<double> oldTimes,newTimes;
    for(unsigned workload=0;workload<4;++workload){
        for(unsigned i=0;i<count;++i){auto& f=samples[i];f={};if(workload==1)f={0x122,{0.5f,-0.25f,0.75f,-0.5f,0.25f,1.f}};
            if(workload>=2){f.buttons=static_cast<int>(workload==2?i/8:i);for(unsigned j=0;j<6;++j)f.axis[j]=static_cast<float>((workload==2?i/8:i)+j)/100000.f;}}
        measure(samples,newHash);measure(samples,oldHash);oldTimes.clear();newTimes.clear();
        for(unsigned repeat=0;repeat<5;++repeat){if(repeat%2){newTimes.push_back(measure(samples,newHash));oldTimes.push_back(measure(samples,oldHash));}else{oldTimes.push_back(measure(samples,oldHash));newTimes.push_back(measure(samples,newHash));}}
        std::sort(oldTimes.begin(),oldTimes.end());std::sort(newTimes.begin(),newTimes.end());
        std::cout<<"BENCH workload="<<std::array<const char*,4>{"idle","held","change-every-8","all-changing"}[workload]<<" frames="<<count<<" oldUs="<<oldTimes[2]<<" newUs="<<newTimes[2]<<" ratio="<<newTimes[2]/oldTimes[2]<<'\n';
    }
    std::cout<<"NATIVE_FINGERPRINT_SUMMARY assertions="<<assertions<<" mismatches=0 realCrypto=true hardwareAccess=false productRun=false\n";
}
"""
cpp=preamble+'\nnamespace oldpath {\nstatic const char kC6aHexDigits[]= "0123456789abcdef";\n'+a+'\n}\nnamespace newpath {\nstatic const char kC6aHexDigits[]= "0123456789abcdef";\n'+b+'\n}\n'+body
(out/'fixture.cpp').write_text(cpp,encoding='utf-8',newline='\n')
result={'baseline':str(before),'baselineSha256':hashlib.sha256(before.read_bytes()).hexdigest(),'currentMainSha256':hashlib.sha256((repo/'native/main.cpp').read_bytes()).hexdigest(),'productionHelperSha256':hashlib.sha256((repo/'native/canonical_frame_fingerprint_memo.h').read_bytes()).hexdigest(),'functions':{n:{'before':hashlib.sha256(extract(old,n).encode()).hexdigest(),'current':hashlib.sha256(extract(current,n).encode()).hexdigest()}for n in names},'scope':'extracted actual native pure functions, real SHA256 CryptoAPI, standalone private process; no HID/Host/device/registry calls'}
(out/'source-binding.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
for profile,flags in [('release',['/O2','/DNDEBUG']),('checked',['/Od','/RTC1'])]:
    exe=out/(profile+'.exe')
    command=['cl.exe','/nologo','/std:c++17','/EHsc','/W4','/WX','/utf-8','/MT',*flags,str(out/'fixture.cpp'),'/I'+str(repo/'native'),'/Fo'+str(out/(profile+'.obj')),'/Fe'+str(exe),'/link','Advapi32.lib','/INCREMENTAL:NO']
    c=subprocess.run(command,cwd=out,capture_output=True,text=True);(out/(profile+'-build.log')).write_text(c.stdout+c.stderr,encoding='utf-8')
    if c.returncode:print(c.stdout+c.stderr);raise SystemExit(c.returncode)
    r=subprocess.run([str(exe)],cwd=out,capture_output=True,text=True,timeout=60);(out/(profile+'-run.log')).write_text(r.stdout+r.stderr,encoding='utf-8');print(profile,r.stdout,r.stderr)
    if r.returncode:raise SystemExit(r.returncode)
    result[profile]={'exit':r.returncode,'binarySha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'benchmarks':[dict(re.findall(r'(\w+)=(\S+)',line))for line in r.stdout.splitlines()if line.startswith('BENCH')]}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
