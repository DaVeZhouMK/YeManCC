// Read-only cache contract for one original native selection; no producer calls.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {snapshotPreHostInputs,verifyPreHostInputs} from './prehost-input-provenance.mjs';
import {collectPreHostOutputProvenance} from './prehost-output-provenance.mjs';

export const PREHOST_ID='controller.foreground-shield-field';
export const PREHOST_CONTRACT_SOURCE='tools/ymcc_sim/global/controller-foreground-latency/prehost-cache-contract.mjs';
const SOURCES=[PREHOST_CONTRACT_SOURCE,...['prehost-input-provenance.mjs','prehost-output-provenance.mjs','prehost-toolchain.mjs',
    'prehost-toolchain-inputs.json','prehost-header-inputs.json'].map(name=>'tools/ymcc_sim/global/controller-foreground-latency/'+name)];
const KIND='ymcc-prehost-native-reference-v1';
const key=file=>path.resolve(file).toLowerCase();
const inside=(root,file)=>{const rel=path.relative(root,file);return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));};
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const need=(condition,message)=>{if(!condition)throw Error('PREHOST_CACHE_'+message);};
function read(file){
    const full=path.resolve(file),stat=fs.lstatSync(full);
    need(stat.isFile()&&!stat.isSymbolicLink()&&key(fs.realpathSync(full))===key(full),'REDIRECTED_OR_INVALID_FILE:'+full);
    need(stat.size<=16*1024*1024,'RECEIPT_SIZE_LIMIT');
    return JSON.parse(fs.readFileSync(full,'utf8').replace(/^\uFEFF/,''));
}
export function isPreHostCacheAdapter(adapter){
    return adapter?.id===PREHOST_ID&&Boolean(adapter.args?.some(flag=>flag==='--prehost-writer'||flag==='--win5-return-exit2'));
}
const preHostSelection=adapter=>adapter.args?.includes('--win5-return-exit2')?'win5-return-exit2':'prehost-writer';
// The common engine intentionally excludes id/title from execution identity.
// Restore only this exact entry's ID for its mandatory reference consumer.
export function preHostManifestAdapter(adapter){
    if(adapter?.path!=='tools/shield_foreground_field_selftest.mjs'||!isPreHostCacheAdapter({...adapter,id:PREHOST_ID}))return null;
    need(adapter.id===undefined||adapter.id===PREHOST_ID,'MANIFEST_ID_CHANGED');
    return {...adapter,id:PREHOST_ID};
}
function descriptor(adapter){
    need(isPreHostCacheAdapter(adapter)&&adapter.kind==='node'&&adapter.path==='tools/shield_foreground_field_selftest.mjs'&&
        adapter.scope==='behavioral-mock'&&JSON.stringify(adapter.args)===JSON.stringify(['--output','{outDir}/foreground-field','--'+preHostSelection(adapter)])&&
        JSON.stringify(adapter.resultFiles)===JSON.stringify(['foreground-field/result.json'])&&adapter.cacheOutputManifest===undefined,'DESCRIPTOR_SCOPE_CHANGED');
    for(const file of SOURCES.slice(1))need(adapter.evidenceInputs?.includes(file),'DESCRIPTOR_INPUT_MISSING:'+file);
}

export function preHostDescriptorContract(adapter,repoRoot){
    if(!isPreHostCacheAdapter(adapter))return null;
    descriptor(adapter);
    need(!process.env.NODE_OPTIONS&&!process.env.NODE_PATH,'INHERITED_NODE_PRELOAD_UNSUPPORTED');
    const snapshot=snapshotPreHostInputs(repoRoot),config=read(path.join(repoRoot,SOURCES.find(file=>file.endsWith('prehost-toolchain-inputs.json'))));
    need(key(process.execPath)===key(config.node),'NODE_ENGINE_CHANGED');
    const contract={schemaVersion:1,kind:KIND,selection:preHostSelection(adapter),buildProfile:'Unoptimized /Od /MT /DNDEBUG',
        inputFingerprint:snapshot.fingerprint,inputCount:snapshot.inputCount,inputBytes:snapshot.pinnedBytes,
        membership:snapshot.membership,headerDeclaration:snapshot.headerDeclaration,
        childEnvironment:'constructed-from-empty;fixed-tools;private-owned-output-paths',
        platform:{release:os.release(),version:os.version(),architecture:process.arch},
        outputPathClass:'owned-core-Evidence/Build/S30-xxxxxx/Unoptimized',
        nodePreload:'none',productMainExecuted:false,hardwareAcceptance:false};
    return {contract,tools:snapshot.inputPins.map(({path,sha256})=>({path,sha256})),sourceInputs:SOURCES,
        environmentIdentity:sha(JSON.stringify({selection:contract.selection,childEnvironment:contract.childEnvironment,nodePreload:contract.nodePreload,platform:contract.platform}))};
}

