// Semantic/security consumer of verbatim current native fragments. No timing loop.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "json.hpp"
#include "input_host_compact_frame_reply.h"
#include "game_feedback_receipt.h"
using json=nlohmann::json;
static std::string g_inputHostRunId="fixture-run",g_inputHostStartupNonce="fixture-nonce",g_inputHostHostInstanceId="fixture-instance",g_inputHostFrameReplyBinding;
static unsigned long long g_inputHostEpoch=2,g_inputHostConfigRevision=1,fixturePower=3;
static const char* kInputHostTargetId="fixture-target";
static const char* kInputHostOwnerIdentity="fixture-owner";
static const char* kInputHostProtocolConfigHash="fixture-config";
static const char* kInputHostLockedCoreSha256="fixture-core-sha";
static const char* kInputHostLockedCoreMvid="fixture-mvid";
static unsigned long long inputHostProtocolPowerGenerationLocked(){return fixturePower;}
static std::string inputHostBoundOrCurrentPersona(){return "steamdeck";}
static std::string inputHostBoundOrCurrentProfileIdentity(){return "fixture-profile";}
#include "extracted.hpp"
struct DomResult {bool accepted=false,expanded=false,firstFrame=false;unsigned long long submitElapsedUs=0;std::string hostInstanceId,exception;ymcc::game_feedback::Sample gameMotor;json response;};
#include "dom_contract.hpp"
#include "request_contract.hpp"

