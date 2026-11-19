// Read-only reference consumer for the two original managed lifecycle fixtures.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import {getControllerHostLifecycleAdapters} from './host-adapter.mjs';
import {toolchainFiles} from '../inputhost-protocol/adapter.mjs';
import {managedFixtureEnvironment,managedFixtureResolverInputs as resolverInputs,managedFixtureLookupGuards as captureHostProtocolLookupGuards} from '../controller-signal-evidence/environment.mjs';
export {captureHostProtocolLookupGuards};
import {validateHostSourceBindings,validateHostManagedEnvironmentReceipt,
    validateHostCompilerInputReceipt,validateHostLifecycleReceipt} from './host-run.mjs';

export const HOST_PROTOCOL_ID='controller.host-lifecycle-protocol';
export const HOST_PARENT_ID='controller.host-parent-lifetime';
export const HOST_PROTOCOL_CONTRACT_SOURCE='tools/ymcc_sim/global/controller-lifecycle-completion/host-cache-contract.mjs';
const KIND='ymcc-original-host-protocol-reference-v1';
const key=file=>path.resolve(file).toLowerCase();
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const need=(value,message)=>{if(!value)throw Error('HOST_PROTOCOL_CACHE_'+message);};
const inside=(root,file)=>{const rel=path.relative(root,file);return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));};
const canonical=value=>Array.isArray(value)?value.map(canonical):value&&typeof value==='object'?Object.fromEntries(Object.keys(value).sort().map(name=>[name,canonical(value[name])])):value;
const same=(a,b)=>JSON.stringify(canonical(a))===JSON.stringify(canonical(b));
function read(file){
    const stat=fs.lstatSync(file);
    need(stat.isFile()&&!stat.isSymbolicLink()&&key(fs.realpathSync(file))===key(file),'REDIRECTED_RECEIPT');
    need(stat.size<=16*1024*1024,'RECEIPT_SIZE_LIMIT');
    return JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
}
function specification(repoRoot){return read(path.join(repoRoot,'tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json'));}
export function isHostProtocolCacheAdapter(adapter){
    return adapter?.path==='tools/ymcc_sim/global/controller-lifecycle-completion/host-run.mjs'&&
        same(adapter.args,['--case',HOST_PROTOCOL_ID,'--evidence-dir','{outDir}/host-lifecycle']);
}
function selection(adapter){
    const id=adapter?.args?.[1];
    if(adapter?.path!=='tools/ymcc_sim/global/controller-lifecycle-completion/host-run.mjs'||
        ![HOST_PROTOCOL_ID,HOST_PARENT_ID].includes(id)||
        !same(adapter.args,['--case',id,'--evidence-dir','{outDir}/host-lifecycle']))return null;
    const parent=id===HOST_PARENT_ID;
    return {id,parent,mode:parent?'parent':'protocol',
        kind:parent?'ymcc-original-host-parent-reference-v1':KIND,
        negativeGroup:parent?'negative-cancellation-omitted':'negative-dispose-retention-omitted'};
}
export function isHostLifecycleCacheAdapter(adapter){return selection(adapter)!==null;}
function descriptor(adapter){
    const selected=selection(adapter);
    need(selected&&adapter.kind==='node'&&adapter.scope==='behavioral-mock'&&
        (adapter.id===undefined||adapter.id===selected.id)&&adapter.timeoutMs===240000&&
        same(adapter.resultFiles,['host-lifecycle/result.json'])&&adapter.cacheOutputManifest===undefined,'DESCRIPTOR_CHANGED');
    return selected;
}
export function hostProtocolCacheAdapter(){
    return getControllerHostLifecycleAdapters().find(adapter=>adapter.id===HOST_PROTOCOL_ID);
}
export function hostLifecycleCacheAdapter(id){
    need([HOST_PROTOCOL_ID,HOST_PARENT_ID].includes(id),'UNKNOWN_LIFECYCLE_ID');
    return getControllerHostLifecycleAdapters().find(adapter=>adapter.id===id);
}
export function hostProtocolDescriptorContract(adapter,repoRoot){
    return isHostProtocolCacheAdapter(adapter)?hostLifecycleDescriptorContract(adapter,repoRoot):null;
}
export function hostLifecycleDescriptorContract(adapter,repoRoot){
    if(!isHostLifecycleCacheAdapter(adapter))return null;
    const selected=descriptor(adapter);
    need(!process.env.NODE_OPTIONS&&!process.env.NODE_PATH&&process.execArgv.length===0,'NODE_PRELOAD_OR_FLAGS_UNSUPPORTED');
    const spec=specification(repoRoot),tools=toolchainFiles(repoRoot),declared=new Map(spec.pinned.map(row=>[key(row.path),row]));
    need(declared.size===spec.pinned.length&&tools.length===declared.size&&
        tools.every(file=>declared.has(key(file))),'TOOL_MEMBERSHIP_CHANGED');
    need(same([...adapter.artifactInputs].map(key).sort(),tools.map(key).sort()),'DESCRIPTOR_TOOLS_CHANGED');
    const out=path.resolve(repoRoot,'../../../Testing/YMCC/Workspace/host-protocol-reference-identity');
    const env=managedFixtureEnvironment({ambient:process.env,dotnetRoot:spec.dotnetRoot,out});
    const variables=Object.fromEntries(Object.entries(env).sort(([a],[b])=>a.localeCompare(b)).map(([name,value])=>[name,value.replaceAll(out,'{fixtureRoot}')]));
    const versions=relative=>fs.readdirSync(path.join(spec.dotnetRoot,relative),{withFileTypes:true}).map(entry=>{
        need(!entry.isSymbolicLink(),'REDIRECTED_RESOLVER_MEMBER');return entry.isDirectory()?entry.name:null;
    }).filter(Boolean).sort();
    const resolverMembership={sdk:versions('sdk'),framework:versions('shared/Microsoft.NETCore.App'),fxr:versions('host/fxr')};
    need(same(resolverMembership.sdk,[spec.sdkVersion])&&same(resolverMembership.framework,[spec.runtimeVersion])&&
        same(resolverMembership.fxr,[spec.runtimeVersion]),'RESOLVER_SELECTION_NOT_SINGLE_FIXED_VERSION');
    const platform={platform:process.platform,arch:process.arch,release:os.release(),version:os.version()};
    const contract={schemaVersion:1,kind:selected.kind,canonicalId:selected.id,variables,platform,resolverMembership,
        sdk:spec.sdkVersion,runtime:spec.runtimeVersion,targetingPack:spec.targetingPackVersion,
        softwareScope:selected.parent?'original Release/Debug parent10 and cancellation negative; real BCL pipes; inert SDK; no product Main or hardware':'original Release/Debug lifecycle19 and Dispose negative; inert SDK; no product Main or hardware',
        dynamicOsLoaderTraceClaimed:false};
    const resolver=resolverInputs(spec);
    contract.resolverDirectoryNames=resolver.names;
    contract.resolverManifests=resolver.manifests;
    contract.automaticResponsePolicy='original finite lookup paths must be absent before/after actual producer and at reference';
    return {contract,tools:[...spec.pinned,...resolver.manifests],sourceInputs:[HOST_PROTOCOL_CONTRACT_SOURCE],
        environmentIdentity:sha(JSON.stringify({variables,platform,resolverMembership}))};
}

