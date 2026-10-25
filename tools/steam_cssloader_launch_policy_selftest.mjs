import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),out=process.argv[2];if(!out)throw Error('Explicit evidence output required');fs.mkdirSync(out,{recursive:true});const source=fs.readFileSync(path.join(repo,'native/steam_cssloader_launch.h'),'utf8');const start=source.indexOf('inline deckylaunch::Result create('),open=source.indexOf('{',start);if(start<0||open<0)throw Error('Production launch function missing');let depth=1,end=open+1;while(depth){if(source[end]==='{')depth++;else if(source[end]==='}')depth--;if(++end>source.length)throw Error('Unbalanced launch source');}const actual=source.slice(start,end);
// Both Ensure (which may create CSS) and Automatic must use the same token route.
const use=fs.readFileSync(path.join(repo,'native/steam_cssloader_use.h'),'utf8');
const begin=use.indexOf('HANDLE launchLocked('),finish=use.indexOf('void startAutomaticLocked(',begin);
if(begin<0||finish<0)throw Error('CSS runtime launch route missing');
const route=use.slice(begin,finish);
const acceptsRoute=s=>/const auto launch = steamcsslaunch::create\(/.test(s)&&!s.includes('deckylaunch::create(');
if(!acceptsRoute(route))throw Error('Ensure and Automatic must retain YMCC token');
const downgraded=route.replace('const auto launch = steamcsslaunch::create(', 'const auto launch = automatic ? steamcsslaunch::create(')+' deckylaunch::create(';
if(acceptsRoute(downgraded))throw Error('Ensure-only downgrade negative was not detected');
fs.writeFileSync(path.join(out,'runtime-route-result.json'),JSON.stringify({ok:true,checks:2,ensureAndAutomaticCurrentToken:true,ensureDowngradeNegativeDetected:true},null,2));
const prefix=String.raw`
#include <string>
#include <vector>
#include <cstring>
#include <iostream>
#include <fstream>
#include <cstdint>
using DWORD=unsigned long;using HANDLE=uintptr_t;using BOOL=int;
constexpr DWORD TOKEN_QUERY=8,ERROR_ACCESS_DENIED=5,CREATE_SUSPENDED=4,CREATE_NO_WINDOW=0x08000000,CREATE_UNICODE_ENVIRONMENT=0x400,WAIT_OBJECT_0=0;
constexpr BOOL FALSE=0;enum TOKEN_INFORMATION_CLASS{TokenElevation,TokenIntegrityLevel};
struct TOKEN_ELEVATION{DWORD TokenIsElevated=0;};struct TOKEN_MANDATORY_LABEL{struct{void* Sid=nullptr;}Label;};struct STARTUPINFOW{};struct PROCESS_INFORMATION{HANDLE hProcess=0,hThread=0;DWORD dwProcessId=0,dwThreadId=0;};
struct State{bool callerElevated=false,childElevated=false,callerOpen=true,childOpen=true,query=true,integrity=true,user=true,session=true,logon=true,create=true,terminate=true,wait=true;unsigned creates=0,terminated=0,applied=0,closed=0;DWORD flags=0;};static State state;
static HANDLE GetCurrentProcess(){return 1;}static DWORD GetLastError(){return 5;}
static bool OpenProcessToken(HANDLE process,DWORD,HANDLE* out){if((process==1&&!state.callerOpen)||(process==2&&!state.childOpen))return false;*out=process==1?3:4;return true;}
static bool GetTokenInformation(HANDLE token,TOKEN_INFORMATION_CLASS,void* data,DWORD,DWORD*size){if(!state.query)return false;static_cast<TOKEN_ELEVATION*>(data)->TokenIsElevated=token==3?state.callerElevated:state.childElevated;*size=sizeof(TOKEN_ELEVATION);return true;}
static bool EqualSid(void*a,void*b){return a==b;}static bool CloseHandle(HANDLE){state.closed++;return true;}static bool TerminateProcess(HANDLE,DWORD){state.terminated++;return state.terminate;}static DWORD WaitForSingleObject(HANDLE,DWORD){return state.wait?0:258;}
namespace ymcc::deckylaunch{struct Result{bool created=false;DWORD error=0;std::string stage,mode;};struct Handle{HANDLE value=0;~Handle(){if(value)CloseHandle(value);}};
inline std::vector<unsigned char> tokenData(HANDLE token,TOKEN_INFORMATION_CLASS){std::vector<unsigned char>b(sizeof(TOKEN_MANDATORY_LABEL));TOKEN_MANDATORY_LABEL row;row.Label.Sid=reinterpret_cast<void*>(token==4&&!state.integrity?2:1);std::memcpy(b.data(),&row,sizeof(row));return b;}
inline bool equalUser(HANDLE,HANDLE){return state.user;}inline bool sameSession(HANDLE,HANDLE){return state.session;}inline bool sameLogon(HANDLE,HANDLE){return state.logon;}}
namespace ymcc::processEfficiency{inline bool createProcess(const wchar_t*,wchar_t*,void*,void*,BOOL,DWORD flags,wchar_t*,const wchar_t*,STARTUPINFOW*,PROCESS_INFORMATION*out){state.creates++;state.flags=flags;if(!state.create)return false;out->hProcess=2;out->hThread=5;return true;}inline void applyOwnedChild(HANDLE){state.applied++;}}
namespace ymcc::steamcsslaunch{
`;
const suffix=String.raw`
}
static unsigned checks=0;static void check(bool ok,const char*text){if(!ok)throw std::runtime_error(text);++checks;}
static ymcc::deckylaunch::Result run(){STARTUPINFOW s;PROCESS_INFORMATION child;std::wstring command=L"inert";return ymcc::steamcsslaunch::create(L"inert",command,nullptr,L"inert",s,child);}
int main(int argc,char**argv){try{if(argc!=2)throw std::runtime_error("output required");for(bool elevated:{false,true}){state={};state.callerElevated=elevated;state.childElevated=elevated;const auto r=run();check(r.created&&r.mode=="ymcc-current-token"&&state.creates==1&&state.applied==1,"actual consumer preserves regular and elevated caller tokens");check(state.flags==(CREATE_SUSPENDED|CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT),"actual helper creation stays hidden and suspended");}
state={};state.callerElevated=true;state.childElevated=false;auto r=run();check(!r.created&&r.stage=="css-child-token-verification"&&state.terminated==1&&state.applied==0,"downgraded child is rejected and retired");
state={};state.callerElevated=false;state.childElevated=true;r=run();check(!r.created&&state.terminated==1,"unexpected elevation is equally rejected");
for(int fault=0;fault<4;fault++){state={};if(fault==0)state.user=false;if(fault==1)state.session=false;if(fault==2)state.logon=false;if(fault==3)state.integrity=false;r=run();check(!r.created&&state.terminated==1&&state.applied==0,"changed child identity or integrity is rejected");}
state={};state.callerOpen=false;r=run();check(!r.created&&state.creates==0&&r.stage=="css-caller-token-open","unreadable caller cannot create helper");
state={};state.query=false;r=run();check(!r.created&&state.creates==0,"unknown caller elevation cannot create helper");
state={};state.childOpen=false;r=run();check(!r.created&&state.terminated==1,"unreadable child identity is retired before resume");
state={};state.create=false;r=run();check(!r.created&&state.creates==1&&state.terminated==0&&r.stage=="css-create-process-current-token","creation failure has one attempt and no downgrade fallback");
state={};state.session=false;state.wait=false;r=run();check(!r.created&&r.stage=="css-child-retirement-unconfirmed","unconfirmed destruction retains failure rather than success");
std::ofstream(argv[1])<<"{\"ok\":true,\"passed\":"<<checks<<",\"actualConsumer\":true,\"elevatedAndRegularTokenTerminals\":true,\"realNativeEffects\":false}";std::cout<<"CSS_POLICY_OK checks="<<checks<<'\n';return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
`;
fs.writeFileSync(path.join(out,'policy.cpp'),prefix+actual+suffix);console.log('Extracted exact CSS launch consumer for inert token cases.');