static std::ofstream raw,errors,transports;
static std::uint64_t cases=0,checks=0,failures=0,fastAccepted=0,domAccepted=0,fallbackAccepted=0,pipeCases=0,pipeFragments=0;
static json groups=json::object();
static const std::string tokenA(64,'a'),tokenB(64,'b');
static json full(){return {{"protocolVersion",1},{"command","SUBMIT_FRAME"},{"requestId","r"},{"sequence",5},{"runId",g_inputHostRunId},{"epoch",g_inputHostEpoch},{"powerGeneration",fixturePower},{"targetId",kInputHostTargetId},{"ownerIdentity",kInputHostOwnerIdentity},{"persona",inputHostBoundOrCurrentPersona()},{"profileIdentity",inputHostBoundOrCurrentProfileIdentity()},{"startupNonce",g_inputHostStartupNonce},{"configRevision",g_inputHostConfigRevision},{"configHash",kInputHostProtocolConfigHash},{"assetVerified",true},{"lockedCoreSha256",kInputHostLockedCoreSha256},{"lockedCoreMvid",kInputHostLockedCoreMvid},{"hostInstanceId",g_inputHostHostInstanceId},{"ok",true},{"status","frame-accepted"},{"firstFrame",false},{"submitElapsedUs",27}};}
static json compact(){return {{"compactFrameReplyVersion",1},{"protocolVersion",1},{"ok",true},{"command","SUBMIT_FRAME"},{"requestId","r"},{"sequence",5},{"frameReplyBinding",tokenA},{"status","frame-accepted"},{"firstFrame",false},{"submitElapsedUs",27}};}
static std::string append(std::string base,const std::string& tail){base.insert(base.size()-1,tail);return base;}
static std::string hex(const std::string& s){const char* digits="0123456789abcdef";std::string r;r.reserve(2*s.size());for(unsigned char c:s){r+=digits[c>>4];r+=digits[c&15];}return r;}
static void require(bool pass,const char* why,const std::string& wire=""){
 ++checks;if(pass)return;++failures;errors<<json({{"case",cases},{"assertion",why},{"wireHex",hex(wire)}}).dump()<<'\n';
}
static bool receiptSame(const InputHostFastReceipt&a,const InputHostFastReceipt&b){return a.used==b.used&&a.firstFrame==b.firstFrame&&a.submitElapsedUs==b.submitElapsedUs&&a.hostInstanceId==b.hostInstanceId&&ymcc::game_feedback::same(a.gameMotor,b.gameMotor);}
struct Probe {bool fast=false,dom=false;InputHostFastReceipt receipt;DomResult parsed;};
static Probe probe(const std::string& group,const std::string& wire,int wantFast=-1,int wantDom=-1,const std::string* binding=&tokenA,const std::string& requestId="r",unsigned long long sequence=5){
 ++cases;auto& count=groups[group];if(count.is_null())count=0;count=count.get<std::uint64_t>()+1;
 Probe p;p.receipt.used=true;p.receipt.firstFrame=true;p.receipt.submitElapsedUs=777;p.receipt.hostInstanceId="sentinel";p.receipt.gameMotor=ymcc::game_feedback::fromWords({9,9,1,2,1,7},63);const auto sentinel=p.receipt;
 const bool scanner=c7aReceiptFast::scanSubmitFrameReceiptBound(wire.data(),wire.data()+wire.size(),"frame-accepted",requestId,sequence,p.receipt,binding);
 // Actual request uses a 16 KiB message read. Oversize must fail transport
 // before either parser; the unbounded primitive's result is recorded separately.
 const bool inBounds=wire.size()<=16*1024;p.fast=inBounds&&scanner&&!g_inputHostHostInstanceId.empty()&&p.receipt.hostInstanceId==g_inputHostHostInstanceId;
 if(!scanner)require(receiptSame(p.receipt,sentinel),"failed scan mutated output",wire);
 p.parsed=actualDom(wire,binding,"SUBMIT_FRAME","frame-accepted",requestId,sequence);p.dom=inBounds&&p.parsed.accepted;
 if(p.fast){++fastAccepted;require(p.dom,"fast accepted DOM-rejected wire",wire);require(p.receipt.firstFrame==p.parsed.firstFrame&&p.receipt.submitElapsedUs==p.parsed.submitElapsedUs&&p.receipt.hostInstanceId==p.parsed.hostInstanceId,"fast/DOM ACK values differ",wire);require(ymcc::game_feedback::same(p.receipt.gameMotor,p.parsed.gameMotor),"fast/DOM motor differs",wire);}
 if(p.dom)++domAccepted;if(!p.fast&&p.dom)++fallbackAccepted;
 if(wantFast>=0)require(p.fast==(wantFast!=0),"unexpected fast eligibility",wire);
 if(wantDom>=0)require(p.dom==(wantDom!=0),"unexpected DOM acceptance",wire);
 if(!binding){InputHostFastReceipt old;const bool wrapper=c7aReceiptFast::scanSubmitFrameReceipt(wire.data(),wire.data()+wire.size(),"frame-accepted",requestId,sequence,old);require(wrapper==scanner,"six-argument wrapper drift",wire);}
 raw<<cases<<'\t'<<group<<'\t'<<wire.size()<<'\t'<<scanner<<'\t'<<p.fast<<'\t'<<p.dom<<'\t'<<wantFast<<'\t'<<wantDom<<'\t'<<p.parsed.expanded<<'\t'<<p.parsed.firstFrame<<'\t'<<p.parsed.submitElapsedUs<<'\t'<<p.parsed.gameMotor.present<<'\t'<<p.parsed.gameMotor.valid<<'\t'<<p.parsed.gameMotor.generation<<'\t'<<p.parsed.gameMotor.revision<<'\t'<<unsigned(p.parsed.gameMotor.left)<<'\t'<<unsigned(p.parsed.gameMotor.right)<<'\t'<<unsigned(p.parsed.gameMotor.compatibility)<<'\t'<<p.parsed.gameMotor.sdkSequence<<'\n';
 return p;
}
static std::string uescape(unsigned value){char out[7];std::snprintf(out,sizeof(out),"\\u%04X",value);return out;}
static void core(){
 auto f=full(),c=compact();probe("old-full",f.dump(),1,1,nullptr);probe("old-full-with-binding",f.dump(),1,1);probe("compact",c.dump(),1,1);probe("compact-unbound",c.dump(),0,0,nullptr);
 std::string empty;probe("compact-empty-binding",c.dump(),0,0,&empty);probe("compact-stale-binding",c.dump(),0,0,&tokenB);
 for(bool first:{false,true})for(std::uint64_t elapsed:{0ull,27ull,18446744073709551615ull}){auto j=c;j["firstFrame"]=first;j["submitElapsedUs"]=elapsed;auto p=probe("receipt-values",j.dump(),1,1);require(p.receipt.firstFrame==first&&p.receipt.submitElapsedUs==elapsed,"lost firstFrame/submit value");}
 for(const auto& key:{"compactFrameReplyVersion","frameReplyBinding","protocolVersion","ok","command","requestId","sequence","status","firstFrame","submitElapsedUs"}){
  auto j=c;j.erase(key);probe("missing-compact-key",j.dump(),0,0);
  for(const auto& value:std::vector<json>{nullptr,false,0,"wrong",json::object(),json::array()}){j=c;j[key]=value;const bool valid=(std::string(key)=="firstFrame"&&value.is_boolean())||(std::string(key)=="submitElapsedUs"&&value.is_number_integer()&&value==0);probe("compact-bad-type-value",j.dump(),valid?1:0,valid?1:0);}
  probe("duplicate-compact-key",append(c.dump(),",\""+std::string(key)+"\":"+c[key].dump()),0,0);
  probe("duplicate-compact-key-conflict",append(c.dump(),",\""+std::string(key)+"\":null"),0,0);
 }
 for(const auto& key:{"compactFrameReplyVersion","protocolVersion"})for(std::uint64_t n:{2ull,4294967297ull,18446744073709551615ull}){auto j=c;j[key]=n;probe("version-overflow",j.dump(),0,0);}
 for(const auto& key:{"compactFrameReplyVersion","protocolVersion","sequence","submitElapsedUs"})for(const auto& n:{"-1","1e9999","18446744073709551616"}){auto j=c;j.erase(key);probe("numeric-overflow",append(j.dump(),",\""+std::string(key)+"\":"+n),0,0);}
 for(const auto& key:{"protocolVersion","sequence"}){auto j=c;j[key]=std::string(key)=="protocolVersion"?1.0:5.0;probe("compact-float-identity",j.dump(),0,0);}
 for(const auto& t:std::vector<std::string>{"",std::string(63,'a'),std::string(65,'a'),std::string(64,'A'),std::string(64,'g'),std::string(2048,'a')}){auto j=c;j["frameReplyBinding"]=t;probe("invalid-token",j.dump(),0,0);probe("invalid-expected-token",c.dump(),0,0,&t);}
 for(auto it=f.begin();it!=f.end();++it)if(ymcc::inputHost::compactReply::legacyIdentityField(it.key())){auto j=c;j[it.key()]=it.value();probe("mixed-any-legacy",j.dump(),0,0);}
 auto both=f;for(auto it=c.begin();it!=c.end();++it)both[it.key()]=it.value();probe("mixed-full-compact",both.dump(),0,0);both.erase("compactFrameReplyVersion");probe("mixed-full-token-only",both.dump(),0,0);both.erase("frameReplyBinding");both["compactFrameReplyVersion"]=1;probe("mixed-full-marker-only",both.dump(),0,0);
 probe("wrong-request-context",c.dump(),0,0,&tokenA,"different",5);probe("wrong-sequence-context",c.dump(),0,0,&tokenA,"r",6);
 for(const auto& key:{"runId","epoch","powerGeneration","targetId","ownerIdentity","persona","profileIdentity","startupNonce","configRevision","configHash","assetVerified","lockedCoreSha256","lockedCoreMvid","hostInstanceId"}){auto j=f;j.erase(key);probe("full-original-tuple-missing",j.dump(),0,0,nullptr);j=f;j[key]=nullptr;probe("full-original-tuple-null",j.dump(),0,0,nullptr);}
 for(auto it=f.begin();it!=f.end();++it)for(const auto& v:std::vector<json>{nullptr,false,0,"wrong",json::object(),json::array()}){auto j=f;j[it.key()]=v;probe("full-old-type-contract",j.dump(),-1,-1,nullptr);}
 auto saved=g_inputHostHostInstanceId;g_inputHostHostInstanceId.clear();probe("unbound-host-compact",c.dump(),0,0);probe("unbound-host-full",f.dump(),-1,0,nullptr);g_inputHostHostInstanceId=saved;
}
static void parserCorpus(){
 for(bool small:{false,true}){
  const std::string base=(small?compact():full()).dump();const std::string prefix=small?"compact-":"full-";const std::string* binding=small?&tokenA:nullptr;
  for(unsigned u=0;u<=0xffff;++u){const bool surrogate=u>=0xd800&&u<=0xdfff;probe(prefix+"unicode-bmp",append(base,",\"detail\":\""+uescape(u)+"\""),surrogate?0:1,surrogate?0:1,binding);}
  for(unsigned hi=0xd800;hi<=0xdbff;++hi)for(unsigned lo:{0xdc00u,0xdfffu})probe(prefix+"surrogate-pairs",append(base,",\"detail\":\""+uescape(hi)+uescape(lo)+"\""),1,1,binding);
  for(const auto& bad:std::vector<std::string>{"\\uD800","\\uDC00","\\uD800\\u0041","\\uD800\\uD800","\\uDC00\\uD800"}){
   probe(prefix+"unicode-bad-nested",append(base,",\"detail\":{\"nested\":[\""+bad+"\"]}"),0,0,binding);probe(prefix+"unicode-bad-key",append(base,",\"detail\":{\""+bad+"\":true}"),0,0,binding);
  }
  for(const auto& bad:std::vector<std::string>{std::string("\xc0\xaf",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\xe2\x82",2),std::string("\x80",1)})probe(prefix+"bad-utf8",append(base,",\"detail\":\""+bad+"\""),0,0,binding);
  for(const auto& n:std::vector<std::string>{"-1","0.0","1e-9999","-0","1.25","9.3e+20","18446744073709551616"})probe(prefix+"dynamic-nonuint-fallback",append(base,",\"extension\":[{\"x\":"+n+"}]"),0,1,binding);
  for(unsigned e=0;e<10000;e+=97){probe(prefix+"numeric-exponents",append(base,",\"detail\":1e"+std::to_string(e)),0,-1,binding);probe(prefix+"numeric-exponents",append(base,",\"detail\":1e-"+std::to_string(e)),0,1,binding);}
  for(const auto& invalid:std::vector<std::string>{"1.","+1","01",".5","-","NaN","Infinity","[1,]","{\"x\":}","\"\\xAA\"","1e9999","1.7976931348623159e308"})probe(prefix+"malformed-json",append(base,",\"detail\":"+invalid),0,0,binding);
  for(std::size_t n=0;n<base.size();++n)probe(prefix+"every-truncation",base.substr(0,n),0,0,binding);
  // The original DOM accepts a terminal NUL; fast must still fall back.
  for(const auto& suffix:std::vector<std::string>{std::string(1,'\0')," trailing","[]","\"tail\""})probe(prefix+"trailing-junk",base+suffix,0,suffix==std::string(1,'\0')&&!small?1:0,binding);
  probe(prefix+"whitespace"," \t\r\n"+base+"\r\n",1,1,binding);
  for(const auto& shape:std::vector<std::string>{"[]","["+base+"]","null","true","42","\"text\"","{}"})probe(prefix+"wrong-top-level",shape,0,0,binding);
  std::string escaped=base;const auto pos=escaped.find("\"requestId\"");escaped.replace(pos,11,"\"request\\u0049d\"");probe(prefix+"escaped-known-key",escaped,0,1,binding);
  probe(prefix+"escaped-duplicate-key",append(base,",\"request\\u0049d\":\"r\""),0,small?0:1,binding);
  probe(prefix+"escaped-value",append(base.substr(0,base.size()-1)+"}",",\"ex\\u0074ension\":{\"emoji\":\"\\uD83D\\uDE00\",\"x\":1.25}"),0,1,binding);
  std::string deep="0";for(unsigned n=0;n<40;++n)deep="["+deep+"]";probe(prefix+"depth-fallback",append(base,",\"extension\":"+deep),0,1,binding);
  probe(prefix+"nested-identity-is-extension",append(base,",\"extension\":{\"runId\":\"other\",\"frameReplyBinding\":\"other\"}"),1,1,binding);
  auto longWire=append(base,",\"extension\":\""+std::string(20*1024,'x')+"\"");probe(prefix+"over-actual-transport-bound",longWire,0,0,binding);
  std::mt19937_64 rng(20261015);
  for(unsigned n=0;n<4000;++n){auto j=small?compact():full();switch(n%4){case 0:j["detail"]=rng();break;case 1:j["detail"]=static_cast<std::int64_t>(rng());break;case 2:j["detail"]="valid-微星-fixture-"+std::to_string(rng());break;default:j["detail"]={{"nested",json::array({rng(),true,nullptr,"😀"})}};break;}probe(prefix+"seeded-original-generator",j.dump(),-1,1,binding);}
 }
 auto extension=append(compact().dump(),",\"dynamicExtension\":{\"amount\":0.125,\"keys\":[\"新\",\"😀\"]}");probe("extension-still-token-checked",extension,0,1);probe("extension-stale-token",extension,0,0,&tokenB);probe("extension-no-token",extension,0,0,nullptr);
}
static void motor(){
 ymcc::game_feedback::Inbox inbox;inbox.bind(g_inputHostEpoch,fixturePower,9,1);
 for(bool small:{false,true})for(unsigned revision=1;revision<=8;++revision){auto j=small?compact():full();const unsigned left=revision==8?0:revision*13,right=revision==8?0:revision*17;
  j["gameFeedbackGeneration"]=9;j["gameFeedbackRevision"]=revision;j["gameFeedbackLeft"]=left;j["gameFeedbackRight"]=right;j["gameFeedbackCompatibility"]=1;j["gameFeedbackSdkSequence"]=revision*19;
  const auto p=probe("dynamic-motor-revision-and-stop",j.dump(),1,1,small?&tokenA:nullptr);
  require(p.receipt.gameMotor.valid&&p.receipt.gameMotor.revision==revision&&p.receipt.gameMotor.left==left&&p.receipt.gameMotor.right==right&&p.receipt.gameMotor.sdkSequence==revision*19,"dynamic motor was cached or altered");
  if(revision==1)inbox.bind(g_inputHostEpoch,fixturePower,9,1);inbox.stage(p.receipt.gameMotor,revision);require(inbox.commit(revision,g_inputHostEpoch,fixturePower,true),"actual inbox rejected changing motor revision");
  if(revision==8)require(inbox.snapshot().last.left==0&&inbox.snapshot().last.right==0,"stop motor lost");
 }
 auto good=compact();const std::array<std::uint64_t,6> words={9,10,127,255,1,4294967295ull};for(unsigned i=0;i<6;++i)good[std::string(ymcc::game_feedback::fieldNames[i])]=words[i];
 for(unsigned i=0;i<6;++i){const std::string key(ymcc::game_feedback::fieldNames[i]);auto j=good;j.erase(key);auto p=probe("partial-motor-invalid-sample",j.dump(),1,1);require(p.receipt.gameMotor.present&&!p.receipt.gameMotor.valid,"partial motor admitted");
  for(const auto& value:std::vector<json>{nullptr,"bad",-1,1.5,json::array()}){j=good;j[key]=value;p=probe("typed-motor-invalid-sample",j.dump(),-1,1);require(!p.parsed.gameMotor.valid,"invalid DOM motor admitted");}
  p=probe("duplicate-motor-DOM-invalid-sample",append(good.dump(),",\""+key+"\":"+good[key].dump()),0,1);require(!p.parsed.gameMotor.valid,"duplicate motor admitted");
 }
 for(auto [index,value]:std::vector<std::pair<unsigned,std::uint64_t>>{{0,0},{1,0},{2,256},{3,256},{4,0},{4,5},{5,4294967296ull}}){auto j=good;j[std::string(ymcc::game_feedback::fieldNames[index])]=value;auto p=probe("motor-range-invalid",j.dump(),1,1);require(!p.receipt.gameMotor.valid,"out of range motor admitted");}
 auto sample=ymcc::game_feedback::fromWords(words,63);inbox.bind(g_inputHostEpoch,fixturePower,9,1);inbox.stage(sample,99);require(!inbox.commit(100,g_inputHostEpoch,fixturePower,true),"motor sequence bypass");inbox.stage(sample,99);require(inbox.commit(99,g_inputHostEpoch,fixturePower,true),"motor valid commit");inbox.stage(sample,100);require(!inbox.commit(100,g_inputHostEpoch,fixturePower,true),"duplicate motor revision bypass");inbox.revoke();inbox.stage(sample,101);require(!inbox.commit(101,g_inputHostEpoch,fixturePower,true),"revoked motor accepted");
}
static void lifecycle(){
 auto prepared=full();prepared["command"]="PREPARE_TARGET";prepared["status"]="target-prepared";prepared["frameReplyBinding"]=tokenA;prepared["targetPnpAdmitted"]=true;
 auto publish=[&](const json& j,const char* command="PREPARE_TARGET"){InputHostGameMotorDomKeys keys;auto parsed=json::parse(j.dump(),[&](int d,json::parse_event_t e,json& v){return keys(d,e,v);});auto dom=actualDom(j.dump(),nullptr,command,"target-prepared");if(dom.accepted)actualPublish(parsed,keys);return dom.accepted;};
 g_inputHostFrameReplyBinding.clear();require(publish(prepared),"valid full prepare rejected");require(g_inputHostFrameReplyBinding==tokenA,"accepted prepare token not published");
 auto request=std::string("{\"command\":\"SUBMIT_FRAME\"}");require(actualAttach("SUBMIT_FRAME",request),"current frame token not attached");require(json::parse(request)["frameReplyBinding"]==tokenA,"wrong attached token");
 for(const auto& key:{"runId","hostInstanceId","targetPnpAdmitted","ok","status"}){actualRevoke("PREPARE_TARGET");auto bad=prepared;bad[key]=nullptr;require(!publish(bad),"unverified prepare accepted");require(g_inputHostFrameReplyBinding.empty(),"unverified prepare published token");probe("invalid-prepare-old-ACK",compact().dump(),0,0,&g_inputHostFrameReplyBinding);}
 for(const char* command:{"HELLO","PREPARE_TARGET","RECREATE_TARGET","QUIESCE","RELEASE_TARGET","SHUTDOWN"}){g_inputHostFrameReplyBinding=tokenA;actualRevoke(command);require(g_inputHostFrameReplyBinding.empty(),"lifecycle did not revoke token");probe("revoked-token-ACK",compact().dump(),0,0,&g_inputHostFrameReplyBinding);}
 auto next=prepared;next["command"]="RECREATE_TARGET";next["frameReplyBinding"]=tokenB;actualRevoke("RECREATE_TARGET");require(publish(next,"RECREATE_TARGET"),"valid full recreate rejected");require(g_inputHostFrameReplyBinding==tokenB,"recreate token not replaced");probe("old-generation-ACK",compact().dump(),0,0,&g_inputHostFrameReplyBinding);auto fresh=compact();fresh["frameReplyBinding"]=tokenB;probe("new-generation-ACK",fresh.dump(),1,1,&g_inputHostFrameReplyBinding);
 request="{}";require(!actualAttach("QUERY",request)&&request=="{}","token attached to non-frame");g_inputHostFrameReplyBinding.clear();request="{}";require(!actualAttach("SUBMIT_FRAME",request)&&request=="{}","unbound frame asked for compact");
 // This adapter covers token attachment/publication, not product timeout/replay:
 // no fake transport boolean is substituted for the actual request function.
}
struct Handle {HANDLE h=INVALID_HANDLE_VALUE;explicit Handle(HANDLE value=INVALID_HANDLE_VALUE):h(value){}~Handle(){if(h!=INVALID_HANDLE_VALUE&&h)CloseHandle(h);}Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;};
struct Operation {HANDLE io;OVERLAPPED ov{};Handle event;explicit Operation(HANDLE h):io(h),event(CreateEventW(nullptr,TRUE,FALSE,nullptr)){if(!event.h)throw std::runtime_error("pipe event");ov.hEvent=event.h;}~Operation(){CancelIoEx(io,&ov);DWORD ignored=0;GetOverlappedResult(io,&ov,&ignored,TRUE);}};
static void pipeRequire(bool b,const char* what){if(!b)throw std::runtime_error(std::string(what)+":"+std::to_string(GetLastError()));}
static std::string throughPrivatePipe(const std::string& wire,const char* label,bool& backpressure){
 const auto name=L"\\\\.\\pipe\\YMCC-compact-offline-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++pipeCases);
 PSECURITY_DESCRIPTOR descriptor=nullptr;pipeRequire(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;OW)",SDDL_REVISION_1,&descriptor,nullptr)!=FALSE,"pipe DACL");SECURITY_ATTRIBUTES sa{};sa.nLength=sizeof(sa);sa.lpSecurityDescriptor=descriptor;
 Handle server(CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,256,256,0,&sa));LocalFree(descriptor);pipeRequire(server.h!=INVALID_HANDLE_VALUE,"private server");
 Operation connect(server.h);const BOOL connected=ConnectNamedPipe(server.h,&connect.ov);const DWORD connectError=connected?0:GetLastError();pipeRequire(connected||connectError==ERROR_IO_PENDING||connectError==ERROR_PIPE_CONNECTED,"private connect pending");
 Handle client(CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));pipeRequire(client.h!=INVALID_HANDLE_VALUE,"private client");
 if(!connected&&connectError==ERROR_IO_PENDING){pipeRequire(WaitForSingleObject(connect.ov.hEvent,2000)==WAIT_OBJECT_0,"private connect timeout");DWORD transferred=0;pipeRequire(GetOverlappedResult(server.h,&connect.ov,&transferred,FALSE)!=FALSE,"private connect result");}
 Operation write(client.h);DWORD written=0;BOOL w=WriteFile(client.h,wire.data(),static_cast<DWORD>(wire.size()),&written,&write.ov);DWORD writeError=w?0:GetLastError();pipeRequire(w||writeError==ERROR_IO_PENDING,"private write");backpressure=!w&&writeError==ERROR_IO_PENDING;
 std::string received;unsigned fragments=0;for(;;){Operation read(server.h);std::array<char,73> buffer{};DWORD count=0;BOOL r=ReadFile(server.h,buffer.data(),static_cast<DWORD>(buffer.size()),&count,&read.ov);DWORD error=r?0:GetLastError();if(!r&&(error==ERROR_IO_PENDING||error==ERROR_MORE_DATA)){if(error==ERROR_IO_PENDING&&WaitForSingleObject(read.ov.hEvent,2000)!=WAIT_OBJECT_0){CancelIoEx(server.h,&read.ov);WaitForSingleObject(read.ov.hEvent,2000);throw std::runtime_error("private read timeout");}r=GetOverlappedResult(server.h,&read.ov,&count,FALSE);error=r?0:GetLastError();}pipeRequire(r||error==ERROR_MORE_DATA,"private fragmented read");received.append(buffer.data(),count);++fragments;if(r)break;pipeRequire(received.size()<=1024*1024,"private pipe bounded response");}
 if(!w){if(WaitForSingleObject(write.ov.hEvent,2000)!=WAIT_OBJECT_0){CancelIoEx(client.h,&write.ov);WaitForSingleObject(write.ov.hEvent,2000);throw std::runtime_error("private write timeout");}pipeRequire(GetOverlappedResult(client.h,&write.ov,&written,FALSE)!=FALSE,"private write result");}
 pipeFragments+=fragments;transports<<json({{"case",pipeCases},{"label",label},{"wireEqual",received==wire},{"sentBytes",wire.size()},{"writeCompletionBytes",written},{"receivedBytes",received.size()},{"fragments",fragments},{"writePendingBeforeReader",backpressure},{"writeError",writeError},{"complete",true}}).dump()<<'\n';pipeRequire(written==wire.size()&&received==wire,"private wire changed");require(fragments>1,"pipe fragmentation not exercised");return received;
}
static void pipes(){
 bool pending=false;probe("private-pipe-full",throughPrivatePipe(full().dump(),"full",pending),1,1,nullptr);probe("private-pipe-compact",throughPrivatePipe(compact().dump(),"compact",pending),1,1);
 auto pressured=append(compact().dump(),",\"extension\":\""+std::string(12*1024,'x')+"\"");probe("private-pipe-backpressure",throughPrivatePipe(pressured,"compact-backpressure",pending),1,1);require(pending,"private pipe backpressure not exercised");
 auto malformed=compact().dump();malformed.resize(malformed.size()-3);probe("private-pipe-truncated",throughPrivatePipe(malformed,"truncated",pending),0,0);
 auto bad=compact();bad["frameReplyBinding"]=tokenB;probe("private-pipe-stale-token",throughPrivatePipe(bad.dump(),"wrong-token",pending),0,0);
}

