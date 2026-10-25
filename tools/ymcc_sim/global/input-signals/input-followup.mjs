/** Current production diagnostic expressions/branches and keyboard identity gate.
 * No product text transformation; only source extraction and IO boundary fakes.
 */
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import crypto from 'node:crypto';
import {FOLLOWUP_IDENTITY_SOURCES,followupToolDeclaration,beginFollowupScope,auditedFollowupBundle,verifyFollowupVmCode,buildAuditedFollowupNative,verifyFollowupScope,saveFollowupOutput,finishFollowupScope} from './input-followup-tool-scope.mjs';
import {fileURLToPath} from 'node:url';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {extractNativeFollowup} from './source-extract.mjs';

const exec=promisify(execFile),here=path.dirname(fileURLToPath(import.meta.url));
const defaultRepo=path.resolve(here,'../../../..');
const hash=data=>crypto.createHash('sha256').update(data).digest('hex');
const readJson=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
function writeJson(file,value){fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');}
const relativeInputs=['native/main.cpp','native/input_shortcut_toggle.h','native/shortcut_frontend_button.h','deps/json/json.hpp','src/bridge/controllerShortcutRules.ts','src/bridge/controllerPersona.ts','tools/controller_shortcut_rules_selftest.ts','tools/ymcc_sim/global/runner.mjs'];
const tools=['keyboard-contract.ts','source-extract.mjs','native-diagnostic-fixture.cpp','input-followup.mjs','input-followup.test.mjs'];
export function followupSourceInputs(){return [...new Set([...relativeInputs,...tools.map(p=>'tools/ymcc_sim/global/input-signals/'+p),'tools/fixtures/hc_reviewed_calibration/toolchain.mjs',...FOLLOWUP_IDENTITY_SOURCES])];}
/** Parent registers this descriptor; this module never edits the registry. */
export function getInputFollowupAdapter(repoRoot=defaultRepo){
 return {id:'controller.input-diagnostics',domain:'controller',title:'Current native OEM diagnostic identities and complete keyboard mapping contract',
  kind:'node',path:'tools/ymcc_sim/global/input-signals/input-followup.mjs',
  args:['--work-dir','{outDir}/input-followup-work','--output','{outDir}/input-followup-result.json'],
  timeoutMs:120000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['input-followup-result.json'],
  evidenceInputs:followupSourceInputs(),...followupToolDeclaration(repoRoot)};
}
function sourceHashes(repoRoot){return followupSourceInputs().map(p=>({path:p,sha256:hash(fs.readFileSync(path.join(repoRoot,p)))}));}
async function bundleRun(scope,source,entryFile){
 const built=await auditedFollowupBundle(scope,source,entryFile);
 const module={exports:{}},messages=[];
 const processPort=Object.freeze({env:Object.freeze({NODE_ENV:'production'})});
 const context=vm.createContext({module,exports:module.exports,process:processPort,Buffer,require(spec){throw Error('UNREVIEWED_TEST_REQUIRE:'+spec);},console:{log:(...items)=>messages.push(items.map(String).join(' '))}},{codeGeneration:{strings:false,wasm:false}});
 let failure;
 verifyFollowupVmCode(scope,built,'vm:before');
 try{new vm.Script(built.code,{filename:entryFile??'input-followup-entry.bundle.cjs'}).runInContext(context,{timeout:10000});}
 catch(error){failure=String(error.message);}
 verifyFollowupVmCode(scope,built,'vm:after');
 saveFollowupOutput(scope,built.record.label+'.vm-result.json',JSON.stringify({failure:failure??null,messages},null,2)+'\n','actual original VM feedback');
 return {exports:module.exports,messages,failure,inputs:built.inputs};
}
function nativeIncludes(spans){
 const by=Object.fromEntries(spans.map(s=>[s.name,s]));
 const include=names=>names.map(name=>`#line ${by[name].startLine} "${by[name].path??"product-native-main.cpp"}"\n${by[name].text}\n`).join('\n');
 return {'current-native-types.inc':include(['producerEnum','producerCount','oemBits','producerTokens','ruleStruct']),
  'current-native-dispatch.inc':include(['dispatch']),
  'current-native-mapping.inc':include(['desktopMapping','actionMapping','inputFlags']),
  'current-native-string.inc':include(['inputString']),
  'current-native-inputs.inc':include(['maskMapping','requiredMask','desktopKeys','allInputs']),
  'current-native-summary.inc':include(['summaryLog']),'current-native-click.inc':include(['clickLog']),
  'current-native-edge.inc':include(['edgeEmitLogs']),'current-native-numpad-enter.inc':include(['numpadEnterAdmission'])};
}
async function buildNative(repoRoot,workDir,spans,scope){
 return buildAuditedFollowupNative(scope,{fixture:path.join(here,'native-diagnostic-fixture.cpp'),generated:nativeIncludes(spans)});
}
/** This gate deliberately returns FAIL against the unfixed current product. */
export async function runInputFollowupRegressions(options={}){
 const repoRoot=fs.realpathSync(options.repoRoot??defaultRepo),workDir=path.resolve(options.workDir??path.join(process.cwd(),'work/input-followup'));
 const scope=beginFollowupScope(repoRoot,workDir,followupSourceInputs());
 const before=sourceHashes(repoRoot),nativeText=fs.readFileSync(path.join(repoRoot,'native/main.cpp'),'utf8');
 const spans=extractNativeFollowup(nativeText,{inputShortcutHeader:fs.readFileSync(path.join(repoRoot,'native/input_shortcut_toggle.h'),'utf8')});
 const model=await bundleRun(scope,`export * from './tools/ymcc_sim/global/input-signals/keyboard-contract';`);
 if(model.failure)throw Error('KEYBOARD_CONTRACT_BUNDLE_FAILED:'+model.failure);
 const keyboard=model.exports,inspection=keyboard.inspectKeyboardActionContracts();
 let contract;
 try{contract=keyboard.assertKeyboardActionContract();}catch(error){contract={status:'FAIL',error:error.message};}
 const oldTest=await bundleRun(scope,null,'tools/controller_shortcut_rules_selftest.ts');
 const build=await buildNative(repoRoot,workDir,spans,scope);
 const fixture={schemaVersion:1,mappings:keyboard.keyboardMappingCases(),defaults:inspection.map(i=>({actionId:i.actionId,code:i.defaultKey}))};
 const fixtureJson=JSON.stringify(fixture,null,2)+'\n',fixtureSha256=hash(fixtureJson),fixtureFile=path.join(build.dir,'keyboard-fixture-'+fixtureSha256.slice(0,20)+'.json');saveFollowupOutput(scope,path.relative(workDir,fixtureFile),fixtureJson,'actual keyboard mapping/default fixture');if(hash(fs.readFileSync(fixtureFile))!==fixtureSha256)throw Error('NATIVE_FIXTURE_BYTES_CHANGED');
 verifyFollowupScope(scope,'native:before-execution');
 let stdout,exitCode=0;
 try{({stdout}=await exec(build.executable,[fixtureFile],{cwd:build.dir,windowsHide:true,timeout:20000,maxBuffer:8*1024*1024}));}
 catch(error){if(typeof error.code!=='number')throw error;exitCode=error.code;stdout=error.stdout;}
 verifyFollowupScope(scope,'native:after-execution');
 saveFollowupOutput(scope,'native-execution.stdout.json',stdout,'actual original native consumer feedback');
 saveFollowupOutput(scope,'native-execution.json',JSON.stringify({exitCode,executable:build.executable,executableSha256:build.executableSha256,naturalExit:true,productionOsInput:false},null,2)+'\n','actual owned native process terminal');
 const native=JSON.parse(stdout),after=sourceHashes(repoRoot);
 const scoped=finishFollowupScope(scope);
 const drift=before.filter(s=>after.find(a=>a.path===s.path)?.sha256!==s.sha256).map(s=>s.path);
 const passed=native.status==='PASS'&&contract.status==='PASS'&&!oldTest.failure&&!drift.length;
 return {schemaVersion:1,kind:'input-diagnostic-keyboard-followup',status:passed?'PASS':'FAIL',
  verdict:passed?'SOFTWARE_DIAGNOSTIC_AND_KEYBOARD_IDENTITIES_PASS':'CURRENT_PRODUCTION_OR_TEST_CONTRACT_FAILURE',
  summary:{diagnosticChecks:native.diagnostic?.checks??0,diagnosticFailures:native.diagnostic?.failures?.length??0,keyboardChecks:native.keyboard?.checks??0,keyboardFailures:native.keyboard?.failures?.length??0,keyboardOptions:fixture.mappings.length,legacySelftest:oldTest.failure?'FAIL':'PASS'},
  sourceChanges:options.sourceChanges??[
   {path:'native/main.cpp',beforeSha256:'415cb57ee07a4e60dcc08c3fbe1fa7fbf90ab795dcffc73317ddd369df71c12e',afterSha256:before.find(s=>s.path==='native/main.cpp').sha256,appliedBy:'parent-serialized-integration',reason:'Four diagnostic action identities honor uiAction; summary digest notices uiAction-only edits.'},
   {path:'src/bridge/controllerShortcutRules.ts',beforeSha256:'be74445e162684db14a974b17bcdde161512159d03d61bf64ccf4b36ec14d3cb',afterSha256:before.find(s=>s.path==='src/bridge/controllerShortcutRules.ts').sha256,reason:'Symbol default uses Backquote, admitted by UI options, validation and native mapping.'},
   {path:'tools/controller_shortcut_rules_selftest.ts',beforeSha256:'68d61d2fc84f33a9d6be8e755a5560e05c3fa59c774ae5460b8ca21637d47ff3',afterSha256:before.find(s=>s.path==='tools/controller_shortcut_rules_selftest.ts').sha256,reason:'Exact keyboard identities, defaults, reload and negatives replace stale counts; align existing mixed-input/presentation/frontend expectations to actual current native consumers.'}
  ],sourceHashes:scoped.sourceHashes,toolScope:scoped.identity,sourceDrift:drift,
  executedProductModules:[...new Map([...model.inputs,...oldTest.inputs].map(s=>[s.path,{...s,execution:'unchanged-typescript-bundle-in-isolated-vm'}])).values(),
   {path:'native/main.cpp',sha256:before.find(s=>s.path==='native/main.cpp').sha256,execution:'exact-current-source-excerpts-in-native-helper',spans:spans.map(({text,...row})=>row)},
   {path:'native/shortcut_frontend_button.h',sha256:before.find(s=>s.path==='native/shortcut_frontend_button.h').sha256,execution:'actual-header-parser'}],
  inputConsumerStateFeedback:[...(native.diagnostic?.inputConsumerStateFeedback??[]).map(r=>({provenance:'source-derived-fixture',...r})),...(native.keyboard?.inputConsumerStateFeedback??[]).map(r=>({provenance:'source-derived-fixture',...r}))],
  native:{...native,exitCode,fixtureSha256,executableSha256:build.executableSha256,importAudit:build.importAudit},
  keyboardContract:{...contract,inspection},legacySelftest:{status:oldTest.failure?'FAIL':'PASS',error:oldTest.failure??null,messages:oldTest.messages},
  compatibility:{schemaVersion:1,sourceExtractContract:'exact-declarations-functions-and-log-branches;unknown-source-shape-fails',hcRuntimeRequired:false,hcVersionPinned:false},
  boundaries:{actualOsInput:false,hardwareAccess:false,userSettingsRead:false,ordinaryAppStarted:false,hostStarted:false,realOsSleep:false,physicalSuppressionConfirmed:false},
  remainingGaps:[{stage:'physical',code:'DIAGNOSTIC_IDENTITY_IS_NOT_OEM_SUPPRESSION'},{stage:'os-input',code:'MAPPING_AND_FLAGS_DO_NOT_PROVE_ACTUAL_DELIVERY'}]};
}
function linePatch(text,replacements){
 const lines=text.split('\n'),hunks=[];
 for(let i=0;i<lines.length;i++)for(const [oldValue,newValue]of replacements)if(lines[i].includes(oldValue)){
  const start=Math.max(0,i-2),end=Math.min(lines.length,i+3);
  hunks.push(`@@ -${start+1},${end-start} +${start+1},${end-start} @@\n`+lines.slice(start,i).map(l=>' '+l).join('\n')+'\n-'+lines[i]+'\n+'+lines[i].replace(oldValue,newValue)+'\n'+lines.slice(i+1,end).map(l=>' '+l).join('\n')+'\n');
 }
 return '--- a/native/main.cpp\n+++ b/native/main.cpp\n'+hunks.join('');
}
export function prepareNativeDiagnosticProposal({repoRoot=defaultRepo,outDir}={}){
 const file=path.join(repoRoot,'native/main.cpp'),bytes=fs.readFileSync(file),text=bytes.toString('utf8');
 const oldExpression='{"actionId", rule.ownAction.empty() ? rule.inputAction : rule.ownAction}',newExpression='{"actionId", !rule.uiAction.empty() ? rule.uiAction : rule.ownAction.empty() ? rule.inputAction : rule.ownAction}';
 const oldDigest='rule.trigger + rule.ownAction + rule.inputAction + rule.preset',newDigest='rule.trigger + rule.uiAction + rule.ownAction + rule.inputAction + rule.preset';
 const locations=text.split('\n').flatMap((line,i)=>line.includes(oldExpression)?[{line:i+1,old:line,new:line.replace(oldExpression,newExpression)}]:[]);
 if(locations.length!==4)throw Error('EXPECTED_FOUR_UNFIXED_DIAGNOSTIC_SITES:'+locations.length);
 if(text.split(oldDigest).length!==2)throw Error('OEM_DIGEST_ANCHOR_CHANGED');
 fs.mkdirSync(outDir,{recursive:true});
 fs.writeFileSync(path.join(outDir,'native-main-four-identities.patch'),linePatch(text,[[oldExpression,newExpression]]));
 fs.writeFileSync(path.join(outDir,'native-main-ui-digest.patch'),linePatch(text,[[oldDigest,newDigest]]));
 fs.writeFileSync(path.join(outDir,'native-main-combined-proposal.patch'),linePatch(text,[[oldExpression,newExpression],[oldDigest,newDigest]]));
 const result={schemaVersion:1,status:'PROPOSAL_NOT_APPLIED',source:'native/main.cpp',sourceSha256:hash(bytes),locations,
  extraDigest:{old:oldDigest,new:newDigest,reason:'Changing only uiAction must invalidate the actual summary log digest.'},
  boundary:'Diagnostic identity and summary invalidation only; action acceptance is not suppression completion.'};
 writeJson(path.join(outDir,'native-main-proposal.json'),result);return result;
}
async function main(){
 const args=process.argv.slice(2),options={};let output,proposal;
 for(let i=0;i<args.length;i++){const key=args[i],value=args[++i];if(key==='--work-dir')options.workDir=value;else if(key==='--repo')options.repoRoot=value;else if(key==='--output')output=value;else if(key==='--proposal-dir')proposal=value;else throw Error('UNKNOWN_FOLLOWUP_ARGUMENT:'+key);}
 if(proposal){const result=prepareNativeDiagnosticProposal({...options,outDir:path.resolve(proposal)});console.log(JSON.stringify({status:result.status,sourceSha256:result.sourceSha256,locations:result.locations.map(l=>l.line)}));}
 const result=await runInputFollowupRegressions(options);if(output)writeJson(path.resolve(output),result);
 console.log(JSON.stringify({status:result.status,summary:result.summary,output:output??null}));if(result.status!=='PASS')process.exitCode=1;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))await main();







