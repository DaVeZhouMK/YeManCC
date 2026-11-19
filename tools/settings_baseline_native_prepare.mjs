import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';
const root=process.cwd(),evidence=process.env.YMCC_BASELINE_EVIDENCE;
const virtualStartWriteFailure=process.argv.includes('--virtual-start-write-failure');
if(virtualStartWriteFailure&&process.argv.length!==3)throw Error('virtual-start-write-failure requires its own exclusive scope');
const monitorRootWriteOnly=process.argv.includes('--monitor-root-write');
if(monitorRootWriteOnly&&['--serial-config','--section-cache','--cleanup-history'].some(flag=>process.argv.includes(flag)))throw Error('monitor-root-write requires its own exclusive scope');
const serialConfigOnly=process.argv.includes('--serial-config');
const sectionCacheOnly=process.argv.includes('--section-cache'),candidateAt=process.argv.indexOf('--section-cache-candidate');
if(serialConfigOnly&&(sectionCacheOnly||process.argv.includes('--cleanup-history')))throw Error('serial-config requires its own exclusive scope');
if(sectionCacheOnly&&process.argv.includes('--cleanup-history'))throw Error('section-cache and cleanup-history scopes are exclusive');
if(candidateAt>=0&&(!sectionCacheOnly||!process.argv[candidateAt+1]||process.argv[candidateAt+1].startsWith('--')))throw Error('section-cache-candidate requires explicit --section-cache and a file');
const toolDirectory=path.dirname(fileURLToPath(import.meta.url));
const {extract:extractMainMember}=await import(pathToFileURL(path.join(root,'tools/fixtures/legion_motion_consumer/extract_main.mjs')).href);
const fixturePath=path.join(toolDirectory,'fixtures/settings_baseline_native.cpp');
// Private running-build consumer: actual pure policy; protected store IO remains a
// declared fixture port. Absolute include binds this T header with --source-main
// even when the unchanged compiler recipe is invoked from the mainline root.
const upgradePolicyPath=fs.realpathSync(path.resolve(toolDirectory,'../native/decky_running_build_recovery.h'));
const upgradePolicyDetailPath=fs.realpathSync(path.join(path.dirname(upgradePolicyPath),'decky_build_recovery_contract.h'));
if(!fs.statSync(upgradePolicyDetailPath).isFile()||/["\r\n]/.test(upgradePolicyDetailPath))throw Error('Invalid private running-policy transitive header');
const upgradePolicyDetailBytes=fs.readFileSync(upgradePolicyDetailPath);
if(!fs.statSync(upgradePolicyPath).isFile()||/["\r\n]/.test(upgradePolicyPath))throw Error('Invalid private upgrade-policy header');
const upgradePolicyBytes=fs.readFileSync(upgradePolicyPath);
const upgradePolicyHeader='#include "'+upgradePolicyPath.replaceAll('\\','/')+'"';
const sourceMainFlag=(name)=>{
 const found=process.argv.map((arg,index)=>arg===name?index:-1).filter(index=>index>=0);
 if(found.length>1)throw Error('Duplicate '+name);
 if(!found.length)return null;
 const value=process.argv[found[0]+1];
 if(!value||value.startsWith('--'))throw Error('Missing value for '+name);
 return value;
};
const sourceMainArgument=sourceMainFlag('--source-main'),sourceMainExpectedSha256=sourceMainFlag('--source-main-sha256');
if(Boolean(sourceMainArgument)!==Boolean(sourceMainExpectedSha256))throw Error('source-main and source-main-sha256 must be paired');
if(sourceMainArgument&&(!path.isAbsolute(sourceMainArgument)||!/^[0-9a-f]{64}$/.test(sourceMainExpectedSha256)))throw Error('source-main requires an absolute path and lowercase SHA256');
if(sourceMainArgument&&candidateAt>=0)throw Error('source-main and section-cache-candidate cannot be combined');
const sourceMainPath=sourceMainArgument?fs.realpathSync(sourceMainArgument):path.join(root,'native/main.cpp');
if(!fs.statSync(sourceMainPath).isFile())throw Error('source-main must be a regular file');
const mainBytes=fs.readFileSync(sourceMainPath);
if(sourceMainExpectedSha256&&crypto.createHash('sha256').update(mainBytes).digest('hex')!==sourceMainExpectedSha256)throw Error('source-main SHA256 mismatch');
if(!evidence)throw Error('YMCC_BASELINE_EVIDENCE is required');fs.mkdirSync(evidence,{recursive:true});
const main=mainBytes.toString('utf8').replace(/\r\n/g,'\n');
const slice=(start,end)=>{const a=main.indexOf(start),b=main.indexOf(end,a+start.length);if(a<0||b<0||main.indexOf(start,a+1)!==-1)throw Error('production source anchor mismatch: '+start);return main.slice(a,b);};
// The reserve function has unrelated worker helpers after it. Extract only its
// actual definition; the reader is likewise compiled from its real source body.
const cleanupReservation=extractMainMember(main,'deckySidebarReserveContextCleanup');
let frontendReader=null;
if(!sectionCacheOnly){
 const bootstrap=fs.readFileSync(path.join(root,'native/decky_sidebar_bootstrap.h'),'utf8').replace(/\r\n/g,'\n');
 const signature='inline std::string frontendBuildId(';
 if(bootstrap.split(signature).length!==2)throw Error('production frontend identity reader mismatch');
 // This existing extractor selects static definitions. Adapt only the six-byte
 // declaration keyword in memory, then restore it; the emitted body is original.
 const found=extractMainMember(bootstrap.replace(signature,'static std::string frontendBuildId('),'frontendBuildId');
 frontendReader={...found,code:found.code.replace(/^static /,'inline ')};
}
const bodies=[
 slice('static void ymSettingsApplyDelta(', 'static json ymSettingsReadUnlocked()'),
 slice('static json ymSettingsReadUnlocked()', 'class SettingsFileGuard'),
 slice('static bool ymSettingsBootWriteMatches(', 'static bool ymSettingsArchiveFile('),
 slice('static bool ymSettingsHasCompatibleWriteBase(', '// Explicit-read metadata validation;'),
 slice('static json ymSettingsResetApplication(', 'static json ymSettingsRead()'),
 slice('static bool ymSettingsWriteSection(', '// Native MSI shield writes'),
 slice('static bool ymSettingsPatchSection(', '// ================================================================\n// Native monitor daemon'),
 cleanupReservation.code,
];
const ioBodies=[
 slice(main.includes('static void ymSettingsReadRawInto(') ? 'static void ymSettingsReadRawInto(' : 'static std::string ymSettingsReadRaw(', '// Merge only actual changes'),
 slice('class SettingsFileGuard {', '// Defined beside the effective input consumer.'),
 slice('static json ymSettingsReadSnapshot(', '// This storage-only recovery endpoint'),
 slice('static json ymSettingsReadUnlocked()', 'class SettingsFileGuard'),
 slice('static json ymSettingsRead() {', 'static json ymSettingsSection(const char* section);'),
 slice('bool sgWriteFileAtomicFlags(', 'static bool sgWriteFileAtomicTransient('),
 bodies[0],
 slice('static bool ymSettingsFileExceedsLimit(', '// Ordinary native writers may recover a readable backup,'),
 bodies[3],
 slice('static std::string W2U(const wchar_t*', 'static std::wstring U2W('),
 bodies[4], bodies[5], bodies[6], // Actual reset/section/patch consumers, same owned IO port.
 bodies[7], // Exact Decky pair-commit helper also executes against isolated real Win32 files.
];
if(!sectionCacheOnly)ioBodies.splice(ioBodies.length-1,0,
 'namespace fspath = std::filesystem;\nstatic void traceLog(const char*) {}\n'+
 extractMainMember(main,'is_legacy_power_control_user_file').code+'\n'+
 extractMainMember(main,'migrate_legacy_power_control_dir').code);
// The existing IO fixture does not own CSS. Keep its notification port explicit;
// the original appearance consumer separately compiles and executes the actual Runtime.
const pendingDeclaration='static thread_local bool g_settingsBootOwnerChanged = false;';
if(main.includes(pendingDeclaration)){
 if(ioBodies[3].split(pendingDeclaration).length!==2)throw Error('Settings notification declaration mismatch');
 ioBodies[3]=ioBodies[3].replace(pendingDeclaration,'');
 ioBodies.unshift(pendingDeclaration+'\nstatic struct {bool notifyRequestChanged() noexcept {return false;}} g_steamCssUse;\n');
}
// Explicit scope: select only helper/getter fragments, never rewrite main.cpp.
// A legacy shared getter remains a real consumer and can fail the reuse assertion.
let sectionCacheSegments,compiledSectionFragments='',candidateIdentity;
const hash=value=>crypto.createHash('sha256').update(value).digest('hex');
const compiledFragmentPath=sectionCacheOnly?path.resolve(evidence,'section-cache-compiled-fragments.cpp'):null;
if(sectionCacheOnly){
 // This read-cache leaf does not compile the unrelated Decky cleanup bridge.
 ioBodies.pop();
 const helperAnchor='struct YmSettingsSectionReadCache {',sectionAnchor='static json ymSettingsSection(const char* section) {';
 let helper='',section,origin;
 if(candidateAt>=0){
  const absolutePath=fs.realpathSync(path.resolve(process.argv[candidateAt+1]));
  if(!fs.statSync(absolutePath).isFile()||fs.statSync(absolutePath).size>64*1024)throw Error('section-cache candidate must be a fragment file at most 64 KiB');
  const bytes=fs.readFileSync(absolutePath),text=bytes.toString('utf8').replace(/^\uFEFF/,'').replace(/\r\n/g,'\n');
  const at=text.indexOf(sectionAnchor);
  if(at<0||text.indexOf(sectionAnchor,at+sectionAnchor.length)>=0||text.split(helperAnchor).length!==2||text.includes('#include')||text.includes('class SettingsFileGuard')||text.includes('static json ymSettingsReadUnlocked(')||text.includes('int main('))throw Error('candidate must contain only the section helper/getter fragment');
  helper=text.slice(0,at);section=text.slice(at);origin='candidate';
  candidateIdentity={path:absolutePath,absolutePath,sha256:hash(bytes),bytes:bytes.length,kind:'section-cache-candidate',scope:'section-cache'};
 }else{
  section=slice(sectionAnchor,'static ymcc::fxsound::Service g_fxSoundRecovery;');
  if(main.includes(helperAnchor)){helper=slice(helperAnchor,sectionAnchor);origin='shared-helper';}
  else origin='shared-legacy';
 }
 const parserCall='ymcc::settingsBaseline::parseDocument(';
 const wrapper='static std::size_t sectionCacheParserCalls=0;\nstatic json sectionCacheParseDocument(const std::string& raw) noexcept {\n ++sectionCacheParserCalls;\n return ymcc::settingsBaseline::parseDocument(raw);\n}\n';
 let parserConsumer;
 if(helper){
  if(helper.split(parserCall).length!==2)throw Error('section-cache helper needs exactly one real parser call');
  parserConsumer=helper.replace(parserCall,'sectionCacheParseDocument(');
  ioBodies.push(parserConsumer,section);
 }else{
  if(ioBodies[3].split(parserCall).length!==2)throw Error('legacy reader needs exactly one real parser call');
  ioBodies[3]=ioBodies[3].replace(parserCall,'sectionCacheParseDocument(');
  parserConsumer=ioBodies[3];ioBodies.push(section);
 }
 // The wrapper is test instrumentation, not another production source segment.
 compiledSectionFragments=wrapper+parserConsumer+'\n'+section;
 sectionCacheSegments={origin,productionSegments:helper?2:1,testOnlyParserWrappers:1,consumerNamespace:'settingsIoFixture',actualProductionSection:true,memorySectionStubExcluded:true,helperSha256:helper?hash(helper):null,sectionSha256:hash(section),instrumentedParserConsumerSha256:hash(parserConsumer),compiledFragmentPath,compiledFragmentsSha256:hash(compiledSectionFragments),candidate:candidateIdentity||null,parser:'test-only counter delegates to native/settings_baseline.h parseDocument'};
 // Declaration precedes both the legacy reader and the new helper. All original
 // guard/raw/writer bodies remain actual shared consumers in this namespace.
 ioBodies[0]=wrapper+ioBodies[0];
}
const handler=slice('    ipc_on("settings.write", [](const json& a) -> json {', '    // FAN-927');
const resetHandler=slice('    ipc_on("settings.reset", [](const json& a) -> json {','    ipc_on("settings.read",');
const chargeRegistration=sectionCacheOnly?'':slice('static void reg_battery_charge() {','static void reg_secondary_display() {');
const displayMembers=sectionCacheOnly?'':slice('struct DisplayModeInfo {','// Resolution choices must follow')+
 slice('static std::string displayModeId(','static std::vector<DisplayModeInfo> enumerateCurrentDisplayModes(');
const displayRegistration=sectionCacheOnly?'':slice('static void reg_display() {','static void reg_gyro() {');
const displayOwnerCheck=!sectionCacheOnly&&displayRegistration.includes('mouseModeBootOwnerCheck(')?extractMainMember(main,'mouseModeBootOwnerCheck').code:'';
const displayRemember=sectionCacheOnly||!main.includes('static bool nativeRememberDisplayPreference(const json& patch) {')?'':
 slice('static bool nativeRememberDisplayPreference(const json& patch) {','static const GUID YM_GUID_YEMAN_SCHEME');
const brightnessGuids=sectionCacheOnly?'':slice('static const GUID YM_GUID_YEMAN_SCHEME','static void nativeApplyBrightness(int dir, bool enabled, int step) {');
const brightnessBody=sectionCacheOnly?'':slice('static void nativeApplyBrightness(int dir, bool enabled, int step) {','// RTSS 锁帧：');
let chargeValidation='';
if(!sectionCacheOnly){
 const charge=fs.readFileSync(path.join(root,'native/battery_charge_client.h'),'utf8').replace(/\r\n/g,'\n');
 const a=charge.indexOf('inline Json failure('),b=charge.indexOf('inline void trace(',a);
 if(a<0||b<=a)throw Error('original battery request validation anchor missing');
 chargeValidation=charge.slice(a,b);
}
// Selected source-origin controls reuse the actual current-byte reader and its
// existing owned Win32 ports; default/cleanup/section-cache selection is unchanged.
const serialConfigReaders=serialConfigOnly?[
 extractMainMember(main,'YmSettingsSectionReadCache','struct'),
 extractMainMember(main,'ymSettingsSectionReadCachedUnlocked'),
 extractMainMember(main,'ymSettingsFields'),
]:[];
if(serialConfigOnly)ioBodies.push(...serialConfigReaders.map(item=>item.code));
let serialConfigObservation=null;
if(serialConfigOnly){
 const entry='bool sgWriteFileAtomicFlags(const std::wstring& path, const std::string& content, bool durable,\n    const std::function<void()>& prepare = nullptr,\n    const std::function<bool()>& current = {}) {';
 // Notification/support fragments may precede the writer. Its unique actual
 // definition, rather than the fragment order, owns this observation.
 const writers=ioBodies.map((code,index)=>({index,count:code.split(entry).length-1})).filter(item=>item.count);
 if(writers.length!==1||writers[0].count!==1)throw Error('Expected one actual atomic writer entry for the owned observer');
 const writerIndex=writers[0].index,original=ioBodies[writerIndex];
 ioBodies[writerIndex]=original.replace(entry,entry+'\n    serialConfigFixture::observeAtomicWrite(path, content);');
 serialConfigObservation={source:'native/main.cpp',symbol:'sgWriteFileAtomicFlags',productionFragmentSha256:hash(original),compiledFragmentSha256:hash(ioBodies[writerIndex]),observationsOnly:true,addedCalls:1,productionBehaviorRewritten:false};
}

// Bind the actual configuration-action fence only when selected storage bodies use it.
const serialActionBodyPaths=[...(sectionCacheOnly?[]:bodies),...ioBodies,...(sectionCacheOnly?[]:[handler,resetHandler])];
const serialActionFenceNeeded=serialActionBodyPaths.some(code=>/\bGamepadSerialEvent::(?:ConfigurationWrite|rulesPublished)\b|\bgamepadSerialActionRule(?:Snapshot|sDiffer)\s*\(/.test(code));
const serialActionDependencies=serialActionFenceNeeded?[
 extractMainMember(main,'GamepadSerialEvent','struct'),
 extractMainMember(main,'gamepadSerialActionRuleSnapshot'),
 extractMainMember(main,'gamepadSerialActionRulesDiffer'),
]:[];
// A support definition already inside a broad source slice is relocated verbatim,
// rather than duplicated. Each body path then resolves the one global declaration.
for(const dependency of serialActionDependencies){
 for(const bodyList of [bodies,ioBodies])for(let i=0;i<bodyList.length;++i){
  const count=bodyList[i].split(dependency.code).length-1;
  if(count>1)throw Error('duplicate serial action dependency in one body path: '+dependency.symbol);
  if(count===1)bodyList[i]=bodyList[i].replace(dependency.code,'');
 }
}
const serialActionSupport=serialActionDependencies.map(item=>item.code).join('\n');
const receiptSourcePath=path.join(root,'native/steam_settings_runtime.h');
const receiptSource=fs.readFileSync(receiptSourcePath,'utf8');
const receiptSymbols=['steamMonitorAccountValid','steamMonitorValuesValid','steamMonitorSavedValid'];
const receiptParts=receiptSymbols.map(name=>extractMainMember(receiptSource,name));
const boundedMonitorConsumer=extractMainMember(main,'ymSettingsWriteBoundedRecovery').code;
const fixtureOriginal=fs.readFileSync(fixturePath,'utf8');
if(fixtureOriginal.split('// SETTINGS_BASELINE_MONITOR_RECEIPT_VALIDATION').length!==2)throw Error('monitor receipt validation slot');
const fixtureSource=fixtureOriginal.replace('// SETTINGS_BASELINE_MONITOR_RECEIPT_VALIDATION',receiptParts.map(x=>x.code).join('\n'));
const upgradePortBegin='// SETTINGS_BASELINE_DECKY_UPGRADE_FIXTURE_PORT_BEGIN',upgradePortEnd='// SETTINGS_BASELINE_DECKY_UPGRADE_FIXTURE_PORT_END';
const upgradePortAt=fixtureSource.indexOf(upgradePortBegin),upgradePortLast=fixtureSource.indexOf(upgradePortEnd,upgradePortAt);
if(upgradePortAt<0||upgradePortLast<=upgradePortAt||fixtureSource.split(upgradePortBegin).length!==2||fixtureSource.split(upgradePortEnd).length!==2||fixtureSource.split('// SETTINGS_BASELINE_UPGRADE_POLICY_HEADER').length!==2)throw Error('Original fixture upgrade port scope changed');
const upgradePortSource=fixtureSource.slice(upgradePortAt,upgradePortLast+upgradePortEnd.length);
const upgradePortCompiled=upgradePortSource.replace('// SETTINGS_BASELINE_UPGRADE_POLICY_HEADER',upgradePolicyHeader);
const serialActionJsonAlias='using json = nlohmann::json;';
if(serialActionFenceNeeded&&fixtureSource.split(serialActionJsonAlias).length!==2)throw Error('one fixture json alias is required for actual serial action support');
const fixtureWithSerialActionSupport=serialActionFenceNeeded?fixtureSource.replace(serialActionJsonAlias,serialActionJsonAlias+'\n#include "gyro_telemetry_record.h"\n'+serialActionSupport):fixtureSource;
let serialConfigSelection=null;
if(serialConfigOnly){
 if(!serialActionFenceNeeded)throw Error('serial-config requires actual production configuration fence bodies');
 const names=[...fixtureSource.matchAll(/constexpr const char\* kSerialConfigCaseNames\[\]\s*=\s*\{([^]*?)\};/g)];
 if(names.length!==1)throw Error('serial-config needs one fixture-owned case selection');
 const entries=[...names[0][1].matchAll(/"([^"\\]+)"/g)].map(match=>match[1]);
 if(!entries.length||new Set(entries).size!==entries.length||names[0][1].replace(/"[^"\\]+"/g,'').replace(/[\s,]/g,''))throw Error('Invalid serial-config case selection');
 serialConfigSelection={scope:'serial-config-storage',names:entries,source:fixturePath,selectionDeclarationSha256:hash(names[0][0])};
}
let virtualStartGuard;
if(virtualStartWriteFailure){
 const parts=['inputHostStartProtectionNow','inputHostTargetKey','inputHostStartProtectionJson','inputHostStartProtectionSnapshot',
  'inputHostStartProtectionLog','inputHostStartProtectionFinish','inputHostTargetAdmission'].map(name=>extractMainMember(main,name));
 const declaration=slice('struct InputHostTargetWriteGuard {','// Configuration recovery streams');
 const methods=slice('bool InputHostTargetWriteGuard::admit(', 'static std::atomic<std::uint64_t> g_inputHostStartProtectionBoundTicket');
 const stateDeclarations=['static ymcc::VirtualStartProtection g_virtualStartProtection;','static std::atomic<std::uint64_t> g_inputHostSwitchVerificationPending{0};'];
 for(const text of stateDeclarations)if(main.split(text).length!==2)throw Error('Unexpected virtual-start state declaration: '+text);
 const code=[...stateDeclarations,...parts.map(row=>row.code),declaration,methods].join('\n');
 if(fixtureWithSerialActionSupport.split('// SETTINGS_BASELINE_VIRTUAL_START_GUARD').length!==2)throw Error('Expected one virtual-start guard insertion');
 virtualStartGuard={code,parts:parts.map(({symbol,startLine,endLine,sha256})=>({symbol,startLine,endLine,sha256})),
  declarationSha256:hash(declaration),methodsSha256:hash(methods),compiledSha256:hash(code)};
}
const source=fixtureWithSerialActionSupport
 .replace('// SETTINGS_BASELINE_VIRTUAL_START_GUARD',virtualStartGuard?.code||'')
 .replace('// SETTINGS_BASELINE_UPGRADE_POLICY_HEADER',upgradePolicyHeader)
 .replace('// SETTINGS_BASELINE_CHARGE_VALIDATION',chargeValidation)
 .replace('// SETTINGS_BASELINE_CHARGE_REGISTRATION',chargeRegistration)
 .replace('// SETTINGS_BASELINE_DISPLAY_MEMBERS',displayMembers)
 .replace('// SETTINGS_BASELINE_DISPLAY_REMEMBER',displayRemember+displayOwnerCheck)
 .replace('// SETTINGS_BASELINE_DISPLAY_REGISTRATION',displayRegistration)
 .replace('// SETTINGS_BASELINE_BRIGHTNESS_GUIDS',brightnessGuids)
 .replace('// SETTINGS_BASELINE_BRIGHTNESS_BODY',brightnessBody)
 .replace('// SETTINGS_BASELINE_FRONTEND_IDENTITY_READER',frontendReader?.code||'')
 .replace('// SETTINGS_BASELINE_PRODUCTION_BODIES',sectionCacheOnly?'':bodies.join('\n'))
 .replace('// SETTINGS_BASELINE_MONITOR_BOUNDED_CONSUMER',monitorRootWriteOnly?boundedMonitorConsumer:'')
 .replace('// SETTINGS_BASELINE_PRODUCTION_IO',ioBodies.join('\n'))
 .replace('// SETTINGS_BASELINE_PRODUCTION_HANDLER',sectionCacheOnly?'':handler+'\n'+resetHandler);
fs.writeFileSync(path.join(evidence,'settings-baseline-native.cpp'),(virtualStartWriteFailure?'#define YMCC_SETTINGS_VIRTUAL_START_WRITE_FAILURE_SCOPE 1\n':monitorRootWriteOnly?'#define YMCC_SETTINGS_MONITOR_ROOT_WRITE_SCOPE 1\n':sectionCacheOnly?'#define YMCC_SETTINGS_SECTION_CACHE_SCOPE 1\n':serialConfigOnly?'#define YMCC_SETTINGS_SERIAL_CONFIG_SCOPE 1\n':'')+'#include <fstream>\n'+source);
const inputs=['native/settings_upgrade_recovery.h','native/update_config_state.h','native/main.cpp','native/decky_sidebar_manual_repair.h','native/steam_command_coordinator.h','native/steam_command_disposition.h','native/decky_sidebar_session.h','native/decky_update_recovery.h','native/steam_session_observer.h','native/steam_window_event_budget.h','native/settings_baseline.h','native/boot_mode_shortcuts.h','native/controller_persona_settings.h','native/controller_persona.h','deps/json/json.hpp','tools/settings_baseline_native_prepare.mjs','tools/settings_baseline_native_selftest.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/fixtures/settings_baseline_native.cpp','PowerControl/settings-baseline.json','PowerControl/settings-defaults-v1.json'];
inputs.push('native/boot_mode_preferences.h','tools/lib/hc_controller_motion_fixture.mjs','native/decky_running_build_recovery.h','native/decky_build_recovery_contract.h');
if(serialActionFenceNeeded)inputs.push('native/gyro_telemetry_record.h');
if(virtualStartWriteFailure)inputs.push('native/input_shortcut_toggle.h','native/game_input_override.h','native/virtual_start_protection.h');
if(!sectionCacheOnly)inputs.push('native/battery_charge_client.h','native/shortcut_feedback_native.h');
if(!sectionCacheOnly)inputs.push('native/decky_sidebar_bootstrap.h','native/decky_loader_identity.h');
inputs.push('native/steam_settings_runtime.h','native/steam_monitor_settings.h');
inputs.push('native/sleep_power_state.h','native/sleep_power_observation.h','native/sleep_power_revocation.h');
inputs.push('native/decky_sidebar_diagnostics.h'); // Actual extracted reserve uses RecoveryBudgetFacts in both storage namespaces.
const segments={memoryStorage:sectionCacheOnly?0:bodies.length,realWin32Io:ioBodies.length,ipcHandlers:sectionCacheOnly?0:2,total:(sectionCacheOnly?0:bodies.length+2)+ioBodies.length};
segments.cleanupUpgradePort={
 purePolicy:{path:upgradePolicyPath,declaredPath:'native/decky_running_build_recovery.h',bytes:upgradePolicyBytes.length,sha256:hash(upgradePolicyBytes)},
 purePolicyTransitive:[{path:upgradePolicyDetailPath,declaredPath:'native/decky_build_recovery_contract.h',bytes:upgradePolicyDetailBytes.length,sha256:hash(upgradePolicyDetailBytes)}],
 policyNamespace:'ymcc::deckyupgrade::running',buildFields:['id','sha256','version'],testVersionMacro:'APP_VER_STR (version only; not running identity proof)',
 fixtureSupport:{path:fixturePath,begin:upgradePortBegin,end:upgradePortEnd,sourceSha256:hash(upgradePortSource),compiledSha256:hash(upgradePortCompiled)},
 actualReserve:{symbol:cleanupReservation.symbol,startLine:cleanupReservation.startLine,endLine:cleanupReservation.endLine,sha256:cleanupReservation.sha256,storeCallCount:cleanupReservation.code.split('ymcc::deckyupgrade::runningstore::reserve(').length-1},
 protectedInstallationIO:'DECLARED_MEMORY_PORT_DEFAULT_FALSE',imageDigest:'DECLARED_READ_ONLY_PORT',current:'CALLER_DECLARED_CURRENT_CALLBACK',
 realStoreHeaderIncluded:false,privilegedAclPositiveClaimed:false,
 settingsPairIO:'ORIGINAL_EXTRACTED_WIN32_GUARD_DURABLE_ATOMIC_WRITE_READBACK',
 enabledOnlyInsideOriginalCase:'cleanup pair-commit fallback and duplicate',newCanonicalId:false
};
if(!sectionCacheOnly){
 segments.additionalProductionReaders=1;segments.total+=1;
 segments.charge={actualRegistration:true,operations:['get','refresh','set','release'],registeredHandlers:4,extractedFragments:3,externalDeviceOwner:'memory-only',registrationSha256:hash(chargeRegistration),validationSha256:hash(chargeValidation)};
 segments.ipcHandlers+=4;segments.total+=3;
 segments.display={actualRegistration:true,operations:['display.getModes','display.setMode'],registeredHandlers:2,extractedFragments:displayRemember?6:5,displaySha256:hash(displayRegistration),brightnessSha256:hash(brightnessBody),rememberSha256:displayRemember?hash(displayRemember):null,feedbackHeader:'native/shortcut_feedback_native.h',deviceAndPowerApis:'explicit memory ports only'};
 segments.ipcHandlers+=2;segments.total+=segments.display.extractedFragments;
 if(displayOwnerCheck){segments.display.ownerGuardSha256=hash(displayOwnerCheck);segments.display.extractedFragments++;segments.total++;}
 segments.cleanupIdentity={reserve:{symbol:cleanupReservation.symbol,startLine:cleanupReservation.startLine,endLine:cleanupReservation.endLine,sha256:cleanupReservation.sha256},frontendReader:{source:'native/decky_sidebar_bootstrap.h',symbol:frontendReader.symbol,startLine:frontendReader.startLine,endLine:frontendReader.endLine,sha256:hash(frontendReader.code),declarationKeywordRestored:true},actualReader:true,fixtureOwnedMetadata:true};
}
if(sectionCacheOnly){segments.sectionCache=sectionCacheSegments;segments.excludedFromLeaf=['Original memory and IPC consumer bodies (default retained)','Decky cleanup bridge and old default/cleanup runtime cases (original scopes retained)'];}
if(serialActionFenceNeeded){segments.serialActionRules={actualType:true,globalSupportOnce:true,recordHeader:'native/gyro_telemetry_record.h',jsonHeader:'deps/json/json.hpp',bodyPaths:sectionCacheOnly?['realWin32Io']:['memoryStorage','realWin32Io'],dependencies:serialActionDependencies.map(({symbol,startLine,endLine,sha256})=>({symbol,startLine,endLine,sha256}))};segments.total+=serialActionDependencies.length;}
if(serialConfigOnly){segments.serialConfigSelection=serialConfigSelection;segments.serialConfigReaders=serialConfigReaders.map(({symbol,startLine,endLine,sha256})=>({symbol,startLine,endLine,sha256}));segments.serialConfigObservation=serialConfigObservation;}
if(sourceMainArgument)segments.sourceMain={path:sourceMainPath,declaredPath:'native/main.cpp',sha256:hash(mainBytes),bytes:mainBytes.length,explicitCandidate:true};
if(virtualStartGuard){
 const {code,...identity}=virtualStartGuard;
 segments.virtualStartWriteFailure={...identity,handlerSha256:hash(handler),consumerNamespace:'memoryFixture',
  ports:['owned memory storage and main-write fault','controlled monotonic clock','lifecycle log sink and timeout observation','owned session identity','external/runtime refresh and ROG OFF ports']};
}
fs.writeFileSync(path.join(evidence,'source-segments.json'),JSON.stringify(segments,null,2)+'\n');
const privateTools={
 'native/decky_running_build_recovery.h':upgradePolicyPath,
 'native/decky_build_recovery_contract.h':upgradePolicyDetailPath,
 'tools/settings_baseline_native_prepare.mjs':fileURLToPath(import.meta.url),
 'tools/fixtures/settings_baseline_native.cpp':fixturePath,
 'tools/settings_baseline_native_selftest.mjs':path.join(toolDirectory,'settings_baseline_native_selftest.mjs'),
 ...(sourceMainArgument?{'native/main.cpp':sourceMainPath}:{}),
};
const inputIdentities=inputs.map(file=>privateTools[file]?{path:path.resolve(privateTools[file]),declaredPath:file,sha256:hash(fs.readFileSync(privateTools[file]))}:{path:file,sha256:hash(sectionCacheOnly&&file==='native/main.cpp'?mainBytes:fs.readFileSync(path.join(root,file)))});
if(sectionCacheOnly){
 fs.writeFileSync(compiledFragmentPath,compiledSectionFragments);
 if(candidateIdentity)inputIdentities.push({...candidateIdentity,compiledFragmentsSha256:hash(compiledSectionFragments)});
 inputIdentities.push({path:compiledFragmentPath,absolutePath:compiledFragmentPath,sha256:hash(compiledSectionFragments),kind:'section-cache-compiled-fragments',scope:'section-cache'});
}
fs.writeFileSync(path.join(evidence,'native-source-inputs.json'),JSON.stringify(inputIdentities,null,2)+'\n');console.log('NATIVE_CONSUMERS_EXTRACTED '+bodies.length);
