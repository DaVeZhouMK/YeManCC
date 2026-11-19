import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';

const here=path.dirname(fileURLToPath(import.meta.url));
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const nativeHeaders=[
 'input_frame_submission.h','touch_haptic_dispatch.h','game_feedback_receipt.h',
 'legion_motion_source.h','windows_motion_submission.h','virtual_output_status.h',
 'msi_virtual_report_diagnostic.h','input_host_shared_power.h',
 'sleep_power_state.h','sleep_power_observation.h','sleep_power_revocation.h',
];
const requiredFunctions=[
 'sgShouldKeepInputDuringStandby','sgVirtualWriterAllowedNow','inputCaptureBindStandardInput',
 'inputHostPhysicalPublicationSourceAdmitted','inputHostFrameSubmissionSample',
 'inputHostFrameSubmissionCurrent','inputHostWindowsMotionSubmissionCurrent',
 'inputHostFrameSharedPowerCurrent','inputHostFrameSubmissionMayIssue',
];

// Preparation only. This module has no subprocess/compiler/fixture execution API.
// Producer ownership stays with the caller and the original registered fixture.
export async function prepareSharedPowerFixture({repo,mainFile,headerRoot,out,
 selectedCase=null,expectedMainSha256=null}){
 for(const [name,value]of Object.entries({repo,mainFile,headerRoot,out}))
  assert(typeof value==='string'&&value.length&&path.isAbsolute(value),'EXPLICIT_ABSOLUTE_'+name.toUpperCase());
 if(selectedCase!==null)assert(typeof selectedCase==='string'&&selectedCase.length&&
  !selectedCase.startsWith('--')&&!/[\r\n\0]/.test(selectedCase),'EMPTY_OR_INVALID_CASE');
 assert(fs.statSync(repo).isDirectory()&&fs.statSync(headerRoot).isDirectory(),'INPUT_ROOT_DIRECTORY_REQUIRED');
 assert(!fs.existsSync(out),'PREPARATION_OUTPUT_MUST_BE_NEW');
 const mainBytes=fs.readFileSync(mainFile); // One actual source read for this epoch.
 const main=mainBytes.toString('utf8');
 if(expectedMainSha256!==null)assert.equal(sha(mainBytes),expectedMainSha256,'PRIVATE_MAIN_CHANGED');
 const bindingPath=path.join(repo,'tools/fixtures/msi_owned_return_retired_io/source-binding.mjs');
 const {extract}=await import(pathToFileURL(bindingPath).href);
 const bindings=[];
 const take=name=>extract(main,'function',name,mainFile,bindings);
 const structure=name=>extract(main,'struct',name,mainFile,bindings);
 const powerEnum=main.match(/^enum class PowerLifecycle\s*:\s*uint8_t\s*\{[^\n]+\};\r?$/m);
 const familyEnum=main.match(/^enum class YmccFamily\s*:\s*int\s*\{[\s\S]*?^\};/m);
 assert(powerEnum&&familyEnum,'ORIGINAL_POWER_AND_FAMILY_ENUMS_REQUIRED');
 const functions=Object.fromEntries(requiredFunctions.map(name=>[name,take(name)]));
 const tlsLine=main.match(/^static thread_local std::shared_ptr<ymcc::inputHost::sharedPower::Scope> t_inputHostRequestPower;\r?$/m);
 assert(tlsLine,'ORIGINAL_TLS_SCOPE_DECLARATION_REQUIRED');
 const parts={
  TLS_SCOPE:tlsLine[0],
  FAMILY_ENUM:familyEnum[0],POWER_ENUM:powerEnum[0],
  KEEP:functions.sgShouldKeepInputDuringStandby,WRITER:functions.sgVirtualWriterAllowedNow,
  SNAPSHOT:structure('InputCaptureGamepadSnapshot'),FRAME:structure('InputFrameSubmission'),
  REQUEST_SCOPE:structure('InputHostRequestPowerScope'),
  BIND_STANDARD_INPUT:functions.inputCaptureBindStandardInput,
  SOURCE_ADMITTED:functions.inputHostPhysicalPublicationSourceAdmitted,
  SAMPLE:functions.inputHostFrameSubmissionSample,CURRENT:functions.inputHostFrameSubmissionCurrent,
  WINDOWS_CURRENT:functions.inputHostWindowsMotionSubmissionCurrent,
  SHARED_CURRENT:functions.inputHostFrameSharedPowerCurrent,MAY_ISSUE:functions.inputHostFrameSubmissionMayIssue,
 };
 assert(parts.SNAPSHOT.includes('ymcc::sleeppower::SharedPowerV1 sharedPower;'),'ACTUAL_SOURCE_PUBLIC_POWER_FIELD_REQUIRED');
 assert(parts.FRAME.includes('ymcc::sleeppower::SharedPowerV1 sourcePower;')&&
  parts.FRAME.includes('std::shared_ptr<ymcc::inputHost::sharedPower::Scope> powerScope;'),'ACTUAL_FRAME_PUBLIC_FIELDS_REQUIRED');
 assert(parts.MAY_ISSUE.includes('inputHostFrameSharedPowerCurrent(*frame)'),'ACTUAL_FINAL_SHARED_GUARD_REQUIRED');
 assert(parts.MAY_ISSUE.indexOf('const auto verdict=')<parts.MAY_ISSUE.indexOf('inputHostFrameSharedPowerCurrent(*frame)'),
  'ORIGINAL_SPECIFIC_DECIDE_REASONS_MUST_PRECEDE_SHARED_CHECK');
 const templatePath=path.join(here,'power.cpp'),templateBytes=fs.readFileSync(templatePath);
 let code=templateBytes.toString('utf8');
 for(const [name,value]of Object.entries(parts)){
  const marker='@@'+name+'@@';assert.equal(code.split(marker).length,2,'UNIQUE_SLOT_'+name);
  code=code.replace(marker,()=>value);
 }
 assert(!/@@\w+@@/.test(code),'NO_UNBOUND_TEMPLATE_MARKERS');
 const inputs=new Map();
 const pin=(file,role)=>{
  file=path.resolve(file);const existing=inputs.get(file);
  if(existing){if(!existing.roles.includes(role))existing.roles.push(role);return existing;}
  const bytes=fs.readFileSync(file),entry={path:file,bytes,roles:[role]};inputs.set(file,entry);return entry;
 };
 inputs.set(path.resolve(mainFile),{path:path.resolve(mainFile),bytes:mainBytes,roles:['actual-product-main']});
 for(const file of ['tools/input_power_gate_r122_selftest.mjs','tools/fixtures/input_power_gate_r122/power.cpp',
  'tools/lib/hc_controller_input_fixture.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs',
  'tools/fixtures/msi_owned_return_retired_io/source-binding.mjs'])pin(path.join(repo,file),'original-runner-or-source-dependency');
 for(const name of ['shared-power-prepare.mjs','power.cpp'])pin(path.join(here,name),'formal-fixture-source');
 const searchRoots=[headerRoot,path.join(repo,'native'),path.join(repo,'capabilities'),path.join(repo,'deps/json')];
 const headers=new Map();
 function header(file){
  file=path.resolve(file);const name=path.basename(file);
  if(headers.has(name)){assert.equal(headers.get(name).path,file,'HEADER_BASENAME_COLLISION_'+name);return;}
  const entry=pin(file,'actual-header-input');headers.set(name,entry);
  const text=entry.bytes.toString('utf8');
  for(const match of text.matchAll(/^\s*#\s*include\s*"([^"\r\n]+)"/gm)){
   const candidates=[path.resolve(path.dirname(file),match[1]),...searchRoots.map(root=>path.resolve(root,match[1]))];
   const resolved=candidates.find(candidate=>fs.existsSync(candidate)&&fs.statSync(candidate).isFile());
   assert(resolved,'UNRESOLVED_QUOTED_HEADER_'+match[1]);header(resolved);
  }
 }
 for(const name of nativeHeaders){
  const found=searchRoots.map(root=>path.join(root,name)).find(file=>fs.existsSync(file));
  assert(found,'REQUIRED_DEPENDENCY_'+name);header(found);
 }
 for(const name of ['HcControllerInputCapabilityCatalog.h','HcControllerInputCapabilityCatalog.generated.h'])
  header(path.join(repo,'capabilities',name));
 header(path.join(repo,'deps/json/json.hpp'));
 fs.mkdirSync(out,{recursive:true});
 const save=(name,value)=>{
  const destination=path.join(out,name);fs.mkdirSync(path.dirname(destination),{recursive:true});
  const bytes=Buffer.isBuffer(value)?value:Buffer.from(typeof value==='string'?value:JSON.stringify(value,null,2)+'\n');
  fs.writeFileSync(destination,bytes,{flag:'wx'});return {path:destination,bytes:bytes.length,sha256:sha(bytes)};
 };
 const fixedMain=save('fixed-main.cpp',mainBytes);
 const fixture=save('fixture.cpp',code);
 const copiedHeaders=[...headers.values()].map(entry=>({...save('include/'+path.basename(entry.path),entry.bytes),source:entry.path}));
 const args=['--shared-power-only',...(selectedCase!==null?['--case',selectedCase]:[])];
 const vc=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
 const toolchainFile=path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs');
 const {calibrationToolchainPreamble}=await import(pathToFileURL(toolchainFile).href);
 const quote=value=>{assert(!/["%\r\n!]/.test(value),'UNSAFE_CMD_VALUE');return '"'+value+'"';};
 const recipes=[];
 for(const profile of ['Release','Checked'])for(const diagnostic of [false,true]){
  const label=profile+(diagnostic?'-diagnostic-on':'-diagnostic-off'),dir=path.join(out,label);
  const flags=profile==='Release'?'/O2 /MT /DNDEBUG':'/Od /MTd /RTC1 /D_DEBUG';
  const recipe=calibrationToolchainPreamble(vc,{boundInheritedPath:true})+'set VSLANG=1033\r\n'+
   'cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /permissive- /sdl '+flags+
   (diagnostic?' /DYMCC_MSI_REPORT_CHAIN_DIAGNOSTIC':'')+' /I'+quote(path.join(out,'include'))+
   ' '+quote(fixture.path)+' /Fo'+quote(path.join(dir,'fixture.obj'))+' /Fe'+quote(path.join(dir,'fixture.exe'))+
   ' /sourceDependencies '+quote(path.join(dir,'dependencies.json'))+' /link /VERBOSE:LIB\r\nexit /b %errorlevel%\r\n';
  recipes.push({profile,diagnostic,...save(label+'/build.cmd',recipe),
   execution:{program:path.join(dir,'fixture.exe'),args},executed:false});
 }
 const current=fs.readFileSync(mainFile); // Closing observation only; never regenerate against moving source.
 const changedDuringPreparation=sha(current)!==sha(mainBytes);
 const metadata={schemaVersion:1,status:'PREPARED_NOT_COMPILED_NOT_EXECUTED',
  originalCanonicalId:'controller.unified-standard-input',originalEntry:'tools/input_power_gate_r122_selftest.mjs',
  mode:'shared-power-only',selectedCase,mainFile,headerRoot,
  mainObserved:{bytes:mainBytes.length,sha256:sha(mainBytes)},mainAtClosing:{bytes:current.length,sha256:sha(current)},
  changedDuringPreparation,fixedMain,fixture,copiedHeaders,bindings,
  compiledParts:Object.entries(parts).map(([symbol,value])=>({symbol,bytes:Buffer.byteLength(value),sha256:sha(value)})),
  inputPins:[...inputs.values()].map(({path,bytes,roles})=>({path,bytes:bytes.length,sha256:sha(bytes),roles})),
  recipes,actualPreparedRecipeCount:4,actualCompiles:0,actualExecutions:0,
  scope:'Original default family/phase/neutral and negative table is shared with explicit public observation/final issue extensions; this prepare-only route does not execute or accept the default parent.',
  selection:'C++ collects existing run registrations without test-body execution, rejects unknown/empty/duplicate selector, then runs exact selected ordered set. Mode required; no automatic default suite.',
  adaptations:[
   'Full actual MayIssue and FrameSharedPowerCurrent functions extracted without body replacement.',
   'Full original snapshot/frame/RAII structures retained, including both public SharedPower values and shared_ptr owner.',
   'readSharedSystemPower is a controlled Reader port projecting the original fixture phase/generation facts; not production Native Reader acceptance.',
   'Actual public helper, state, observation and RevocationMirrors with real Windows cancellation events.',
   'Existing frame/sample/reset test adapters gain original public source observation and real Scope Cache.',
   'OneX/deferred lifecycle and Legion motion remain excluded fail-closed leaves; all cases assert lifecycle leaves unentered.',
   'Original literal assertion calls are retained; their execution and all behavior remain unproven until a declared producer runs.',
  ],
  productionWritten:false,fullParentExecuted:false,devicesOrSteamTouched:false,acceptanceGranted:false};
 save('PREPARED.json',metadata);
 if(changedDuringPreparation)throw Error('SOURCE_CHANGED_DURING_PREPARATION_FIXED_EPOCH_RETAINED');
 return metadata;
}
