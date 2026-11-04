import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';
const root=process.cwd(),evidence=process.env.YMCC_BASELINE_EVIDENCE;
const sectionCacheOnly=process.argv.includes('--section-cache'),candidateAt=process.argv.indexOf('--section-cache-candidate');
if(sectionCacheOnly&&process.argv.includes('--cleanup-history'))throw Error('section-cache and cleanup-history scopes are exclusive');
if(candidateAt>=0&&(!sectionCacheOnly||!process.argv[candidateAt+1]||process.argv[candidateAt+1].startsWith('--')))throw Error('section-cache-candidate requires explicit --section-cache and a file');
const toolDirectory=path.dirname(fileURLToPath(import.meta.url));
const {extract:extractMainMember}=await import(pathToFileURL(path.join(root,'tools/fixtures/legion_motion_consumer/extract_main.mjs')).href);
const fixturePath=path.join(toolDirectory,'fixtures/settings_baseline_native.cpp');
if(!evidence)throw Error('YMCC_BASELINE_EVIDENCE is required');fs.mkdirSync(evidence,{recursive:true});
const mainBytes=fs.readFileSync(path.join(root,'native/main.cpp'));
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
 slice('static std::string ymSettingsReadRaw(', '// Merge only actual changes'),
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
const source=fs.readFileSync(fixturePath,'utf8')
 .replace('// SETTINGS_BASELINE_FRONTEND_IDENTITY_READER',frontendReader?.code||'')
 .replace('// SETTINGS_BASELINE_PRODUCTION_BODIES',sectionCacheOnly?'':bodies.join('\n'))
 .replace('// SETTINGS_BASELINE_PRODUCTION_IO',ioBodies.join('\n'))
 .replace('// SETTINGS_BASELINE_PRODUCTION_HANDLER',sectionCacheOnly?'':handler+'\n'+resetHandler);
fs.writeFileSync(path.join(evidence,'settings-baseline-native.cpp'),(sectionCacheOnly?'#define YMCC_SETTINGS_SECTION_CACHE_SCOPE 1\n':'')+'#include <fstream>\n'+source);
const inputs=['native/main.cpp','native/decky_sidebar_manual_repair.h','native/decky_sidebar_session.h','native/decky_update_recovery.h','native/steam_session_observer.h','native/steam_window_event_budget.h','native/settings_baseline.h','native/boot_mode_shortcuts.h','native/controller_persona_settings.h','native/controller_persona.h','deps/json/json.hpp','tools/settings_baseline_native_prepare.mjs','tools/settings_baseline_native_selftest.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/fixtures/settings_baseline_native.cpp','PowerControl/settings-baseline.json','PowerControl/settings-defaults-v1.json'];
if(!sectionCacheOnly)inputs.push('native/decky_sidebar_bootstrap.h','native/decky_loader_identity.h');
const segments={memoryStorage:sectionCacheOnly?0:bodies.length,realWin32Io:ioBodies.length,ipcHandlers:sectionCacheOnly?0:2,total:(sectionCacheOnly?0:bodies.length+2)+ioBodies.length};
if(!sectionCacheOnly){
 segments.additionalProductionReaders=1;segments.total+=1;
 segments.cleanupIdentity={reserve:{symbol:cleanupReservation.symbol,startLine:cleanupReservation.startLine,endLine:cleanupReservation.endLine,sha256:cleanupReservation.sha256},frontendReader:{source:'native/decky_sidebar_bootstrap.h',symbol:frontendReader.symbol,startLine:frontendReader.startLine,endLine:frontendReader.endLine,sha256:hash(frontendReader.code),declarationKeywordRestored:true},actualReader:true,fixtureOwnedMetadata:true};
}
if(sectionCacheOnly){segments.sectionCache=sectionCacheSegments;segments.excludedFromLeaf=['Original memory and IPC consumer bodies (default retained)','Decky cleanup bridge and old default/cleanup runtime cases (original scopes retained)'];}
fs.writeFileSync(path.join(evidence,'source-segments.json'),JSON.stringify(segments,null,2)+'\n');
const privateTools={
 'tools/settings_baseline_native_prepare.mjs':fileURLToPath(import.meta.url),
 'tools/fixtures/settings_baseline_native.cpp':fixturePath,
 ...(sectionCacheOnly?{'tools/settings_baseline_native_selftest.mjs':path.join(toolDirectory,'settings_baseline_native_selftest.mjs')}:{}),
};
const inputIdentities=inputs.map(file=>privateTools[file]?{path:path.resolve(privateTools[file]),declaredPath:file,sha256:hash(fs.readFileSync(privateTools[file]))}:{path:file,sha256:hash(sectionCacheOnly&&file==='native/main.cpp'?mainBytes:fs.readFileSync(path.join(root,file)))});
if(sectionCacheOnly){
 fs.writeFileSync(compiledFragmentPath,compiledSectionFragments);
 if(candidateIdentity)inputIdentities.push({...candidateIdentity,compiledFragmentsSha256:hash(compiledSectionFragments)});
 inputIdentities.push({path:compiledFragmentPath,absolutePath:compiledFragmentPath,sha256:hash(compiledSectionFragments),kind:'section-cache-compiled-fragments',scope:'section-cache'});
}
fs.writeFileSync(path.join(evidence,'native-source-inputs.json'),JSON.stringify(inputIdentities,null,2)+'\n');console.log('NATIVE_CONSUMERS_EXTRACTED '+bodies.length);