// Optional actual Host-generated first-witness payload: the original scanner and
// DOM contract must keep it across full/compact replies and a private pipe.
static void sdkWitnesses(const std::string& out){
 std::ifstream input(out+"/../../inputs/sdk-first-response.json");if(!input)return;
 json actual;input>>actual;require(actual.contains("sdkFirstWitnesses")&&actual["sdkFirstWitnesses"].is_array()&&!actual["sdkFirstWitnesses"].empty(),"actual SDK sample missing");
 auto synthetic=full();synthetic["sdkFirstWitnesses"]=json::array({{{"outcome","returned"},{"sample",{{"ordinal","1"},{"sdkArguments",{{"kind","SubmitRawReport"},{"hex","000100"}}}}}}});
 auto p=probe("logging-sdk-synthetic-witness",synthetic.dump(),0,1,nullptr);require(p.parsed.response["sdkFirstWitnesses"]==synthetic["sdkFirstWitnesses"],"integer-only evidence skipped by fast parser");
 auto coverage=full();coverage["sdkFirstWitnessCoverage"]={{"emittedSamples",0},{"dropped",1},{"scope","response-emission-only"}};
 p=probe("logging-sdk-coverage-only",coverage.dump(),0,1,nullptr);require(p.parsed.response["sdkFirstWitnessCoverage"]==coverage["sdkFirstWitnessCoverage"],"coverage-only evidence lost");
 auto f=full(),c=compact();for(const char* key:{"sdkFirstWitnesses","sdkFirstWitnessCoverage"}){f[key]=actual[key];c[key]=actual[key];}
 p=probe("logging-sdk-actual-full",f.dump(),0,1,nullptr);require(p.parsed.response["sdkFirstWitnesses"]==actual["sdkFirstWitnesses"]&&p.parsed.response["sdkFirstWitnessCoverage"]==actual["sdkFirstWitnessCoverage"],"actual full SDK payload changed");
 p=probe("logging-sdk-actual-compact",c.dump(),0,1);require(p.parsed.expanded&&p.parsed.response["sdkFirstWitnesses"]==actual["sdkFirstWitnesses"],"compact DOM expansion dropped SDK payload");
 auto bad=f;bad["requestId"]="wrong-request";probe("logging-sdk-bad-request",bad.dump(),0,0,nullptr);
 bad=f;bad["powerGeneration"]=fixturePower+1;probe("logging-sdk-bad-power",bad.dump(),0,0,nullptr);
 probe("logging-sdk-stale-binding",c.dump(),0,0,&tokenB);
 bool pending=false;p=probe("logging-sdk-private-pipe",throughPrivatePipe(f.dump(),"logging-sdk-witness",pending),0,1,nullptr);
 require(p.parsed.response["sdkFirstWitnesses"]==actual["sdkFirstWitnesses"]&&p.parsed.response["sdkFirstWitnessCoverage"]==actual["sdkFirstWitnessCoverage"],"pipe SDK payload changed");
}