function context(repoRoot,caseDir,selection){
    const caseRoot=path.resolve(caseDir),run=path.resolve(caseRoot,'../..'),core=path.resolve(repoRoot,'../../../Testing/YMCC');
    need(inside(path.join(core,'Evidence'),run)&&key(caseRoot)===key(path.join(run,'cases',PREHOST_ID)),'CASE_NOT_ORIGINAL_CORE_OUTPUT');
    const output=path.join(caseRoot,'foreground-field'),file=path.join(output,'result.json'),producer=read(file);
    need(producer.passed===true&&producer.status==='PASS'&&producer.selection===selection&&producer.productMainExecuted===false&&
        producer.hardwareAccess===false&&producer.realSteamOrSleep===false,'PRODUCER_SCOPE_OR_STATUS');
    const workspace=path.resolve(producer.workspace),build=path.join(workspace,'Unoptimized');
    need(key(path.dirname(workspace))===key(path.join(core,'Evidence/Build'))&&/^S30-[a-z0-9]{6}$/i.test(path.basename(workspace)),'WORKSPACE_OWNERSHIP');
    const expected={before:path.join(output,'prehost-inputs.before.json'),after:path.join(output,'prehost-inputs.after.json'),outputs:path.join(output,'prehost-output-provenance.json')};
    for(const [name,pin]of [['before',producer.inputProvenance?.before],['after',producer.inputProvenance?.after],['outputs',producer.outputProvenance]]){
        need(typeof pin?.path==='string'&&key(pin.path)===key(expected[name])&&/^[a-f0-9]{64}$/i.test(pin.sha256??''),'SIDECAR_MAPPING:'+name);
        need(sha(fs.readFileSync(expected[name]))===pin.sha256.toLowerCase(),'SIDECAR_CHANGED:'+name);
    }
    return {caseRoot,run,output,file,producer,workspace,build,expected};
}
export function preHostDependencyPaths(result,adapter){
    if(!isPreHostCacheAdapter(adapter))return [];
    const file=path.join(result.evidenceDir,'foreground-field/result.json');
    if(!fs.existsSync(file))return [];
    const rows=read(file).sourceHashes;
    if(rows===undefined)return [];
    need(rows&&typeof rows==='object'&&!Array.isArray(rows),'SOURCE_GRAPH_TYPE');
    const files=Object.keys(rows);need(files.length>0&&files.length<=10000,'SOURCE_GRAPH_COUNT');
    for(const file of files)need(!path.isAbsolute(file)&&!file.split(/[\\/]/).includes('..')&&/^[a-f0-9]{64}$/i.test(rows[file]),'SOURCE_GRAPH_ROW');
    return files;
}
function parentFiles(run){return ['result.json','execution-budget.json','owned-job.json','budget-context.json','budget-arguments.json','budget.stdout.log','budget.stderr.log'].map(file=>path.join(run,file));}

/** List original nested outputs for the existing cache index; no second index. */
export function preHostEvidenceFiles(result,adapter,{includeParent=true,repoRoot=path.resolve(import.meta.dirname,'../../../..')}={}){
    if(!isPreHostCacheAdapter(adapter)||result.status!=='PASS')return [];
    const c=context(repoRoot,result.evidenceDir,preHostSelection(adapter));
    const outputs=read(c.expected.outputs);
    need(Array.isArray(outputs.outputPins)&&outputs.outputPins.length>0&&outputs.outputPins.length<=10000,'OUTPUT_GRAPH_COUNT');
    for(const row of outputs.outputPins)need(typeof row.path==='string'&&inside(c.build,row.path)&&/^[a-f0-9]{64}$/.test(row.sha256??''),'OUTPUT_GRAPH_PATH');
    const processFiles=['Unoptimized-build.log','Unoptimized-build-process.json'].map(file=>path.join(c.output,file));
    return [c.file,...Object.values(c.expected),...processFiles,...outputs.outputPins.map(row=>row.path),...(includeParent?parentFiles(c.run):[])];
}

