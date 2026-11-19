import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {pathToFileURL,fileURLToPath} from 'node:url';
import {prepareOwnedReturnSources} from './prepare_base.mjs';
const here=path.dirname(fileURLToPath(import.meta.url)),sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const pin=file=>{const bytes=fs.readFileSync(file);return {path:path.resolve(file),bytes:bytes.length,sha256:sha(bytes)};};
const save=(file,value)=>fs.writeFileSync(file,typeof value==='string'?value:JSON.stringify(value,null,2)+'\n',{flag:'wx'});

export async function prepareRealStoreSources({repo,output,profile='Checked',runName}={}){
 assert(['Release','Checked'].includes(profile),'EXACT_ORIGINAL_PROFILE_REQUIRED');
 const original=prepareOwnedReturnSources({repo,output,selectedProfiles:[profile],runName});
 const {run,snapshot,frozen,ranges,extract,endBrace}=original;
const crashRelative='native/msi_crash_recovery_runtime.h',crashBytes=fs.readFileSync(path.join(repo,crashRelative));frozen.set(crashRelative,crashBytes);
fs.copyFileSync(path.join(repo,crashRelative),path.join(snapshot,crashRelative),fs.constants.COPYFILE_EXCL);
const {extractFunction}=await import(pathToFileURL(path.join(repo,'tools/ymcc_sim/global/controller-startup-return/extend-fixture.mjs')).href);
function writerFunction(name){
 const raw=frozen.get(name==='msiCrashReadOwnedRecord'?crashRelative:'native/main.cpp').toString('utf8');
 if(name!=='sgWriteFileAtomicFlags')return extractFunction(raw,name);
 const match=/^bool sgWriteFileAtomicFlags\(/m.exec(raw);assert(match);
 const row=extractFunction(raw.slice(0,match.index)+'static '+raw.slice(match.index),name),code=row.code.slice(7);
 assert.equal(raw.slice(match.index,match.index+code.length),code,'ORIGINAL_NONSTATIC_WRITER_EXACT_BYTES');return {...row,code,sha256:sha(code),nonstaticTokenOnlyForExtraction:true};
}
const writerFunctions=['sgWriteFileAtomicFlags','sgWriteFileAtomic','msiCrashReadOwnedRecord'].map(writerFunction);
const writerInclude=path.join(snapshot,'tools/actual-store-writer.inc');save(writerInclude,writerFunctions.map(row=>row.code).join('\n\n')+'\n');
const businessNames=['msiCrashRecoveryPath','msiCrashNote','msiCrashStoreLocked','msiCrashRecoveryAfterModeDischarge'];
const businessInclude=path.join(snapshot,'tools/production_store_business.inc');save(businessInclude,businessNames.map(name=>extract('function',name,crashRelative)).join('\n\n')+'\n');
const originalCpp=frozen.get('tools/msi_owned_return_runtime_selftest.cpp').toString('utf8');let cpp=originalCpp;const edits=[];
function change(before,after,role){assert.equal(cpp.split(before).length,2,'EXACT_ORIGINAL_CONSUMER_ANCHOR:'+role);cpp=cpp.replace(before,()=>after);edits.push({role,beforeBase64:Buffer.from(before).toString('base64'),afterBase64:Buffer.from(after).toString('base64')});}
change('#include "json.hpp"','#include "json.hpp"\n#include <type_traits>\n#include "store_bridge.h"','two-TU-test-bridge-only');
change('static DWORD GetCurrentProcessId() { return 43100; }','static DWORD GetCurrentProcessId() { return msiStoreFixture::processId(); }','current-real-test-process-id');
change('static std::string msiCrashProcessCreation(HANDLE) { return "133900000000000001"; }','static std::string msiCrashProcessCreation(HANDLE) { return msiStoreFixture::processCreation(); }','current-real-test-process-birth-string');
const fakeAfter=extract('function','msiCrashRecoveryAfterModeDischarge','tools/msi_owned_return_runtime_selftest.cpp');
change(fakeAfter,`static std::string W2U(const std::wstring& path);
static bool g_msiCrashLoaded=true;
static std::wstring g_realStoreRoot;
static std::wstring app_data_dir(){return g_realStoreRoot;}
static bool sgWriteFileAtomic(const std::wstring& file,const std::string& text){return msiStoreFixture::atomicWrite(file,text);}
#include "production_store_business.inc"`,'replace-in-memory-After-with-verbatim-product-path-note-store-After');
const log='static void appendNativeLifecycleLog(const char* domain, const json& row) { g_logs.emplace_back(domain, row);if(g_logHook)g_logHook(row); }';
change(log,`static void appendNativeLifecycleLog(const char* domain,const json& row) {
 if(row.value("phase",std::string{})=="record-committed" && row.value("reason",std::string{})=="qualified-XInput-return-not-Steam-consumption-proof" && row.value("durableRecordCommitted",false))++g_count.durableDischarges;
 g_logs.emplace_back(domain,row);if(g_logHook)g_logHook(row);
}`,'count-only-real-committed-discharge-log');
const caseName='prior-command/physical-match-settles-even-when-command-busy';
const caseStart=originalCpp.indexOf('    c.run("'+caseName+'",[&]{');assert(caseStart>=0);
const open=originalCpp.indexOf('{',caseStart),end=endBrace(originalCpp,open);assert.equal(originalCpp.slice(end,end+2),');');
const originalCase=originalCpp.slice(caseStart,end+2);let selectedCase=originalCase;
const setup=`
        const auto expected=g_msiCrashRecord;g_msiCrashLoaded=true;
        c.require(msiCrashStoreLocked(expected,"fixture-owned-loaded-seed"),"real StoreLocked seeds only the private loaded-owner journal");
        std::string beforeBytes;unsigned long error=0;
        c.require(msiStoreFixture::readOwned(msiCrashRecoveryPath(),beforeBytes,error),"real original reader reads the seeded record");
        c.require(beforeBytes==msiRecovery::encodeRecord(expected),"seeded bytes exactly preserve the original owner and all debts");
        const auto beforeIo=json::parse(msiStoreFixture::traceJson());
        if(deny)c.require(msiStoreFixture::acquireNoDeleteLease(msiCrashRecoveryPath(),error),"owned read-only lease blocks delete without touching other files");
`;
selectedCase=selectedCase.replace('[&]{',()=> '[&]{'+setup);
assert.equal(selectedCase.split('c.settled();').length,2);
selectedCase=selectedCase.replace('c.settled();',()=>`if(deny){
 c.owed("failed real file commit retains the original in-memory and durable debt");
 c.require(!g_msiOwnedReturnRecovery.completed()&&g_msiOwnedReturnMatchedEpisode.load()==0,"failed commit publishes no completion or matched certificate");
 c.require(g_count.durableDischarges==0,"failed real commit emits no successful durable discharge");
 }else c.settled();`);
const post=`
        std::string afterBytes;c.require(msiStoreFixture::readOwned(msiCrashRecoveryPath(),afterBytes,error),"real original reader reads the result under the owned lease");
        const auto decoded=msiRecovery::decodeRecord(afterBytes);
        auto wanted=expected;if(!deny){wanted.modeReturnPending=false;wanted.modeAttemptOwner.clear();}
        c.require(decoded.state==msiRecovery::DecodeState::Valid&&afterBytes==msiRecovery::encodeRecord(wanted),"actual file transaction changes only the qualified mode obligation");
        c.require(msiRecovery::encodeRecord(g_msiCrashRecord)==afterBytes,"published in-memory record equals the actual committed file");
        if(deny)c.require(afterBytes==beforeBytes,"sharing-denied settlement preserves every original file byte");
        if(deny)c.require(msiStoreFixture::releaseNoDeleteLease(error),"owned no-delete lease is explicitly returned");
        const auto afterIo=json::parse(msiStoreFixture::traceJson());
        c.require(afterIo.value("fileHandlesLive",99u)==0&&!afterIo.value("leaseHeld",true),"all owned real file handles are closed before reporting");
        c.require(afterIo.value("writes",0u)>beforeIo.value("writes",0u)&&afterIo.value("flushes",0u)>beforeIo.value("flushes",0u),"settlement really writes and flushes an owned temporary");
        if(deny)c.require(afterIo.value("moves",0u)-beforeIo.value("moves",0u)==6&&afterIo.value("replaces",0u)-beforeIo.value("replaces",0u)==1,"real sharing denial exercises the original six moves and replacement fallback");
        storeEvidence={{"parameter",deny?"sharing-denial":"success"},{"beforeRecord",msiRecovery::encodeRecord(expected)},{"afterRecord",afterBytes},{"beforeIo",beforeIo},{"afterIo",afterIo},{"durableDischarges",g_count.durableDischarges},{"syntheticCanonicalCache",true},{"logicalOnAccepted",false},{"physicalReaderAccepted",false}};
`;
const closing=selectedCase.lastIndexOf('}');selectedCase=selectedCase.slice(0,closing)+post+selectedCase.slice(closing);
const mainStart=cpp.indexOf('int main() {');assert(mainStart>=0);const mainEnd=endBrace(cpp,cpp.indexOf('{',mainStart));
const newMain=`int wmain(int argc,wchar_t** argv) {
 if(argc!=3)return 2;const std::wstring parameter=argv[2];if(parameter!=L"success"&&parameter!=L"sharing-denial")return 2;
 const bool deny=parameter==L"sharing-denial";unsigned long error=0;g_realStoreRoot=argv[1];if(!msiStoreFixture::initialize(g_realStoreRoot,error))return 3;
 const std::array<void(*)(Checks&),11> legacyGroups={classify,trackingAndReturn,invalidSignals,contextAndOwnership,foregroundIsolation,commandBoundaries,windowsAndDormancy,failedMode2ReturnDuty,commandReadyBoundary,refreshedActualOutcome,offTerminalHandoff};static_cast<void>(legacyGroups);
 Checks c;json storeEvidence=json::object();
${selectedCase}
 const bool releaseOk=msiStoreFixture::releaseNoDeleteLease(error);c.require(releaseOk,"final owned lease release is confirmed");
 c.require(msiStoreFixture::shutdown(error),"owned directory and file handles are explicitly returned before reporting");
 const auto finalIo=json::parse(msiStoreFixture::traceJson());c.require(finalIo.value("fileHandlesLive",99u)==0&&finalIo.value("directoryHandlesLive",99u)==0&&!finalIo.value("leaseHeld",true),"final real file and directory ownership is empty");
 const json report={{"passed",c.failures.empty()},{"tests",c.cases.size()},{"assertions",c.assertions},{"failures",c.failures},{"cases",c.cases},{"storeEvidence",storeEvidence},{"finalIo",finalIo},{"realFileTransaction",true},{"originalCase",${JSON.stringify(caseName)}},{"noInputHost",true},{"noSdkDeviceIo",true},{"logicalOnAccepted",false},{"physicalReaderAccepted",false},{"legacyOtherCases","NOT_RUN"}};
 std::cout<<report.dump(2)<<'\\n';return c.failures.empty()?0:1;
}`;
change(cpp.slice(mainStart,mainEnd),newMain,'only-original-selected-case-and-two-file-parameters');
let inverse=cpp;for(const edit of [...edits].reverse()){const before=Buffer.from(edit.beforeBase64,'base64').toString('utf8'),after=Buffer.from(edit.afterBase64,'base64').toString('utf8');assert.equal(inverse.split(after).length,2);inverse=inverse.replace(after,()=>before);}assert.equal(inverse,originalCpp,'EXACT_CONSUMER_DELTA_INVERSE');
const consumer=path.join(snapshot,'tools/msi_owned_return_real_store_selftest.cpp');save(consumer,cpp);

 const bridgeRows=[];for(const name of ['store_bridge.h','store_bridge.cpp']){const source=path.join(here,name),target=path.join(snapshot,'tools',name);fs.copyFileSync(source,target,fs.constants.COPYFILE_EXCL);bridgeRows.push({source:pin(source),candidate:pin(target)});}
 const sourceRecords=[...original.sources,crashRelative].map(relative=>({relative,original:path.join(repo,relative),snapshot:path.join(snapshot,relative),...pin(path.join(snapshot,relative))}));
 const binding={status:'PREPARED_NOT_EXECUTED',run,snapshot,profile,originalRawBinding:pin(path.join(run,'source-binding.json')),sourceRecords,consumer:pin(consumer),edits,exactConsumerInverse:true,bridgeRows,originalSelectedCase:{name:caseName,textBase64:Buffer.from(originalCase).toString('base64'),sha256:sha(originalCase)},writerInclude:pin(writerInclude),writerFunctions:writerFunctions.map(({code,...row})=>row),businessInclude:pin(businessInclude),businessFunctions:businessNames,extractedRanges:ranges,boundary:{loadedOwnerOnly:true,syntheticCanonicalCache:true,syntheticHostAndPowerFacts:true,logicalOnAccepted:false,physicalReaderAccepted:false,normalProductMainExecuted:false,realDeviceIo:false}};
 save(path.join(run,'REAL-STORE-SOURCES.json'),binding);return binding;
}