function context(result,adapter,repoRoot){
    const selected=descriptor(adapter);
    const caseDir=path.resolve(result.evidenceDir),run=path.resolve(caseDir,'../..');
    const evidenceRoot=path.resolve(repoRoot,'../../../Testing/YMCC/Evidence');
    need(inside(evidenceRoot,run)&&key(caseDir)===key(path.join(run,'cases',selected.id)),'CASE_OUTSIDE_ORIGINAL_CORE_LAYOUT');
    const out=path.join(caseDir,'host-lifecycle'),moduleFile=path.join(out,'result.json'),module=read(moduleFile);
    need(module.id===selected.id&&module.status==='PASS'&&module.passed===true,'MODULE_NOT_SUCCESS');
    need(module.boundary?.productMainExecuted===false&&module.boundary.sdkLoaded===false&&module.boundary.hardwareIo===false&&
        module.boundary.physicalReturnValidated===false&&module.boundary.fullLifecycleAcceptance===false,'SOFTWARE_SCOPE_CHANGED');
    need(Array.isArray(module.sourceHashes)&&module.sourceHashes.length>0&&module.sourceHashes.length<=10000,'NESTED_OUTPUT_GRAPH_INVALID');
    return {caseDir,run,out,moduleFile,module,selected};
}
const guardFiles=dir=>['execution-budget.json','owned-job.json','budget-context.json','budget-arguments.json','budget.stdout.log','budget.stderr.log'].map(name=>path.join(dir,name));
export function hostProtocolEvidenceFiles(result,adapter,{repoRoot,proof,includeParent=true}={}){
    return isHostProtocolCacheAdapter(adapter)?hostLifecycleEvidenceFiles(result,adapter,{repoRoot,proof,includeParent}):[];
}
export function hostLifecycleEvidenceFiles(result,adapter,{repoRoot,proof,includeParent=true}={}){
    if(!isHostLifecycleCacheAdapter(adapter)||result.status!=='PASS')return [];
    const c=context(result,adapter,repoRoot),files=[c.moduleFile];
    for(const row of c.module.sourceHashes){
        need(typeof row?.path==='string'&&/^[a-f0-9]{64}$/.test(row.sha256??''),'NESTED_OUTPUT_ROW_INVALID');
        const file=path.resolve(repoRoot,row.path);if(inside(c.caseDir,file))files.push(file);
    }
    if(includeParent){
        const parent=proof??{kind:'optimized',result:path.join(c.run,'result.json'),guards:[c.run]};
        need(['global','optimized'].includes(parent.kind)&&typeof parent.result==='string'&&Array.isArray(parent.guards),'PARENT_PROOF_REQUIRED');
        files.push(parent.result,...parent.guards.flatMap(guardFiles));
        if(parent.lookupGuards)files.push(parent.lookupGuards.before.path,parent.lookupGuards.after.path);
    }
    return [...new Set(files)];
}

