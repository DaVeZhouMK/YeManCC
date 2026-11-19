// Conservative input snapshots for the existing, explicit pre-Host producer.
// This observes reviewed search roots; it does not grant cache/Job admission.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {resolvePreHostToolchain} from './prehost-toolchain.mjs';

const HEADER_FILE='tools/ymcc_sim/global/controller-foreground-latency/prehost-header-inputs.json';
const MAX_FILES=3072,MAX_MEMBERS=80000;
const key=file=>path.resolve(file).toLowerCase();
const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const inside=(root,file)=>{const rel=path.relative(root,file);return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));};
function regular(file,directory=false){
    const full=path.resolve(file),stat=fs.lstatSync(full);
    assert(!stat.isSymbolicLink()&&key(fs.realpathSync.native(full))===key(full),'PREHOST_INPUT_REDIRECTION:'+full);
    assert(directory?stat.isDirectory():stat.isFile(),'PREHOST_INPUT_TYPE:'+full);
    return stat;
}
function pin(file){
    const full=path.resolve(file);regular(full);
    const fd=fs.openSync(full,'r');
    try{
        const before=fs.fstatSync(fd,{bigint:true}),hash=crypto.createHash('sha256'),buffer=Buffer.allocUnsafe(1024*1024);
        let count,total=0;
        while((count=fs.readSync(fd,buffer,0,buffer.length,null))>0){hash.update(buffer.subarray(0,count));total+=count;}
        const after=fs.fstatSync(fd,{bigint:true});
        assert(['dev','ino','size','mtimeNs','ctimeNs'].every(name=>before[name]===after[name])&&BigInt(total)===before.size,'PREHOST_INPUT_CHANGED_DURING_READ:'+full);
        return {path:full,bytes:total,sha256:hash.digest('hex')};
    }finally{fs.closeSync(fd);}
}
function members(root){
    regular(root,true);
    const rows=[],files=[];
    function visit(directory){
        for(const entry of fs.readdirSync(directory,{withFileTypes:true}).sort((a,b)=>a.name<b.name?-1:a.name>b.name?1:0)){
            const file=path.join(directory,entry.name),isDirectory=entry.isDirectory();
            assert(!entry.isSymbolicLink()&&(isDirectory||entry.isFile()),'PREHOST_INPUT_MEMBER_TYPE:'+file);
            regular(file,isDirectory);
            rows.push([path.relative(root,file).replaceAll('\\','/'),isDirectory?'directory':'file']);
            assert(rows.length<=MAX_MEMBERS,'PREHOST_INPUT_MEMBERS_EXCESSIVE');
            if(isDirectory)visit(file);else files.push(file);
        }
    }
    visit(root);
    return {rows,files,sha256:digest(JSON.stringify(rows))};
}
function roots(build){
    return {
        inventories:{'msvc-bin':build.msvcBin,'sdk-bin':path.join(build.sdkRoot,'bin',build.sdkVersion,'x64'),
            powershell:build.powershellHome,'lib-msvc':build.libraryPaths[2],'lib-ucrt':build.libraryPaths[0],'lib-um':build.libraryPaths[1]},
        headers:Object.fromEntries([...['ucrt','shared','um','winrt','cppwinrt'].map((name,index)=>['sdk-'+name,build.includePaths[index]]),['msvc',build.includePaths[5]]]),
    };
}
function seed(repo,headerRoots){
    const file=path.join(repo,HEADER_FILE);regular(file);
    const value=JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
    assert.deepEqual(Object.keys(value).sort(),['headers','kind','schemaVersion']);
    assert(value.schemaVersion===1&&value.kind==='ymcc-prehost-compiler-headers-v1','PREHOST_HEADER_SEED_SCHEMA');
    assert(value.headers&&typeof value.headers==='object'&&!Array.isArray(value.headers),'PREHOST_HEADER_SEED_OBJECT');
    assert.deepEqual(Object.keys(value.headers).sort(),Object.keys(headerRoots).sort(),'PREHOST_HEADER_ROOTS');
    const files=[],seen=new Set();
    for(const [name,root]of Object.entries(headerRoots)){
        const entries=value.headers[name];assert(Array.isArray(entries)&&entries.length>0,'PREHOST_HEADER_SEED_EMPTY:'+name);
        for(const relative of entries){
            assert(typeof relative==='string'&&relative.length>0&&!path.isAbsolute(relative)&&!/[\\:\x00-\x1f]/.test(relative)&&!relative.split('/').some(part=>!part||part==='.'||part==='..'),'PREHOST_HEADER_PATH');
            const actual=path.resolve(root,relative);assert(inside(root,actual)&&!seen.has(key(actual)),'PREHOST_HEADER_DUPLICATE_OR_ESCAPE');
            seen.add(key(actual));files.push(actual);
            assert(files.length<=MAX_FILES,'PREHOST_HEADER_SEED_EXCESSIVE');
        }
    }
    return {file,files};
}

