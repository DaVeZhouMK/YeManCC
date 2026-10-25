import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';import {spawnSync} from 'node:child_process';import {createHash} from 'node:crypto';import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),args=process.argv.slice(2);
if(args.length!==2||args[0]!=='--out')throw Error('Usage --out evidence-directory');const out=path.resolve(args[1]);fs.mkdirSync(out,{recursive:true});
const main=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8').replace(/\r\n/g,'\n'),sourceHash=createHash('sha256').update(fs.readFileSync(path.join(repo,'native/main.cpp'))).digest('hex');
const signatures=['static const std::vector<std::string>& ymccDeviceClassLightingSet()','static const std::vector<std::string>& ymccDeviceClassSecondColorSet()','static bool ymccDeviceClassSupportsLighting(','static bool ymccDeviceClassSupportsSecondColor(','static std::vector<std::string> ymccDeviceClassLedEffects('];
function block(sig){const start=main.indexOf(sig),end=main.indexOf('\n}',start);if(start<0||end<0)throw Error('Missing actual function '+sig);return main.slice(start,end+2);}
const fixture=path.join(repo,'tools/fixtures/hc_lighting_catalog');
const include=file=>'#include '+JSON.stringify(file.replaceAll('\\','/'))+'\n';
const code=String.raw`#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
`+include(path.join(repo,'native/oem_lighting_operations.h'))+include(path.join(repo,'native/lighting_device_core_binding.h'))+include(path.join(fixture,'operations-before.h'))+include(path.join(fixture,'binding-before.h'))+'namespace before {\n'+fs.readFileSync(path.join(fixture,'native-before.inc'),'utf8')+'\n}\n'+signatures.map(block).join('\n')+String.raw`
int main(int argc,char**argv){std::ifstream f(argv[1]);json catalog;f>>catalog;int checks=0,models=0;std::vector<std::string>failures;
auto check=[&](bool ok,const std::string&id){++checks;if(!ok)failures.push_back(id);};
for(const auto& d:catalog["devices"]){++models;const std::string cls=d["consumerClass"];
check(ymccDeviceClassSupportsLighting(cls)==before::ymccDeviceClassSupportsLighting(cls),cls+":lighting");
check(ymccDeviceClassSupportsSecondColor(cls)==before::ymccDeviceClassSupportsSecondColor(cls),cls+":second-color");
check(ymccDeviceClassLedEffects(cls)==before::ymccDeviceClassLedEffects(cls),cls+":effect-order");
check(ymcc::lighting::presetImplemented(cls)==before::lighting::presetImplemented(cls),cls+":preset-implementation");
check(ymcc::lighting::presetDeclaredButUnavailable(cls)==before::lighting::presetDeclaredButUnavailable(cls),cls+":preset-source-disabled");
if(ymccDeviceClassSupportsLighting(cls))check(ymcc::lighting::brightnessImplemented(cls)==before::lighting::brightnessImplemented(cls),cls+":existing-brightness");
for(const char* action:{"color","brightness","status","off","preset","unknown"})for(const char*mode:{"solid","rainbow","preset","wave","","SOLID"})
check(ymcc::lighting::useIndependentDeviceCore(cls,action,mode)==before::lighting::useIndependentDeviceCore(cls,action,mode),cls+":backend:"+action+":"+mode);
}
check(ymcc::lighting::kOneXPresets.size()==before::lighting::kOneXPresets.size(),"preset-count");
for(size_t i=0;i<ymcc::lighting::kOneXPresets.size();i++){const auto&a=ymcc::lighting::kOneXPresets[i];const auto&b=before::lighting::kOneXPresets[i];check(std::string(a.id)==b.id&&a.hcValue==b.hcValue&&std::string(a.label)==b.label,"preset-value-and-order");}
check(!ymcc::lighting::brightnessImplemented("Unreviewed.Vendor.ROGAlly"),"unknown-brightness-denied");
json result={{"status",failures.empty()?"PASS":"FAIL"},{"checks",checks},{"models",models},{"failures",failures},{"hardwareIo",false}};std::cout<<result.dump();return failures.empty()?0:1;}
`;
fs.writeFileSync(path.join(out,'actual.cpp'),code);const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
fs.writeFileSync(path.join(out,'build.cmd'),calibrationToolchainPreamble(vcvars)+`cl /nologo /std:c++20 /utf-8 /EHsc /MT "${out}/actual.cpp" /I"${repo}/deps/json" /Fo"${out}/actual.obj" /Fe"${out}/actual.exe"\r\nexit /b %errorlevel%\r\n`);
function run(exe,args,log){const r=spawnSync(exe,args,{cwd:out,encoding:'utf8',windowsHide:true,timeout:120000,maxBuffer:2*1024*1024});fs.writeFileSync(path.join(out,log),(r.stdout??'')+(r.stderr??''));if(r.status!==0)throw Error('Native parity failed: '+log);return r.stdout;}
run('cmd.exe',['/d','/c',path.join(out,'build.cmd')],'compile.log');const result=JSON.parse(run(path.join(out,'actual.exe'),[path.join(repo,'capabilities/hc-device-catalog.json')],'execute.log'));result.sourceHash=sourceHash;
if(createHash('sha256').update(fs.readFileSync(path.join(repo,'native/main.cpp'))).digest('hex')!==sourceHash)throw Error('Native source changed during parity run');
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result));