static void focusedFailures(){
 const auto f=full().dump(),c=compact().dump();probe("directed-full-terminal-nul",f+std::string(1,'\0'),0,1,nullptr);probe("directed-compact-terminal-nul",c+std::string(1,'\0'),0,0);probe("directed-compact-nul-suffix",c+std::string(1,'\0')+"{garbage}",0,0);
 auto extension=append(c,",\"extension\":{\"amount\":1.25,\"key\":\"新😀\"}");probe("directed-extension-fallback",extension,0,1);probe("directed-extension-stale-token",extension,0,0,&tokenB);
 const auto emptyExtension=append(c,",\"extension\":\"\"");auto bounded=append(c,",\"extension\":\""+std::string(16*1024-emptyExtension.size(),'x')+"\"");auto p=probe("directed-exact-16KiB",bounded,1,1);require(p.dom,"exact response bound rejected");bounded.insert(bounded.size()-2,"x");p=probe("directed-over-16KiB",bounded,0,0);InputHostFastReceipt receipt;require(!c7aReceiptFast::scanSubmitFrameReceiptBound(bounded.data(),bounded.data()+bounded.size(),"frame-accepted","r",5,receipt,&tokenA),"scanner omitted actual 16KiB bound");
 auto defaults=full();defaults.erase("firstFrame");defaults.erase("submitElapsedUs");p=probe("directed-full-default-values",defaults.dump(),1,1,nullptr);require(!p.receipt.firstFrame&&p.receipt.submitElapsedUs==0,"legacy full defaults changed");
 probe("directed-valid-unicode",append(c,",\"extension\":\"\\uD83D\\uDE00\""),1,1);probe("directed-invalid-unicode",append(c,",\"extension\":\"\\uD800\""),0,0);probe("directed-invalid-utf8",append(c,",\"extension\":\""+std::string("\xc0\xaf",2)+"\""),0,0);
 InputHostGameMotorDomKeys keys;const auto parsed=json::parse(c,[&](int d,json::parse_event_t e,json& v){return keys(d,e,v);});auto unobserved=parsed;require(!inputHostExpandCompactFrameReplyLocked(unobserved,&tokenA,keys.compact),"unobserved raw wire admitted");
}


