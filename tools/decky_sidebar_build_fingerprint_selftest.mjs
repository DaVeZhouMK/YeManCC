// Verify the actual compiled plugin's immutable fingerprint and final SHA.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';
import {root,task,validation} from './fixtures/decky_task_paths.mjs';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const manifest=JSON.parse(fs.readFileSync(path.join(validation,'plugin-build.json'),'utf8').replace(/^\ufeff/,'')),bytes=fs.readFileSync(path.join(task,'Build/DeckyPlugin/dist/index.js'));
assert.match(manifest.buildId,/^[a-f0-9]{64}$/);assert(bytes.includes(Buffer.from(manifest.buildId)));
assert.equal(sha(bytes),manifest.files.find(x=>x.name==='dist/index.js').sha256);
assert.equal(sha(Buffer.from(bytes.toString().replaceAll(manifest.buildId,'0'.repeat(64)))),manifest.buildId);
const builder=fs.readFileSync(path.join(root,'tools/build-decky-sidebar-plugin.mjs'),'utf8');assert(builder.includes("const template=await build(options),buildId=hash(withFrontendVersion(template.outputFiles[0].contents,'0'.repeat(64)))"));
const cpp=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');const callback=cpp.slice(cpp.indexOf('static std::string deckySidebarStartMirror()'),cpp.indexOf('// YMCC Decky sidebar:',cpp.indexOf('static std::string deckySidebarStartMirror()')));
assert(callback.includes('[buildReceipt=std::string{}]'));assert(callback.includes('if(build!=buildReceipt)'));assert(callback.includes('ymcc::deckydiag::validBuildId'));
const fixture=`#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>
#include "decky_sidebar_diagnostics.h"
using json=nlohmann::json;
struct FakeBroker {struct Config{std::function<void(const std::string&,const json&)> request;std::function<void(const std::string&,const std::string&,bool)> peer;};Config config;bool start(Config c){config=std::move(c);return true;}void stop(){}json status(){return {{"connection","1"},{"runId",std::string(32,'a')},{"revision",0}};}bool reply(const std::string&,const json&){return true;}}g_deckyMirrorBroker;
std::vector<json> logs;
static void deckySidebarDiagnostic(const char* event,const json& detail,bool=false){logs.push_back(ymcc::deckydiag::record(event,detail));}
static bool deckySidebarPost(const char*,const json&){return true;}
static std::string deckySidebarInjectBinding(){return {};}
`+callback+`
static size_t builds(){return std::count_if(logs.begin(),logs.end(),[](const auto& r){return r.value("event",std::string{})=="plugin-build";});}
int main(){try{
 const std::string a(64,'a'),b(64,'b');
 auto packet=[&](const std::string& id){return json{{"command","snapshot"},{"buildId",id},{"subscribe",true}};};
 if(!deckySidebarStartMirror().empty())throw std::runtime_error("start");
 for(unsigned n=0;n<1000;++n)g_deckyMirrorBroker.config.request("1",packet(a));
 if(builds()!=1)throw std::runtime_error("1000 snapshots must produce one build record");
 g_deckyMirrorBroker.config.request("1",packet(b));if(builds()!=2)throw std::runtime_error("replacement build not recorded");
 deckySidebarStartMirror();g_deckyMirrorBroker.config.request("1",packet(b));if(builds()!=3)throw std::runtime_error("new broker lifecycle must record even the same build");
 g_deckyMirrorBroker.config.request("2",packet(a));if(builds()!=3)throw std::runtime_error("stale peer recorded");
 g_deckyMirrorBroker.config.request("1",packet("private-path"));if(builds()!=3)throw std::runtime_error("arbitrary build label recorded");
 g_deckyMirrorBroker.config.request("1",json{{"command","snapshot"},{"subscribe",false}});
 if(logs.back().value("subscribe",true))throw std::runtime_error("pause direction lost");
 std::cout<<"NATIVE_BUILD_RECEIPT_OK passed=6 realSteam=0 realPorts=0"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
`;
fs.writeFileSync(path.join(validation,'native-build-receipt-fixture.cpp'),fixture);
const report={status:'passed',checks:7,buildId:manifest.buildId,finalPluginSha256:sha(bytes),actualCompiledPlugin:true,scope:'Fingerprint normalizes the fixed build-ID slot in bundled runtime inputs; final file SHA independently identifies exported bytes. Native dedup is scoped to a broker lifecycle, not every snapshot.'};
fs.writeFileSync(path.join(validation,'BUILD-FINGERPRINT.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));
