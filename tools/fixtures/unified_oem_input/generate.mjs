import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {closeBrace} from '../hc_remaining_model_capabilities/expectations.mjs';
import {captureOemInputHeaders, oemInputBindingFunctions} from '../../lib/hc_oem_input_fixture.mjs';
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
export function generateOemConsumers({repo,out,sourcePath=path.join(repo,'native/main.cpp')}) {
 fs.mkdirSync(out,{recursive:true});
 const bytes=fs.readFileSync(sourcePath),source=bytes.toString('utf8'),fragments=[];
 const headers=captureOemInputHeaders({repo,out,source});
 if(!headers.inputs.length)throw Error('ACTUAL_OEM_MAIN_BINDING_REQUIRED');
 const block=marker=>{
  let start=-1,open;
  do {start=source.indexOf(marker,start+1);if(start<0)throw Error('ACTUAL_DEFINITION_MISSING:'+marker);open=source.indexOf('{',start);}while(source.indexOf(';',start)>=0&&source.indexOf(';',start)<open);
  let end=closeBrace(source,open)+1;if(source[end]===';')++end;
  const code=source.slice(start,end);fragments.push({marker,start,sha256:sha(code)});return code;
 };
 const fn=name=>{
  const matches=[...source.matchAll(new RegExp('^static [^\\n]*\\b'+name+'\\(','gm'))];
  for(const m of matches){const open=source.indexOf('{',m.index),semi=source.indexOf(';',m.index);if(semi>=0&&semi<open)continue;return block(m[0]);}
  throw Error('ACTUAL_FUNCTION_MISSING:'+name);
 };
 const global=name=>{
  const match=source.match(new RegExp('^static [^\\n;]*\\b'+name+'\\b[^;]*;','m'));if(!match)throw Error('ACTUAL_STATE_MISSING:'+name);
  fragments.push({marker:name,start:match.index,sha256:sha(match[0])});return match[0];
 };
 const definitions=[block('enum class YmccFamily : int {'),block('enum OemVkBit : uint32_t {')];
 for(const m of source.matchAll(/^static constexpr uint64_t kOemBit[^;]+;/gm))definitions.push(m[0]);
 definitions.push(block('struct OemChordRule {'),block('struct OemChordProfile {'));
 for(const m of source.matchAll(/^static const OemChordRule \w+\[\] = \{/gm))definitions.push(block(m[0]));
 definitions.push(block('enum class OemChordProfileId : int {'),block('static const OemChordProfile kOemProfiles[] = {'));
 definitions.push(fn('oemChordProfileFor'),fn('oemChordProfileIdFor'),fn('oemChordEvalRules'),fn('oemBackHookVkBit'));
 definitions.push(block('enum class OemKeyForm : int {'),block('struct OemKeyDef {'),block('struct OemCatalogRef {'));
 for(const m of source.matchAll(/^static const OemKeyDef \w+\[\] = \{/gm))definitions.push(block(m[0]));
 definitions.push('template<size_t N>\n'+fn('oemCatalogOf'),fn('oemCatalogForFamily'));
 definitions.push(...oemInputBindingFunctions.map(fn),fn('oemKeyCaptureImplemented'),fn('nativeKeyboardShortcutMaskBit'));
 for(const name of ['g_oemBackProfileId','g_oemShortcutEdgeEpoch','g_oemKbDown','g_oemKbDownRaw','g_oemMouseLeft','g_oemKeyHooksActive','g_oemKeyMask','g_oemBackStateKb','g_gpdBackIfaceGen','g_gpdBackIfaceReady','g_gpdChordSnapPhysical','g_gpdChordSnapBaseKey','g_gpdChordSnapPrefix','g_gpdChordPubGen','g_oemRawPathActive','g_oemRawVkAllowed','g_oemRawAdmitDev','g_oemHooksWanted','g_oemBackMappingEnabled','g_oemGateTargetEnabled','g_oemGatePersona','g_oemGateBackMapping'])definitions.push(global(name));
 definitions.push(String.raw`
struct {YmccFamily family=YmccFamily::Unknown;std::string deviceClass;bool resolved=true;} g_machineIdentity;
struct {bool hasControl=false,hasStableInput=false;std::wstring controlPath,inputPath;} g_rog;
static bool testContainerReady=false,testUsbReady=false,testLogs=true;
static unsigned testHookBoundaryCalls=0,testIdentityCalls=0;
static std::vector<json> testLogRows;
static void ymccResolveMachineIdentityOnce(){++testIdentityCalls;}
static bool deviceInterfaceContainerId(const wchar_t*,GUID*){return testContainerReady;}
static bool deviceInterfaceUsbInstance(const wchar_t*,std::wstring& value){if(testUsbReady)value=L"test-usb";return testUsbReady;}
static std::string W2U(const std::wstring& value){return std::string(value.begin(),value.end());}
static void writeLog(const char*,const std::string&){}
static void appendNativeLifecycleLog(const char* event,json row){if(testLogs){row["event"]=event;testLogRows.push_back(std::move(row));}}
static bool domainLogEnabled(const char*){return testLogs;}
// Terminal hook installation is an in-memory boundary; no Windows hook is installed.
static void oemBackMappingSetEnabled(bool enable){++testHookBoundaryCalls;g_oemHooksWanted.store(enable);}
`);
 definitions.push(fn('ymccIsRogFamily'),fn('oemBackHookRecompute'),fn('oemRawPathReset'),fn('oemBackMappingRefreshGate'),fn('ymccFamilyName'),fn('gamepadOemPressedKeys'));
 const ipc=source.indexOf('ipc_on("oem.keys.get",'),start=source.indexOf('[]',ipc),open=source.indexOf('{',start),end=closeBrace(source,open)+1;
 if(ipc<0||start<ipc)throw Error('ACTUAL_OEM_QUERY_MISSING');
 const query=source.slice(start,end);fragments.push({marker:'oem.keys.get:lambda',start,sha256:sha(query)});
 definitions.push('static json actualOemKeysGet(){auto handler='+query+';return handler(json::object());}');
 const catalogPath=path.join(repo,'capabilities/hc-device-catalog.json'),catalogBytes=fs.readFileSync(catalogPath),catalog=JSON.parse(catalogBytes);
 const cases=catalog.devices.flatMap(d=>d.aliases.map(alias=>({alias,canonical:d.consumerClass})));
 const familyBlock=definitions[0],familyNames=familyBlock.slice(familyBlock.indexOf('{')+1,familyBlock.lastIndexOf('}')).replace(/\/\/[^\n]*/g,'').split(',').map(s=>s.trim().split('=')[0].trim()).filter(Boolean);
 const harness=path.join(repo,'tools/fixtures/unified_oem_input/harness.cpp'),harnessBytes=fs.readFileSync(harness);
 const jsonHeader=path.join(repo,'deps/json/json.hpp'),jsonBytes=fs.readFileSync(jsonHeader),jsonCopy=path.join(out,'json.hpp');fs.writeFileSync(jsonCopy,jsonBytes);
 const cpp=headers.preamble+'#define WIN32_LEAN_AND_MEAN\n#define NOMINMAX\n#include <windows.h>\n#include <xinput.h>\n#include <algorithm>\n#include <atomic>\n#include <cstring>\n#include <cstdint>\n#include <iostream>\n#include <string>\n#include <vector>\n#include <stdexcept>\n#include "json.hpp"\nusing json=nlohmann::json;\n'+definitions.join('\n')+'\nstruct AliasCase{const char* alias;const char* canonical;};\nstatic const AliasCase aliases[]={\n'+cases.map(r=>'{'+JSON.stringify(r.alias)+','+JSON.stringify(r.canonical)+'}').join(',\n')+'\n};\nstatic const YmccFamily families[]={'+familyNames.map(n=>'YmccFamily::'+n).join(',')+'};\n'+harnessBytes.toString('utf8');
 fs.writeFileSync(path.join(out,'actual-oem-consumers.cpp'),cpp);fs.writeFileSync(path.join(out,'main.snapshot.cpp'),bytes);
 const inputs=[{file:'native/main.cpp',path:sourcePath,sha256:sha(bytes)},{file:'capabilities/hc-device-catalog.json',path:catalogPath,sha256:sha(catalogBytes)},{file:'tools/fixtures/unified_oem_input/harness.cpp',path:harness,sha256:sha(harnessBytes)},{file:'deps/json/json.hpp',path:jsonHeader,sha256:sha(jsonBytes)},...headers.inputs.map(r=>({file:r.file,path:r.original,sha256:r.sha256}))];
 for(const file of ['tools/fixtures/unified_oem_input/generate.mjs','tools/hc_oem_input_consumer_selftest.mjs','tools/lib/hc_oem_input_fixture.mjs','tools/fixtures/hc_remaining_model_capabilities/expectations.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs']){const original=path.join(repo,file);inputs.push({file,path:original,sha256:sha(fs.readFileSync(original))});}
 const result={inputs,fragments,selections:cases.length*familyNames.length,generatedCppSha256:sha(cpp),hardwareIo:false,scope:'actual complete gate, query, pressed-key enumeration, hook recompute and raw-path reset; hook install/device identity/log sinks are memory boundaries'};
 fs.writeFileSync(path.join(out,'source-binding.json'),JSON.stringify(result,null,2)+'\n');return result;
}
