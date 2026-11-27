import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';

const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');

export function assertPreHostBatchSelection(batch,expectedSelections,directedWin5){
    if(directedWin5)assert.deepEqual(expectedSelections,['win5-return-exit2'],'Exact directed Win5 selection required');
    else assert(Array.isArray(expectedSelections)&&expectedSelections.length===9&&new Set(expectedSelections).size===9&&!expectedSelections.includes('win5-return-exit2'),'Exact original nine selections required');
    assert(batch.passed&&batch.error===null&&batch.notRun.length===0&&batch.incompleteStartedCases.length===0,'Incomplete pre-Host batch');
    assert.deepEqual(batch.selectedCases,expectedSelections,'Pre-Host selected cases differ');
    assert.deepEqual(batch.startedCases,expectedSelections,'Pre-Host started cases differ');
    assert.deepEqual(batch.cases.map(row=>row.selection),expectedSelections,'Pre-Host case rows differ');
}

// BEGIN_PREHOST_OUTPUT_PROVENANCE
function collectPreHostOutputProvenance(workspaceParent,workspaceRoot,buildRoot,snapshotManifest,batch,expectedSelections,{requireBuildDetails=false,expectedProvenance,directedWin5=false}={}){
    assertPreHostBatchSelection(batch,expectedSelections,directedWin5);
    const key=file=>path.resolve(file).toLowerCase();
    const inside=(parent,file)=>{const rel=path.relative(parent,file);return rel!==''&&!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep);};
    const parent=fs.realpathSync(workspaceParent),workspace=fs.realpathSync(workspaceRoot),build=fs.realpathSync(buildRoot);
    assert.equal(key(workspace),key(workspaceRoot),'Redirected pre-Host workspace');
    assert.equal(key(path.dirname(workspace)),key(parent),'Pre-Host workspace outside owned parent');
    assert(/^S30-[a-z0-9]{6}$/i.test(path.basename(workspace)),'Unexpected pre-Host workspace name');
    assert.equal(key(build),key(path.join(workspace,'Unoptimized')),'Unexpected pre-Host build root');
    const files=new Map(),snapshot=path.join(build,'source-snapshot');
    const add=(file,expectedSha)=>{
        const resolved=path.resolve(file);
        assert(inside(build,resolved),'Pre-Host output outside owned build');
        assert.equal(key(fs.realpathSync(resolved)),key(resolved),'Redirected pre-Host output');
        const stat=fs.lstatSync(resolved);
        assert(stat.isFile()&&!stat.isSymbolicLink(),'Pre-Host output must be a regular file');
        const bytes=fs.readFileSync(resolved),sha256=hash(bytes);
        if(expectedSha!==undefined)assert.equal(sha256,expectedSha.toLowerCase(),'Pre-Host output hash changed: '+file);
        const previous=files.get(key(resolved));
        if(previous)assert.equal(previous.sha256,sha256,'Pre-Host output changed while collecting');
        else files.set(key(resolved),{path:resolved,bytes:bytes.length,sha256});
    };
    for(const name of ['compiled-snapshot-hashes.json','compiler-tools.json','verification-summary.json',
        'regression-dependencies.json','dsu-dependencies.json','steam-recovery-dependencies.json',
        'regression.obj','dsu.obj','steam-recovery.obj','msi-foreground-native-regression.exe',
        'module-package.json','prehost-writer/result.json'])add(path.join(build,name));
    if(requireBuildDetails)for(const name of ['build-environment.json','link-search.log','link-observed-libraries.json'])add(path.join(build,name));
    assert.equal(fs.readFileSync(path.join(build,'module-package.json'),'utf8'),'{}','Pre-Host private module gate changed');
    assert(/^[a-f0-9]{64}$/.test(batch.executableSha256),'Pre-Host executable pin missing');
    assert.equal(files.get(key(path.join(build,'msi-foreground-native-regression.exe'))).sha256,batch.executableSha256,'Pre-Host executable changed');
    assert.deepEqual(JSON.parse(fs.readFileSync(path.join(build,'prehost-writer/result.json'),'utf8')),batch,'Pre-Host batch differs from original raw');
    for(const row of batch.cases){
        assert(/^[a-z][a-z0-9-]*$/.test(row.selection),'Invalid pre-Host selection path');
        const directory=path.join(build,'prehost-writer',row.selection);
        for(const name of ['result.json','stdout.raw','stderr.raw'])add(path.join(directory,name));
        assert.deepEqual(JSON.parse(fs.readFileSync(path.join(directory,'result.json'),'utf8')),row,'Pre-Host case differs from original raw');
    }
    assert(Array.isArray(snapshotManifest)&&snapshotManifest.length>0&&snapshotManifest.length<=10000,'Pre-Host snapshot manifest missing or excessive');
    const members=new Set();
    for(const row of snapshotManifest){
        assert(typeof row.Path==='string'&&path.isAbsolute(row.Path)&&!row.Path.split(/[\\/]/).includes('..')&&/^[a-f0-9]{64}$/i.test(row.Hash??''),'Invalid pre-Host snapshot row');
        assert(inside(snapshot,path.resolve(row.Path)),'Pre-Host snapshot member outside original root');
        assert(!members.has(key(row.Path)),'Duplicate pre-Host snapshot member');members.add(key(row.Path));
        add(row.Path,row.Hash);
    }
    const provenance={schemaVersion:1,kind:'ymcc-prehost-owned-output-provenance',workspaceParent:parent,workspace,buildRoot:build,
        selections:[...expectedSelections],outputPins:[...files.values()],pinnedBytes:[...files.values()].reduce((n,row)=>n+row.bytes,0),
        scope:'Original compiled snapshot, three objects, executable, module gate and exact scene raw; no business replay or cache admission',
        buildDetailsBound:requireBuildDetails,cacheAdmissionGranted:false,toolClosureAccepted:false};
    if(expectedProvenance!==undefined)assert.deepEqual(provenance,expectedProvenance,'Pre-Host output provenance changed or incomplete');
    return provenance;
}
// END_PREHOST_OUTPUT_PROVENANCE

export {collectPreHostOutputProvenance};