/** This is additional mandatory evidence for this exact descriptor, not a PASS. */
export function verifyPreHostCacheReference({manifest,raw,caseDir,requirePin}){
    const adapter=preHostManifestAdapter(manifest.adapter);
    if(!adapter)return null;
    descriptor(adapter);
    need(raw.id===PREHOST_ID,'RESULT_ID_CHANGED');
    const selection=preHostSelection(adapter);
    need(manifest.options.preHostFixedContract?.kind===KIND&&manifest.options.preHostFixedContract.selection===selection,'FIXED_CONTRACT_MISSING');
    need(raw.status==='PASS','SUCCESS_INPUT_GRAPH_REQUIRED');
    const c=context(manifest.repoRoot,caseDir,selection),producer=c.producer;
    for(const file of preHostEvidenceFiles(raw,adapter,{repoRoot:manifest.repoRoot}))requirePin(file);
    const declared=new Map([...manifest.inputs,...manifest.tools].map(row=>[key(row.path),row]));
    const sourceHashes=producer.sourceHashes;
    need(sourceHashes&&typeof sourceHashes==='object'&&!Array.isArray(sourceHashes)&&Object.keys(sourceHashes).length>0,'SOURCE_GRAPH_MISSING');
    for(const [relative,hash]of Object.entries(sourceHashes)){
        need(!path.isAbsolute(relative)&&!relative.split(/[\\/]/).includes('..')&&declared.get(key(path.join(manifest.repoRoot,relative)))?.sha256===hash,'COMPILED_SOURCE_UNDECLARED_OR_CHANGED:'+relative);
    }
    const before=read(c.expected.before),after=read(c.expected.after);
    need(before.fingerprint===manifest.options.preHostFixedContract.inputFingerprint&&after.fingerprint===before.fingerprint,'INPUT_GRAPH_CHANGED');
    for(const row of before.inputPins)need(declared.get(key(row.path))?.sha256===row.sha256,'TOOL_OR_HEADER_NOT_IN_IDENTITY:'+row.path);
    const inputBinding=verifyPreHostInputs(manifest.repoRoot,path.join(c.build,'source-snapshot'),before,after,producer.actualCompilerInputs,read(path.join(c.build,'link-observed-libraries.json')));
    const snapshot=path.join(c.build,'source-snapshot');
    for(const row of producer.actualCompilerInputs){
        const relative=inside(snapshot,row.path)?path.relative(snapshot,row.path):inside(manifest.repoRoot,row.path)?path.relative(manifest.repoRoot,row.path):null;
        if(relative!==null)need(sourceHashes[relative.replaceAll('\\','/')]===row.sha256,'ACTUAL_COMPILER_SOURCE_BINDING_CHANGED:'+relative);
    }
    const header=fs.readFileSync(path.join(snapshot,'tools/msi_native_foreground_regression.h'),'utf8'),start=header.indexOf('    if(preHostWriterCase){');
    need(start>=0,'ORIGINAL_SELECTION_ANCHOR');
    const allowed=header.slice(start).match(/allowed=\{([\s\S]*?)\};/);need(allowed,'ORIGINAL_SELECTION_SET');
    const selections=selection==='win5-return-exit2'?[selection]:[...allowed[1].matchAll(/"([^"]+)"/g)].map(row=>row[1]);
    const batch=read(path.join(c.build,'prehost-writer/result.json'));
    for(const row of batch.cases??[])need(row.passed&&row.exitCode===0&&row.job?.atomicJobAdmission&&row.job.zeroObservedBeforeDispose&&
        row.job.activeProcessesFinal===0&&row.job.disposed&&!row.job.terminateRequested,'SCENE_JOB_OR_STATUS');
    const output=collectPreHostOutputProvenance(path.dirname(c.workspace),c.workspace,c.build,read(path.join(c.build,'compiled-snapshot-hashes.json')),batch,selections,
        {requireBuildDetails:true,directedWin5:selection==='win5-return-exit2',expectedProvenance:read(c.expected.outputs)});
    for(const row of output.outputPins)requirePin(row.path,row.sha256);
    const process=read(path.join(c.output,'Unoptimized-build-process.json'));
    need(process.exitCode===0&&process.signal===null&&process.spawnError===null&&process.environmentMode==='fixed-prehost-child','BUILD_PROCESS');
    const parent=read(path.join(c.run,'result.json')),budget=read(path.join(c.run,'execution-budget.json')),job=read(path.join(c.run,'owned-job.json'));
    need(['PASS_SELECTED_SCOPE','COMPLETE_WITH_CACHED_REFERENCES'].includes(parent.status)&&budget.status===parent.status&&budget.exitCode===0&&
        budget.ownedJobVerified===true&&budget.resourceZeroVerified===true&&!budget.timedOut&&budget.sourceDrift?.length===0&&budget.sourceReadErrors?.length===0,'PARENT_EXECUTION_NOT_CLOSED');
    assert.deepEqual(budget.ownedJob,job,'PREHOST_CACHE_PARENT_JOB_RECEIPT_CHANGED');
    need(job.jobAssigned===true&&job.childExitedBeforeReturn===true&&job.zeroObservedBeforeDispose===true&&job.activeProcessesFinal===0&&job.cleanupError===null,'PARENT_JOB_NOT_ZERO');
    const parentRow=parent.rows?.filter(row=>row.id===PREHOST_ID);need(parentRow?.length===1&&parentRow[0].status==='PASS'&&typeof parentRow[0].reference?.path==='string'&&key(parentRow[0].reference.path)===key(path.join(c.caseRoot,'result.json')),'PARENT_RESULT_NOT_BOUND');
    requirePin(parentRow[0].reference.path,parentRow[0].reference.sha256);
    return {status:'CACHED_REFERENCE_EVIDENCE_ONLY',selectedCases:selections.length,assertions:batch.cases.reduce((n,row)=>n+row.assertions.length,0),
        inputBinding,outputs:output.outputPins.length,executedThisRun:false,actualPassedThisRun:false,
        outerCleanup:{activeBefore:job.activeProcessesBeforeCleanup,forced:job.terminationRequested===true,zeroBeforeDispose:true,naturalExitClaimed:job.terminationRequested!==true},
        hardwareAccepted:false};
}
