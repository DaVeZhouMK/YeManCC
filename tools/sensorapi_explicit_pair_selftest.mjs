import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';import {spawnSync} from 'node:child_process';import {createHash} from 'node:crypto';import assert from 'node:assert/strict';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),root=path.resolve(repo,'../../../Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/SensorApiExplicitPair-20261018');
const control=process.argv[2]||'';assert.ok(['','--expected-missing-contract','--expected-shared-source-failure','--ignore-request','--allow-duplicates','--guess-default'].includes(control));
const out=path.join(root,new Date().toISOString().replace(/[:.]/g,'-')+'-'+process.pid+'-'+(control||'baseline'));fs.mkdirSync(out,{recursive:true});
const sha=b=>createHash('sha256').update(b).digest('hex'),inputs=[],freeze=p=>{const bytes=fs.readFileSync(p);inputs.push({path:p,sha256:sha(bytes)});return bytes;},result={schema:1,status:'running',control,inputs,configurations:[],hardwareIo:false,newProductionDlls:[]};
const save=()=>fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2)+'\n');
function once(text,from,to){assert.equal(text.split(from).length,2,from.slice(0,90));return text.replace(from,()=>to);}
try{
 freeze(fileURLToPath(import.meta.url));freeze(path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'));
 let transport=freeze(path.join(repo,'tools/msi_sensor_adapter_selftest.cpp')).toString('utf8');transport=transport.slice(transport.indexOf('using namespace ymcc::msi_sensor;'),transport.indexOf('int main()'));
 transport=once(transport,'bool block=false,freeze=false,','bool combined=false,duplicateIds=false,block=false,freeze=false,');
 transport=once(transport,'std::shared_ptr<Shared> state;Kind kind;unsigned sequence;','std::shared_ptr<Shared> state;Kind kind;unsigned sequence,ordinal;');
 transport=once(transport,'Report(std::shared_ptr<Shared> s,Kind k,unsigned seq):state(std::move(s)),kind(k),sequence(seq)','Report(std::shared_ptr<Shared> s,Kind k,unsigned seq,unsigned id):state(std::move(s)),kind(k),sequence(seq),ordinal(id)');
 transport=once(transport,'new Report(state,kind,count)','new Report(state,kind,count,id.Data1)');
 transport=once(transport,'new Sensor(state,Kind::Gyro,3)','new Sensor(state,Kind::Gyro,state->duplicateIds?1:3)');
 transport=once(transport,'if(!IsEqualGUID(key.fmtid,kClawHidMotion)||key.pid<7||key.pid>9||(state->missingAxis&&key.pid==9))return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);','const DWORD first=state->combined?(key.pid>=10?10:2):(kind==Kind::Gyro?10:2);if(!IsEqualGUID(key.fmtid,kSdkMotion)||key.pid<first||key.pid>first+2||(state->missingAxis&&key.pid==first+2))return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);');
 transport=once(transport,'out->dblVal=static_cast<double>((kind==Kind::Gyro?10:100)+key.pid);','out->dblVal=first==10?double(ordinal*10+key.pid-first+1):std::array<double,3>{.25,-.5,1}[key.pid-first];');
 transport=once(transport,'*out=GUID{};return S_OK;}// EX-style custom type filtered by WinRT.','*out=state->combined?GUID{}:(kind==Kind::Gyro?kGyroType:kAccelType);return S_OK;}// Standard SDK-only source.');
 transport=once(transport,'for(DWORD p=7;p<=9;++p)keys->keys.push_back(PROPERTYKEY{kClawHidMotion,p});','if(state->combined){for(DWORD p:{2u,3u,4u,10u,11u,12u})keys->keys.push_back(PROPERTYKEY{kSdkMotion,p});}else{const DWORD first=kind==Kind::Gyro?10:2;for(DWORD p=first;p<=first+2;++p)keys->keys.push_back(PROPERTYKEY{kSdkMotion,p});}');
 transport=once(transport,'*out=IsEqualGUID(key.fmtid,kClawHidMotion)&&key.pid>=7&&key.pid<=9?VARIANT_TRUE:VARIANT_FALSE;','const DWORD first=state->combined?(key.pid>=10?10:2):(kind==Kind::Gyro?10:2);*out=IsEqualGUID(key.fmtid,kSdkMotion)&&key.pid>=first&&key.pid<=first+2?VARIANT_TRUE:VARIANT_FALSE;');
 transport=once(transport,'if(!state->noSensors){ComPtr<ISensor> gyro;','if(state->combined){ComPtr<ISensor> both;both.Attach(new Sensor(state,Kind::Gyro,5));collection->sensors.push_back(both);}else if(!state->noSensors){ComPtr<ISensor> gyro;');
 const source=freeze(path.join(repo,'native/msi_legacy_sensor.h'));let header=source.toString('utf8');
 if(control==='--ignore-request')header=once(header,'state->options.explicitPair ? exactPair[0] :','false ? exactPair[0] :');
 if(control==='--allow-duplicates')header=once(header,'if(found[k]>=0)return {-1,-1};','/* intentionally permit duplicate IDs */');
 if(control==='--guess-default')header=once(header,'return ambiguous ? -1 : index;','return index;');
 fs.writeFileSync(path.join(out,'msi_legacy_sensor.h'),header);
 const preamble='#define _WIN32_WINNT 0x0A00\n#define NOMINMAX\n#define YMCC_FAULT_INJECT\n#include <initguid.h>\n#include "msi_legacy_sensor.h"\n#include <mutex>\n#include <thread>\n#include <iostream>\n#include <fstream>\n#include "json.hpp"\n';
 const cases=freeze(path.join(repo,'tools/fixtures/sensorapi_explicit_pair/cases.cpp')).toString('utf8');fs.writeFileSync(path.join(out,'test.cpp'),preamble+transport+'\n'+cases);fs.writeFileSync(path.join(out,'json.hpp'),freeze(path.join(repo,'deps/json/json.hpp')));
 for(const [name,flags]of [['Release','/O2 /MT /DNDEBUG'],['Checked','/Od /RTC1 /MTd']]){const dir=path.join(out,name);fs.mkdirSync(dir);const script=path.join(dir,'build.cmd');
  fs.writeFileSync(script,calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+`cl /nologo /std:c++20 /utf-8 /EHsc ${flags} "${path.join(out,'test.cpp')}" /Fo"${path.join(dir,'test.obj')}" /Fe"${path.join(dir,'test.exe')}" /link ole32.lib oleaut32.lib\r\nexit /b %errorlevel%\r\n`);
  const build=spawnSync('cmd.exe',['/d','/c',script],{cwd:dir,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:6e6});fs.writeFileSync(path.join(dir,'build.log'),(build.stdout||'')+(build.stderr||''));assert.equal(build.status,0,'Build '+name);
  const file=path.join(dir,'REPORT.json'),run=spawnSync(path.join(dir,'test.exe'),[file],{cwd:dir,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:6e6});fs.writeFileSync(path.join(dir,'run.log'),(run.stdout||'')+(run.stderr||''));
  const report=JSON.parse(fs.readFileSync(file,'utf8'));result.configurations.push({name,exitCode:run.status,report});save();assert.equal(run.status,control?1:0,'Run '+name);if(control)assert.ok(report.failures.length>0);else assert.equal(report.status,'pass');console.log(name+': '+report.status+' '+report.checks+' checks / '+report.failures.length+' failures');
 }
 assert.ok(inputs.every(i=>sha(fs.readFileSync(i.path))===i.sha256),'Source drift');result.sourcesUnchanged=true;result.headerSha256=sha(source);result.status=control?'expected-failure':'pass';save();console.log('Evidence: '+out);
}catch(error){result.status='fail';result.error=error.stack;save();console.error(error.stack);console.error(out);process.exitCode=1;}