// Promote the R64 actual-command inspection into the permanent read-only consumer.
function verifyCscArguments(receipt,out,module,repoRoot,requirePin){
    const owned=new Map(module.sourceHashes.map(row=>[key(path.resolve(repoRoot,row.path)),row.sha256]));
    const tools=new Map(module.toolchain.map(row=>[key(row.path),row.sha256]));
    const scalars=new Set(['/noconfig','/unsafe-','/checked-','/nowarn','/fullpaths','/nostdlib+','/errorreport','/warn','/define','/debug-','/debug+','/debug','/filealign','/optimize+','/optimize-','/target','/highentropyva+','/warnaserror-','/warnaserror+','/utf8output','/deterministic+','/langversion','/nullable','/features','/preferreduilang']);
    const inputFlags=new Map([['/reference','Reference'],['/analyzer','Analyzer'],['/analyzerconfig','AnalyzerConfig'],['/additionalfile','AdditionalFile'],['/win32manifest','Win32Manifest'],['/win32res','Win32Resource'],['/resource','Resource'],['/linkresource','LinkedResource'],['/sourcelink','SourceLink'],['/ruleset','RuleSet'],['/appconfig','AppConfig']]);
    const outputFlags=new Set(['/out','/refout','/doc','/pdb']);
    const unquote=value=>{if(value.startsWith('"')||value.endsWith('"')){need(value.startsWith('"')&&value.endsWith('"'),'CSC_UNPAIRED_QUOTES');return value.slice(1,-1);}return value;};
    const graph=read(receipt.compilerInputs.manifest.path),observations=[];
    for(const config of receipt.configs){
        const command=path.join(out,'compiler-commandline-'+config+'.txt'),properties=path.join(out,'compiler-properties-'+config+'.txt');
        requirePin(command);requirePin(properties);
        const args=fs.readFileSync(command,'utf8').replace(/^\uFEFF/,'').split(/\r?\n/).filter(Boolean),inputs=[];
        need(args.includes('/noconfig')&&args.includes('/nostdlib+'),'CSC_IMPLICIT_INPUTS_ENABLED');
        for(const arg of args){
            let kind,value;
            if(arg.startsWith('/')){
                const colon=arg.indexOf(':'),flag=colon<0?arg:arg.slice(0,colon);value=colon<0?'':arg.slice(colon+1);
                if(scalars.has(flag))continue;
                if(outputFlags.has(flag)){const file=path.resolve(out,unquote(value));need(inside(out,file)&&owned.has(key(file)),'CSC_OUTPUT_OUTSIDE_GRAPH');requirePin(file,owned.get(key(file)));continue;}
                kind=inputFlags.get(flag);need(kind,'CSC_UNKNOWN_ARGUMENT');
            }else{need(!arg.startsWith('@'),'CSC_UNDECLARED_RESPONSE_FILE');kind='Compile';value=arg;}
            const file=path.resolve(out,unquote(value)),expected=owned.get(key(file))??tools.get(key(file));
            need(expected&&sha(fs.readFileSync(file))===expected,'CSC_INPUT_UNDECLARED_OR_CHANGED');
            if(inside(out,file))requirePin(file,expected);
            inputs.push({kind,path:file,sha256:expected});
        }
        for(const kind of ['Compile','Reference','Analyzer']){
            const actual=inputs.filter(row=>row.kind===kind).map(row=>[key(row.path),row.sha256]).sort();
            const wanted=graph.rows.filter(row=>row.ledger==='evaluated-inputs-'+config+'.txt'&&row.kind===kind).map(row=>[key(row.path),row.sha256]).sort();
            need(same(actual,wanted),'CSC_ARGUMENT_LEDGER_MISMATCH');
        }
        const props={};for(const line of fs.readFileSync(properties,'utf8').replace(/^\uFEFF/,'').split(/\r?\n/)){
            if(!line.trim())continue;const [name,...parts]=line.split('\t');need(!Object.hasOwn(props,name),'CSC_DUPLICATE_PROPERTY');props[name]=parts.join('\t');
        }
        for(const name of ['CompilerResponseFile','Win32Manifest','Win32Resource','ApplicationIcon','SourceLink','AppConfigForCompiler','ResolvedCodeAnalysisRuleSet'])need(props[name]==='','CSC_OPTIONAL_FILE_SCOPE_CHANGED');
        observations.push({config,fileInputs:inputs.length});
    }
    return observations;
}

