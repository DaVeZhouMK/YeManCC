// Exact target selection / ownership loops / target unhide loops, fake boundaries only.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const [beforeArg,afterArg,outArg]=process.argv.slice(2);if(!beforeArg||!afterArg||!outArg)throw Error('usage: node msi_refresh_ownership_selftest.mjs before-main.cpp after-main.cpp fresh-output');
const out=path.resolve(outArg);if(fs.existsSync(out))throw Error('Use a fresh output directory');fs.mkdirSync(out,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function end(t,start){let d=0,m='code';for(let i=start;i<t.length;i++){let c=t[i],n=t[i+1];if(m==='line'){if(c==='\n')m='code';continue;}if(m==='block'){if(c==='*'&&n==='/'){m='code';i++;}continue;}if(m==='string'||m==='char'){if(c==='\\'){i++;continue;}if(c===(m==='string'?'"':"'"))m='code';continue;}if(c==='/'&&n==='/'){m='line';i++;continue;}if(c==='/'&&n==='*'){m='block';i++;continue;}if(c==='"'||c==="'"){m=c==='"'?'string':'char';continue;}if(c==='{')d++;else if(c==='}'&&--d===0)return i+1;}throw Error('Unclosed brace');}
function fn(t,name){const re=new RegExp('^static[^\\n;{]*\\b'+name+'\\s*\\(','mg');let m;while((m=re.exec(t))){const a=t.indexOf('{',m.index),s=t.indexOf(';',m.index);if(a<0||(s>=0&&s<a))continue;return t.slice(m.index,end(t,a));}throw Error('Missing '+name);}
function loop(t,anchor,required){const found=[];for(let a=t.indexOf(anchor);a>=0;a=t.indexOf(anchor,a+anchor.length)){const body=t.slice(a,end(t,t.indexOf('{',a)));if(body.includes(required))found.push(body);}if(found.length!==1)throw Error('Loop selector not unique '+required);return found[0];}
function en(t,name){const a=t.indexOf('enum class '+name+' : int {');if(a<0)throw Error('Enum anchor');return t.slice(a,end(t,t.indexOf('{',a)))+';';}
const sources=[{source:fileURLToPath(import.meta.url),sha256:sha(fs.readFileSync(fileURLToPath(import.meta.url)))},{source:path.join(repo,'deps/json/json.hpp'),sha256:sha(fs.readFileSync(path.join(repo,'deps/json/json.hpp')))}];const meta=[];let cpp='#include <string>\n#include <vector>\n#include <algorithm>\n#include <cctype>\n#include <iostream>\n#include <fstream>\n#include <stdexcept>\n#include "json.hpp"\nusing json=nlohmann::json;\n';
const common=String.raw`
std::string W2U(const std::wstring&p){std::string result;for(auto c:p){if(c<0||c>127)throw std::runtime_error("ASCII fixture path required");result.push_back(static_cast<char>(c));}return result;}std::wstring U2W(const std::string&p){return std::wstring(p.begin(),p.end());}
std::string trim_ascii(std::string s){auto a=s.find_first_not_of(" \r\n\t"),b=s.find_last_not_of(" \r\n\t");return a==std::string::npos?std::string{}:s.substr(a,b-a+1);}std::string ascii_lower(std::string s){for(auto&c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}std::string hidHideUpper(std::string s){for(auto&c:s)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));return s;}
struct HidHidePhysicalIdentity{std::string deviceInstanceId,baseContainerDeviceInstanceId;};
struct HidHideVisibilitySnapshot{std::vector<std::wstring>hidden;};struct Journal{std::vector<std::wstring>hiddenByUs;HidHideVisibilitySnapshot before;}g_hidHideVisibilityJournal;
std::vector<std::wstring> hidden,successfulHides;std::vector<std::string> calls;unsigned failMask=0;const std::wstring B=L"USB\\VID_0DB0&PID_1902\\BASE",P=L"HID\\VID_0DB0&PID_1902&MI_00&COL01\\PAD",F=L"HID\\OTHER-OWNER\\KEEP";
bool hidHideIsVirtualIdentity(const std::string&p,const std::string&){return p.find("VIRTUAL")!=std::string::npos;}
bool has(const std::vector<std::wstring>&a,const std::wstring&p){return std::any_of(a.begin(),a.end(),[&](const auto&v){return ascii_lower(W2U(v))==ascii_lower(W2U(p));});}
bool hide(const std::wstring&p){calls.push_back("hide:"+W2U(p));if(((p==B?1u:p==P?2u:0u)&failMask)!=0)return false;if(!has(hidden,p))hidden.push_back(p);successfulHides.push_back(p);return true;}
bool unhide(const std::wstring&p,const char*route){calls.push_back(std::string(route)+":"+W2U(p));std::erase_if(hidden,[&](const auto&v){return ascii_lower(W2U(v))==ascii_lower(W2U(p));});return true;}
bool inputHostHidHideApplyViaPipeLocked(const std::wstring&op,const std::wstring&p){if(op!=L"hide")throw std::runtime_error("unexpected mutation");return hide(p);}
bool hidHideRun(const std::wstring&cmd){auto a=cmd.find(L'"'),b=cmd.rfind(L'"');if(a==std::wstring::npos||b<=a)throw std::runtime_error("bad fake CLI");auto p=cmd.substr(a+1,b-a-1);if(cmd.starts_with(L"--dev-hide "))return hide(p);if(cmd.starts_with(L"--dev-unhide "))return unhide(p,"cli-unhide");throw std::runtime_error("unexpected CLI");}
bool inputHostRequestLocked(const char*cmd,const json&payload,const char*,int,json*){if(std::string(cmd)!="HIDHIDE_UNHIDE")throw std::runtime_error("unexpected pipe");return unhide(U2W(payload.at("path").get<std::string>()),"pipe-unhide");}
void seed(int family,unsigned hiddenMask,unsigned ownedMask,unsigned failures){g_machineIdentity.family=static_cast<YmccFamily>(family);(void)ymccFamilyInterfacePolicy(g_machineIdentity.family);g_hidHideVisibilityJournal={};hidden={F};successfulHides.clear();calls.clear();failMask=failures;for(unsigned i=0;i<2;++i){auto p=i?P:B;if(hiddenMask&(1u<<i))hidden.push_back(p);if(ownedMask&(1u<<i))g_hidHideVisibilityJournal.hiddenByUs.push_back(p);}g_hidHideVisibilityJournal.before.hidden=hidden;}
json snapshot(){return json{{"hidden",hidden},{"owned",g_hidHideVisibilityJournal.hiddenByUs},{"calls",calls},{"success",successfulHides}};}
`;
for(const [ns,arg] of [['reference',beforeArg],['candidate',afterArg]]){
 const source=path.resolve(arg),bytes=fs.readFileSync(source),t=bytes.toString('utf8');sources.push({source,sha256:sha(bytes)});
 const defs=[en(t,'YmccFamily'),en(t,'YmccInterfacePolicy'),fn(t,'ymccFamilyInterfacePolicy')];
 const helpers=[fn(t,'hidHideContainsPath'),fn(t,'hidHideAppendTarget'),fn(t,'hidHidePhysicalTargets')];
 const apply=loop(fn(t,'hidHideApplyPhysicalTransaction'),'    for (const auto& target : targets) {','if (carriedInherited)');
 const refresh=loop(fn(t,'hidHideRefreshPhysicalHide'),'    for (const auto& target : targets) {','const bool alreadyHidden');
 const live=loop(fn(t,'inputHostRestoreVisibilityViaPipeLocked'),'    for (const auto& path : journal.hiddenByUs) {','HIDHIDE_UNHIDE');
 const cli=loop(fn(t,'hidHideRestorePhysicalTransaction'),'        for (const auto& path : journal.hiddenByUs) {','++cliFallbackCommands');
 const actual=defs.join('\n')+'\nstruct Machine { YmccFamily family=YmccFamily::Unknown; } g_machineIdentity;\n'+common+'\n'+helpers.join('\n')+`
std::vector<std::string> targets(){return hidHidePhysicalTargets(HidHidePhysicalIdentity{W2U(P),W2U(B)});}
bool initial(bool carriedInherited=false,const std::vector<std::wstring>&carriedTargets={}){auto targets=::${ns}::targets();HidHideVisibilitySnapshot before{hidden};bool ok=true;${apply}return ok;}
bool refresh(){auto targets=::${ns}::targets();HidHideVisibilitySnapshot before{hidden};bool ok=true;${refresh}return ok;}
bool restore(bool usePipe){auto&journal=g_hidHideVisibilityJournal;if(usePipe){bool pipeOk=true;${live}return pipeOk;}bool cliFallbackAllOk=true;int cliFallbackCommands=0;${cli}(void)cliFallbackCommands;return cliFallbackAllOk;}
`;
 fs.writeFileSync(path.join(out,ns+'.inc'),actual);meta.push({namespace:ns,applyLoopSha256:sha(Buffer.from(apply)),refreshLoopSha256:sha(Buffer.from(refresh)),liveTargetRestoreLoopSha256:sha(Buffer.from(live)),cliTargetRestoreLoopSha256:sha(Buffer.from(cli)),referenceHasMsiGuard:refresh.includes('ymccFamilyInterfacePolicy')});cpp+=`namespace ${ns}{\n#include "${ns}.inc"\n}\n`;
}
cpp+=String.raw`
int main(int argc,char**argv){try{unsigned long long checks=0,cases=0,nonMsi=0,witnesses=0;auto check=[&](bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);};
const int msi=static_cast<int>(candidate::YmccFamily::MsiClaw);
for(int family=-2;family<32;++family)for(unsigned h=0;h<4;++h)for(unsigned owned=0;owned<4;++owned)for(unsigned failure=0;failure<4;++failure)for(bool pipe:{false,true}){
 ++cases;reference::seed(family,h,owned,failure);candidate::seed(family,h,owned,failure);const bool a=reference::refresh(),b=candidate::refresh();check(a==b,"hide result changed");check(reference::calls==candidate::calls,"hide I/O changed");check(reference::hidden==candidate::hidden,"hide state changed");
 if(family!=msi){++nonMsi;check(reference::snapshot()==candidate::snapshot(),"non-MSI ownership changed");}
 else{for(unsigned bit=0;bit<2;++bit){auto p=bit?candidate::P:candidate::B;bool expected=(owned&(1u<<bit))!=0||candidate::has(candidate::successfulHides,p);check(candidate::has(candidate::g_hidHideVisibilityJournal.hiddenByUs,p)==expected,"MSI claimed unowned prehidden target or lost ownership");}}
 auto preReturn=candidate::hidden;auto expectedOwned=candidate::g_hidHideVisibilityJournal.hiddenByUs;auto refBefore=reference::g_hidHideVisibilityJournal.hiddenByUs;reference::calls.clear();candidate::calls.clear();check(reference::restore(pipe)&&candidate::restore(pipe),"target restore failed");check(candidate::has(candidate::hidden,candidate::F),"unrelated third-party path removed");
 if(family!=msi){check(reference::snapshot()==candidate::snapshot(),"non-MSI target restore changed");}
 else{for(auto p:{candidate::B,candidate::P}){const bool keep=candidate::has(preReturn,p)&&!candidate::has(expectedOwned,p);check(candidate::has(candidate::hidden,p)==keep,"MSI third-party target not preserved / owned target not returned");if(keep&&candidate::has(refBefore,p)&&!reference::has(reference::hidden,p))++witnesses;}}
}
// Actual initial apply -> actual refresh -> actual target restore, both transports.
for(bool pipe:{false,true})for(unsigned foreignMask:{1u,2u,3u}){reference::seed(msi,foreignMask,0,0);candidate::seed(msi,foreignMask,0,0);check(reference::initial()&&candidate::initial(),"initial apply failed");check(reference::snapshot()==candidate::snapshot(),"initial apply changed");check(candidate::refresh()&&reference::refresh(),"refresh failed");check(candidate::restore(pipe)&&reference::restore(pipe),"restore failed");for(unsigned i=0;i<2;++i)check(candidate::has(candidate::hidden,i?candidate::P:candidate::B)==((foreignMask&(1u<<i))!=0),"full target lifecycle removed foreign baseline");}
for(bool pipe:{false,true}){candidate::seed(msi,1,0,0);check(candidate::initial(true,{candidate::B}),"carried initial failed");check(candidate::has(candidate::g_hidHideVisibilityJournal.hiddenByUs,candidate::B),"carried path not admitted");check(candidate::refresh()&&candidate::restore(pipe),"carried lifecycle failed");check(!candidate::has(candidate::hidden,candidate::B),"carried-owned prehidden path not returned");}
json result={{"pass",true},{"cases",cases},{"checks",checks},{"nonMsiDifferentialCases",nonMsi},{"oldBugWitnesses",witnesses},{"deviceCalls",0},{"wholeLifecycleExecuted",false},{"scope","Exact selector and apply/refresh/pipe+CLI target loops, injected in-memory boundaries; admission, full restore, driver, mode, reboot and consumer behavior not executed"}};if(argc>1)std::ofstream(argv[1])<<result.dump(2);std::cout<<result.dump()<<'\n';return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
`;
fs.writeFileSync(path.join(out,'Fixture.cpp'),cpp);fs.copyFileSync(path.join(repo,'deps/json/json.hpp'),path.join(out,'json.hpp'));
const profiles=[];for(const [profile,flags] of [['Release','/O2 /DNDEBUG /MT'],['Checked','/Od /RTC1 /D_DEBUG /MTd']]){
 const cmd=`@echo off\r\nset VSLANG=1033\r\ncall "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX ${flags} Fixture.cpp /Fo${profile}.obj /Fe${profile}.exe /Fd${profile}.pdb\r\nexit /b %errorlevel%\r\n`;fs.writeFileSync(path.join(out,profile+'.cmd'),cmd);
 for(const [command,args,log] of [['cmd.exe',['/d','/c',profile+'.cmd'],profile+'-build.log'],[path.join(out,profile+'.exe'),[profile+'-result.json'],profile+'-run.log']]){
  const r=spawnSync(command,args,{cwd:out,encoding:'utf8',windowsHide:true,timeout:120000,maxBuffer:4*1024*1024});fs.writeFileSync(path.join(out,log),(r.stdout||'')+(r.stderr||''));if(r.error||r.status!==0)throw Error(log+': '+(r.error||r.status));}
 const r=JSON.parse(fs.readFileSync(path.join(out,profile+'-result.json'),'utf8'));profiles.push({profile,...r});console.log(profile,r.cases,'cases',r.checks,'checks',r.oldBugWitnesses,'old-bug witnesses');}
for(const s of sources)if(sha(fs.readFileSync(s.source))!==s.sha256)throw Error('source drift '+s.source);
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify({pass:true,sources,extracted:meta,profiles,clockOrDeviceCalls:0,sourceStableDuringRun:true},null,2)+'\n');