static void requestBudget(const std::string& out,bool negativeOnly=false){
 std::ofstream details(out+"/request-budget.jsonl",std::ios::binary);
 if(!details)throw std::runtime_error("request budget raw log");
 const auto limit=ymcc::inputHost::compactReply::kMaxReplyBytes;
 const auto overhead=tokenA.size()+sizeof(",\"frameReplyBinding\":\"\"")-1;
 require(overhead==87,"binding byte overhead changed");
 auto base=[](const std::string& source="ascii"){return "{\"command\" : \"SUBMIT_FRAME\", \"sequence\":17,\"source\":\""+source+"\",\"pad\":\"\"}";};
 auto diagnostic=[&](std::string text,bool enabled,const char* command){
  if(enabled&&std::strcmp(command,"SUBMIT_FRAME")==0&&!text.empty()&&text.back()=='}')
   text.insert(text.size()-1,",\"msiReportChainSource\":"+fixtureDiagnosticMetadata.dump());
  return text;
 };
 auto sized=[&](std::size_t finalSize,bool enabled,const std::string& source="ascii"){
  auto text=base(source);auto full=diagnostic(text,enabled,"SUBMIT_FRAME");
  if(full.size()>finalSize)throw std::runtime_error("bad sized fixture");
  const auto at=text.find("\"pad\":\"\"")+7;
  text.insert(at,finalSize-full.size(),'x');return text;
 };
 auto execute=[&](const char* name,const std::string& before,bool enabled,const std::string& token,bool wantAttach,const char* command="SUBMIT_FRAME",bool oldOrder=false){
  ++cases;const auto group=std::string("request-budget-")+name;groups[group]=1;
  const auto failedBefore=failures;
  fixtureDiagnosticEnabled=enabled;fixtureDiagnosticAppend=false;fixtureDiagnosticCalls=0;g_inputHostFrameReplyBinding=token;
  auto text=before;const bool attached=oldOrder?oldOrderAttachNegativeControl(command,text):actualAttach(command,text);
  auto full=diagnostic(before,enabled,command),expected=oldOrder?before:full;
  if(wantAttach)expected.insert(expected.size()-1,",\"frameReplyBinding\":\""+token+"\"");
  if(oldOrder)expected=diagnostic(expected,enabled,command);
  require(attached==wantAttach,"wrong request binding eligibility",text);
  require(text==expected,"original request or diagnostic bytes changed",text);
  require(fixtureDiagnosticCalls==1,"diagnostic glue duplicated or omitted",text);
  if(!oldOrder&&attached)require(text.size()<=limit,"bound request exceeds 16KiB",text);
  if(oldOrder)require(attached&&text.size()==limit+1,"negative old ordering did not reproduce overflow",text);
  if(!text.empty()&&text.back()=='}'){
   const auto parsed=json::parse(text);require(parsed["sequence"]==17,"original sequence changed",text);
   require(parsed.contains("frameReplyBinding")==attached,"binding bytes and state disagree",text);
   if(enabled&&std::strcmp(command,"SUBMIT_FRAME")==0)require(parsed["msiReportChainSource"]==fixtureDiagnosticMetadata,"diagnostic metadata changed",text);
  }
  raw<<cases<<'\t'<<group<<'\t'<<text.size();for(int i=0;i<16;++i)raw<<"\tNA";raw<<'\n';
  details<<json({{"case",cases},{"group",group},{"beforeBytes",before.size()},{"diagnosticFullBytes",full.size()},{"afterBytes",text.size()},{"attached",attached},{"diagnosticEnabled",enabled},{"diagnosticAppend",fixtureDiagnosticAppend},{"oldOrderNegativeControl",oldOrder},{"failures",failures-failedBefore},{"hardwareExecuted",false}}).dump()<<'\n';
 };
 if(!negativeOnly){
 execute("normal-no-diagnostic",base(),false,tokenA,true);
 execute("diagnostic-small",base(),true,tokenA,true);
 execute("diagnostic-exact-limit",sized(limit-overhead,true),true,tokenA,true);
 execute("diagnostic-one-byte-short",sized(limit-overhead+1,true),true,tokenA,false);
 execute("diagnostic-full-limit",sized(limit,true),true,tokenA,false);
 execute("diagnostic-over-limit",sized(limit+1,true),true,tokenA,false);
 execute("utf8-exact-limit",sized(limit-overhead,true,"中文🎮"),true,tokenA,true);
 execute("non-frame",base(),false,tokenA,false,"QUERY");
 execute("unbound",base(),true,"",false);
 execute("invalid-token",base(),true,std::string(64,'g'),false);
 execute("empty","",true,tokenA,false);
 execute("missing-terminal-brace","{",true,tokenA,false);
 }
 execute("negative-old-order",sized(limit-overhead+1,true),true,tokenA,true,"SUBMIT_FRAME",true);
 if(!negativeOnly)execute("no-diagnostic-exact-limit",sized(limit-overhead,false),false,tokenA,true);
 fixtureDiagnosticEnabled=false;g_inputHostFrameReplyBinding.clear();
}

