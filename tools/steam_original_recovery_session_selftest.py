"""Execute extracted old/new session() with memory ports; never inspect live Steam.

Requires a pre-change header snapshot, not an independently rewritten old model.
Use --verifier-only for the bounded single-handle verifiedClientProcess change.
Example: python tools/steam_original_recovery_session_selftest.py --baseline
<session-before.h> --output <fresh external session-test directory>
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time


def sha(data):
    return hashlib.sha256(data).hexdigest()


def extract(text, pattern, semicolon=False):
    # Mask comments/strings while keeping offsets, so braces in literals do not count.
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: ' ' * len(m.group()), text)
    match = re.search(pattern, masked)
    if not match:
        raise ValueError('production declaration not found: ' + pattern)
    start = masked.index('{', match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (masked[end] == '{') - (masked[end] == '}')
        end += 1
    if semicolon:
        assert text[end] == ';'
        end += 1
    return text[match.start():end]


def function(text, name):
    return extract(text, r'(?:static|inline)\s+[^;{}\n]+\b' + name + r'\s*\([^;{}]*\)\s*\{')


CPP = r'''
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
using DWORD=std::uint32_t;using ULONGLONG=unsigned long long;using HANDLE=int;
constexpr int HKEY_CURRENT_USER=1,PROCESS_QUERY_LIMITED_INFORMATION=1,SYNCHRONIZE=2,FALSE=0,WAIT_TIMEOUT=258;
std::string W2U(const std::wstring& s){std::string out;for(auto c:s)out+=static_cast<char>(c);return out;}
@@PATH_KEY@@
@@SESSION_STRUCT@@
@@SAME_PROCESS@@
@@VALID_SESSION@@
struct Observed{DWORD pid=0,account=91;ULONGLONG created=0;std::wstring root;};
struct Input{DWORD registeredPid=0,account=0;std::wstring root;Observed observed;Session pinned;int first=0,second=0;bool registryOK=true;};
struct Call{DWORD pid=0;std::wstring root;bool ok=false;ULONGLONG birth=0;bool operator==(const Call&)const=default;};
struct Trace{std::vector<Call> calls;std::vector<std::string> registry;unsigned snapshots=0,focus=0,opens=0,waits=0,closes=0;};
Input input;Trace trace;int response=0;std::mutex liveAccountMx;Session liveAccountSession;
void sofRegDWORD(int hive,const wchar_t* key,const wchar_t* name,DWORD* value){
 if(hive!=HKEY_CURRENT_USER||std::wstring(key)!=L"Software\\Valve\\Steam\\ActiveProcess")throw std::runtime_error("registry DWORD location changed");
 const bool account=std::wstring(name)==L"ActiveUser";
 if(!account&&std::wstring(name)!=L"pid")throw std::runtime_error("registry value changed");
 trace.registry.push_back(account?"ActiveUser":"pid");
 if(input.registryOK)*value=account?input.account:input.registeredPid;
}
std::wstring sofRegSZ(int hive,const wchar_t* key,const wchar_t* name){
 if(hive!=HKEY_CURRENT_USER||std::wstring(key)!=L"Software\\Valve\\Steam"||std::wstring(name)!=L"SteamPath")throw std::runtime_error("registry SZ location changed");
 trace.registry.push_back("SteamPath");return input.registryOK?input.root:L"";
}
struct Observer{Observed sessionSnapshot(){++trace.snapshots;return input.observed;}}g_steamSettingsObserver;
// Response codes exercise the actual verifiedClientProcess body, including ABA.
// 0 focus failure; 1 zero birth; 2 wrong executable; 3 open denied;
// 4 handle birth changed; 5 exited; 6/7/8 valid births 100/200/300;
// 9 valid normalized executable; 10 wait failure.
ULONGLONG responseBirth(){return response==1?0:response==7?200:response==8?300:100;}
bool focusQueryProcessIdentity(DWORD,std::wstring* path,ULONGLONG* birth){
 ++trace.focus;*birth=responseBirth();*path=trace.calls.back().root+L"\\steam.exe";
 if(response==2)*path=trace.calls.back().root+L"\\other.exe";
 if(response==9){std::replace(path->begin(),path->end(),L'\\',L'/');for(auto& c:*path)if(c>=L'a'&&c<=L'z')c-=L'a'-L'A';}
 return response!=0;
}
HANDLE OpenProcess(int,int,DWORD){++trace.opens;return response==3?0:1;}
ULONGLONG nativeProcessCreatedFromHandle(HANDLE){return responseBirth()+(response==4?1:0);}
int WaitForSingleObject(HANDLE,int){++trace.waits;return response==5?0:response==10?-1:WAIT_TIMEOUT;}
void CloseHandle(HANDLE){++trace.closes;}
@@VERIFIED_IMPL@@
bool verifiedClientProcess(DWORD pid,const std::wstring& root,ULONGLONG& created){
 const auto index=trace.calls.size();response=index==0?input.first:input.second;
 trace.calls.push_back({pid,root,false,0});
 const bool ok=verifiedClientProcessImpl(pid,root,created);
 trace.calls[index].ok=ok;trace.calls[index].birth=created;return ok;
}
@@FUNCTIONS@@
using Consumer=Session(*)();
struct Result{Session session;Trace trace;};
Result run(Consumer consumer){trace={};liveAccountSession=input.pinned;auto s=consumer();return{s,trace};}
auto fields(const Session& s){return std::tie(s.account,s.pid,s.registeredPid,s.created,s.root,s.source,s.reason,s.accountSource,s.accountPinned,s.liveAccountPinned,s.foreignRegistryAccountIgnored);}
bool equal(const Session& a,const Session& b){return fields(a)==fields(b);}
std::string sessionJson(const Session& s){
 // All test strings are ASCII; quote/backslash escaping is sufficient here.
 auto quote=[](const std::string& str){std::string out="\"";for(char c:str){if(c=='"'||c=='\\')out+='\\';out+=c;}return out+'"';};
 return "{\"account\":"+std::to_string(s.account)+",\"pid\":"+std::to_string(s.pid)+",\"registeredPid\":"+std::to_string(s.registeredPid)+",\"created\":"+std::to_string(s.created)+",\"root\":"+quote(W2U(s.root))+",\"source\":"+quote(s.source)+",\"reason\":"+quote(s.reason)+",\"accountSource\":"+quote(s.accountSource)+",\"accountPinned\":"+(s.accountPinned?"true":"false")+",\"liveAccountPinned\":"+(s.liveAccountPinned?"true":"false")+",\"foreignRegistryAccountIgnored\":"+(s.foreignRegistryAccountIgnored?"true":"false")+"}";
}
bool reuse(const Result& before){
 const auto& first=before.trace.calls.at(0);
 return first.ok&&!before.session.root.empty()&&pathKey(W2U(before.session.root))==pathKey(W2U(input.observed.root))&&input.observed.pid==input.registeredPid&&input.observed.created==first.birth;
}
bool conforms(const Result& baseline,const Result& candidate){
 const auto& a=baseline.trace;const auto& b=candidate.trace;
 if(!equal(baseline.session,candidate.session)||a.registry!=b.registry||a.snapshots!=b.snapshots)return false;
 const bool skip=reuse(baseline);
 const std::size_t expected=a.calls.size()-(skip?1u:0u);
 if(b.calls.size()!=expected||b.calls.empty()||!(a.calls.front()==b.calls.front()))return false;
 if(!skip)return a.calls==b.calls&&a.focus==b.focus&&a.opens==b.opens&&a.waits==b.waits&&a.closes==b.closes;
 return b.focus==1&&b.opens==1&&b.waits==1&&b.closes==1;
}
struct Negative{const char* name;Consumer consumer;bool caught=false;bool outputDiff=false;std::uint64_t witness=0;Session oldResult,newResult;std::size_t oldCalls=0,newCalls=0,expectedCalls=0;std::string inputJson;};
std::string witnessJson(int roots,int pin){return "{\"rootCase\":"+std::to_string(roots)+",\"registryOK\":"+(input.registryOK?"true":"false")+",\"registeredPid\":"+std::to_string(input.registeredPid)+",\"observedPid\":"+std::to_string(input.observed.pid)+",\"observedBirth\":"+std::to_string(input.observed.created)+",\"firstResponse\":"+std::to_string(input.first)+",\"secondResponse\":"+std::to_string(input.second)+",\"registryAccount\":"+std::to_string(input.account)+",\"observerAccount\":"+std::to_string(input.observed.account)+",\"pinCase\":"+std::to_string(pin)+"}";}
int main(){try{
 std::vector<Negative> negatives={@@NEGATIVES@@};
 const std::vector<std::pair<std::wstring,std::wstring>> roots={
 {L"D:\\Steam",L"D:\\Steam"},{L"D:/STEAM///",L"d:/steam"},{L"\\\\?\\D:\\Steam\\",L"d:\\steam"},
 {L"D:\\Steam",L"E:\\Steam"},{L"",L"D:\\Steam"},{L"D:\\Steam",L""},{L"/",L"D:\\Steam"},
 {L"D:\\Steam",L"D:\\Steam\\"},{L"\\\\?\\UNC\\server\\Steam",L"\\\\server\\steam"}};
 std::uint64_t cases=0,skipped=0,oldCalls=0,newCalls=0,oldFocus=0,newFocus=0;
 std::array<std::uint64_t,11> skippedSecondResponses{};
 std::uint64_t freshDifferentBirth=0,freshDifferentPid=0,freshFailedRegistered=0,ambiguous=0;
 for(int ri=0;ri<static_cast<int>(roots.size());++ri)
 for(bool registryOK:{true,false})for(DWORD registeredPid:{0u,11u})for(DWORD observedPid:{0u,11u,22u})
 for(ULONGLONG observedBirth:{0ull,100ull,200ull,300ull})for(int first=0;first<11;++first)for(int second=0;second<11;++second)
 for(DWORD account:{0u,17u,23u})for(int pin=0;pin<10;++pin){
 input={};input.registryOK=registryOK;input.registeredPid=registeredPid;input.account=account;input.root=roots[ri].first;
 input.observed={observedPid,91,observedBirth,roots[ri].second};input.first=first;input.second=second;
 if(pin){auto& p=input.pinned;p.pid=observedPid;p.created=observedBirth;p.root=roots[ri].second;p.account=37;p.accountPinned=true;p.accountSource="live-pin";
 if(pin==1)p.accountPinned=false;if(pin==2)p.liveAccountPinned=true;
 if(pin==3)++p.pid;if(pin==4)++p.created;if(pin==5)p.root=L"Z:\\Foreign";
 if(pin==6)p.account=0;if(pin==7)p.pid=0;if(pin==8)p.created=0;if(pin==9)p.root.clear();}
 const auto before=run(session_old);const auto after=run(session_new);++cases;
 if(before.trace.registry!=std::vector<std::string>{"ActiveUser","pid","SteamPath"}||before.trace.snapshots!=1)throw std::runtime_error("old registry/observer contract");
 if(!conforms(before,after)){std::cerr<<"FAIL positive case "<<cases<<" "<<witnessJson(ri,pin)<<" old="<<sessionJson(before.session)<<" new="<<sessionJson(after.session)<<" oldCalls="<<before.trace.calls.size()<<" newCalls="<<after.trace.calls.size()<<'\n';return 1;}
 oldCalls+=before.trace.calls.size();newCalls+=after.trace.calls.size();oldFocus+=before.trace.focus;newFocus+=after.trace.focus;
 if(reuse(before)){++skipped;++skippedSecondResponses[static_cast<std::size_t>(second)];}
 else if(before.trace.calls.size()==2){
 if(registeredPid==observedPid&&before.trace.calls[0].ok&&before.trace.calls[0].birth!=observedBirth)++freshDifferentBirth;
 if(registeredPid!=observedPid)++freshDifferentPid;if(!before.trace.calls[0].ok)++freshFailedRegistered;}
 if(before.session.reason=="steam-process-ambiguous")++ambiguous;
 for(auto& neg:negatives)if(!neg.caught){const auto bad=run(neg.consumer);if(!conforms(before,bad)&&(neg.consumer==session_old||(!equal(before.session,bad.session)&&((std::string(neg.name)!="different_pid_reused"&&std::string(neg.name)!="failed_registered_reused")||(registeredPid&&observedPid))))){
 neg.caught=true;neg.witness=cases;neg.outputDiff=!equal(before.session,bad.session);neg.oldResult=before.session;neg.newResult=bad.session;
 neg.oldCalls=before.trace.calls.size();neg.newCalls=bad.trace.calls.size();neg.expectedCalls=before.trace.calls.size()-(reuse(before)?1u:0u);neg.inputJson=witnessJson(ri,pin);}}
 }
 bool passed=skipped>0&&freshDifferentBirth>0&&freshDifferentPid>0&&freshFailedRegistered>0&&ambiguous>0;
 std::cout<<"{\"cases\":"<<cases<<",\"oldVerifiedCalls\":"<<oldCalls<<",\"newVerifiedCalls\":"<<newCalls<<",\"skippedCalls\":"<<skipped<<",\"oldFocusQueries\":"<<oldFocus<<",\"newFocusQueries\":"<<newFocus<<",\"freshDifferentBirth\":"<<freshDifferentBirth<<",\"freshDifferentPid\":"<<freshDifferentPid<<",\"freshFailedRegistered\":"<<freshFailedRegistered<<",\"ambiguousCases\":"<<ambiguous<<",\"skippedSecondResponses\":[";
 for(std::size_t i=0;i<skippedSecondResponses.size();++i){if(i)std::cout<<',';std::cout<<skippedSecondResponses[i];passed=passed&&skippedSecondResponses[i]>0;}
 std::cout<<"],\"negatives\":[";
 for(std::size_t i=0;i<negatives.size();++i){const auto& n=negatives[i];if(i)std::cout<<',';passed=passed&&n.caught;
 // Safety mutations must change output, not merely make a redundant call.
 if(i>0)passed=passed&&n.outputDiff;
 std::cout<<"{\"name\":\""<<n.name<<"\",\"caught\":"<<(n.caught?"true":"false")<<",\"outputDiff\":"<<(n.outputDiff?"true":"false")<<",\"witness\":"<<n.witness<<",\"input\":"<<(n.caught?n.inputJson:"null")<<",\"oldCalls\":"<<n.oldCalls<<",\"expectedCalls\":"<<n.expectedCalls<<",\"newCalls\":"<<n.newCalls<<",\"oldResult\":"<<sessionJson(n.oldResult)<<",\"mutantResult\":"<<sessionJson(n.newResult)<<"}";}
 std::cout<<"],\"passed\":"<<(passed?"true":"false")<<"}\n";return passed?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
'''


# Same leaf entry, bounded verifier mode. All Win32 calls below are memory ports;
# production helpers and both verifier definitions are extracted, not reimplemented.
CPP_VERIFIER = r'''
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using DWORD=std::uint32_t;using ULONGLONG=unsigned long long;using HANDLE=int;
struct FILETIME{DWORD dwLowDateTime=0,dwHighDateTime=0;};
#pragma warning(push)
#pragma warning(disable:4201) // Match the Windows ULARGE_INTEGER anonymous fields.
union ULARGE_INTEGER{struct{DWORD LowPart,HighPart;};ULONGLONG QuadPart;};
#pragma warning(pop)
constexpr int FALSE=0,PROCESS_QUERY_LIMITED_INFORMATION=0x1000,SYNCHRONIZE=0x100000,WAIT_TIMEOUT=258;
std::string W2U(const std::wstring& s){std::string out;for(auto c:s)out+=static_cast<char>(c);return out;}
@@PATH_KEY@@
struct Input{DWORD pid=11;std::wstring root=L"D:\\Steam",path=L"D:\\Steam\\steam.exe";bool open=true,image=true,times=true;ULONGLONG birth=100;int wait=WAIT_TIMEOUT;bool reuse=false;};
struct Trace{int opens=0,images=0,times=0,waits=0,closes=0,active=0;bool single=false;std::string events;};
Input input;Trace trace;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void held(HANDLE h){require(h&&h==trace.active,"wrong or closed process handle");}
HANDLE OpenProcess(int rights,int inherit,DWORD pid){
 require(pid==input.pid&&inherit==FALSE,"wrong PID/inherit");
 require(rights==(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE)||(!trace.single&&rights==PROCESS_QUERY_LIMITED_INFORMATION),"wrong access rights");
 require(!trace.active,"overlapping process opens");++trace.opens;trace.events+='O';
 if(!input.open)return 0;trace.active=trace.opens;return trace.active;
}
bool QueryFullProcessImageNameW(HANDLE h,int flags,wchar_t* buffer,DWORD* length){
 held(h);require(flags==0&&*length==32768,"path query flags/buffer changed");++trace.images;trace.events+='Q';
 require(input.path.size()<*length,"test path too long");std::copy(input.path.begin(),input.path.end(),buffer);
 *length=static_cast<DWORD>(input.path.size());return input.image;
}
bool GetProcessTimes(HANDLE h,FILETIME* created,FILETIME*,FILETIME*,FILETIME*){
 held(h);++trace.times;trace.events+='T';const auto birth=input.birth+(input.reuse&&h==2?1:0);
 created->dwLowDateTime=static_cast<DWORD>(birth);created->dwHighDateTime=static_cast<DWORD>(birth>>32);return input.times;
}
int WaitForSingleObject(HANDLE h,int milliseconds){held(h);require(milliseconds==0,"blocking wait");++trace.waits;trace.events+='W';return input.wait;}
void CloseHandle(HANDLE h){held(h);trace.active=0;++trace.closes;trace.events+='C';}
@@HELPERS@@
@@VERIFIERS@@
using Verifier=bool(*)(DWORD,const std::wstring&,ULONGLONG&);
constexpr ULONGLONG sentinel=0x123456789abcdef0ULL;
bool check(Verifier verifier,bool expected,bool single){
 trace={};trace.single=single;ULONGLONG output=sentinel;const bool result=verifier(input.pid,input.root,output);
 require(result==expected,"verdict changed");require(output==(expected?input.birth:sentinel),"created output changed on failure or wrong birth");
 require(!trace.active&&trace.closes==(input.open?trace.opens:0),"process handle leak/double close");
 if(single){require(trace.opens<=1&&trace.images<=1&&trace.times<=1&&trace.waits<=1,"duplicate process work");
 if(expected)require(trace.opens==1&&trace.images==1&&trace.times==1&&trace.waits==1&&trace.closes==1,"missing identity operation");}
 return true;
}
struct Mutation{const char* name;Verifier verifier;bool caught=false;};
int main(){try{
 struct Paths{std::wstring root,path;bool match;};
 const std::vector<Paths> paths={
 {L"D:\\Steam",L"D:\\Steam\\steam.exe",true},{L"D:/STEAM",L"d:\\steam/STEAM.EXE",true},
 {L"\\\\?\\D:\\Steam",L"d:\\steam\\steam.exe",true},{L"\\\\server\\Steam",L"\\\\?\\UNC\\server\\steam\\steam.exe",true},
 {L"D:\\Steam",L"E:\\Steam\\steam.exe",false},{L"D:\\Steam",L"D:\\Steam\\other.exe",false},
 {L"D:\\Steam",L"D:\\Steam\\steam.exe.bak",false},{L"D:\\Steam",L"D:\\Steam\\child\\steam.exe",false},
 {L"D:\\Steam",L"D:\\SteamElse\\steam.exe",false},{L"D:\\Steam",L"",false},{L"",L"\\steam.exe",false},
 {L"D:\\"+std::wstring(32750,L'x'),L"D:\\"+std::wstring(32750,L'x')+L"\\steam.exe",true}};
 std::vector<Mutation> mutations={@@MUTATIONS@@};
 std::uint64_t cases=0,accepted=0,oldOpens=0,newOpens=0,oldTimes=0,newTimes=0;
 for(const auto& path:paths)for(DWORD pid:{0u,11u})for(bool open:{false,true})for(bool image:{false,true})for(bool times:{false,true})
 for(ULONGLONG birth:{0ull,100ull,0x123456789ull})for(int wait:{WAIT_TIMEOUT,0,-1,128}){
 input={pid,path.root,path.path,open,image,times,birth,wait,false};
 const bool expected=pid&&!path.root.empty()&&open&&image&&times&&birth&&wait==WAIT_TIMEOUT&&path.match;
 check(verifier_old,expected,false);oldOpens+=trace.opens;oldTimes+=trace.times;
 if(expected)require(trace.events=="OQTCOTWC","old success flow changed");
 check(verifier_new,expected,true);newOpens+=trace.opens;newTimes+=trace.times;
 if(expected){++accepted;require(trace.events=="OQTWC","new success flow changed");}++cases;
 for(auto& mutation:mutations)if(!mutation.caught){try{check(mutation.verifier,expected,true);}catch(const std::exception&){mutation.caught=true;}}
 }
 input={};input.reuse=true;check(verifier_old,false,false);require(trace.opens==2,"old ABA boundary not reached");
 check(verifier_new,true,true);require(trace.events=="OQTWC","single held object changed during reuse window");
 std::cout<<"{\"cases\":"<<cases<<",\"accepted\":"<<accepted<<",\"oldOpens\":"<<oldOpens<<",\"newOpens\":"<<newOpens<<",\"oldGetTimes\":"<<oldTimes<<",\"newGetTimes\":"<<newTimes<<",\"oldSuccessFlow\":\"OQTCOTWC\",\"newSuccessFlow\":\"OQTWC\",\"reuseBoundaryChecked\":true,\"negatives\":[";
 for(std::size_t i=0;i<mutations.size();++i){if(i)std::cout<<',';const auto& m=mutations[i];require(m.caught,"mutation survived");std::cout<<"{\"name\":\""<<m.name<<"\",\"caught\":true}";}
 std::cout<<"],\"passed\":true}\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
'''


def verifier_fixture(old_text, new_text, tracking, main_source):
    old = function(old_text, 'verifiedClientProcess')
    new = function(new_text, 'verifiedClientProcess')
    if old_text.replace(old, '@@VERIFIER@@', 1) != new_text.replace(new, '@@VERIFIER@@', 1):
        raise ValueError('production header differs outside verifiedClientProcess()')
    if function(old_text, 'session') != function(new_text, 'session'):
        raise ValueError('session selection changed')
    helper_names = ('focusFileTimeTicks', 'focusQueryProcessIdentity', 'nativeProcessCreatedFromHandle')
    helpers = '\n'.join(function(main_source, name) for name in helper_names)
    mutations = {
        'image_read_failure_accepted': ('!imageKnown||', '(imageKnown&&!imageKnown)||'),
        'zero_birth_accepted': ('!birth||', ''),
        'non_timeout_accepted': ('!alive||', '(alive&&!alive)||'),
        'path_relaxed': (r'pathKey(W2U(std::wstring(image.data(),length)))!=pathKey(W2U(root+L"\\steam.exe"))', 'false'),
        'handle_leaked': ('CloseHandle(process);', ''),
        'failure_output_overwritten': ('if(!pid||root.empty())return false;', 'created=42;if(!pid||root.empty())return false;'),
        'synchronize_right_removed': ('PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE', 'PROCESS_QUERY_LIMITED_INFORMATION'),
    }
    functions = {'old': old, 'new': new}
    functions.update({name: replace_once(new, *change) for name, change in mutations.items()})
    cpp = CPP_VERIFIER
    for key, value in {
        'PATH_KEY': function(tracking, 'pathKey'), 'HELPERS': helpers,
        'VERIFIERS': '\n'.join(replace_once(body, 'verifiedClientProcess(', 'verifier_' + name + '(') for name, body in functions.items()),
        'MUTATIONS': ','.join('{"' + name + '",verifier_' + name + '}' for name in mutations),
    }.items():
        cpp = cpp.replace('@@' + key + '@@', value)
    return cpp, helpers, helper_names


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('mutation anchor must occur exactly once: ' + old)
    return text.replace(old, new)


def compiler_environment():
    # Use installed build tools; launch only our isolated fixture executable.
    vswhere = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    found = subprocess.run([str(vswhere), '-latest', '-products', '*', '-find',
                            r'VC\Tools\MSVC\**\bin\Hostx64\x64\cl.exe'],
                           capture_output=True, text=True, check=True, timeout=15)
    compiler = Path(found.stdout.strip().splitlines()[-1])
    vc = compiler.parents[3]
    sdk = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Windows Kits/10'
    versions = sorted((p for p in (sdk / 'Include').iterdir() if (p / 'ucrt').is_dir()),
                      key=lambda p: tuple(map(int, p.name.split('.'))))
    version = versions[-1].name
    env = {k: v for k, v in os.environ.items() if k.upper() not in ('CL', '_CL_', 'LINK', '_LINK_')}
    env['INCLUDE'] = ';'.join(map(str, [vc / 'include', sdk / 'Include' / version / 'ucrt']))
    env['LIB'] = ';'.join(map(str, [vc / 'lib/x64', sdk / 'Lib' / version / 'ucrt/x64', sdk / 'Lib' / version / 'um/x64']))
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    return compiler, env, version


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--verifier-only', action='store_true', help='bounded old/new single-handle verifier checks')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    if out == repo or repo in out.parents or out in repo.parents:
        parser.error('use an independent external evidence directory')
    if out.exists() and any(out.iterdir()):
        parser.error('output must be fresh; old failures must be preserved')
    out.mkdir(parents=True, exist_ok=True)
    total_start = time.perf_counter()
    report = {'passed': False, 'failedChecks': [], 'scope': 'compiled extracted session and verifier, in-memory ports only; no field CPU claim'}
    try:
        source_path = repo / 'native/sleep_steam_recovery.h'
        source = source_path.read_bytes()
        baseline = args.baseline.read_bytes()
        tracking = (repo / 'native/sleep_steam_tracking.h').read_bytes()
        old_text, new_text = baseline.decode('utf-8-sig'), source.decode('utf-8-sig')
        if args.verifier_only:
            main_path = repo / 'native/main.cpp'
            main_source = main_path.read_text(encoding='utf-8-sig')
            cpp, helpers, helper_names = verifier_fixture(old_text, new_text, tracking.decode('utf-8-sig'), main_source)
            report.update(scope='compiled old/new verifier and original helpers; memory Win32 ports only',
                          mode='verifier-only', executedThisRun=False, cached=False, parentRun=False,
                          sha256={'baselineHeader': sha(baseline), 'currentHeader': sha(source),
                                  'selftest': sha(Path(__file__).read_bytes()), 'trackingHeader': sha(tracking),
                                  'productionHelpers': sha(helpers.encode())})
            (out / 'production-helpers.cpp').write_bytes(helpers.encode())
            for label, body in [('baseline', old_text), ('current', new_text)]:
                (out / (label + '-verifier.cpp')).write_bytes(function(body, 'verifiedClientProcess').encode())
        else:
            old_session, new_session = function(old_text, 'session'), function(new_text, 'session')
            old_body = baseline.replace(old_session.encode(), b'@@SESSION@@', 1)
            new_body = source.replace(new_session.encode(), b'@@SESSION@@', 1)
            # Only session() is authorized in the production header.
            if old_body != new_body:
                raise ValueError('production header differs outside session(); preserve/rebase concurrent edits before testing')
            if 'reuseRegistered' in old_session or old_session.count('verifiedClientProcess(') != 2:
                raise ValueError('baseline must contain the actual original two-query session')
            report['returnFields'] = ['account', 'pid', 'registeredPid', 'created', 'root', 'source', 'reason', 'accountSource', 'accountPinned', 'liveAccountPinned', 'foreignRegistryAccountIgnored']
            report['dimensions'] = {'rootPairs': 9, 'registrySuccess': 2, 'registeredPids': [0, 11], 'observedPids': [0, 11, 22], 'observerBirths': [0, 100, 200, 300], 'firstAndSecondResponsesEach': 11, 'registryAccounts': [0, 17, 23], 'observerAccount': 91, 'pinnedAccount': 37, 'pinStates': 10}
            report['sha256'] = {'baselineHeader': sha(baseline), 'currentHeader': sha(source), 'baselineSession': sha(old_session.encode()),
                                'currentSession': sha(new_session.encode()), 'selftest': sha(Path(__file__).read_bytes()), 'trackingHeader': sha(tracking)}
            for name, data in [('baseline-session.cpp', old_session), ('current-session.cpp', new_session)]:
                (out / name).write_bytes(data.encode('utf-8'))
            parts = {'PATH_KEY': function(tracking.decode('utf-8-sig'), 'pathKey'),
                     'SESSION_STRUCT': extract(new_text, r'struct\s+Session\s*\{', True),
                     'SAME_PROCESS': function(new_text, 'sameClientProcess'), 'VALID_SESSION': function(new_text, 'validSession'),
                     'VERIFIED_IMPL': replace_once(function(new_text, 'verifiedClientProcess'), 'verifiedClientProcess(', 'verifiedClientProcessImpl(')}
            mutations = {
                'different_birth_reused': ('&&observed.created==registeredBirth', ''),
                'different_pid_reused': ('&&observed.pid==s.registeredPid', ''),
                'failed_registered_reused': ('reuseRegistered=registeredValid&&rootMatches', 'reuseRegistered=rootMatches'),
                'root_relaxed': ('const bool rootMatches=!s.root.empty()&&pathKey(W2U(s.root))==pathKey(W2U(observed.root));', 'const bool rootMatches=true;'),
                'registry_account_mixed': ('s.foreignRegistryAccountIgnored=true;', 's.account=registeredAccount;s.foreignRegistryAccountIgnored=true;'),
                'observer_account_mixed': ('s.foreignRegistryAccountIgnored=true;', 's.account=observed.account;s.foreignRegistryAccountIgnored=true;'),
            }
            consumers = {'old': old_session, 'new': new_session}
            consumers.update({name: replace_once(new_session, *mutation) for name, mutation in mutations.items()})
            parts['FUNCTIONS'] = '\n'.join(replace_once(body, 'session()', 'session_' + name + '()') for name, body in consumers.items())
            parts['NEGATIVES'] = ','.join('{"' + name + '",session_' + consumer + '}' for name, consumer in
                                           [('legacy_duplicate_query', 'old')] + [(name, name) for name in mutations])
            cpp = CPP
            for key, value in parts.items():
                cpp = cpp.replace('@@' + key + '@@', value)
            if '@@' in cpp:
                raise ValueError('unresolved fixture token')
        fixture = out / 'session-fixture.cpp'
        fixture.write_bytes(cpp.encode('utf-8'))
        report['sha256']['fixture'] = sha(fixture.read_bytes())
        compiler, env, sdk = compiler_environment()
        env['TMP'] = env['TEMP'] = str(out)
        exe = out / 'session-fixture.exe'
        command = [str(compiler), '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/O2', '/W4', '/WX',
                   str(fixture), '/Fo' + str(out / 'session-fixture.obj'), '/Fe' + str(exe), '/link', '/INCREMENTAL:NO']
        python_binary = Path(sys.base_prefix) / Path(sys.executable).name
        report['compiler'] = {'command': command, 'sdk': sdk, 'sha256': sha(compiler.read_bytes()),
                              'linkerSha256': sha(compiler.with_name('link.exe').read_bytes()),
                              'python': str(python_binary), 'pythonSha256': sha(python_binary.read_bytes())}
        compile_start = time.perf_counter()
        built = subprocess.run(command, cwd=out, env=env, capture_output=True, timeout=60)
        (out / 'compile.stdout').write_bytes(built.stdout)
        (out / 'compile.stderr').write_bytes(built.stderr)
        report['compileMs'] = (time.perf_counter() - compile_start) * 1000
        report['compileReturnCode'] = built.returncode
        if built.returncode:
            raise RuntimeError('fixture compilation failed; see preserved compile.stdout/stderr')
        run_start = time.perf_counter()
        ran = subprocess.run([str(exe)], cwd=out, env=env, capture_output=True, timeout=180)
        (out / 'run.stdout').write_bytes(ran.stdout)
        (out / 'run.stderr').write_bytes(ran.stderr)
        report['executedThisRun'] = True
        report['executionMs'] = (time.perf_counter() - run_start) * 1000
        report['executableSha256'] = sha(exe.read_bytes())
        report['runReturnCode'] = ran.returncode
        if ran.returncode:
            raise RuntimeError('fixture checks failed; see preserved run.stdout/stderr')
        report['metrics'] = json.loads(ran.stdout)
        report['passed'] = report['metrics']['passed']
        if args.verifier_only:
            current_main = main_path.read_text(encoding='utf-8-sig')
            if any(function(current_main, name) != function(main_source, name) for name in helper_names):
                raise RuntimeError('production identity helper changed during execution')
        # Fail if shared source changes while compiling/executing.
        if source_path.read_bytes() != source or sha(Path(__file__).read_bytes()) != report['sha256']['selftest']:
            raise RuntimeError('tested source changed during execution')
    except Exception as error:
        report['passed'] = False
        report['failedChecks'].append(str(error))
    report['totalMs'] = (time.perf_counter() - total_start) * 1000
    (out / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'passed': report['passed'], 'report': str(out / 'report.json'), 'failedChecks': report['failedChecks'],
                      'metrics': report.get('metrics')}, ensure_ascii=False))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
