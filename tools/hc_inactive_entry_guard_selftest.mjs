import fs from'node:fs';import path from'node:path';import{createHash}from'node:crypto';import{spawnSync}from'node:child_process';import{pathToFileURL,fileURLToPath}from'node:url';
// Reusable disabled-entry boundary check. Executes actual consumer definitions with memory IO only.
const opt={};for(let i=2;i<process.argv.length;i+=2){const name=process.argv[i];if(!['--repo','--out'].includes(name)||!process.argv[i+1]||opt[name.slice(2)])throw Error('Usage: --out fresh-evidence-directory [--repo source-root]');opt[name.slice(2)]=process.argv[i+1];}
const repo=path.resolve(opt.repo??path.join(path.dirname(fileURLToPath(import.meta.url)),'..')),out=path.resolve(opt.out??'');if(!opt.out||fs.existsSync(out))throw Error('FRESH_EVIDENCE_REQUIRED');const relativeOut=path.relative(repo,out);if(relativeOut===''||(!path.isAbsolute(relativeOut)&&relativeOut!=='..'&&!relativeOut.startsWith('..'+path.sep)))throw Error('EVIDENCE_OUTSIDE_REPO_REQUIRED');fs.mkdirSync(out,{recursive:true});
const sha=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex'),sourceFile=repo+'/native/main.cpp',sourceBytes=fs.readFileSync(sourceFile),main=sourceBytes.toString('utf8');fs.writeFileSync(out+'/main.before.cpp',sourceBytes,{flag:'wx'});
const{extract}=await import(pathToFileURL(repo+'/tools/fixtures/legion_motion_consumer/extract_main.mjs'));const fragments=['nativeSecondaryDisplayRequest','nativeSecondaryDisplayKeyboardConsume'].map(symbol=>({symbol,...extract(main,symbol)})),fragmentFile=out+'/actual-entry-fragments.json';fs.writeFileSync(fragmentFile,JSON.stringify(fragments,null,2)+'\n');const production=fragments.map(f=>f.code.replaceAll('\r\n','\n')).join('\n\n');
const declarations=main.match(/^static constexpr bool kSecondaryDisplayProductEnabled = false;$/gm);if(declarations?.length!==1)throw Error('PRODUCT_DISABLED_CONSTANT_NOT_ACTUAL_FALSE');const declaration=declarations[0];
const evaluate=extract(main,'nativeKeyboardShortcutsEvaluate').code.replaceAll('\r\n','\n'),empty='if (g_nativeKeyboardShortcuts.empty()) { nativeOemKeyboardConsumePublish(); return; }',mask='const uint64_t oemMask = oemEdge ? *oemEdge : nativeOemKeyMask(true);';if(!evaluate.includes('if constexpr (kSecondaryDisplayProductEnabled)')||!evaluate.includes(empty)||!evaluate.includes(mask)||evaluate.indexOf(empty)>evaluate.indexOf(mask))throw Error('DISABLED_ENTRY_STEADY_LOOP_BOUNDARY_CHANGED');
fs.writeFileSync(out+'/scope-source-contract.json',JSON.stringify({constant:declaration,constantFromActualMain:true,compilerFixtureUsesExactDeclaration:true,oemCallerCheck:'source-structure only; complete OEM function is not executed by this check',callerDefinitionSha256:createHash('sha256').update(evaluate).digest('hex'),sourceMainSha256:sha(sourceFile),protocolOrDisplayActivationTest:false},null,2)+'\n');
const{calibrationToolchainPreamble}=await import(pathToFileURL(repo+'/tools/fixtures/hc_reviewed_calibration/toolchain.mjs'));
const prefix=String.raw`#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>
#include "secondary_display_client.h"
using json=nlohmann::json;
static unsigned powerQueries=0,hardwareQueries=0,leases=0,pipeRequests=0,queueSubmits=0,logs=0,cases=0;
static std::uint64_t currentPowerGeneration(){++powerQueries;return 7;}
static bool hardwareWriteAllowed(){++hardwareQueries;return true;}
struct {std::string deviceClass="HandheldCompanion.Devices.AYANEOFlipDS";bool resolved=true;}g_machineIdentity;
struct {bool isolated=false;}g_aiFanMockSession;
static std::atomic<bool>g_exitRequested{false},g_shortcutRecordingActive{false};
static std::atomic<std::uint64_t>g_oemRearMapToL5{0},g_oemRearMapToR5{0},g_oemShortcutEdgeEpoch{3};
static std::uint64_t g_nativeKeyboardShortcutRevision=11;
struct Rule{std::uint64_t requiredMask=0;};static std::vector<Rule>g_nativeKeyboardShortcuts;
static constexpr std::uint64_t kOemBitScreen=8;
struct HardwareWriteLease{explicit HardwareWriteLease(const char*){++leases;}};
static std::string POWER_CONTROL_DIR="owned-fixture";
static std::string yemancc_data_dir(){return "owned-fixture-data";}
namespace ymcc::batterycharge {static json request(const std::string&,const std::string&,const json&){++pipeRequests;return json{{"ok",false}};}}
static void appendNativeLifecycleLog(const char*,json){++logs;}
enum class SerialJobKind{YmccOwn};
template<class Job>static bool nativeYmccShortcutSubmit(Job&&,SerialJobKind,std::uint64_t){++queueSubmits;return true;}
static constexpr bool kSecondaryDisplayProductEnabled=false;
`;
const compiledPrefix=prefix.replace('static constexpr bool kSecondaryDisplayProductEnabled=false;',declaration);
const suffix=String.raw`
static bool quiet(){return powerQueries==0&&hardwareQueries==0&&leases==0&&pipeRequests==0&&queueSubmits==0&&logs==0;}
int main(){try{
 for(const std::string model:{"HandheldCompanion.Devices.AYANEOFlipDS","HandheldCompanion.Devices.AYANEOFlip1SDS","Unknown.Model",""}){
  g_machineIdentity.deviceClass=model;
  for(const std::string operation:{"get","set","toggle","release","unknown",""}){const auto result=nativeSecondaryDisplayRequest(operation,json{{"target",50},{"fence",std::string(64,'a')}});if(!quiet())throw std::runtime_error("disabled-api-reached-work");if(result.value("ok",true)||result.value("supported",true)||result.value("reason",std::string{})!="副屏能力未启用，仅保留未来接入资料")throw std::runtime_error("disabled-api-failure-contract");++cases;}
  for(std::uint64_t producer:{0ull,1ull})for(std::uint64_t mask:{0ull,1ull,8ull,~0ull}){nativeSecondaryDisplayKeyboardConsume(mask,3,producer);if(!quiet())throw std::runtime_error("disabled-keyboard-reached-work");++cases;}
 }
 std::cout<<json{{"status","PASS"},{"cases",cases},{"powerQueries",powerQueries},{"hardwareQueries",hardwareQueries},{"leases",leases},{"pipeRequests",pipeRequests},{"queueSubmits",queueSubmits},{"logs",logs},{"hardwareIo",false}}.dump()<<'\n';return 0;
}catch(const std::exception&e){std::cout<<json{{"status","FAIL"},{"cases",cases},{"error",e.what()}}.dump()<<'\n';return 1;}}
`;
const sourceInputs=[sourceFile,fragmentFile,fileURLToPath(import.meta.url),repo+'/tools/fixtures/legion_motion_consumer/extract_main.mjs',repo+'/native/secondary_display_client.h',repo+'/deps/json/json.hpp',repo+'/tools/fixtures/hc_reviewed_calibration/toolchain.mjs'];const inputs=sourceInputs.map(file=>({file,sha256:sha(file)}));const variants=[{id:'current',body:production,expected:null},{id:'request-guard-removed',body:production.replace('    if constexpr (!kSecondaryDisplayProductEnabled) return ymcc::secondarydisplay::failure("副屏能力未启用，仅保留未来接入资料");\n',''),expected:'disabled-api-reached-work'},{id:'keyboard-guard-removed',body:production.replace('    if constexpr (!kSecondaryDisplayProductEnabled) return;\n',''),expected:'disabled-keyboard-reached-work'}];for(const variant of variants)if(variant.id!=='current'&&variant.body===production)throw Error('FAULT_ANCHOR_MISSING');const results=[];
for(const variant of variants)for(const configuration of ['Release','Checked']){
 const dir=out+'/'+variant.id+'/'+configuration;fs.mkdirSync(dir,{recursive:true});const cpp=dir+'/probe.cpp';fs.writeFileSync(cpp,compiledPrefix+variant.body+suffix);const flags=configuration==='Checked'?'/Od /RTC1 /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2 /MDd':'/O2 /DNDEBUG /MD';const cmd=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+'cl /nologo /std:c++20 /EHsc /utf-8 /W4 /permissive- '+flags+' /I"'+repo+'/native" /I"'+repo+'/deps/json" "'+cpp+'" /Fo"'+dir+'/probe.obj" /Fe"'+dir+'/probe.exe" /sourceDependencies "'+dir+'/dependencies.json"\r\nexit /b %errorlevel%\r\n';fs.writeFileSync(dir+'/compile.cmd',cmd);
 const build=spawnSync('cmd.exe',['/d','/c',dir+'/compile.cmd'],{cwd:dir,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:4*1024*1024});fs.writeFileSync(dir+'/compile.stdout.log',build.stdout??'');fs.writeFileSync(dir+'/compile.stderr.log',build.stderr??'');if(build.error||build.status!==0)throw Error('GUARD_COMPILE_FAILED:'+variant.id+':'+configuration);
 const execution=spawnSync(dir+'/probe.exe',[],{cwd:dir,encoding:'utf8',windowsHide:true,timeout:10000});fs.writeFileSync(dir+'/stdout.log',execution.stdout??'');fs.writeFileSync(dir+'/stderr.log',execution.stderr??'');if(execution.error||execution.signal)throw Error('GUARD_EXECUTION_NOT_NATURAL');const report=JSON.parse(execution.stdout.trim()),accepted=variant.expected?execution.status===1&&report.status==='FAIL'&&report.error===variant.expected:execution.status===0&&report.status==='PASS'&&report.cases===56;const row={variant:variant.id,configuration,compiled:true,naturalExit:true,exitCode:execution.status,accepted,report};results.push(row);console.log(JSON.stringify(row));if(!accepted)throw Error('GUARD_ASSERTION_NOT_ACCEPTED');
}
const drift=inputs.filter(input=>sha(input.file)!==input.sha256);if(drift.length)throw Error('GUARD_INPUT_DRIFT');const result={schemaVersion:1,status:'PASS',actualProductionFunctions:true,positiveConfigurations:2,compiledGuardFaults:2,negativeExecutions:4,results,sourceInputs:inputs,sourceDrift:drift,scope:'disabled-entry no-work guard only; no display protocol/effect tests, no production main, Host or hardware',mainlineModified:false};fs.writeFileSync(out+'/result.json',JSON.stringify(result,null,2)+'\n');