/** Hash tool runtimes, all declared library search members, and reviewed headers. */
export function snapshotPreHostInputs(repoRoot){
    const started=performance.now(),repo=path.resolve(repoRoot),build=resolvePreHostToolchain(repo),search=roots(build);
    const headerSeed=seed(repo,search.headers),files=new Map(),membership=[];
    const add=file=>{files.set(key(file),path.resolve(file));assert(files.size<=MAX_FILES,'PREHOST_INPUT_FILES_EXCEED_3072');};
    for(const [name,root]of Object.entries(search.inventories)){
        const observed=members(root);for(const file of observed.files)add(file);
        membership.push({role:name,path:path.resolve(root),members:observed.rows.length,sha256:observed.sha256});
    }
    for(const [name,root]of Object.entries(search.headers)){
        const observed=members(root);
        membership.push({role:'headers-'+name,path:path.resolve(root),members:observed.rows.length,sha256:observed.sha256});
    }
    for(const file of [...headerSeed.files,build.cmd,build.node,path.join(repo,'deps/webview2/build/native/x64/WebView2LoaderStatic.lib')])add(file);
    const inputPins=[...files.values()].sort((a,b)=>key(a)<key(b)?-1:key(a)>key(b)?1:0).map(pin);
    // A new or removed search member while hashing also invalidates this snapshot.
    for(const row of membership)assert.equal(members(row.path).sha256,row.sha256,'PREHOST_INPUT_MEMBERSHIP_CHANGED:'+row.path);
    const stable={schemaVersion:1,kind:'ymcc-prehost-input-provenance-v1',headerDeclaration:pin(headerSeed.file),membership,inputPins};
    return {...stable,fingerprint:digest(JSON.stringify(stable)),inputCount:inputPins.length,
        pinnedBytes:inputPins.reduce((sum,row)=>sum+row.bytes,0),headerCount:headerSeed.files.length,
        elapsedMs:performance.now()-started,cacheAdmissionGranted:false,toolClosureAccepted:false};
}

/** Bind actual compiler/library observations to the snapshots taken around this build. */
export function verifyPreHostInputs(repoRoot,snapshotRoot,before,after,compilerInputs,linkObservation){
    assert(before?.kind==='ymcc-prehost-input-provenance-v1'&&after?.kind===before.kind,'PREHOST_INPUT_SNAPSHOTS_REQUIRED');
    assert.equal(after.fingerprint,before.fingerprint,'PREHOST_INPUT_OR_SEARCH_MEMBERSHIP_CHANGED');
    const pins=new Map(before.inputPins.map(row=>[key(row.path),row]));
    const repo=path.resolve(repoRoot),snapshot=path.resolve(snapshotRoot),external=new Set();
    assert(Array.isArray(compilerInputs)&&compilerInputs.length>0,'PREHOST_ACTUAL_COMPILER_GRAPH_REQUIRED');
    for(const row of compilerInputs){
        assert(typeof row?.path==='string'&&path.isAbsolute(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256??''),'PREHOST_COMPILER_INPUT_INVALID');
        if(inside(repo,row.path)||inside(snapshot,row.path))continue;
        const expected=pins.get(key(row.path));
        assert(expected&&expected.sha256===row.sha256,'PREHOST_UNDECLARED_OR_CHANGED_EXTERNAL_COMPILER_INPUT:'+row.path);
        external.add(key(row.path));
    }
    assert(external.size>0,'PREHOST_EXTERNAL_COMPILER_GRAPH_EMPTY');
    assert(linkObservation?.observationStatus==='OBSERVED_ABSOLUTE_LIBRARY_PATHS_ONLY'&&linkObservation.linkExitCode===0&&
        linkObservation.linkInvocationError===null&&linkObservation.logWriteError===null&&linkObservation.observationError===null&&
        Array.isArray(linkObservation.libraries)&&linkObservation.libraries.length>0&&linkObservation.libraryPinErrors?.length===0&&
        linkObservation.observedAbsolutePathCount===linkObservation.libraries.length,'PREHOST_LINK_OBSERVATION_INCOMPLETE');
    for(const row of linkObservation.libraries){
        const expected=pins.get(key(row.path));
        assert(expected&&typeof row.sha256==='string'&&/^[a-f0-9]{64}$/i.test(row.sha256)&&expected.sha256===row.sha256.toLowerCase()&&expected.bytes===row.bytes,'PREHOST_UNDECLARED_OR_CHANGED_LINK_LIBRARY:'+row.path);
    }
    return {status:'PASS_INPUT_AND_OBSERVED_LINK_BINDING',externalCompilerInputs:external.size,
        observedLibraries:linkObservation.libraries.length,inputCount:before.inputCount,pinnedBytes:before.pinnedBytes,
        fingerprint:before.fingerprint,cacheAdmissionGranted:false,toolClosureAccepted:false,
        remaining:'Cache consumer and completed parent Job admission are not established by these snapshots.'};
}
