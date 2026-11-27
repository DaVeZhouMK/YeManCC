import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {register} from 'node:module';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {selector} from './win5_quarantine_projection.mjs';

// Explicit Win5 entry only. Keep the producer and C++ inputs shared with
// Terminal29 intact; the existing producer owns compilation and pipe execution.
const here=path.dirname(fileURLToPath(import.meta.url));
const repo=path.resolve(here,'../../..');
const producer=pathToFileURL(path.join(repo,'tools/input_coordinator_lifecycle_selftest.mjs')).href;
const helper=pathToFileURL(path.join(here,'win5_quarantine_projection.mjs')).href;
const scenario=path.join(here,'win5_elite_quarantine_cases.inc');
const digest=p=>createHash('sha256').update(readFileSync(p)).digest('hex');
const args=process.argv.slice(2);
assert.deepEqual(args.filter((_,i)=>i%2===0),['--out','--suite','--case','--managed-dll','--dotnet'],'WIN5_EXACT_MANAGED_SCOPE_REQUIRED');
assert.equal(args.length,10,'WIN5_EXACT_MANAGED_SCOPE_REQUIRED');
assert.equal(args[3],'lifecycle-directed');
assert.equal(args[5],selector,'WIN5_EXACT_SELECTOR_REQUIRED');
for(const file of [args[1],args[7],args[9]])assert(path.isAbsolute(file)&&!/[\0\r\n]/.test(file),'WIN5_ABSOLUTE_PATH_REQUIRED');
assert.equal(path.extname(args[7]).toLowerCase(),'.dll','WIN5_MANAGED_DLL_REQUIRED');
assert.equal(path.basename(args[9]).toLowerCase(),'dotnet.exe','WIN5_DOTNET_REQUIRED');
assert(!process.env.NODE_OPTIONS&&!process.env.NODE_PATH&&process.execArgv.length===0,'WIN5_AMBIENT_NODE_INJECTION_REFUSED');
const deadline=Number(process.env.YMCC_SIM_CASE_DEADLINE_EPOCH_MS);
assert(Number.isSafeInteger(deadline)&&deadline>Date.now(),'WIN5_INHERITED_FUTURE_DEADLINE_REQUIRED');
const inputs=[fileURLToPath(producer),fileURLToPath(helper),scenario];
const [producerSha,helperSha,scenarioSha]=inputs.map(digest);