int main(int argc,char**argv){
 if(argc!=2&&argc!=4)return 2;const std::string selected=argc==4?argv[3]:"all";const bool directed=selected=="compact-regressions",budget=selected=="request-budget"||selected=="request-budget-negative";if(argc==4&&(std::string(argv[2])!="--case-group"||(!directed&&!budget)))return 2;const std::string out=argv[1];raw.open(out+"/cases.tsv",std::ios::binary);errors.open(out+"/failures.jsonl",std::ios::binary);transports.open(out+"/pipe.jsonl",std::ios::binary);if(!raw||!errors||!transports)return 3;
 raw<<"case\tgroup\tbytes\trawScanner\tfast\tdom\texpectedFast\texpectedDom\texpanded\tfirstFrame\tsubmitElapsedUs\tmotorPresent\tmotorValid\tgeneration\trevision\tleft\tright\tcompatibility\tsdkSequence\n";
 bool complete=false;std::string exception;try{if(budget)requestBudget(out,selected=="request-budget-negative");else{core();if(directed)focusedFailures();else parserCorpus();motor();lifecycle();pipes();sdkWitnesses(out);}complete=true;}catch(const std::exception& e){exception=e.what();require(false,exception.c_str());}
 raw.flush();errors.flush();transports.flush();json summary={{"selectedCaseGroup",selected},{"status",complete&&failures==0?"PASS":"FAIL"},{"complete",complete},{"cases",cases},{"checks",checks},{"failures",failures},{"fastAccepted",fastAccepted},{"domAccepted",domAccepted},{"fallbackAccepted",fallbackAccepted},{"groups",groups},{"pipeCases",pipeCases},{"pipeFragments",pipeFragments},{"exception",exception},{"performanceMeasured",false},{"realTransportScope",budget?"NOT_EXECUTED; request construction only":"private Win32 message pipes only; 73-byte diagnostic reassembly, not product transport integration"},{"oneSendTimeoutReplay","source-bound attachment only; full product timeout/replay path NOT_EXECUTED"}};std::ofstream(out+"/summary.json")<<summary.dump(2)<<'\n';std::cout<<summary.dump()<<'\n';return complete&&failures==0?0:1;
}