function verifyParent(c,raw,proof,requirePin){
    need(proof&&['global','optimized'].includes(proof.kind)&&key(proof.result)===key(path.join(c.run,'result.json'))&&
        Array.isArray(proof.guards)&&proof.guards.length===(proof.kind==='global'?2:1),'PARENT_PROOF_INVALID');
    requirePin(proof.result);const parent=read(proof.result);
    const current=captureHostProtocolLookupGuards(proof.repoRoot,c.caseDir,{negativeGroup:c.selected.negativeGroup});
    const verifyLookup=(lookup,startedAt,finishedAt)=>{
        need(lookup?.before&&lookup?.after,'ACTUAL_LOOKUP_GUARDS_MISSING');
        requirePin(lookup.before.path,lookup.before.sha256);requirePin(lookup.after.path,lookup.after.sha256);
        const before=read(lookup.before.path),after=read(lookup.after.path);
        need(before.stage==='before'&&after.stage==='after'&&Number.isFinite(startedAt)&&Number.isFinite(finishedAt)&&
            Date.parse(before.atUtc)<=startedAt&&Date.parse(after.atUtc)>=finishedAt,'LOOKUP_GUARD_TIME_BINDING');
        need(same(before.snapshot,after.snapshot)&&same(before.snapshot,current),'LOOKUP_GUARDS_CHANGED');
    };
    if(c.selected.parent&&!proof.lookupGuards){
        const guards=c.module.managedLookupGuards;
        need(Array.isArray(guards)&&guards.length===2&&new Set(guards.map(row=>row.name)).size===2,'PARENT_GROUP_LOOKUP_GUARDS_MISSING');
        for(const name of ['positive',c.selected.negativeGroup]){
            const guard=guards.find(row=>row.name===name),group=read(path.join(c.out,name,'result.json'));
            need(guard&&key(guard.before?.path)===key(path.join(c.out,name+'.lookup-before.json'))&&
                key(guard.after?.path)===key(path.join(c.out,name+'.lookup-after.json')),'PARENT_GROUP_LOOKUP_PATH_CHANGED');
            need(Array.isArray(group.commands)&&group.commands.length===4,'PARENT_COMMAND_SET_CHANGED');
            verifyLookup(guard,Math.min(...group.commands.map(row=>Date.parse(row.startedAtUtc))),Math.max(...group.commands.map(row=>Date.parse(row.finishedAtUtc))));
        }
    }else verifyLookup(proof.lookupGuards,Date.parse(raw.startedAt),Date.parse(raw.finishedAt));
    if(proof.kind==='global')need(Array.isArray(parent.results)&&parent.results.length===1&&same(parent.results[0],raw),'GLOBAL_PARENT_CASE_BINDING');
    else need(parent.rows?.some(row=>row.id===c.selected.id&&row.executedThisRun===true&&row.actualPassedThisRun===true&&
        row.reference?.sha256===sha(fs.readFileSync(path.join(c.caseDir,'result.json')))),'OPTIMIZED_PARENT_CASE_BINDING');
    const observations=[];
    for(const dir of proof.guards){
        for(const file of guardFiles(dir))requirePin(file);
        const receipt=read(path.join(dir,'execution-budget.json')),job=receipt.ownedJob,context=read(path.join(dir,'budget-context.json'));
        need(same(job,read(path.join(dir,'owned-job.json'))),'PARENT_JOB_RECEIPT_MISMATCH');
        need(receipt.exitCode===0&&!receipt.timedOut&&receipt.resourceZeroVerified===true&&job?.jobAssigned===true&&
            job.childExitedBeforeReturn===true&&job.zeroObservedBeforeDispose===true&&job.activeProcessesBeforeCleanup===0&&
            job.activeProcessesFinal===0&&job.terminationRequested===false,'PARENT_NOT_NATURALLY_RETURNED');
        need(key(receipt.runDir)===key(dir)&&key(context.runDir)===key(dir)&&context.deadlineEpochMs===job.deadlineEpochMs,'PARENT_BUDGET_BINDING');
        const started=Date.parse(raw.startedAt),finished=Date.parse(raw.finishedAt);
        need(Number.isFinite(started)&&Number.isFinite(finished)&&started>=context.deadlineEpochMs-context.budgetMs&&
            finished<=job.finishedEpochMs,'PARENT_TIME_BINDING');
        const stdout=fs.readFileSync(path.join(dir,'budget.stdout.log'),'utf8');
        if(proof.kind==='global'){
            need(stdout.split(/\r?\n/).includes('RESULT '+proof.result),'GLOBAL_PARENT_OUTPUT_BINDING');
            const argv=read(path.join(dir,'budget-arguments.json')),test=argv.indexOf('-Tests'),evidence=argv.indexOf('-EvidenceDir');
            need(test>=0&&argv[test+1]===c.selected.id&&evidence>=0&&key(argv[evidence+1])===key(path.dirname(c.run)),'GLOBAL_PARENT_SELECTION_BINDING');
        }else need(key(dir)===key(c.run),'OPTIMIZED_PARENT_LAYOUT');
        observations.push({runDir:dir,rootPid:job.childPid,zeroBeforeDispose:true,forced:false});
    }
    return observations;
}