register('data:text/javascript,'+encodeURIComponent(String.raw`
import {createHash} from 'node:crypto';
let data;
const sha=b=>createHash('sha256').update(b).digest('hex');
export function initialize(options){data=options;}
export async function load(url,context,next){
 const loaded=await next(url,context);
 if(url!==data.producer)return loaded;
 if(sha(loaded.source)!==data.producerSha)throw Error('WIN5_QUARANTINE_PRODUCER_CHANGED');
 const original=Buffer.from(loaded.source).toString('utf8');
 let text=original;
 const edits=[];
 const patch=(before,after)=>{
  if(text.split(before).length!==2)throw Error('WIN5_QUARANTINE_PRODUCER_ANCHOR:'+before);
  text=text.replace(before,()=>after);edits.push([before,after]);
 };
 patch("const gpdExpiredFirstOnFullCycleSelected=selectedCase==='gpd-expired-first-on-full-cycle'||gpdSettledFirstOnFullCycleSelected;",[
  'import {selector as gpdEliteQuarantineSelector,win5QuarantineScope,projectWin5QuarantineFixture,win5QuarantineSelectionReceipt,bindWin5BusDiscovery} from '+JSON.stringify(data.helper)+';',
  'const gpdEliteQuarantineSelected=selectedCase===gpdEliteQuarantineSelector;',
  'const gpdEliteQuarantineSource='+JSON.stringify(data.scenario)+';',
  'const gpdEliteQuarantineText=fs.readFileSync(gpdEliteQuarantineSource,"utf8");',
  'const gpdExpiredFirstOnFullCycleSelected=selectedCase==="gpd-expired-first-on-full-cycle"||gpdSettledFirstOnFullCycleSelected||gpdEliteQuarantineSelected;',
 ].join('\n'));
 patch('function readGpdExpiredFirstOnFullCycleSelection(source){','function readGpdExpiredFirstOnFullCycleSelection(source){\n if(gpdEliteQuarantineSelected)return win5QuarantineScope(selectedCase,gpdEliteQuarantineText);');
 patch('if(gpdSettledFirstOnFullCycleSelected){\n const {extractGpdFirstOnBusAdmission}',
       'if(gpdSettledFirstOnFullCycleSelected||gpdEliteQuarantineSelected){\n const {extractGpdFirstOnBusAdmission}');
 patch('if(prepareRejectionSelected){\n const capture=extract(source,\'inputCaptureOnGamepad\').code;',[
  'if(gpdEliteQuarantineSelected){',
  ' const capture=extract(source,"inputCaptureOnGamepad").code;',
  ' const begin="    // Explicit user intent: a fresh virtual-target ON edge lifts the backend",end="    preambleFrameSetupCost.finish();";',
  ' if(capture.split(begin).length!==2||capture.split(end).length!==2||capture.indexOf(begin)>=capture.indexOf(end))throw Error("WIN5_ON_EDGE_SOURCE_CHANGED");',
  ' const edge=capture.slice(capture.indexOf(begin),capture.indexOf(end));',
  ' if(!edge.includes("lastVirtualTargetWanted = wanted;")||inert.split(edge).length!==2)throw Error("WIN5_ON_EDGE_NOT_EXACT");',
  ' inert+="\\nstatic void fixtureWin5OriginalOnEdge(){\\n"+edge+"}\\n";',
  ' substitutions.push({symbol:"inputCaptureOnGamepad/Win5 explicit ON edge",sha256:sha(edge),copiedUnchanged:true});',
  '}',
  'if(prepareRejectionSelected){',
  ' const capture=extract(source,\'inputCaptureOnGamepad\').code;',
 ].join('\n'));
 patch('...(gpdSettledFirstOnFullCycleSelected?[\'tools/gpd-idle-wake.mjs\']:[])',
       '...((gpdSettledFirstOnFullCycleSelected||gpdEliteQuarantineSelected)?[\'tools/gpd-idle-wake.mjs\']:[])');
 patch('const mutatedHeaderFiles=[],mutatedHeaderInputs=[];',[
  'if(gpdEliteQuarantineSelected){',
  ' const file=path.join(out,"fixture.cpp"), input=fs.readFileSync(file,"utf8");',
  ' const projected=projectWin5QuarantineFixture(input,gpdEliteQuarantineText,selectedCase,replaceUniqueSourceText);',
  ' if(projected.receipt.scenarioSha256!=='+JSON.stringify(data.scenarioSha)+'||sha(fs.readFileSync(fileURLToPath('+JSON.stringify(data.helper)+'))) !== '+JSON.stringify(data.helperSha)+')throw Error("WIN5_QUARANTINE_INPUT_CHANGED");',
  ' substitutions.push({symbol:"win5-elite-quarantine-directed-fixture",...projected.receipt,adapterSource:'+JSON.stringify(data.helper)+',adapterSourceSha256:'+JSON.stringify(data.helperSha)+',producerBeforeSha256:'+JSON.stringify(sha(original))+',scenario:gpdEliteQuarantineSource});',
  ' fs.writeFileSync(file,projected.text);',
  ' const busDiscovery=bindWin5BusDiscovery(fs.readFileSync(discoveryFile,"utf8"));',
  ' if(busDiscovery.receipt){',
  '  fs.writeFileSync(discoveryFile,busDiscovery.text);',
  '  substitutions.push({symbol:"gamepadReadStateUnlocked/Win5 BUS discovery parameter",...busDiscovery.receipt});',
  ' }',
  '}',
  'const mutatedHeaderFiles=[],mutatedHeaderInputs=[];',
 ].join('\n'));
 patch('}else if(gpdExpiredFirstOnFullCycleSelected){',[
  '}else if(gpdEliteQuarantineSelected){',
  ' selectedCaseScope=win5QuarantineSelectionReceipt(results,gpdExpiredFirstOnFullCycleSelection);',
  ' fs.writeFileSync(path.join(out,"win5-selection-observed.json"),JSON.stringify(selectedCaseScope,null,2),{flag:"wx"});',
  ' if(!selectedCaseScope.exact)throw Error("WIN5_QUARANTINE_EXACT_SCOPE_OR_BEHAVIOR_MISMATCH");',
  '}else if(gpdExpiredFirstOnFullCycleSelected){',
 ].join('\n'));
 let reversed=text;
 for(const [before,after] of edits.reverse()){
  if(reversed.split(after).length!==2)throw Error('WIN5_QUARANTINE_PRODUCER_INVERSE');
  reversed=reversed.replace(after,()=>before);
 }
 if(reversed!==original)throw Error('WIN5_QUARANTINE_PRODUCER_INVERSE_BYTES');
 return {...loaded,source:text};
}
`),import.meta.url,{data:{producer,helper,scenario,producerSha,scenarioSha,helperSha}});

process.argv[1]=inputs[0];
await import(producer);
assert.deepEqual(inputs.map(digest),[producerSha,helperSha,scenarioSha],'WIN5_QUARANTINE_INPUT_CHANGED');
