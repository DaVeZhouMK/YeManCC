// ZIP raw fixture + pure C++ policy + extracted current refresh ownership loop.
// No product, Host, SDK, real device/registry/service or live API is executed.
// Arguments: authorized R9 evidence root, original ZIP, corrected ownership main.cpp,
// optional corrected candidate main.cpp, then --integration-binding <binding.json>.
// Rebuild the first stage after fixture/source changes; --integration-only requires
// existing first-stage results and hashes matching this generated fixture exactly.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';import {createHash} from 'node:crypto';import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),root=path.resolve(process.argv[2]||''),zip=path.resolve(process.argv[3]||'');
assert.ok(root.toLowerCase().startsWith('g:\\yemancc-work\\build\\validation\\msi-controller-r9-20261013\\msi-auxiliary-cloak'),'authorized evidence directory required');assert.ok(fs.statSync(zip).isFile(),'ZIP fixture required');
const ownershipSource=path.resolve(process.argv[4]||root+'/ownership-main-before.cpp');
const hash=b=>createHash('sha256').update(b).digest('hex').toUpperCase();const opts={encoding:'utf8',timeout:60000,windowsHide:true,maxBuffer:30*1024*1024};
// Validate the explicit candidate/binding before fixture writes or compilation.
// Lifecycle integration can supply its independent binding without changing the
// original parent integration-binding.json or treating presence as readiness.
const integrationOptions=process.argv.slice(6);let integrationOnly=false;
let integrationBindingPath=path.resolve(root+'/integration-binding.json');
for(let i=0;i<integrationOptions.length;++i){
 const option=integrationOptions[i];
 if(option==='--integration-only'){assert.ok(!integrationOnly,'duplicate integration-only option');integrationOnly=true;}
 else if(option==='--integration-binding'){
  const value=integrationOptions[++i];assert.ok(value&&!value.startsWith('--'),'binding path required');integrationBindingPath=path.resolve(value);
 }else throw new Error('unknown selftest option: '+option);
}
assert.ok(!integrationOnly||process.argv[5],'candidate path required for integration-only');
const integrationInput=process.argv[5]?(()=>{
 const candidatePath=path.resolve(process.argv[5]);const candidateBytes=fs.readFileSync(candidatePath);
 const parentBindingBytes=fs.readFileSync(integrationBindingPath),parentBinding=JSON.parse(parentBindingBytes.toString('utf8').replace(/^\uFEFF/,''));
 assert.match(parentBinding.candidateSha256||'',/^[a-f0-9]{64}$/i,'binding must provide exact candidateSha256');
 assert.equal(hash(candidateBytes),parentBinding.candidateSha256.toUpperCase(),'exact frozen parent candidate binding before execution');
 return {candidatePath,candidateBytes,candidateText:candidateBytes.toString('utf8').replaceAll('\r\n','\n'),parentBinding,parentBindingBytes};
})():null;
const out=root+'/tests';fs.mkdirSync(out,{recursive:true});
const zipReader=String.raw`param([string]$Zip,[string]$Out)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=[IO.Compression.ZipFile]::OpenRead($Zip)
try {
 $names=@('virtual-gamepad.log','msi-controller-visibility.log','native-detail.log')
 $manifest=@()
 foreach($name in $names){
  $entries=@($archive.Entries|Where-Object {($_.FullName.Replace('\','/').Split('/')[-1]) -eq $name})
  if($entries.Count -ne 1){throw "Member not unique: $name"}
  $entry=$entries[0]
  $entryStream=$entry.Open();$memory=[IO.MemoryStream]::new()
  try{$entryStream.CopyTo($memory);$bytes=$memory.ToArray()}finally{$entryStream.Dispose();$memory.Dispose()}
  $p=Join-Path $Out $name;[IO.File]::WriteAllBytes($p,$bytes)
  $hasher=[Security.Cryptography.SHA256]::Create()
  try{$digest=[BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-','')}finally{$hasher.Dispose()}
  $manifest+=[ordered]@{member=$entry.FullName;rawPath=$p;bytes=$bytes.Length;sha256=$digest}
 }
 [IO.File]::WriteAllText((Join-Path $Out 'zip-members.json'),($manifest|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
}finally{$archive.Dispose()}`;
fs.mkdirSync(out+'/raw',{recursive:true});fs.writeFileSync(out+'/read-zip.ps1',zipReader);
const unpack=spawnSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',out+'/read-zip.ps1','-Zip',zip,'-Out',out+'/raw'],opts);fs.writeFileSync(out+'/zip-read.log',(unpack.stdout||'')+(unpack.stderr||''));assert.equal(unpack.status,0,'read ZIP raw only');
function rows(name){return fs.readFileSync(out+'/raw/'+name,'utf8').split(/\r?\n/).map((s,i)=>({line:i+1,text:s})).filter(x=>x.text.trim()).map(x=>({line:x.line,row:JSON.parse(x.text)}));}
const visibility=rows('virtual-gamepad.log'),native=rows('native-detail.log'),observer=rows('msi-controller-visibility.log');
const snapshot=visibility.find(x=>x.row.event==='hidhide.visibility-snapshot'&&x.row.targetInventory?.some(r=>r.miCol==='MI_00&COL02'));
assert.ok(snapshot);const inv=snapshot.row.targetInventory;const main=inv.find(r=>r.miCol==='MI_00&COL01'),vendor=inv.find(r=>r.miCol==='MI_00&COL02'),consumer=inv.find(r=>r.miCol==='MI_01&COL03');assert.ok(main&&vendor&&consumer);
assert.equal(vendor.usagePage,65520);assert.equal(vendor.usage,64);assert.equal(consumer.usagePage,12);assert.equal(consumer.usage,1);
const members=[vendor,consumer].map(r=>{assert.ok(snapshot.row.excludedNonGameTargets.includes(r.instance));const matches=observer.filter(x=>x.row.event==='directinput-device'&&(x.row.pnpInstanceId||'').toUpperCase()===r.instance.toUpperCase());assert.equal(matches.length,184,'exact observed child count');for(const x of matches){assert.equal(x.row.hidVid,0x0DB0);assert.equal(x.row.hidPid,0x1902);assert.equal(x.row.hidUsagePage,r.usagePage);assert.equal(x.row.hidUsage,r.usage);assert.equal(x.row.hidCapabilitiesKnown,true);assert.equal(x.row.stateApiSucceeded,true);assert.deepEqual(x.row.productUtf16,[0x0609]);assert.ok(x.row.usbRootInstance);}const roots=[...new Set(matches.map(x=>x.row.usbRootInstance.toUpperCase()))];assert.equal(roots.length,1);return {record:r,root:roots[0],observerLines:matches.map(x=>x.line)};});
assert.equal(members[0].root,members[1].root);const usbRoot=members[0].root;
const shadow=native.find(x=>x.row.event==='hidhide.msi-source-shadow'&&x.row.candidates?.some(c=>c.deviceInstanceId===main.instance&&c.baseContainerDeviceInstanceId===usbRoot));assert.ok(shadow);
const candidate=shadow.row.candidates.find(c=>c.deviceInstanceId===main.instance);assert.equal(candidate.admitted,true);assert.equal(shadow.row.discoveredSources,1);
const sourceRows=native.filter(x=>x.row.sourceRecord?.valid&&x.row.sourceRecord.interfacePath?.toLowerCase()===main.interfacePath.toLowerCase());
const before=sourceRows.filter(x=>x.row.time<=snapshot.row.time).at(-1),after=sourceRows.find(x=>x.row.time>snapshot.row.time);assert.ok(before&&after);
const identity=x=>({valid:true,ancestryParsed:true,instanceId:main.instance,baseInstanceId:usbRoot,physicalParentId:usbRoot,sourceToken:x.row.sourceRecord.interfacePath,vid:main.vid,pid:x.row.sourceRecord.productId,powerGeneration:x.row.powerGeneration,bindGeneration:x.row.sourceRecord.generation});
assert.deepEqual(identity(before),identity(after));
const records=inv.map(r=>{const aux=members.find(x=>x.record.instance===r.instance);const isMain=r.instance===main.instance;return {instanceId:r.instance,baseInstanceId:aux?.root||(isMain?usbRoot:''),physicalParentId:aux?.root||(isMain?usbRoot:''),miCol:r.miCol,vid:r.vid,pid:r.pid,usagePage:r.usagePage,usage:r.usage,ancestryParsed:!!aux||isMain,capsKnown:true,virtualIdentity:false};});
const fixture={facts:{authoritativeMsiFamily:true,virtualEnabled:true,uniqueManagedSource:true,inventoryReadComplete:true,before:identity(before),after:identity(after),inventory:records},expectedIds:[vendor.instance,consumer.instance],provenance:{zip,zipSha256:hash(fs.readFileSync(zip)),snapshotLine:snapshot.line,sourceShadowLine:shadow.line,sourceBeforeLine:before.line,sourceAfterLine:after.line,observerMembers:members.map(x=>({id:x.record.instance,root:x.root,rows:x.observerLines.length,firstLine:x.observerLines[0]})),assumptionsForPurePositiveCase:['authoritative MSI family gate supplied by caller','inventoryReadComplete=true is a synthetic required fact; log inventory alone cannot prove completion','log source tuple bracket is a fixture; not a production inventory transaction or simultaneous snapshot'],sdlAliasMapping:'unknown',hidhideCliDriverAcceptsAuxiliary:'unverified'}};
fs.writeFileSync(out+'/log-fixture.json',JSON.stringify(fixture,null,2));
const production=fs.readFileSync(ownershipSource,'utf8').replaceAll('\r\n','\n');
// Mask comments/literals while preserving offsets/line breaks, so extracted
// ACTUAL function/type spans cannot be cut by braces in comments or log text.
function cppCodeMask(text){
 const chars=text.split('');
 const blank=(start,end)=>{for(let i=start;i<end;++i)if(chars[i]!=='\n'&&chars[i]!=='\r')chars[i]=' ';};
 for(let i=0;i<text.length;){
  if(text.startsWith('//',i)){const end=text.indexOf('\n',i+2);const next=end<0?text.length:end;blank(i,next);i=next;continue;}
  if(text.startsWith('/*',i)){const end=text.indexOf('*/',i+2);assert.ok(end>=0,'closed C++ block comment');blank(i,end+2);i=end+2;continue;}
  if(text.startsWith('R"',i)){
   const open=text.indexOf('(',i+2);assert.ok(open>=0&&open-i<=18,'valid C++ raw literal delimiter');
   const terminator=')'+text.slice(i+2,open)+'"',end=text.indexOf(terminator,open+1);assert.ok(end>=0,'closed C++ raw literal');
   blank(i,end+terminator.length);i=end+terminator.length;continue;
  }
  if(text[i]==='"'||text[i]==="'"){
   // Decimal/hex numeric separators are code, not character literals.
   if(text[i]==="'"&&/[0-9a-f]/i.test(text[i-1]||'')&&/[0-9a-f]/i.test(text[i+1]||'')){++i;continue;}
   const quote=text[i],start=i++;let closed=false;
   while(i<text.length){if(text[i]==='\\'){i+=2;continue;}if(text[i++]===quote){closed=true;break;}}
   assert.ok(closed,'closed C++ string/character literal');blank(start,i);continue;
  }
  ++i;
 }
 return chars.join('');
}
function blockFromOffset(text,start,mask=cppCodeMask(text)){
 const open=mask.indexOf('{',start);assert.ok(open>=start,'actual body opening');let depth=1;
 for(let i=open+1;i<mask.length;++i){if(mask[i]==='{')++depth;if(mask[i]==='}'&&--depth===0)return text.slice(start,i+1);}
 throw new Error('unbalanced actual C++ body');
}
function block(text,start){const offset=text.indexOf(start);assert.ok(offset>=0,start);return blockFromOffset(text,offset);}
function actualDefinition(text,name){
 assert.match(name,/^[A-Za-z_]\w*$/,'exact C++ function name');const mask=cppCodeMask(text);
 const rx=new RegExp('^static[^;{]*\\b'+name+'\\s*\\(','gm');const bodies=[];
 for(const hit of mask.matchAll(rx)){
  let depth=1,i=hit.index+hit[0].length;
  for(;i<mask.length&&depth;++i){if(mask[i]==='(')++depth;if(mask[i]===')')--depth;}
  assert.equal(depth,0,'closed actual function arguments: '+name);
  const tail=/^\s*(?:noexcept\s*)?\{/.exec(mask.slice(i));if(!tail)continue;
  bodies.push(blockFromOffset(text,hit.index,mask.slice(0,i).replaceAll('{',' ').replaceAll('}',' ')+mask.slice(i)));
 }
 assert.equal(bodies.length,1,'one actual definition: '+name);return bodies[0];
}
function actualType(text,name,prefix='struct'){
 assert.match(name,/^[A-Za-z_]\w*$/,'exact C++ type name');assert.ok(prefix==='struct'||prefix==='enum class','supported exact C++ type');
 const mask=cppCodeMask(text),rx=new RegExp('\\b'+prefix.replaceAll(' ','\\s+')+'\\s+'+name+'\\b[^;{}]*\\{','g');
 const hits=[...mask.matchAll(rx)];assert.equal(hits.length,1,'one actual type definition: '+name);return blockFromOffset(text,hits[0].index,mask);
}
function actualSpanBinding(text,span){const start=text.indexOf(span);assert.ok(start>=0,'bound exact extracted span');return {sha256:hash(span),line:text.slice(0,start).split('\n').length,bytes:Buffer.byteLength(span),normalization:'CRLF-to-LF; exact actual source span'};}
const contains=block(production,'static bool hidHideContainsPath(');
const refreshBegin=production.indexOf('        const bool alreadyHidden = hidHideContainsPath(before.hidden, target);');assert.ok(refreshBegin>0);const loopStart=production.lastIndexOf('    for (const auto& target : targets) {',refreshBegin);const loop=block(production.slice(loopStart),'    for (const auto& target : targets) {');assert.ok(loop.includes('hiddenByUs.push_back(wide)'));
// Both stages must bind the corrected loop, not the historical ownership snapshot.
assert.ok(cppCodeMask(loop).replace(/\s+/g,' ').includes('if (ok && (!alreadyHidden || ymccFamilyInterfacePolicy(g_machineIdentity.family) != YmccInterfacePolicy::MsiClawDinput)) {'),'ownership source must contain the MSI-only refresh condition');
fs.writeFileSync(out+'/actual-refresh-ownership.cpp.txt',contains+'\n'+loop);
// Family selection is an explicit fake boundary; never detect the local machine.
const refreshFamilyStub=String.raw`
enum class FixtureFamily{Msi,Rog,Gpd,Other};
enum class YmccInterfacePolicy{MsiClawDinput,Other};
static struct{FixtureFamily family=FixtureFamily::Msi;}g_machineIdentity;
static YmccInterfacePolicy ymccFamilyInterfacePolicy(FixtureFamily family){return family==FixtureFamily::Msi?YmccInterfacePolicy::MsiClawDinput:YmccInterfacePolicy::Other;}
`;
const cpp=String.raw`
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include "json.hpp"
#include "msi_virtual_auxiliary_cloak_plan.h"
using Json=nlohmann::json;using namespace ymcc::msi::auxiliaryCloak;
static unsigned cases=0,checks=0;
static void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
static SourceIdentity source(const Json&x){return {x.at("valid"),x.at("ancestryParsed"),x.at("instanceId"),x.at("baseInstanceId"),x.at("physicalParentId"),x.at("sourceToken"),x.at("vid"),x.at("pid"),x.at("powerGeneration"),x.at("bindGeneration")};}
static Facts load(const Json&j){Facts f;f.authoritativeMsiFamily=j.at("authoritativeMsiFamily");f.virtualEnabled=j.at("virtualEnabled");f.uniqueManagedSource=j.at("uniqueManagedSource");f.inventoryReadComplete=j.at("inventoryReadComplete");f.before=source(j.at("before"));f.after=source(j.at("after"));for(const auto&r:j.at("inventory"))f.inventory.push_back({r.at("instanceId"),r.at("baseInstanceId"),r.at("physicalParentId"),r.at("miCol"),r.at("vid"),r.at("pid"),r.at("usagePage"),r.at("usage"),r.at("ancestryParsed"),r.at("capsKnown"),r.at("virtualIdentity")});return f;}
static void reject(const Facts&f){++cases;const auto p=makePlan(f);check(p.targetIds.empty(),"reject must add zero targets");check(!p.reason.empty(),"reject has reason");}
static std::string ascii_lower(std::string s){for(auto&c:s)if(c>='A'&&c<='Z')c=c-'A'+'a';return s;}
static std::string trim_ascii(const std::string&s){return s;}static std::string W2U(const std::wstring&s){return {s.begin(),s.end()};}static std::wstring U2W(const std::string&s){return {s.begin(),s.end()};}
${contains}
static struct{std::vector<std::wstring>hiddenByUs;}g_hidHideVisibilityJournal;
${refreshFamilyStub}
static unsigned hideCalls=0,cliCalls=0;static bool apiOk=true,cliOk=true,lastRefreshOk=true;
static bool hidHideIsVirtualIdentity(const std::string&id,const std::string&){return id.rfind("HID\\VID_",0)!=0&&id.rfind("USB\\VID_",0)!=0;}
static bool inputHostHidHideApplyViaPipeLocked(const wchar_t*,const std::wstring&){++hideCalls;return apiOk;}
static bool hidHideRun(const std::wstring&){++cliCalls;return cliOk;}
static std::vector<std::string> actualRefresh(const std::vector<std::string>&targets,const std::vector<std::string>&beforeHidden,const std::vector<std::string>&owned){
 struct{std::vector<std::wstring>hidden;}before;for(const auto&id:beforeHidden)before.hidden.push_back(U2W(id));g_hidHideVisibilityJournal.hiddenByUs.clear();for(const auto&id:owned)g_hidHideVisibilityJournal.hiddenByUs.push_back(U2W(id));bool ok=true;hideCalls=cliCalls=0;
${loop}
 lastRefreshOk=ok;std::vector<std::string>result;for(const auto&id:g_hidHideVisibilityJournal.hiddenByUs)result.push_back(W2U(id));return result;
}
int main(int argc,char**argv){try{std::ifstream in(argv[1]);Json j;in>>j;const Facts baseline=load(j.at("facts"));const auto expected=j.at("expectedIds").get<std::vector<std::string>>();
 ++cases;const auto plan=makePlan(baseline);check(plan.targetIds==expected,"two exact IDs from real ZIP fixture");check(!plan.generationReadsAtomicTogether,"no atomic snapshot claim");check(plan.targetIds.size()==2,"never root/main/keyboard/mouse/descendant-all");
 for(unsigned index=1;index<3;++index)for(unsigned length:{200u,201u,512u}){++cases;auto f=baseline;auto&r=f.inventory[index];r.instanceId.append(length-r.instanceId.size(),'X');const auto p=makePlan(f);if(length==200){check(p.targetIds.size()==2,"200-byte auxiliary accepted");check(p.targetIds[index-1]==r.instanceId,"accepted exact auxiliary identity preserved");check(p.reason=="two-exact-same-source-auxiliary-collections","boundary success reason");}else{check(p.targetIds.empty(),"over-200 auxiliary adds zero targets");check(p.reason=="auxiliary-instance-id-exceeds-200","explicit auxiliary length rejection");}}
 {++cases;auto f=baseline;f.before.sourceToken.append(512-f.before.sourceToken.size(),'X');f.after.sourceToken=f.before.sourceToken;check(makePlan(f).targetIds==expected,"sourceToken retains 512-byte capacity");}
 {++cases;auto f=baseline;f.before.sourceToken.append(513-f.before.sourceToken.size(),'X');f.after.sourceToken=f.before.sourceToken;const auto p=makePlan(f);check(p.targetIds.empty(),"sourceToken over existing 512 bound rejects");check(p.reason=="source-identity-unknown","source token bound independent of auxiliary bound");}
 {++cases;auto f=baseline;auto&r=f.inventory[0];r.instanceId.append(201-r.instanceId.size(),'X');f.before.instanceId=f.after.instanceId=r.instanceId;check(makePlan(f).targetIds==expected,"new 200 bound does not broaden to main record");}
 for(unsigned gate=0;gate<4;++gate){auto f=baseline;if(gate==0)f.authoritativeMsiFamily=false;if(gate==1)f.virtualEnabled=false;if(gate==2)f.uniqueManagedSource=false;if(gate==3)f.inventoryReadComplete=false;reject(f);}
 for(unsigned field=0;field<11;++field){auto f=baseline;switch(field){case 0:f.after.instanceId+="X";break;case 1:f.after.baseInstanceId+="X";break;case 2:f.after.physicalParentId+="X";break;case 3:f.after.sourceToken+="X";break;case 4:++f.after.powerGeneration;break;case 5:++f.after.bindGeneration;break;case 6:f.after.valid=false;break;case 7:f.after.ancestryParsed=false;break;case 8:++f.after.vid;break;case 9:++f.after.pid;break;case 10:f.after.instanceId=ascii_lower(f.after.instanceId);break;}reject(f);}
 for(unsigned field=0;field<12;++field){auto f=baseline;for(auto*s:{&f.before,&f.after})switch(field){case 0:s->instanceId="";break;case 1:s->baseInstanceId="";break;case 2:s->physicalParentId="";break;case 3:s->sourceToken="";break;case 4:s->powerGeneration=0;break;case 5:s->bindGeneration=0;break;case 6:s->vid=0x0b05;break;case 7:s->pid=0x1901;break;case 8:s->instanceId="ROOT\\VIRTUAL\\01";break;case 9:s->sourceToken="unknown";break;case 10:s->physicalParentId="USB\\ROOT_HUB\\01";break;case 11:s->physicalParentId+="OTHER";break;}reject(f);}
 for(const auto* unknown:{"Unknown","uNrEsOlVeD","Null","nOnE","N/A"}){auto f=baseline;f.before.sourceToken=f.after.sourceToken=unknown;reject(f);}
 for(unsigned index=0;index<3;++index)for(unsigned field=0;field<12;++field){auto f=baseline;auto&r=f.inventory[index];switch(field){case 0:r.instanceId="";break;case 1:r.baseInstanceId="";break;case 2:r.physicalParentId="";break;case 3:r.ancestryParsed=false;break;case 4:r.virtualIdentity=true;break;case 5:r.capsKnown=false;break;case 6:r.vid=0x0b05;break;case 7:r.pid=0x1901;break;case 8:r.usagePage=1; r.usage=6;break;case 9:r.usage=0;break;case 10:r.miCol="MI_02&COL01";break;case 11:r.baseInstanceId+="EXTERNAL";r.physicalParentId+="EXTERNAL";break;}reject(f);}
 for(unsigned index=0;index<3;++index){auto f=baseline;f.inventory.erase(f.inventory.begin()+index);reject(f);f=baseline;f.inventory.push_back(f.inventory[index]);reject(f);f=baseline;auto alias=f.inventory[index];alias.instanceId=ascii_lower(alias.instanceId);f.inventory.push_back(alias);reject(f);f=baseline;auto other=f.inventory[index];other.instanceId+="OTHER";f.inventory.push_back(other);reject(f);}
 for(const auto*mi:{"MI_01&COL01","MI_01&COL02","MI_02&COL01","MI_02&COL02"}){auto f=baseline;auto&r=f.inventory[1];const auto at=r.instanceId.find("MI_00&COL02");r.instanceId.replace(at,11,mi);r.miCol=mi;reject(f);}
 {auto f=baseline;auto external=f.inventory[1];external.instanceId+="EXTERNAL";external.baseInstanceId+="EXTERNAL";external.physicalParentId=external.baseInstanceId;f.inventory.push_back(external);++cases;check(makePlan(f).targetIds==expected,"external different root never selected");}
 for(unsigned family=0;family<3;++family){auto f=baseline;f.authoritativeMsiFamily=false;f.before.vid=f.after.vid=family==0?0x0b05:family==1?0x2f24:0x0DB0;f.before.pid=f.after.pid=family==2?0x1901:1;reject(f);}
 // Replay the ACTUAL corrected refresh loop with fake family/apply boundaries.
 // MSI preserves only existing ownership for pre-hidden paths; auxiliary filters remain.
 for(unsigned hiddenMask=0;hiddenMask<4;++hiddenMask)for(unsigned ownedMask=0;ownedMask<4;++ownedMask)for(unsigned carriedMask=0;carriedMask<4;++carriedMask){
  ++cases;std::vector<std::string>hidden,owned,carried;for(unsigned i=0;i<2;++i){if(hiddenMask&(1<<i))hidden.push_back(expected[i]);if(ownedMask&(1<<i))owned.push_back(expected[i]);if(carriedMask&(1<<i))carried.push_back(expected[i]);}
  const auto eligible=journalEligibleTargets(plan,hidden,owned,carried);for(unsigned i=0;i<2;++i)check(containsIdentity(eligible,expected[i])==(!(hiddenMask&(1<<i))||(ownedMask&(1<<i))||(carriedMask&(1<<i))),"aux ownership eligibility exact predicate");
  auto activeOwned=owned;for(const auto&id:carried)if(containsIdentity(hidden,id)&&!containsIdentity(activeOwned,id))activeOwned.push_back(id); // Caller transfers still-hidden carried claims before refresh.
  const auto afterOwned=actualRefresh(eligible,hidden,activeOwned);for(const auto&id:activeOwned)check(containsIdentity(afterOwned,id),"MSI refresh preserves owned/transferred carried auxiliary entries");for(unsigned i=0;i<2;++i)if((hiddenMask&(1<<i))&&!(ownedMask&(1<<i))&&!(carriedMask&(1<<i)))check(!containsIdentity(afterOwned,expected[i]),"actual refresh does not claim third-party pre-hidden aux");
  unsigned expectedWrites=0;for(const auto&id:eligible)if(!containsIdentity(hidden,id))expectedWrites++;check(hideCalls==expectedWrites&&cliCalls==0,"already hidden aux never sent hide again");
 }
 // Keep the old raw-loop counterexample explicitly NON-MSI; no MSI bug expectation.
 for(const auto family:{FixtureFamily::Rog,FixtureFamily::Gpd,FixtureFamily::Other}){
  ++cases;g_machineIdentity.family=family;const auto bare=actualRefresh(expected,expected,{});check(bare==expected&&hideCalls==0&&cliCalls==0&&lastRefreshOk,"NON-MSI legacy raw-loop claims pre-hidden entries without a hide write");
  const auto eligible=journalEligibleTargets(plan,expected,{},{});check(eligible.empty(),"auxiliary third-party filter retained independently of legacy raw-loop witness");check(actualRefresh(eligible,expected,{}).empty(),"filtered NON-MSI raw-loop witness claims no auxiliary paths");
 }
 g_machineIdentity.family=FixtureFamily::Msi;
 {++cases;check(actualRefresh(expected,expected,{}).empty()&&hideCalls==0&&cliCalls==0&&lastRefreshOk,"MSI raw-loop cannot claim unowned pre-hidden auxiliaries");}
 {++cases;std::vector<std::string>hidden={ascii_lower(expected[0]),ascii_lower(expected[1])};check(journalEligibleTargets(plan,hidden,{},{}).empty(),"case alias third-party ownership remains protected");}
 {++cases;auto rejected=Plan{{},"rejected",false};check(journalEligibleTargets(rejected,{}, {},{}).empty(),"rejected plan cannot add ownership targets");}
 // Base/root and selected device are not removed by the auxiliary eligibility filter.
 const std::vector<std::string>baseTargets={baseline.before.baseInstanceId,baseline.before.instanceId};
 for(const auto&target:baseTargets){
  for(unsigned alias=0;alias<2;++alias){++cases;const auto prior=alias?ascii_lower(target):target;check(actualRefresh({target},{prior},{}).empty()&&lastRefreshOk&&hideCalls==0&&cliCalls==0,"MSI base/root or selected device pre-hidden unowned remains unowned, including case alias");}
  {++cases;const std::vector<std::string>owned={expected[0],target};check(actualRefresh({target},{target},owned)==owned&&lastRefreshOk&&hideCalls==0&&cliCalls==0,"MSI pre-hidden existing base/device and unrelated responsibility are preserved");}
  for(unsigned receipt=0;receipt<3;++receipt){
   ++cases;apiOk=receipt==0;cliOk=receipt!=2;const std::vector<std::string>owned={expected[0]};const auto afterOwned=actualRefresh({target,expected[1]}, {expected[1]},owned);
   check(containsIdentity(afterOwned,target)==(receipt!=2)&&containsIdentity(afterOwned,expected[0])&&!containsIdentity(afterOwned,expected[1]),"MSI new base/device ownership requires successful pipe or fallback hide; pre-hidden third party never added");
   check(lastRefreshOk==(receipt!=2)&&hideCalls==1&&cliCalls==(receipt==0?0u:1u),"MSI successful/fallback/failed base-device receipts and loop result");
  }
  {++cases;apiOk=cliOk=false;const std::vector<std::string>owned={expected[0],target};check(actualRefresh({target}, {},owned)==owned&&!lastRefreshOk&&hideCalls==1&&cliCalls==1,"MSI failed re-hide retains previous base/device responsibility");}
  apiOk=cliOk=true;
 }
 {++cases;apiOk=cliOk=false;const auto failed=actualRefresh(plan.targetIds,{},{});check(failed.empty()&&!lastRefreshOk&&hideCalls==1&&cliCalls==1,"failed existing apply does not claim new ownership or continue to later targets");apiOk=cliOk=true;}
 std::cout<<Json{{"passed",true},{"cases",cases},{"assertions",checks},{"exactIds",plan.targetIds},{"realApiCalls",0},{"productExecuted",false},{"driverAcceptanceProven",false},{"scope","Real ZIP identity/caps fixture; pure actual planner; actual production refresh loop extracted and replayed with fake apply/CLI boundaries"}}.dump()<<'\n';return 0;
 }catch(const std::exception&ex){std::cout<<Json{{"passed",false},{"cases",cases},{"assertions",checks},{"failure",ex.what()}}.dump()<<'\n';return 1;}}
`;
fs.writeFileSync(out+'/pure-plan-and-ownership.cpp',cpp);fs.mkdirSync(out+'/deps',{recursive:true});fs.copyFileSync(repo+'/deps/json/json.hpp',out+'/deps/json.hpp');
const command='@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"'+repo+'/native" /I"'+out+'/deps" "'+out+'/pure-plan-and-ownership.cpp" /Fo"'+out+'/pure-plan-and-ownership.obj" /Fe"'+out+'/pure-plan-and-ownership-synthetic.exe"\r\nexit /b %errorlevel%\r\n';fs.writeFileSync(out+'/compile-synthetic.cmd',command);
if(integrationOnly){
 assert.ok(process.argv[5],'candidate path required for integration-only');
 const prior=JSON.parse(fs.readFileSync(out+'/source-test-binding.json','utf8'));
 assert.equal(prior.headerSha256,hash(fs.readFileSync(repo+'/native/msi_virtual_auxiliary_cloak_plan.h')),'reuse only exact tested header');
 assert.equal(prior.generatedCppSha256,hash(cpp),'reuse only exact first-stage synthetic code');
 assert.equal(prior.fixtureSha256,hash(fs.readFileSync(out+'/log-fixture.json')),'reuse only exact first-stage fixture');
 assert.equal(prior.ownershipSourceSha256,hash(fs.readFileSync(ownershipSource)),'reuse only exact ownership snapshot');
 assert.equal(JSON.parse(fs.readFileSync(out+'/result.json','utf8')).passed,true,'existing first-stage pass required');
 const previousResult=JSON.parse(fs.readFileSync(out+'/result.json','utf8'));
 console.log('First-stage '+previousResult.cases+'/'+previousResult.assertions+' result reused with exact header/fixture/C++/ownership binding; no rerun.');
}else{
const built=spawnSync('cmd.exe',['/d','/c',out+'/compile-synthetic.cmd'],opts);fs.writeFileSync(out+'/build.log',(built.stdout||'')+(built.stderr||''));assert.equal(built.status,0,'pure C++ compile; inspect build.log');
const run=spawnSync(out+'/pure-plan-and-ownership-synthetic.exe',[out+'/log-fixture.json'],opts);fs.writeFileSync(out+'/result.json',(run.stdout||'')+(run.stderr||''));assert.equal(run.status,0,'pure C++ checks; inspect result.json');
const binding={zip,zipSha256:fixture.provenance.zipSha256,memberManifest:out+'/raw/zip-members.json',header:repo+'/native/msi_virtual_auxiliary_cloak_plan.h',headerSha256:hash(fs.readFileSync(repo+'/native/msi_virtual_auxiliary_cloak_plan.h')),ownershipSource,ownershipSourceSha256:hash(fs.readFileSync(ownershipSource)),actualContainsSha256:hash(contains),actualRefreshLoopSha256:hash(loop),refreshOwnershipCases:"MSI prehidden base/root/device unowned, owned/carried, new/fallback/failed hide; NON-MSI legacy witnesses",familyPolicyBoundary:"explicit fake MSI/ROG/GPD/other values; no real machine detection",generatedCppSha256:hash(cpp),fixtureSha256:hash(fs.readFileSync(out+'/log-fixture.json')),selftestSha256:hash(fs.readFileSync(fileURLToPath(import.meta.url))),realApiCalls:0};fs.writeFileSync(out+'/source-test-binding.json',JSON.stringify(binding,null,2));
console.log(run.stdout.trim());}

// Optional second stage: frozen ACTUAL parent wrapper + inventory, external reads
// replaced by deterministic counters/fakes. Pass candidate path as argument 5.
if(integrationInput){
 const {candidatePath,candidateBytes,candidateText,parentBinding,parentBindingBytes}=integrationInput;
 const frozenPath=out+'/wrapper-inventory-source-before.cpp';fs.writeFileSync(frozenPath,candidateBytes);
 const definition=actualDefinition;
 const typeBody=(name,prefix='struct')=>actualType(candidateText,name,prefix);
 const names=['ascii_lower','trim_ascii','hidHideUpper','hidHideIsVirtualIdentity','hidHideContainsPath','hidHideAppendTarget','hidHidePhysicalTargets','hidHideInventoryContainsInstance','hidHideBuildSourceInterfaceInventory','hidHideInverseApplicationListState','hidHideCarriedClaimTargets','hidHideCarriedMergeList','hidHideRememberMsiAuxiliaryPlan','hidHideMsiAuxiliaryOwnedInstance','hidHideVirtualPhysicalTargets'];
 const actual=Object.fromEntries(names.map(name=>[name,definition(candidateText,name)]));
 assert.equal(actual.hidHidePhysicalTargets,definition(production,'hidHidePhysicalTargets'),'shared main/root selector byte-equivalent');
 assert.equal(actual.hidHideContainsPath,contains,'ownership predicate unchanged');
 const refreshAt=candidateText.indexOf('        const bool alreadyHidden = hidHideContainsPath(before.hidden, target);');assert.ok(refreshAt>0);const refresh=block(candidateText.slice(candidateText.lastIndexOf('    for (const auto& target : targets) {',refreshAt)),'    for (const auto& target : targets) {');assert.equal(refresh,loop,'candidate refresh loop exactly matches corrected ownership snapshot');
 const carriedLine=candidateText.match(/^\s*g_hidHideVisibilityJournal\.hiddenByUs = carriedTargets;[^\n]*$/m)?.[0];assert.ok(carriedLine,'actual pre-wrapper carried transfer');
 const typeNames=['HidHidePhysicalIdentity','HidHideVisibilitySnapshot','HidHideVisibilityJournal','HidHideMsiMainSourceRecord','HidHideInterfaceRecord','HidHideMsiAuxiliaryProgress'];
 const actualTypes=Object.fromEntries(typeNames.map(n=>[n,typeBody(n)]));actualTypes.HidHideIfaceRole=typeBody('HidHideIfaceRole','enum class');
 const actualGlobalMatches=[...candidateText.matchAll(/^static HidHideMsiAuxiliaryProgress g_hidHideMsiAuxiliaryProgress;$/gm)];assert.equal(actualGlobalMatches.length,1,'exact actual progress global');
 const actualGlobals={g_hidHideMsiAuxiliaryProgress:actualGlobalMatches[0][0]};
 const records=typeNames.map(n=>actualTypes[n]+';').join('\n');
 fs.writeFileSync(out+'/actual-progress-source.cpp.txt',actualTypes.HidHideMsiAuxiliaryProgress+';\n'+actualGlobals.g_hidHideMsiAuxiliaryProgress+'\n'+actual.hidHideRememberMsiAuxiliaryPlan+'\n'+actual.hidHideMsiAuxiliaryOwnedInstance+'\n');
 const role=actualTypes.HidHideIfaceRole+';';
 const helperLoad=block(cpp,'static SourceIdentity source(')+'\n'+block(cpp,'static Facts load(');
 const synthetic=String.raw`
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <stdexcept>
#include <atomic>
#include <cctype>
#include <limits>
#include "json.hpp"
#include "msi_virtual_auxiliary_cloak_plan.h"
using Json=nlohmann::json;using json=Json;using namespace ymcc::msi::auxiliaryCloak;
using DWORD=std::uint32_t;using BYTE=unsigned char;using USHORT=std::uint16_t;using USAGE=std::uint16_t;using ULONGLONG=std::uint64_t;using HDEVINFO=std::uintptr_t;
static constexpr HDEVINFO INVALID_HANDLE_VALUE=std::numeric_limits<HDEVINFO>::max();
static constexpr DWORD ERROR_NO_MORE_ITEMS=259,DIGCF_PRESENT=2,DIGCF_DEVICEINTERFACE=16;static const int kYeManGuidDeviceInterfaceHid=0;
struct SP_DEVICE_INTERFACE_DATA{DWORD cbSize=0,index=0;};struct SP_DEVINFO_DATA{DWORD cbSize=0,DevInst=0;};struct SP_DEVICE_INTERFACE_DETAIL_DATA_W{DWORD cbSize=0;wchar_t DevicePath[192]{};};
${role}
${records}
static HidHideVisibilityJournal g_hidHideVisibilityJournal;
${refreshFamilyStub}
${actualGlobals.g_hidHideMsiAuxiliaryProgress}
static struct{bool enabled=true,virtualTargetEnabled=true;}g_realStickTest;
${helperLoad}
${actual.ascii_lower}
${actual.trim_ascii}
${actual.hidHideUpper}
// Fixture IDs are validated ASCII. Encoding conversion is a fake boundary.
static std::string W2U(const std::wstring&s){return {s.begin(),s.end()};}static std::wstring U2W(const std::string&s){return {s.begin(),s.end()};}
${actual.hidHideIsVirtualIdentity}
${actual.hidHideContainsPath}
${actual.hidHideAppendTarget}
${actual.hidHidePhysicalTargets}
${actual.hidHideInventoryContainsInstance}
${actual.hidHideInverseApplicationListState}
${actual.hidHideCarriedClaimTargets}
${actual.hidHideCarriedMergeList}
${actual.hidHideRememberMsiAuxiliaryPlan}
${actual.hidHideMsiAuxiliaryOwnedInstance}
struct FakeContext{
 bool authority=true,exit=false,sourceBeforeKnown=true,sourceAfterKnown=true,eligible=true,classFails=false,throwBadAlloc=false,pipeOk=true,cliOk=true;
 int sizeFails=-1,detailFails=-1,instanceFails=-1,probeFails=-1,enumFails=-1;DWORD lastError=ERROR_NO_MORE_ITEMS;
 std::string throwAt;unsigned throwOccurrence=1;std::vector<HidHideInterfaceRecord>inventory;
 HidHideMsiMainSourceRecord sourceBefore,sourceAfter;std::function<void()>afterInventory;
 std::vector<Json>logs;std::vector<std::wstring>hideWrites;
}cfg;
static std::map<std::string,unsigned>observations;static std::vector<std::string>order;
static unsigned cases=0,checks=0;
static void check(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
static void observe(const std::string&key){order.push_back(key);const auto n=++observations[key];if(cfg.throwAt==key&&n==cfg.throwOccurrence){if(cfg.throwBadAlloc)throw std::bad_alloc();throw std::runtime_error("injected "+key);}}
struct ObservedGeneration{uint64_t value=0;const char*key="";uint64_t load(std::memory_order){observe(key);return value;}};
static ObservedGeneration power{1,"power"},g_msiFeedbackModeGeneration{7,"mode"},g_xinputTopologyEpoch{9,"topology"},g_msiFeedbackSamplePower{1,"sample-power"},g_msiFeedbackSampleMode{7,"sample-mode"},g_msiFeedbackSampleTopology{9,"sample-topology"};
static bool msiClawKnownBoardAuthority(){observe("authority-guard");return cfg.authority;}
static bool inputHostExitFenceRaised(){observe("exit-guard");return cfg.exit;}
static uint64_t currentPowerGeneration(){observe("power");return power.value;}
static bool msiDinputManagedMainSourceActive(HidHideMsiMainSourceRecord&out){observe("source");const bool first=observations["source"]==1;out=first?cfg.sourceBefore:cfg.sourceAfter;return first?cfg.sourceBeforeKnown:cfg.sourceAfterKnown;}
static bool msiDinputMainSourceEligible(){observe("eligible");return cfg.eligible;}
static void appendNativeLifecycleLog(const char*event,const json&row){observe("log");auto r=row;r["event"]=event;cfg.logs.push_back(std::move(r));}
static HDEVINFO SetupDiGetClassDevsW(const int*,const void*,const void*,DWORD){observe("class");return cfg.classFails?INVALID_HANDLE_VALUE:1;}
static bool SetupDiEnumDeviceInterfaces(HDEVINFO,const void*,const int*,DWORD index,SP_DEVICE_INTERFACE_DATA*iface){observe("enum");if(static_cast<int>(index)==cfg.enumFails){cfg.lastError=5;return false;}if(index>=cfg.inventory.size()){cfg.lastError=ERROR_NO_MORE_ITEMS;return false;}iface->index=index;return true;}
static DWORD GetLastError(){observe("GetLastError");return cfg.lastError;}
static bool SetupDiGetDeviceInterfaceDetailW(HDEVINFO,const SP_DEVICE_INTERFACE_DATA*iface,SP_DEVICE_INTERFACE_DETAIL_DATA_W*detail,DWORD,DWORD*bytes,SP_DEVINFO_DATA*data){const auto index=iface->index;if(!detail){observe("detail-size");*bytes=static_cast<int>(index)==cfg.sizeFails?0:sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);return false;}observe("detail-full");if(static_cast<int>(index)==cfg.detailFails)return false;data->DevInst=index;const auto p=U2W(cfg.inventory.at(index).interfacePath);check(p.size()<192,"fake interface path fits");std::copy(p.begin(),p.end(),detail->DevicePath);detail->DevicePath[p.size()]=0;return true;}
static std::string hidHideDeviceInstanceId(HDEVINFO,const SP_DEVINFO_DATA&data){observe("instance");return static_cast<int>(data.DevInst)==cfg.instanceFails?"":cfg.inventory.at(data.DevInst).instanceId;}
static std::string hidHideNearestBaseInstance(DWORD node,const std::string&){observe("root");return cfg.inventory.at(node).baseContainerDeviceInstanceId;}
static bool hidHideProbeInterfaceCaps(const wchar_t*,HDEVINFO,const SP_DEVINFO_DATA&data,HidHideInterfaceRecord&record){observe("probe");if(static_cast<int>(data.DevInst)==cfg.probeFails)return false;record=cfg.inventory.at(data.DevInst);return true;}
static bool SetupDiDestroyDeviceInfoList(HDEVINFO){observe("destroy");if(cfg.afterInventory)cfg.afterInventory();return true;}
${actual.hidHideBuildSourceInterfaceInventory}
${actual.hidHideVirtualPhysicalTargets}
static bool inputHostHidHideApplyViaPipeLocked(const wchar_t*,const std::wstring&path){observe("fake-hide");cfg.hideWrites.push_back(path);return cfg.pipeOk;}
static bool hidHideRun(const std::wstring&){observe("fake-cli");return cfg.cliOk;}
static bool lastRefreshOk=true;
static std::vector<std::string>actualRefresh(const std::vector<std::string>&targets,const HidHideVisibilitySnapshot&before){bool ok=true;
${refresh}
 lastRefreshOk=ok;std::vector<std::string>out;for(const auto&id:g_hidHideVisibilityJournal.hiddenByUs)out.push_back(W2U(id));return out;}
static void reset(const Facts&f){cfg={};g_machineIdentity.family=FixtureFamily::Msi;lastRefreshOk=true;observations.clear();order.clear();g_hidHideVisibilityJournal={};g_hidHideMsiAuxiliaryProgress={};g_hidHideVisibilityJournal.transactionId="synthetic";g_realStickTest={true,true};power.value=f.before.powerGeneration;g_msiFeedbackModeGeneration.value=7;g_xinputTopologyEpoch.value=9;g_msiFeedbackSamplePower.value=power.value;g_msiFeedbackSampleMode.value=7;g_msiFeedbackSampleTopology.value=9;
 auto sourceRecord=[](const SourceIdentity&s){HidHideMsiMainSourceRecord r;r.valid=s.valid;r.generation=static_cast<unsigned>(s.bindGeneration);r.interfacePath=s.sourceToken;r.usbInstance=s.physicalParentId;r.vendorId=s.vid;r.productId=s.pid;return r;};cfg.sourceBefore=sourceRecord(f.before);cfg.sourceAfter=sourceRecord(f.after);
 for(const auto&r:f.inventory){HidHideInterfaceRecord x;x.instanceId=r.instanceId;x.interfacePath=r.instanceId;x.baseContainerDeviceInstanceId=r.baseInstanceId;x.vendorId=r.vid;x.productId=r.pid;x.miCol=r.miCol;x.usagePage=r.usagePage;x.usage=r.usage;cfg.inventory.push_back(x);}}
static HidHidePhysicalIdentity physical(const Facts&f){HidHidePhysicalIdentity p;p.resolved=true;p.sourcePath=f.before.sourceToken;p.deviceInstanceId=f.before.instanceId;p.baseContainerDeviceInstanceId=f.before.baseInstanceId;p.vendorId=static_cast<USHORT>(f.before.vid);p.productId=static_cast<USHORT>(f.before.pid);p.usagePage=1;p.usage=5;return p;}
static HidHideVisibilitySnapshot snapshot(){HidHideVisibilitySnapshot s;s.valid=true;s.inverseRaw="off";return s;}
static unsigned extraReads(){unsigned n=0;for(const auto&key:{"source","eligible","power","mode","topology","sample-power","sample-mode","sample-topology","class","enum","detail-size","detail-full","instance","root","probe","destroy","GetLastError"})n+=observations[key];return n;}
static Json progressState(){const auto&p=g_hidHideMsiAuxiliaryProgress;return Json{{"transactionId",p.transactionId},{"lastReason",p.lastReason},{"expectedTargets",p.expectedTargets},{"pending",p.pending},{"exhaustedLogged",p.exhaustedLogged},{"attempts",p.attempts},{"lastAttemptTick",p.lastAttemptTick}};}
static void pendingProgress(const char*reason){check(g_hidHideMsiAuxiliaryProgress.pending,"actual progress.pending for refused plan");check(g_hidHideMsiAuxiliaryProgress.transactionId==g_hidHideVisibilityJournal.transactionId,"actual progress bound to current transaction");check(g_hidHideMsiAuxiliaryProgress.lastReason==reason,"actual progress remembers exact reason");}
static void completedProgress(const std::vector<std::string>&expected){check(!g_hidHideMsiAuxiliaryProgress.pending,"actual completed plan pending=false");check(g_hidHideMsiAuxiliaryProgress.expectedTargets==expected,"actual progress expected IDs, independent of ownership filter");check(g_hidHideMsiAuxiliaryProgress.lastReason=="two-exact-same-source-auxiliary-collections","actual completed reason");}
static std::vector<std::string>ids(const std::vector<HidHideInterfaceRecord>&v){std::vector<std::string>r;for(const auto&x:v)r.push_back(x.instanceId);return r;}
int main(int argc,char**argv){try{std::ifstream in(argv[1]);Json j;in>>j;const Facts fixture=load(j.at("facts"));const auto aux=j.at("expectedIds").get<std::vector<std::string>>();const auto p=physical(fixture);const auto before=snapshot();const auto base=hidHidePhysicalTargets(p);auto expanded=base;expanded.insert(expanded.end(),aux.begin(),aux.end());
 ++cases;reset(fixture);check(base.size()==2,"original main/root plan is two");check(hidHideVirtualPhysicalTargets(p,before,1,"fixture-positive")==expanded,"actual wrapper adds exact two ZIP collections");check(observations["source"]==2&&observations["power"]==2&&observations["mode"]==2&&observations["topology"]==2,"actual before/after read counts");check(observations["class"]==1&&observations["destroy"]==1&&observations["GetLastError"]==1,"explicit completeness enumeration and cleanup");check(cfg.logs.size()==1&&cfg.logs[0].at("plannedAuxiliaryTargets")==aux&&cfg.logs[0].at("journalEligibleAuxiliaryTargets")==aux&&cfg.logs[0].at("physicalVisibilityTargets")==expanded,"actual diagnostic row reflects plan and eligible IDs");check(cfg.logs[0].at("inventoryComplete")==true&&cfg.logs[0].at("generationReadsAtomicTogether")==false,"complete but no atomic snapshot claim");completedProgress(aux);check(cfg.logs[0].at("driverAcceptance")=="unobserved-at-plan"&&cfg.logs[0].at("steamAliasChildMapping")=="unobserved"&&cfg.logs[0].at("steamConsumption")=="unobserved","no driver or consumer acceptance claim");
 {const auto firstSource=std::find(order.begin(),order.end(),"source"),inventory=std::find(order.begin(),order.end(),"class"),destroy=std::find(order.begin(),order.end(),"destroy"),secondSource=std::find(destroy,order.end(),"source");check(firstSource<inventory&&inventory<destroy&&destroy<secondSource,"actual source/inventory/source call order");}
 // MSI base/root and device ownership through the ACTUAL wrapper and refresh.
 {++cases;reset(fixture);auto s=before;for(const auto&id:expanded)s.hidden.push_back(U2W(id));const auto planned=hidHideVirtualPhysicalTargets(p,s,1,"MSI-both-base-prehidden-unowned");check(planned==base,"actual wrapper keeps pre-hidden base/root and main; auxiliary third parties excluded");check(actualRefresh(planned,s).empty()&&lastRefreshOk&&cfg.hideWrites.empty()&&observations["fake-cli"]==0,"MSI both base/root and main stay unowned without writes");}
 for(const auto&target:base){
  for(unsigned alias=0;alias<2;++alias){
   ++cases;reset(fixture);auto s=before;for(const auto&id:expanded)s.hidden.push_back(U2W(alias&&id==target?ascii_lower(id):id));
   for(const auto&id:base)if(id!=target)g_hidHideVisibilityJournal.hiddenByUs.push_back(U2W(id));g_hidHideVisibilityJournal.hiddenByUs.push_back(U2W(aux[0]));const auto prior=g_hidHideVisibilityJournal.hiddenByUs;
   const auto planned=hidHideVirtualPhysicalTargets(p,s,1,"MSI-base-prehidden-unowned");check(containsIdentity(planned,target)&&containsIdentity(planned,aux[0])&&!containsIdentity(planned,aux[1]),"actual wrapper retains base but excludes third-party auxiliary");actualRefresh(planned,s);
   check(g_hidHideVisibilityJournal.hiddenByUs==prior&&lastRefreshOk&&cfg.hideWrites.empty()&&observations["fake-cli"]==0,"MSI root/device third-party pre-hidden never owned; existing base/aux entries preserved including alias");
  }
  for(unsigned receipt=0;receipt<3;++receipt){
   ++cases;reset(fixture);auto s=before;for(const auto&id:expanded)if(id!=target)s.hidden.push_back(U2W(id));g_hidHideVisibilityJournal.hiddenByUs={U2W(aux[0])};cfg.pipeOk=receipt==0;cfg.cliOk=receipt!=2;
   const auto planned=hidHideVirtualPhysicalTargets(p,s,1,"MSI-base-new-hide-receipt");const auto afterOwned=actualRefresh(planned,s);
   check(containsIdentity(afterOwned,target)==(receipt!=2)&&containsIdentity(afterOwned,aux[0])&&afterOwned.size()==(receipt==2?1u:2u),"MSI new root/device owned only after successful hide; no pre-hidden base/aux additions");
   check(lastRefreshOk==(receipt!=2)&&cfg.hideWrites==std::vector<std::wstring>{U2W(target)}&&observations["fake-cli"]==(receipt==0?0u:1u),"actual refresh root/device pipe/fallback/failure receipt; only required hide attempted");
  }
  {++cases;reset(fixture);auto s=before;for(const auto&id:expanded)if(id!=target)s.hidden.push_back(U2W(id));g_hidHideVisibilityJournal.hiddenByUs={U2W(aux[0]),U2W(target)};const auto prior=g_hidHideVisibilityJournal.hiddenByUs;cfg.pipeOk=cfg.cliOk=false;actualRefresh(hidHideVirtualPhysicalTargets(p,s,1,"MSI-base-owned-hide-failed"),s);check(!lastRefreshOk&&g_hidHideVisibilityJournal.hiddenByUs==prior&&cfg.hideWrites==std::vector<std::wstring>{U2W(target)}&&observations["fake-cli"]==1,"MSI failed owned root/device re-hide retains responsibility");}
 }
 for(unsigned carried=0;carried<2;++carried){
  ++cases;reset(fixture);auto s=before;for(const auto&id:expanded)s.hidden.push_back(U2W(id));const std::vector<std::wstring>ownedTargets={U2W(base[0]),U2W(base[1]),U2W(aux[0])};
  if(carried){const auto carriedTargets=hidHideCarriedClaimTargets(ownedTargets,s.hidden);
${carriedLine}
  }else g_hidHideVisibilityJournal.hiddenByUs=ownedTargets;
  actualRefresh(hidHideVirtualPhysicalTargets(p,s,1,"MSI-base-owned-or-carried"),s);check(g_hidHideVisibilityJournal.hiddenByUs==ownedTargets&&lastRefreshOk&&cfg.hideWrites.empty()&&observations["fake-cli"]==0,"MSI existing and ACTUAL transferred carried root/device/aux ownership preserved without writes");
 }
 for(const auto family:{FixtureFamily::Rog,FixtureFamily::Gpd,FixtureFamily::Other}){
  ++cases;reset(fixture);cfg.authority=false;g_machineIdentity.family=family;auto s=before;for(const auto&id:base)s.hidden.push_back(U2W(id));const auto planned=hidHideVirtualPhysicalTargets(p,s,1,"NON-MSI-legacy-base-witness");
  check(planned==base&&extraReads()==0,"NON-MSI wrapper retains baseline selection and zero extra observations");check(actualRefresh(planned,s)==base&&lastRefreshOk&&cfg.hideWrites.empty()&&observations["fake-cli"]==0,"NON-MSI legacy raw-loop base/root ownership claim unchanged without writes");
 }
 // Exercise the EXTRACTED ACTUAL record helper directly; only memory fields
 // may change. No fake Source/SetupAPI/log/hide calls or journal duty changes.
 for(unsigned transaction=0;transaction<3;++transaction)for(unsigned success=0;success<2;++success){++cases;reset(fixture);g_hidHideVisibilityJournal.hiddenByUs={U2W(aux[0])};g_hidHideMsiAuxiliaryProgress.transactionId=transaction==2?"synthetic":"old";g_hidHideMsiAuxiliaryProgress.lastReason="old-reason";g_hidHideMsiAuxiliaryProgress.expectedTargets={"old-expected"};g_hidHideMsiAuxiliaryProgress.pending=true;g_hidHideMsiAuxiliaryProgress.attempts=6;g_hidHideMsiAuxiliaryProgress.lastAttemptTick=55;g_hidHideMsiAuxiliaryProgress.exhaustedLogged=true;if(transaction==0)g_hidHideVisibilityJournal.transactionId.clear();const auto was=progressState();const auto oldOwned=g_hidHideVisibilityJournal.hiddenByUs;const char*reason=success?"two-exact-same-source-auxiliary-collections":"inventory-incomplete";hidHideRememberMsiAuxiliaryPlan(reason,aux);check(order.empty()&&observations.empty(),"actual record performs zero observation/device/log actions");check(g_hidHideVisibilityJournal.hiddenByUs==oldOwned,"actual record does not claim journal ownership");if(transaction==0){check(progressState()==was,"empty transaction record no-op");continue;}check(g_hidHideMsiAuxiliaryProgress.transactionId=="synthetic","actual record transaction identity");check(g_hidHideMsiAuxiliaryProgress.pending==!success&&g_hidHideMsiAuxiliaryProgress.lastReason==reason,"actual success/pending semantics");check(g_hidHideMsiAuxiliaryProgress.attempts==(transaction==1?0:6)&&g_hidHideMsiAuxiliaryProgress.lastAttemptTick==(transaction==1?0:55)&&g_hidHideMsiAuxiliaryProgress.exhaustedLogged==(transaction==2),"actual new transaction resets counters; same transaction retains counters");check(g_hidHideMsiAuxiliaryProgress.expectedTargets==(success?aux:transaction==1?std::vector<std::string>{}:std::vector<std::string>{"old-expected"}),"actual failed reason retains previous expectation only within transaction");}
 {++cases;reset(fixture);hidHideRememberMsiAuxiliaryPlan("two-exact-same-source-auxiliary-collections",aux);hidHideRememberMsiAuxiliaryPlan("inverse-state-not-confirmed-off");pendingProgress("inverse-state-not-confirmed-off");check(g_hidHideMsiAuxiliaryProgress.expectedTargets==aux,"actual pending retains prior completed expected set");check(order.empty()&&observations.empty()&&g_hidHideVisibilityJournal.hiddenByUs.empty(),"record updates progress only, never hide ownership");}
 for(unsigned i=0;i<2;++i)for(unsigned length:{200u,201u}){++cases;reset(fixture);auto id=aux[i];id.append(length-id.size(),'X');check(hidHideMsiAuxiliaryOwnedInstance(id)==(length==200),"actual exact auxiliary classifier length bound");check(order.empty()&&observations.empty(),"actual owned helper zero API observations");}
 for(const auto&id:std::vector<std::string>{fixture.before.instanceId,"HID\\VID_0DB0&PID_1902&MI_01&COL01\\01","HID\\VID_0DB0&PID_1901&MI_00&COL02\\01","ROOT\\VIGEM\\01","unknown",""}){++cases;reset(fixture);check(!hidHideMsiAuxiliaryOwnedInstance(id),"actual owned helper rejects outside exact aux identities");check(order.empty()&&observations.empty(),"actual owned helper reject zero API observations");}
 {++cases;reset(fixture);check(hidHideMsiAuxiliaryOwnedInstance(ascii_lower(aux[0]))&&hidHideMsiAuxiliaryOwnedInstance(ascii_lower(aux[1])),"actual owned helper handles case-insensitive exact collection");check(order.empty()&&observations.empty(),"owned helper does not prove ownership or call APIs");}
 for(unsigned index=1;index<3;++index)for(unsigned length:{200u,201u,512u}){++cases;reset(fixture);auto&r=cfg.inventory[index];r.instanceId.append(length-r.instanceId.size(),'X');auto wanted=expanded;wanted[index+1]=r.instanceId;const auto actualTargets=hidHideVirtualPhysicalTargets(p,before,1,"auxiliary-length");check(actualTargets==(length==200?wanted:base),"actual wrapper auxiliary 200/201/512 bound");check(observations["class"]==1&&observations["source"]==2,"length gate no new read path");if(length>200)check(cfg.logs[0].at("reason")=="auxiliary-instance-id-exceeds-200"&&cfg.logs[0].at("plannedAuxiliaryTargets").empty(),"actual log explicit length rejection");}
 for(unsigned gate=0;gate<4;++gate){++cases;reset(fixture);if(gate==0)cfg.authority=false;if(gate==1)g_realStickTest.enabled=false;if(gate==2)g_realStickTest.virtualTargetEnabled=false;if(gate==3)cfg.exit=true;g_hidHideMsiAuxiliaryProgress.transactionId="earlier";g_hidHideMsiAuxiliaryProgress.lastReason="sentinel";g_hidHideMsiAuxiliaryProgress.pending=true;g_hidHideMsiAuxiliaryProgress.attempts=6;g_hidHideMsiAuxiliaryProgress.lastAttemptTick=55;g_hidHideMsiAuxiliaryProgress.expectedTargets=aux;const auto oldProgress=progressState();check(hidHideVirtualPhysicalTargets(p,before,1,"closed")==base,"nonMSI/nativeoff/virtualoff/exit preserve original targets");check(extraReads()==0&&observations["log"]==0,"closed gates zero new source/generation/SetupAPI observations or log");check(progressState()==oldProgress,"closed gates do not record or mutate progress");}
 for(const auto*raw:{"","unknown","on","--inv-on","garbage"}){++cases;reset(fixture);auto s=before;s.inverseRaw=raw;check(hidHideVirtualPhysicalTargets(p,s,1,"inverse")==base,"inverse not confirmed off rejects aux");check(extraReads()==0&&observations["log"]==1,"inverse guard before new reads");pendingProgress("inverse-state-not-confirmed-off");}
 {++cases;reset(fixture);auto s=before;s.valid=false;check(hidHideVirtualPhysicalTargets(p,s,1,"invalid")==base&&extraReads()==0,"invalid snapshot rejects before new source/API reads");pendingProgress("inverse-state-not-confirmed-off");}
 for(const auto*raw:{"off","--inv-off"}){++cases;reset(fixture);auto s=before;s.inverseRaw=raw;check(hidHideVirtualPhysicalTargets(p,s,1,"inverse-off")==expanded,"actual inverse parser accepts existing off forms");}
 for(unsigned stale=0;stale<4;++stale){++cases;reset(fixture);if(stale==0)cfg.eligible=false;if(stale==1)++g_msiFeedbackSamplePower.value;if(stale==2)++g_msiFeedbackSampleMode.value;if(stale==3)++g_msiFeedbackSampleTopology.value;check(hidHideVirtualPhysicalTargets(p,before,1,"stale-sample")==base,"stale feedback sample rejects");check(observations["class"]==0,"stale source skips inventory");if(stale==0)check(observations["sample-power"]==0&&observations["sample-mode"]==0&&observations["sample-topology"]==0,"eligibility false preserves sample short circuit");if(stale==1)check(observations["sample-mode"]==0&&observations["sample-topology"]==0,"power sample mismatch short circuits later sample loads");if(stale==2)check(observations["sample-topology"]==0,"mode sample mismatch short circuits topology sample");}
 for(unsigned changed=0;changed<14;++changed){++cases;reset(fixture);cfg.afterInventory=[&,changed]{switch(changed){case 0:++power.value;g_msiFeedbackSamplePower.value=power.value;break;case 1:++g_msiFeedbackModeGeneration.value;g_msiFeedbackSampleMode.value=g_msiFeedbackModeGeneration.value;break;case 2:++g_xinputTopologyEpoch.value;g_msiFeedbackSampleTopology.value=g_xinputTopologyEpoch.value;break;case 3:++cfg.sourceAfter.generation;break;case 4:cfg.sourceAfter.interfacePath+="X";break;case 5:cfg.sourceAfter.usbInstance+="X";break;case 6:cfg.sourceAfterKnown=false;break;case 7:cfg.exit=true;break;case 8:g_realStickTest.enabled=false;break;case 9:g_realStickTest.virtualTargetEnabled=false;break;case 10:++g_msiFeedbackSampleMode.value;break;case 11:cfg.sourceAfter.interfacePath=ascii_lower(cfg.sourceAfter.interfacePath);if(cfg.sourceAfter.interfacePath==cfg.sourceBefore.interfacePath)cfg.sourceAfter.interfacePath=hidHideUpper(cfg.sourceAfter.interfacePath);break;case 12:++cfg.sourceAfter.vendorId;break;case 13:++cfg.sourceAfter.productId;break;}};check(hidHideVirtualPhysicalTargets(p,before,1,"drift")==base,"actual source/power/mode/topology/gate drift refuses aux");check(observations["class"]==1&&observations["destroy"]==1,"drift injected after actual inventory cleanup");pendingProgress(cfg.logs[0].at("reason").get<std::string>().c_str());}
 for(unsigned bad=0;bad<7;++bad){++cases;reset(fixture);auto invalid=p;size_t count=1;if(bad==0)count=0;if(bad==1)count=2;if(bad==2)cfg.sourceBeforeKnown=false;if(bad==3)invalid.resolved=false;if(bad==4)invalid.sourcePath+="X";if(bad==5)cfg.sourceBefore.usbInstance="USB\\ROOT_HUB\\01";if(bad==6)cfg.sourceBefore.productId=0x1901;check(hidHideVirtualPhysicalTargets(invalid,before,count,"bad-source")==base,"actual missing/ambiguous source or ancestry refusal");check(observations["class"]==0,"bad source no inventory");}
 // Invoke ACTUAL inventory with and without optional complete. Compare returned
 // rows and ordered API calls; only explicit caller may read GetLastError.
 for(unsigned fault=0;fault<7;++fault)for(int index=0;index<3;++index){++cases;auto setup=[&]{reset(fixture);if(fault==1)cfg.sizeFails=index;if(fault==2)cfg.detailFails=index;if(fault==3)cfg.instanceFails=index;if(fault==4)cfg.probeFails=index;if(fault==5)cfg.enumFails=index;if(fault==6)cfg.classFails=true;};setup();const auto old=hidHideBuildSourceInterfaceInventory(p.baseContainerDeviceInstanceId,p.deviceInstanceId);const auto oldOrder=order;check(observations["GetLastError"]==0,"default inventory caller has no added GetLastError read");setup();bool complete=true;const auto observed=hidHideBuildSourceInterfaceInventory(p.baseContainerDeviceInstanceId,p.deviceInstanceId,&complete);check(ids(old)==ids(observed),"completeness observation preserves existing partial row output");auto withOrder=order;withOrder.erase(std::remove(withOrder.begin(),withOrder.end(),"GetLastError"),withOrder.end());check(oldOrder==withOrder,"default/explicit API call order identical except required last error observation");check(complete==(fault==0),"normal FALSE size query accepted; actual partial/error cases complete false");check(observations["GetLastError"]==(fault==6?0:1),"only enum terminal observation added for explicit caller");setup();check(hidHideVirtualPhysicalTargets(p,before,1,"partial")== (fault==0?expanded:base),"actual wrapper rejects partial/failed inventory");if(fault==0)completedProgress(aux);else pendingProgress("inventory-incomplete");}
 for(unsigned duplicate=0;duplicate<2;++duplicate){++cases;reset(fixture);auto other=cfg.inventory[1];if(duplicate)other.instanceId=ascii_lower(other.instanceId);cfg.inventory.push_back(other);check(hidHideVirtualPhysicalTargets(p,before,1,"duplicate")==base,"actual inventory completeness rejects duplicate/case alias instead of dedup acceptance");}
 for(unsigned missing=0;missing<3;++missing){++cases;reset(fixture);cfg.inventory.erase(cfg.inventory.begin()+missing);check(hidHideVirtualPhysicalTargets(p,before,1,"missing")==base,"actual wrapper requires main and both exact auxiliary records");}
 for(unsigned hidden=0;hidden<4;++hidden)for(unsigned owned=0;owned<4;++owned)for(unsigned carried=0;carried<4;++carried){++cases;reset(fixture);auto s=before;std::vector<std::wstring>ownedTargets,carriedOwned;for(unsigned i=0;i<2;++i){if(hidden&(1<<i))s.hidden.push_back(U2W(aux[i]));if(owned&(1<<i))ownedTargets.push_back(U2W(aux[i]));if(carried&(1<<i))carriedOwned.push_back(U2W(aux[i]));}
 const auto carriedTargets=hidHideCarriedClaimTargets(carriedOwned,s.hidden);
${carriedLine}
 hidHideCarriedMergeList(g_hidHideVisibilityJournal.hiddenByUs,ownedTargets);
 const auto planned=hidHideVirtualPhysicalTargets(p,s,1,"ownership");for(unsigned i=0;i<2;++i){const bool allowed=!(hidden&(1<<i))||(owned&(1<<i))||(carried&(1<<i));check(containsIdentity(planned,aux[i])==allowed,"actual wrapper journal/carried ownership eligibility");}
 const auto priorOwned=g_hidHideVisibilityJournal.hiddenByUs;const auto actualOwned=actualRefresh(planned,s);for(const auto&id:priorOwned)check(containsIdentity(actualOwned,W2U(id)),"actual MSI refresh preserves existing/carried auxiliary responsibility");for(unsigned i=0;i<2;++i)if((hidden&(1<<i))&&!(owned&(1<<i))&&!(carried&(1<<i)))check(!containsIdentity(actualOwned,aux[i]),"actual refresh never claims third-party pre-hidden auxiliary");for(unsigned i=0;i<2;++i)if(hidden&(1<<i))check(std::find(cfg.hideWrites.begin(),cfg.hideWrites.end(),U2W(aux[i]))==cfg.hideWrites.end(),"already hidden auxiliary never written again");}
 {++cases;reset(fixture);auto s=before;s.hidden={U2W(ascii_lower(aux[0])),U2W(ascii_lower(aux[1]))};check(hidHideVirtualPhysicalTargets(p,s,1,"case-hidden")==base,"actual wrapper case-insensitive third-party pre-hidden boundary");}
 for(const auto*point:{"source","power","mode","topology","eligible","sample-power","sample-mode","sample-topology","class","enum","detail-size","detail-full","instance","root","probe","destroy"}){++cases;reset(fixture);cfg.throwAt=point;check(hidHideVirtualPhysicalTargets(p,before,1,"throw")==base,"actual planning exception returns baseline two");pendingProgress("planning-observation-exception");}
 for(const auto*point:{"source","power","mode","topology"}){++cases;reset(fixture);cfg.throwAt=point;cfg.throwOccurrence=2;check(hidHideVirtualPhysicalTargets(p,before,1,"throw-after")==base,"actual after-read exception returns baseline");pendingProgress("planning-observation-exception");}
 {++cases;reset(fixture);cfg.throwAt="probe";cfg.throwBadAlloc=true;check(hidHideVirtualPhysicalTargets(p,before,1,"bad-alloc")==base,"bad_alloc injected at probe boundary returns baseline");pendingProgress("planning-observation-exception");}
 {++cases;reset(fixture);cfg.throwAt="log";check(hidHideVirtualPhysicalTargets(p,before,1,"diagnostic-throw")==expanded,"final diagnostic throw preserves completed plan");completedProgress(aux);}
 {++cases;reset(fixture);auto s=before;s.inverseRaw="unknown";cfg.throwAt="log";check(hidHideVirtualPhysicalTargets(p,s,1,"inverse-log-throw")==base,"inverse diagnostic throw preserves baseline");pendingProgress("planning-observation-exception");}
 std::cout<<Json{{"passed",true},{"cases",cases},{"assertions",checks},{"exactAuxiliaryIds",aux},{"baselineTargets",base},{"expandedTargets",expanded},{"realApiCalls",0},{"productExecuted",false},{"driverAcceptanceProven",false},{"actualProgressExtracted",true},{"scope","Frozen actual wrapper/inventory/progress remember/owned classifier/types/globals/ownership helpers/refresh; fake external boundaries; no production execution"}}.dump()<<'\n';return 0;
 }catch(const std::exception&ex){std::cout<<Json{{"passed",false},{"cases",cases},{"assertions",checks},{"failure",ex.what()}}.dump()<<'\n';return 1;}}
`;
 fs.writeFileSync(out+'/wrapper-inventory-synthetic.cpp',synthetic);
 const compile='@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"'+repo+'/native" /I"'+out+'/deps" "'+out+'/wrapper-inventory-synthetic.cpp" /Fo"'+out+'/wrapper-inventory-synthetic.obj" /Fe"'+out+'/wrapper-inventory-synthetic.exe"\r\nexit /b %errorlevel%\r\n';fs.writeFileSync(out+'/compile-wrapper-inventory.cmd',compile);
 const built=spawnSync('cmd.exe',['/d','/c',out+'/compile-wrapper-inventory.cmd'],opts);fs.writeFileSync(out+'/wrapper-inventory-build.log',(built.stdout||'')+(built.stderr||''));assert.equal(built.status,0,'actual wrapper/inventory synthetic compile');
 const run=spawnSync(out+'/wrapper-inventory-synthetic.exe',[out+'/log-fixture.json'],opts);fs.writeFileSync(out+'/wrapper-inventory-result.json',(run.stdout||'')+(run.stderr||''));assert.equal(run.status,0,'actual wrapper/inventory checks');
 const binding={parentCandidatePath:candidatePath,parentCandidateSha256:hash(candidateBytes),parentBindingPath:integrationBindingPath,parentBindingSha256:hash(parentBindingBytes),frozenPath,headerSha256:hash(fs.readFileSync(repo+'/native/msi_virtual_auxiliary_cloak_plan.h')),selftestSha256:hash(fs.readFileSync(fileURLToPath(import.meta.url))),fixtureSha256:hash(fs.readFileSync(out+'/log-fixture.json')),generatedCppSha256:hash(synthetic),exeSha256:hash(fs.readFileSync(out+'/wrapper-inventory-synthetic.exe')),actualFunctions:Object.fromEntries(names.map(n=>[n,actualSpanBinding(candidateText,actual[n])])),actualTypes:Object.fromEntries(Object.entries(actualTypes).map(([n,span])=>[n,actualSpanBinding(candidateText,span)])),actualGlobals:Object.fromEntries(Object.entries(actualGlobals).map(([n,span])=>[n,actualSpanBinding(candidateText,span)])),actualProgressFunctionsExtracted:true,actualProgressStructExtracted:true,progressMutexScope:"Production existing g_inputHostMx; synthetic single-thread only",refreshLoopSha256:hash(refresh),refreshOwnershipCases:"actual wrapper base/root/device prehidden-unowned and owned/carried; pipe/fallback/failure; NON-MSI legacy",familyPolicyBoundary:"explicit fake MSI/ROG/GPD/other values; no real machine detection",carriedTransferLineSha256:hash(carriedLine),realApiCalls:0,notTested:['real SetupAPI/ancestry/probe behavior','real driver acceptance or Steam effects','production initial apply orchestration outside extracted carried transfer','real allocator exhaustion'],exceptionSemantics:'Source/inventory observation throws -> original targets; final diagnostic throws -> completed selected targets'};
 fs.writeFileSync(out+'/wrapper-inventory-source-test-binding.json',JSON.stringify(binding,null,2));console.log(run.stdout.trim());
}