export function verifyHostProtocolCacheReference({manifest,raw,caseDir,requirePin,cacheEntry}){
    return isHostProtocolCacheAdapter(manifest.adapter)?verifyHostLifecycleCacheReference({manifest,raw,caseDir,requirePin,cacheEntry}):null;
}
export function verifyHostLifecycleCacheReference({manifest,raw,caseDir,requirePin,cacheEntry}){
    if(!isHostLifecycleCacheAdapter(manifest.adapter))return null;
    const selected=descriptor(manifest.adapter);
    need(raw.id===selected.id&&raw.status==='PASS','RAW_CASE_STATUS_CHANGED');
    need(manifest.options.hostProtocolFixedContract?.kind===selected.kind,'FIXED_CONTRACT_MISSING');
    const adapter={...manifest.adapter,id:selected.id},c=context({...raw,evidenceDir:caseDir},adapter,manifest.repoRoot);
    const current=hostLifecycleDescriptorContract(adapter,manifest.repoRoot);
    need(same(current.contract,manifest.options.hostProtocolFixedContract),'CURRENT_CONTRACT_CHANGED');
    const declared=new Map([...manifest.inputs,...manifest.tools].map(row=>[key(row.path),row.sha256]));
    const seen=new Set();
    for(const row of c.module.sourceHashes){
        const file=path.resolve(manifest.repoRoot,row.path);need(!seen.has(key(file)),'DUPLICATE_OUTPUT_GRAPH_MEMBER');seen.add(key(file));
        if(inside(c.caseDir,file))requirePin(file,row.sha256);
        else need(declared.get(key(file))===row.sha256,'SOURCE_GRAPH_UNDECLARED_OR_CHANGED');
    }
    const spec=specification(manifest.repoRoot),tools=c.module.toolchain;
    need(Array.isArray(tools)&&same(tools.map(row=>key(row.path)).sort(),adapter.artifactInputs.map(key).sort())&&
        tools.every(row=>declared.get(key(row.path))===row.sha256),'TOOL_GRAPH_UNDECLARED_OR_CHANGED');
    const commands=[];
    for(const [field,name,negative] of [['positive','positive',false],['negative',selected.negativeGroup,true]]){
        const file=path.join(c.out,name,'result.json');need(key(c.module[field]?.path)===key(file),'GROUP_PATH_CHANGED');
        requirePin(file,c.module[field].sha256);const r=read(file),out=path.dirname(file);
        validateHostSourceBindings(r.sources,selected.id,raw.sourceInputs,{name});
        validateHostManagedEnvironmentReceipt(r,out,spec);
        validateHostCompilerInputReceipt(r,out,tools);
        validateHostLifecycleReceipt(r,selected.mode,{negative,name});
        if(selected.parent){
            need(r.generatedStable===true&&Array.isArray(r.managedSourceInputs)&&r.managedSourceInputs.length===2,'PARENT_MANAGED_INPUTS_UNPROVEN');
            const managed=['tools/ymcc_sim/global/controller-signal-evidence/environment.mjs','tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json'].map(file=>key(path.join(manifest.repoRoot,file)));
            need(new Set(r.managedSourceInputs.map(row=>key(row.path))).size===2&&
                r.managedSourceInputs.every(row=>managed.includes(key(row.path))&&declared.get(key(row.path))===row.sha256),'PARENT_MANAGED_SOURCE_BINDING_CHANGED');
            need(r.commands.filter(row=>row.name==='build').length===2&&
                r.commands.filter(row=>row.name==='build').every(row=>row.argv.includes('-noAutoResponse')),'PARENT_AUTOMATIC_RESPONSE_POLICY_CHANGED');
        }
        need(r.status===(negative?'FAIL':'PASS'),'GROUP_EXPECTED_STATUS_CHANGED');
        commands.push(...verifyCscArguments(r,out,c.module,manifest.repoRoot,requirePin));
    }
    const proof=cacheEntry.hostProtocolEvidence??{kind:'optimized',result:path.join(c.run,'result.json'),guards:[c.run]};
    for(const file of hostLifecycleEvidenceFiles({...raw,evidenceDir:caseDir},adapter,{repoRoot:manifest.repoRoot,proof}))requirePin(file);
    const parentReturn=verifyParent(c,raw,{...proof,repoRoot:manifest.repoRoot},requirePin);
    return {kind:selected.kind,canonicalId:selected.id,normalCases:selected.parent?20:38,expectedNegativeFailures:selected.parent?8:4,
        nestedSourceAndOutputMembers:c.module.sourceHashes.length,cscCommandSets:commands.length,
        cscFileInputs:commands.reduce((sum,row)=>sum+row.fileInputs,0),parentReturn,
        executedThisRun:false,actualPassedThisRun:false,hardwareAcceptance:false};
}
