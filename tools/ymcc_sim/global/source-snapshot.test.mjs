import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {createSourceSnapshot,createCapturedSourceSnapshot,compareSourceSnapshot,verifyCapturedSourceSnapshot,SNAPSHOT_METADATA,SNAPSHOT_MANIFEST,SNAPSHOT_POLICY} from './source-snapshot.mjs';
import {parseSnapshotArguments,SOURCE_SNAPSHOT_HELP} from './create-source-snapshot.mjs';

const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const readJson=file=>JSON.parse(fs.readFileSync(file,'utf8'));
function fixture(t){
 const temp=path.resolve(os.tmpdir()),base=fs.mkdtempSync(path.join(temp,'ymcc-source-snapshot-test-'));
 t.after(()=>{const absolute=path.resolve(base);assert.equal(path.dirname(absolute),temp);assert(path.basename(absolute).startsWith('ymcc-source-snapshot-test-'));fs.rmSync(absolute,{recursive:true,force:true});});
 const parent=path.join(base,'workspace','Mainline','YeManCC-source'),repo=path.join(parent,'YeManCC'),out=path.join(parent,'Frozen');fs.mkdirSync(repo,{recursive:true});
 const write=(rel,bytes)=>{const file=path.join(repo,...rel.split('/'));fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes);return file;};
 write('native/main.cpp',Buffer.from('// uncommitted main\r\nint notRun=7;\r\n'));write('tools/fixture.mjs','// never execute fixture\n');write('src/view.txt','source');fs.mkdirSync(path.join(repo,'empty'),{recursive:true});
 return {base,parent,repo,out,write,create(options={}){return createSourceSnapshot({repoRoot:repo,outDir:out,...options});},compare(manifestFile=path.join(out,...SNAPSHOT_MANIFEST.split('/'))){return compareSourceSnapshot({manifestFile,repoRoot:repo});}};
}
function captured(f){const r=f.create();assert.equal(r.status,'PASS',JSON.stringify(r.failure));assert.equal(r.isCurrent,true);return r;}
const paths=value=>value.files.map(row=>row.path);
test('snapshot freezes exact uncommitted raw bytes and preserves the shared historical workspace',t=>{
 const f=fixture(t),bytes=Buffer.from([0,255,13,10,0,7,128]);f.write('deps/runtime/bin/raw.dll',bytes);f.write('src/未提交 文件.txt','working bytes differ from VCS');const value=captured(f);
 assert.equal(value.sourceWorkspaceRoot,value.snapshotWorkspaceRoot);assert.equal(value.sourceWorkspaceRoot,path.resolve(f.repo,'../../..'));assert.equal(value.copyVerifiedFiles,value.files.length);assert(fs.readFileSync(path.join(f.out,'deps/runtime/bin/raw.dll')).equals(bytes));assert.equal(value.main.before.sha256,hash(fs.readFileSync(path.join(f.repo,'native/main.cpp'))));assert.deepEqual(value.main.before,value.main.snapshot);assert.deepEqual(value.main.before,value.main.sourceAfter);assert.equal(f.compare().status,'CURRENT');assert.equal(value.executionPerformed,false);assert.equal(value.requiresFinalCurrentComparison,true);assert.equal(value.snapshotPassApprovesChangedSource,false);
});
test('every included byte/path has a reversible exact identity in source and snapshot',t=>{const f=fixture(t);f.write('deps/data/all-bytes.bin',Buffer.from(Array.from({length:256},(_,i)=>i)));const value=captured(f);for(const row of value.files){const source=fs.readFileSync(path.join(f.repo,row.path)),copy=fs.readFileSync(path.join(f.out,row.path));assert(source.equals(copy));assert.equal(source.length,row.size);assert.equal(hash(source),row.sha256);assert.equal(hash(copy),row.sha256);}for(const dir of value.directories)assert(fs.statSync(path.join(f.out,dir)).isDirectory());assert.deepEqual(readJson(path.join(f.out,value.sourceBefore.path)).files,value.files);assert.deepEqual(readJson(path.join(f.out,value.snapshotAfter.path)).files,value.files);});
test('a main change outside identical tested functions is stale in its entirety',t=>{const f=fixture(t);f.write('native/main.cpp','static int tested(){return 1;}\n// untouched function\nint unrelated=1;');captured(f);f.write('native/main.cpp','static int tested(){return 1;}\n// untouched function\nint unrelated=2;');const r=f.compare();assert.equal(r.status,'STALE');assert.equal(r.isCurrent,false);assert(r.sourceDiff.changed.some(row=>row.path==='native/main.cpp'));assert.notEqual(r.main.captured.sha256,r.main.current.sha256);assert.equal(readJson(path.join(f.out,...SNAPSHOT_MANIFEST.split('/'))).status,'PASS');});
for(const [name,mutate,field,rel] of [
 ['new file',f=>f.write('tools/new-case.mjs','new'),'added','tools/new-case.mjs'],
 ['deleted file',f=>fs.unlinkSync(path.join(f.repo,'src/view.txt')),'removed','src/view.txt'],
 ['empty directory added',f=>fs.mkdirSync(path.join(f.repo,'new-empty')),'addedDirectories','new-empty'],
 ['empty directory deleted',f=>fs.rmdirSync(path.join(f.repo,'empty')),'removedDirectories','empty'],
])test('compare detects included '+name,t=>{const f=fixture(t);captured(f);mutate(f);const r=f.compare();assert.equal(r.status,'STALE');assert(r.sourceDiff[field].includes(rel));});
test('changed or added excluded content does not become an included identity',t=>{const f=fixture(t);f.write('dist/dev-output.js','before');f.write('.git/config','not read');f.write('node_modules/pkg/token.txt','not read');const value=captured(f);assert(!paths(value).some(p=>p.startsWith('dist/')||p.startsWith('.git/')||p.startsWith('node_modules/')));f.write('dist/dev-output.js','after');f.write('.git/config','changed excluded');f.write('Build/new.txt','new excluded');f.write('node_modules/pkg/new.dll','excluded');assert.equal(f.compare().status,'CURRENT');assert(value.excluded.some(row=>row.path==='dist'&&row.rule==='root-generated'));});
test('narrow exclusions preserve deps build/bin/obj/meshes, vendor dist and source fixtures',t=>{const f=fixture(t);for(const rel of ['deps/webview2/build/x64/runtime.lib','deps/locked/bin/runtime.dll','deps/locked/obj/data.obj','deps/handheldcompanion-runtime/source/3DModels/mesh.obj','decky-plugin/vendor/sdk/dist/index.js','tools/fixtures/bin/original.bin','tools/fixtures/obj/original.obj','tools/fixtures/deps/project/bin/retained.dll','tools/fixtures/sample.log','native/ordinary/bin/fixture.bin'])f.write(rel,Buffer.from('locked '+rel));f.write('native/Project/Project.csproj','<Project/>');f.write('native/Project/bin/release.dll','generated');f.write('native/Project/obj/cache.json','generated');f.write('tools/Probe/Probe.csproj','<Project/>');f.write('tools/Probe/bin/probe.exe','generated');f.write('tools/Probe/obj/cache.json','generated');f.write('tools/__pycache__/temp.pyc','generated');f.write('tools/cache.pyc','generated');f.write('tools/Probe/.vs/cache','generated');const value=captured(f);assert(paths(value).includes('deps/locked/obj/data.obj'));assert(paths(value).includes('decky-plugin/vendor/sdk/dist/index.js'));assert(paths(value).includes('tools/fixtures/sample.log'));assert(paths(value).includes('tools/fixtures/bin/original.bin'));assert(paths(value).includes('native/ordinary/bin/fixture.bin'));assert(!paths(value).includes('native/Project/bin/release.dll'));assert(!paths(value).includes('tools/Probe/obj/cache.json'));assert(!paths(value).some(p=>p.includes('__pycache__')||p==='tools/cache.pyc'));assert(value.excluded.some(row=>row.rule==='dotnet-output'));assert.deepEqual(value.policy,SNAPSHOT_POLICY);});
test('fresh destination cannot be overwritten or reused, even when empty',t=>{const f=fixture(t);fs.mkdirSync(f.out);assert.throws(()=>f.create(),/FRESH_DESTINATION_REQUIRED/);assert.equal(fs.readdirSync(f.out).length,0);});
test('a successful snapshot cannot be reused and its old evidence stays intact',t=>{const f=fixture(t),value=captured(f),bytes=fs.readFileSync(value.manifestFile);assert.throws(()=>f.create(),/FRESH_DESTINATION_REQUIRED/);assert(fs.readFileSync(value.manifestFile).equals(bytes));});
for(const name of ['same','inside','contains','nonsibling'])test('destination rejects '+name+' source relation before creating it',t=>{const f=fixture(t),target=name==='same'?f.repo:name==='inside'?path.join(f.repo,'copy'):name==='contains'?f.parent:path.join(f.base,'elsewhere');assert.throws(()=>createSourceSnapshot({repoRoot:f.repo,outDir:target}),/ROOTS_OVERLAP|SIBLING_DESTINATION_REQUIRED/);assert(!fs.existsSync(path.join(f.repo,'copy')));assert(!fs.existsSync(path.join(f.base,'elsewhere')));});
test('relative root arguments are rejected',t=>{const f=fixture(t);assert.throws(()=>createSourceSnapshot({repoRoot:'.',outDir:f.out}),/ABSOLUTE_PATH_REQUIRED/);assert.throws(()=>createSourceSnapshot({repoRoot:f.repo,outDir:'copy'}),/ABSOLUTE_PATH_REQUIRED/);});
test('a source directory junction is refused without traversal and keeps FAIL evidence',t=>{const f=fixture(t),outside=path.join(f.base,'outside');fs.mkdirSync(outside);fs.writeFileSync(path.join(outside,'secret.txt'),'not to be copied');fs.symlinkSync(outside,path.join(f.repo,'linked'),'junction');const value=f.create();assert.equal(value.status,'FAIL');assert.match(value.failure.code,/REPARSE/);assert(!fs.existsSync(path.join(f.out,'linked')));assert(!fs.existsSync(path.join(f.out,'secret.txt')));assert(fs.existsSync(value.manifestFile));});
test('source-root and destination-root reparse aliases are refused before traversal',t=>{const f=fixture(t),alias=path.join(f.parent,'alias'),target=path.join(f.parent,'destination-target');fs.symlinkSync(f.repo,alias,'junction');assert.throws(()=>createSourceSnapshot({repoRoot:alias,outDir:f.out}),/REPARSE/);fs.mkdirSync(target);fs.symlinkSync(target,f.out,'junction');assert.throws(()=>f.create(),/REPARSE/);assert.equal(fs.readdirSync(target).length,0);});
test('even an excluded root entry cannot be a reparse traversal',t=>{const f=fixture(t),outside=path.join(f.base,'packages');fs.mkdirSync(outside);fs.symlinkSync(outside,path.join(f.repo,'node_modules'),'junction');assert.equal(f.create().status,'FAIL');});
test('source metadata name collision is refused rather than hidden',t=>{const f=fixture(t);f.write(SNAPSHOT_METADATA+'/source.txt','source');const r=f.create();assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'SNAPSHOT_SOURCE_METADATA_COLLISION');assert(!fs.existsSync(path.join(f.out,SNAPSHOT_METADATA,'source.txt')));});
test('main is mandatory and absence leaves failure evidence',t=>{const f=fixture(t);fs.unlinkSync(path.join(f.repo,'native/main.cpp'));const r=f.create();assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'SNAPSHOT_MAIN_CPP_REQUIRED');assert.equal(r.isCurrent,false);});
for(const phase of ['source-before','before-copy-file'])test('source drift during '+phase+' fails without retrying or deleting partial output',t=>{const f=fixture(t);let changed=false;const r=f.create({onProgress:e=>{if(!changed&&e.phase===phase&&(phase!=='before-copy-file'||e.path==='native/main.cpp')){changed=true;f.write('native/main.cpp','new bytes while copying');}}});assert.equal(changed,true);assert.equal(r.status,'FAIL');assert.equal(r.isCurrent,false);assert.match(r.failure.code,/CHANGED_DURING_COPY|SOURCE_OR_COPY_DRIFT/);assert(fs.existsSync(r.manifestFile));assert.throws(()=>f.create(),/FRESH_DESTINATION_REQUIRED/);assert.equal(fs.readFileSync(path.join(f.repo,'native/main.cpp'),'utf8'),'new bytes while copying');});
for(const [name,change,field,rel] of [
 ['new file',f=>f.write('new-file.txt','created after before'),'added','new-file.txt'],
 ['deleted file',f=>fs.unlinkSync(path.join(f.repo,'tools/fixture.mjs')),'removed','tools/fixture.mjs'],
 ['already copied file changed',f=>f.write('native/main.cpp','changed after copy'),'changed','native/main.cpp'],
])test('complete after enumeration detects '+name,t=>{const f=fixture(t);const r=f.create({onProgress:e=>{if(e.phase==='copy-complete')change(f);}});assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'SNAPSHOT_SOURCE_OR_COPY_DRIFT');if(field==='changed')assert(r.sourceDiff.changed.some(p=>p.path===rel));else assert(r.sourceDiff[field].includes(rel));assert(fs.existsSync(path.join(f.out,'native/main.cpp')));assert(fs.existsSync(path.join(f.out,r.sourceAfter.path)));});
test('copy verification detects a tampered destination without deleting its evidence',t=>{const f=fixture(t);const r=f.create({onProgress:e=>{if(e.phase==='copy-complete')fs.writeFileSync(path.join(f.out,'native/main.cpp'),'tampered copy');}});assert.equal(r.status,'FAIL');assert(r.snapshotDiff.changed.some(p=>p.path==='native/main.cpp'));assert.equal(fs.readFileSync(path.join(f.out,'native/main.cpp'),'utf8'),'tampered copy');});
test('included symlink introduced during copy fails closed',t=>{const f=fixture(t),outside=path.join(f.base,'elsewhere');fs.mkdirSync(outside);const r=f.create({onProgress:e=>{if(e.phase==='copy-complete')fs.symlinkSync(outside,path.join(f.repo,'new-link'),'junction');}});assert.equal(r.status,'FAIL');assert.match(r.failure.code,/REPARSE/);});
test('compare verifies the snapshot tree too; changing its main is stale',t=>{const f=fixture(t);captured(f);fs.writeFileSync(path.join(f.out,'native/main.cpp'),'changed snapshot');const r=f.compare();assert.equal(r.status,'STALE');assert(r.snapshotDiff.changed.some(p=>p.path==='native/main.cpp'));assert.equal(r.sourceDiff.equal,true);});
test('compare detects unexpected included snapshot files',t=>{const f=fixture(t);captured(f);fs.writeFileSync(path.join(f.out,'new.exe'),'not executed');assert(f.compare().snapshotDiff.added.includes('new.exe'));});
test('compare rejects a different source root and never rewrites manifest bytes',t=>{const f=fixture(t),value=captured(f),manifest=fs.readFileSync(value.manifestFile),other=path.join(f.parent,'Other');fs.mkdirSync(other);assert.throws(()=>compareSourceSnapshot({repoRoot:other,manifestFile:value.manifestFile}),/MANIFEST_ROOT_DIFFERS/);assert(fs.readFileSync(value.manifestFile).equals(manifest));});
for(const [name,mutate] of [
 ['weakened exclusion policy',m=>m.policy.rules=[]],
 ['main promoted by tested-function identity',m=>m.main.before.sha256='0'.repeat(64)],
 ['removed included row',m=>m.files.pop()],
 ['traversal evidence ref',m=>m.sourceBefore.path='../outside.json'],
 ['success changed to failed',m=>m.status='FAIL'],
])test('compare rejects manifest '+name,t=>{const f=fixture(t),value=captured(f),m=readJson(value.manifestFile);mutate(m);fs.writeFileSync(value.manifestFile,JSON.stringify(m));assert.throws(()=>f.compare());});
test('compare rejects changed source-before receipt hash',t=>{const f=fixture(t),value=captured(f);fs.appendFileSync(path.join(f.out,value.sourceBefore.path),'\n');assert.throws(()=>f.compare(),/EVIDENCE_HASH_CHANGED/);});
test('current source junction introduced after capture yields FAIL rather than current',t=>{const f=fixture(t);captured(f);const outside=path.join(f.base,'outside');fs.mkdirSync(outside);fs.symlinkSync(outside,path.join(f.repo,'new-junction'),'junction');const r=f.compare();assert.equal(r.status,'FAIL');assert.equal(r.isCurrent,false);assert.match(r.failure.code,/REPARSE/);});
test('CLI accepts only create/compare and rejects duplicate/unknown/mixed options',()=>{assert.equal(parseSnapshotArguments(['--help']).mode,'help');assert.equal(parseSnapshotArguments(['--repo','R','--out','O']).mode,'create');assert.equal(parseSnapshotArguments(['--compare','M','--repo','R']).mode,'compare');for(const args of [[],['--repo'],['--out','X'],['--repo','R','--out','A','--out','B'],['--repo','R','--out','O','--compare','M'],['--repo','R','--execute','X'],['--compare','M'],['--repo','R','--out','--compare']])assert.throws(()=>parseSnapshotArguments(args));assert(SOURCE_SNAPSHOT_HELP.includes('never launch'));});
test('actual CLI runs only the snapshot tool on a tiny fixture, compares read-only and reports stale main',t=>{const f=fixture(t),cli=path.join(path.dirname(fileURLToPath(import.meta.url)),'create-source-snapshot.mjs');const run=args=>spawnSync(process.execPath,[cli,...args],{encoding:'utf8',windowsHide:true,timeout:10000});const created=run(['--repo',f.repo,'--out',f.out]);assert.equal(created.status,0,created.stderr);const value=JSON.parse(created.stdout);assert.equal(value.status,'PASS');assert.equal(value.executionPerformed,false);const manifestBytes=fs.readFileSync(value.manifestFile);assert.equal(run(['--compare',value.manifestFile,'--repo',f.repo]).status,0);f.write('native/main.cpp','changed after mock test');const compared=run(['--compare',value.manifestFile,'--repo',f.repo]);assert.equal(compared.status,2);assert.equal(JSON.parse(compared.stdout).isCurrent,false);assert(fs.readFileSync(value.manifestFile).equals(manifestBytes));assert.equal(run(['--repo',f.repo,'--out',f.out]).status,3);});
test('source gitignore rules cannot omit tracked or uncommitted source from this explicit policy',t=>{const f=fixture(t);f.write('.gitignore','native/main.cpp\nsrc/**\ndeps/**\n*.obj\n');f.write('deps/locked/resource.obj','original locked mesh');const r=captured(f);assert(paths(r).includes('.gitignore'));assert(paths(r).includes('native/main.cpp'));assert(paths(r).includes('deps/locked/resource.obj'));assert(paths(r).includes('src/view.txt'));});
test('deps bin and obj remain included even beside real .NET project filenames',t=>{const f=fixture(t);f.write('deps/Locked/Locked.csproj','<Project/>');f.write('deps/Locked/bin/runtime.dll','locked dependency');f.write('deps/Locked/obj/immutable.obj','locked resource');f.write('tools/fixtures/deps/Locked/Locked.csproj','<Project/>');f.write('tools/fixtures/deps/Locked/bin/runtime.dll','locked nested fixture');const r=captured(f);for(const rel of ['deps/Locked/bin/runtime.dll','deps/Locked/obj/immutable.obj','tools/fixtures/deps/Locked/bin/runtime.dll'])assert(paths(r).includes(rel));});
test('source excludes may change during copy without hiding an included main change',t=>{const f=fixture(t);f.write('Build/only-generated.txt','before');const r=f.create({onProgress:e=>{if(e.phase==='copy-complete')f.write('Build/only-generated.txt','after');}});assert.equal(r.status,'PASS');assert.equal(f.compare().status,'CURRENT');assert.equal(fs.readFileSync(path.join(f.repo,'native/main.cpp'),'utf8'),'// uncommitted main\r\nint notRun=7;\r\n');});
test('a new destination file cannot be overwritten during copy',t=>{const f=fixture(t);let injected=false;const r=f.create({onProgress:e=>{if(e.phase==='before-copy-file'&&e.path==='native/main.cpp'){injected=true;fs.writeFileSync(path.join(f.out,'native/main.cpp'),'preexisting output');}}});assert(injected);assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'SNAPSHOT_COPY_OVERWRITE_REJECTED');assert.equal(fs.readFileSync(path.join(f.out,'native/main.cpp'),'utf8'),'preexisting output');assert.equal(fs.readFileSync(path.join(f.repo,'native/main.cpp'),'utf8'),'// uncommitted main\r\nint notRun=7;\r\n');});
test('source hardlinks are copied as independent snapshot bytes without sharing live source ownership',t=>{const f=fixture(t);fs.linkSync(path.join(f.repo,'native/main.cpp'),path.join(f.repo,'native/alias.cpp'));captured(f);fs.writeFileSync(path.join(f.out,'native/main.cpp'),'snapshot-only mutation');assert.notEqual(fs.readFileSync(path.join(f.repo,'native/main.cpp'),'utf8'),'snapshot-only mutation');assert.notEqual(fs.readFileSync(path.join(f.out,'native/alias.cpp'),'utf8'),'snapshot-only mutation');assert.equal(f.compare().status,'STALE');});
test('metadata receipts reject reparse replacement during read-only compare',t=>{const f=fixture(t),r=captured(f),target=path.join(f.base,'elsewhere');fs.mkdirSync(target);const before=fs.readFileSync(r.manifestFile);fs.renameSync(path.join(f.out,SNAPSHOT_METADATA),path.join(f.out,'metadata-original'));fs.symlinkSync(target,path.join(f.out,SNAPSHOT_METADATA),'junction');assert.throws(()=>f.compare(),/REPARSE/);assert.equal(fs.readdirSync(target).length,0);assert(fs.readFileSync(path.join(f.out,'metadata-original','manifest.json')).equals(before));});
test('failed snapshots cannot be compared as successful current source',t=>{const f=fixture(t),r=f.create({onProgress:e=>{if(e.phase==='copy-complete')f.write('native/main.cpp','drift');}});assert.equal(r.status,'FAIL');assert.throws(()=>f.compare(),/SUCCESSFUL_MANIFEST_REQUIRED/);});
// Revision 2 appends to the original 47 fixtures without changing their bytes.
// Only synthetic temporary trees are opened by these boundary tests.
function withoutExcludedContent(roots,action){
 const keys=roots.map(value=>path.resolve(value).toLowerCase()),saved={};
 const guard=file=>{if(typeof file!=='string'&&!Buffer.isBuffer(file))return;const resolved=path.resolve(String(file)).toLowerCase();assert(!keys.some(root=>resolved===root||resolved.startsWith(root+path.sep)),'Excluded subtree content access: '+resolved);};
 for(const name of ['readdirSync','openSync','readFileSync']){saved[name]=fs[name];fs[name]=function(file,...args){guard(file);return saved[name].call(fs,file,...args);};}
 try{return action();}finally{for(const [name,fn] of Object.entries(saved))fs[name]=fn;}
}
test('manifest declares every operational root, its reason and the limited source scope',t=>{
 const f=fixture(t),r=captured(f),expected=['_scratch','work','System.Management.Automation.Internal.Host.InternalHost'];
 assert.deepEqual(r.policy.rootRuntimeDirectoryExclusions.map(row=>row.name),expected);
 for(const row of r.policy.rootRuntimeDirectoryExclusions){assert(row.reason.length>20);assert(r.policy.rules.some(rule=>rule.id===row.id&&rule.reason===row.reason&&rule.match.includes('Root directory '+row.name+' only')));}
 assert.equal(r.policy.version,8);assert.deepEqual(r.policy.retainedStaticSourceData,SNAPSHOT_POLICY.retainedStaticSourceData);assert.equal(r.policy.installationPayloadFrozen,false);assert.equal(r.policy.excludedDirectoryContentsRead,false);assert.equal(r.installationPayloadFrozen,false);assert.equal(f.compare().installationPayloadFrozen,false);
});
for(const root of ['_scratch','work','System.Management.Automation.Internal.Host.InternalHost'])test('root '+root+' contents are never traversed, hashed or copied; excluded changes stay outside currentness',t=>{
 const f=fixture(t),rootPath=path.join(f.repo,root),outside=path.join(f.base,'fake-runtime-outside');
 f.write(root+'/runtime/settings.json','SYNTHETIC SETTINGS');f.write(root+'/runtime/session/token.fake','SYNTHETIC TOKEN');f.write(root+'/runtime/lease.fake','SYNTHETIC LEASE');fs.mkdirSync(outside);fs.symlinkSync(outside,path.join(rootPath,'not-traversed'),'junction');
 withoutExcludedContent([rootPath,path.join(f.out,root)],()=>{
  const r=captured(f),row=r.excluded.find(row=>row.path===root);assert(row);assert(row.reason.length>20);assert.equal(row.reason,SNAPSHOT_POLICY.rules.find(rule=>rule.id===row.rule).reason);assert(!paths(r).some(rel=>rel===root||rel.startsWith(root+'/')));assert(!fs.existsSync(path.join(f.out,root)));assert.equal(f.compare().status,'CURRENT');
 });
 f.write(root+'/runtime/settings.json','CHANGED SYNTHETIC SETTINGS');f.write(root+'/runtime/new.fake','NEW EXCLUDED');fs.unlinkSync(path.join(rootPath,'runtime/lease.fake'));
 withoutExcludedContent([rootPath],()=>assert.equal(f.compare().status,'CURRENT'));
});
test('root-only exclusions retain native InputHost and nested matching source names byte for byte',t=>{
 const f=fixture(t),retained=['native/InputHost/InputHost.csproj','native/InputHost/Program.cs','native/PowerControl/Source.cs','tools/fixtures/InputHost/session.fixture','src/work/logic.txt','native/_scratch/source.cpp','native/System.Management.Automation.Internal.Host.InternalHost/source.cpp'];
 for(const rel of retained)f.write(rel,'controlled source '+rel);
 const r=captured(f);for(const rel of retained){assert(paths(r).includes(rel),rel);assert.equal(fs.readFileSync(path.join(f.out,rel),'utf8'),'controlled source '+rel);}assert.equal(f.compare().status,'CURRENT');
});
test('project-parent bin and obj exclusions include FanLab and other source projects without dropping project inputs',t=>{
 const f=fixture(t),projects=[['FanLab/Probe','Probe.csproj'],['Services/Other','Other.fsproj'],['native/InputHost','InputHost.vbproj']];
 for(const [dir,project] of projects){f.write(dir+'/'+project,'<Project/>');f.write(dir+'/Source.cs','controlled source');f.write(dir+'/bin/Release/generated.dll','generated');f.write(dir+'/obj/Debug/generated.json','generated');}
 const denied=projects.flatMap(([dir])=>[path.join(f.repo,dir,'bin'),path.join(f.repo,dir,'obj')]);
 withoutExcludedContent(denied,()=>{const r=captured(f);for(const [dir,project] of projects){assert(paths(r).includes(dir+'/'+project));assert(paths(r).includes(dir+'/Source.cs'));for(const name of ['bin','obj'])assert(r.excluded.some(row=>row.path===dir+'/'+name&&row.rule==='dotnet-output'&&row.reason.length>0));}assert.equal(f.compare().status,'CURRENT');});
});
test('bin and obj without an immediate project parent remain included even under FanLab',t=>{
 const f=fixture(t);f.write('FanLab/Source/bin/fixture.dll','controlled');f.write('FanLab/Source/obj/mesh.obj','controlled');f.write('Services/Library/Library.csproj','<Project/>');f.write('Services/Library/assets/bin/locked.dll','controlled');f.write('Services/Library/assets/obj/mesh.obj','controlled');
 const r=captured(f);for(const rel of ['FanLab/Source/bin/fixture.dll','FanLab/Source/obj/mesh.obj','Services/Library/assets/bin/locked.dll','Services/Library/assets/obj/mesh.obj'])assert(paths(r).includes(rel),rel);
});
test('deps originals retain .NET bin obj and Logs under FanLab runtime and nested source projects',t=>{
 const f=fixture(t),dirs=['deps/FanLab/Locked','FanLab/deps/Locked','native/runtime/deps/Locked','tools/fixtures/deps/Locked'];
 for(const dir of dirs){f.write(dir+'/Locked.csproj','<Project/>');for(const rel of ['bin/locked.dll','obj/mesh.obj','Logs/locked.log','runtime/logs/locked.log'])f.write(dir+'/'+rel,'original '+dir+'/'+rel);}
 const r=captured(f);for(const dir of dirs)for(const rel of ['bin/locked.dll','obj/mesh.obj','Logs/locked.log','runtime/logs/locked.log']){assert(paths(r).includes(dir+'/'+rel));assert.equal(fs.readFileSync(path.join(f.out,dir,rel),'utf8'),'original '+dir+'/'+rel);}
});
test('FanLab runtime and project log directories have explicit exclusions and are never read',t=>{
 const f=fixture(t),logDirs=['FanLab/Logs','FanLab/nested/logs','native/runtime/Logs','Services/Project/data/logs'];f.write('Services/Project/Project.csproj','<Project/>');
 for(const dir of logDirs)f.write(dir+'/session.fake','SYNTHETIC RUNTIME LOG');
 withoutExcludedContent(logDirs.map(dir=>path.join(f.repo,dir)),()=>{const r=captured(f);for(const dir of logDirs)assert(r.excluded.some(row=>row.path===dir&&row.rule==='runtime-logs'&&row.reason.length>0));assert.equal(f.compare().status,'CURRENT');});
});
test('controlled tools fixtures logs stay included under runtime or .NET project ancestors',t=>{
 const f=fixture(t);f.write('tools/fixtures/Contract/Contract.csproj','<Project/>');const retained=['tools/fixtures/Logs/case.log','tools/fixtures/Contract/logs/case.log','tools/fixtures/runtime/Logs/case.log','tools/fixtures/FanLab/logs/case.log'];for(const rel of retained)f.write(rel,'CONTROLLED SAMPLE '+rel);
 const r=captured(f);for(const rel of retained){assert(paths(r).includes(rel));assert.equal(fs.readFileSync(path.join(f.out,rel),'utf8'),'CONTROLLED SAMPLE '+rel);}assert(!r.excluded.some(row=>row.rule==='runtime-logs'));
});
test('log names alone and ordinary source files do not trigger operational exclusions',t=>{
 const f=fixture(t),retained=['Logs/controlled.log','native/ordinary/logs/contract.log','src/Logs/contract.log','FanLab/log/singular-sample.log','FanLab/source.log','native/runtime/Logs.cs','native/runtime/logs-contract.txt'];for(const rel of retained)f.write(rel,'controlled '+rel);
 const r=captured(f);for(const rel of retained)assert(paths(r).includes(rel),rel);
});
test('excluded operational churn cannot hide any full-main or included-source change',t=>{
 const f=fixture(t);f.write('PowerControl/session/token.fake','EXCLUDED SYNTHETIC');f.write('FanLab/Logs/log.fake','EXCLUDED SYNTHETIC');f.write('native/InputHost/Program.cs','before');const r=captured(f);
 f.write('PowerControl/session/token.fake','CHANGED EXCLUDED');f.write('FanLab/Logs/new.fake','CHANGED EXCLUDED');f.write('native/main.cpp','// unchanged tested method\nint outsideMethod=999;');f.write('native/InputHost/Program.cs','after');
 withoutExcludedContent([path.join(f.repo,'PowerControl/session/token.fake'),path.join(f.repo,'FanLab/Logs')],()=>{const compared=f.compare();assert.equal(compared.status,'STALE');assert.deepEqual(compared.sourceDiff.changed.map(row=>row.path),['native/InputHost/Program.cs','native/main.cpp']);assert.notEqual(compared.main.current.sha256,r.main.before.sha256);});
});
for(const [name,mutate,field,rel] of [
 ['addition',f=>f.write('native/InputHost/new.cs','new included'),'added','native/InputHost/new.cs'],
 ['deletion',f=>fs.unlinkSync(path.join(f.repo,'FanLab/Source.cs')),'removed','FanLab/Source.cs'],
])test('included project '+name+' remains stale beside excluded runtime roots and logs',t=>{
 const f=fixture(t);f.write('FanLab/Source.cs','controlled source');f.write('FanLab/Logs/log.fake','EXCLUDED');f.write('PowerControl/session.fake','EXCLUDED');captured(f);mutate(f);const r=f.compare();assert.equal(r.status,'STALE');assert(r.sourceDiff[field].includes(rel));
});
test('new project marker cannot silently reclassify already captured bin bytes as generated',t=>{
 const f=fixture(t);f.write('FanLab/Project/bin/previously-included.dll','controlled before project exists');captured(f);f.write('FanLab/Project/Project.csproj','<Project/>');const r=f.compare();assert.equal(r.status,'STALE');assert(r.sourceDiff.added.includes('FanLab/Project/Project.csproj'));assert(r.sourceDiff.removed.includes('FanLab/Project/bin/previously-included.dll'));assert(r.excludedNow.some(row=>row.path==='FanLab/Project/bin'));
});
test('manifest cannot promote an excluded installation payload to frozen identity',t=>{
 const f=fixture(t),r=captured(f),m=readJson(r.manifestFile);m.installationPayloadFrozen=true;fs.writeFileSync(r.manifestFile,JSON.stringify(m));assert.throws(()=>f.compare(),/SUCCESSFUL_MANIFEST_REQUIRED/);
});


test('actual root InputHost production project and HcVerbatim source stay included while only its outputs are excluded',t=>{
 const f=fixture(t);for(const [name,body] of [['InputHost/YeManInputHost.csproj','<Project Sdk="Microsoft.NET.Sdk"/>'],['InputHost/Program.cs','class Program { static void TrySubmitFrame() {} }'],['InputHost/HcVerbatim/Frame.cs','class Frame {}'],['InputHost/bin/generated.dll','OUTPUT'],['InputHost/obj/project.assets.json','OUTPUT'],['InputHost/Logs/session.fake','OUTPUT']])f.write(name,body);
 const r=captured(f);for(const name of ['InputHost/YeManInputHost.csproj','InputHost/Program.cs','InputHost/HcVerbatim/Frame.cs']){assert(r.files.some(row=>row.path===name));assert.equal(fs.readFileSync(path.join(f.out,name),'utf8'),fs.readFileSync(path.join(f.repo,name),'utf8'));}
 for(const name of ['InputHost/bin','InputHost/obj','InputHost/Logs'])assert(r.excluded.some(row=>row.path===name));
});
test('root InputHost production edits cannot be hidden by unchanged native main or excluded build/log changes',t=>{
 const f=fixture(t);f.write('InputHost/YeManInputHost.csproj','<Project/>');f.write('InputHost/Program.cs','original production');f.write('InputHost/bin/generated.dll','generated');captured(f);f.write('InputHost/Program.cs','changed production');f.write('InputHost/bin/generated.dll','changed ignored');const compared=f.compare();assert.equal(compared.status,'STALE');assert.deepEqual(compared.sourceDiff.changed.map(row=>row.path),['InputHost/Program.cs']);
});



test('mixed PowerControl retains actual catalog/code paths and dispatcher scripts without reading opaque runtime bytes',t=>{
 const f=fixture(t),included=['PowerControl/decky/plugins/ymcc-sidebar/src/index.ts','PowerControl/MemoryScan/Program.cs','PowerControl/_gamedetect.ps1','PowerControl/decky/plugins/ymcc-sidebar/package.json'],opaque=['PowerControl/runtime/settings.json','PowerControl/runtime/session/token.fake','PowerControl/runtime/lease.fake','PowerControl/runtime/bin/vendor.dll'];for(const file of included)f.write(file,file.endsWith('package.json')?'{"name":"controlled-first-party"}':'controlled source');for(const file of opaque)f.write(file,'SYNTHETIC DO NOT READ');withoutExcludedContent(opaque.map(file=>path.join(f.repo,file)),()=>{const r=captured(f);for(const file of included){assert(r.files.some(row=>row.path===file));assert(fs.existsSync(path.join(f.out,file)));}for(const file of opaque){assert(r.excluded.some(row=>row.path===file&&row.rule==='powercontrol-nonsource'));assert(!fs.existsSync(path.join(f.out,file)));}assert.equal(f.compare().status,'CURRENT');});
});
test('mixed PowerControl source change stays stale while opaque runtime state remains outside source identity',t=>{
 const f=fixture(t);f.write('PowerControl/decky/plugins/ymcc-sidebar/src/index.ts','original source');f.write('PowerControl/runtime/settings.json','SYNTHETIC');captured(f);f.write('PowerControl/runtime/settings.json','CHANGED OPAQUE');assert.equal(f.compare().status,'CURRENT');f.write('PowerControl/decky/plugins/ymcc-sidebar/src/index.ts','changed source');const r=f.compare();assert.equal(r.status,'STALE');assert.deepEqual(r.sourceDiff.changed.map(row=>row.path),['PowerControl/decky/plugins/ymcc-sidebar/src/index.ts']);
});


// Version provenance is separate from primary acceptance: creation/current FAIL stays FAIL.
test('fully captured version remains executable provenance after primary moves, without approving current main',t=>{
 const f=fixture(t),created=f.create({onProgress:event=>{if(event.phase==='copy-complete')f.write('native/main.cpp','changed primary after full capture');}});assert.equal(created.status,'FAIL');assert.equal(created.snapshotDiff.equal,true);const manifestFile=path.join(f.out,...SNAPSHOT_MANIFEST.split('/')),manifestSha=hash(fs.readFileSync(manifestFile)),before=fs.readFileSync(manifestFile);const verified=verifyCapturedSourceSnapshot({manifestFile,repoRoot:f.repo,expectedManifestSha256:manifestSha});assert.equal(verified.status,'FROZEN_SOURCE_VERIFIED');assert.equal(verified.snapshotIntegrityVerified,true);assert.equal(verified.creationStatus,'FAIL');assert.equal(verified.isCurrent,false);assert.equal(verified.sourceApprovalGranted,false);assert.equal(verified.sourceDiff.changed[0].path,'native/main.cpp');assert(fs.readFileSync(manifestFile).equals(before));assert.throws(()=>f.compare(),/SNAPSHOT_SUCCESSFUL_MANIFEST_REQUIRED/);
});
test('captured-version verification refuses an incomplete copy or later changes anywhere in the snapshot',t=>{
 const f=fixture(t),created=captured(f),manifestFile=path.join(f.out,...SNAPSHOT_MANIFEST.split('/')),manifestSha=hash(fs.readFileSync(manifestFile));fs.writeFileSync(path.join(f.out,'src/view.txt'),'modified snapshot');assert.throws(()=>verifyCapturedSourceSnapshot({manifestFile,repoRoot:f.repo,expectedManifestSha256:manifestSha}),/SNAPSHOT_CAPTURED_VERSION_CHANGED/);
});
test('captured-version verification pins the original manifest and never signs a modified full primary main',t=>{
 const f=fixture(t);captured(f);const manifestFile=path.join(f.out,...SNAPSHOT_MANIFEST.split('/')),manifestSha=hash(fs.readFileSync(manifestFile));assert.throws(()=>verifyCapturedSourceSnapshot({manifestFile,repoRoot:f.repo,expectedManifestSha256:'0'.repeat(64)}),/SNAPSHOT_EXPECTED_MANIFEST_SHA_REQUIRED/);f.write('native/main.cpp','// same tested function\nint unrelatedMainChange=42;');const r=verifyCapturedSourceSnapshot({manifestFile,repoRoot:f.repo,expectedManifestSha256:manifestSha});assert.equal(r.isCurrent,false);assert.equal(r.snapshotIntegrityVerified,true);assert.equal(r.sourceApprovalGranted,false);assert(r.sourceDiff.changed.some(row=>row.path==='native/main.cpp'));
});


test('exact imported game-title source data is captured while similarly named runtime JSON is never opened',t=>{
 const f=fixture(t),sourceData='PowerControl/GameTitlePolicy.json',sourceImport='src/bridge/gamedetect.ts';
 const opaque=['PowerControl/runtime/settings.json','PowerControl/runtime/GameTitlePolicy.json','PowerControl/GameTitlePolicy.json.bak','PowerControl/runtime/session.json'];
 const data='{"schema":1,"launchSuffixes":["controlled-static-rule"]}';
 f.write(sourceImport,"import gameTitlePolicy from '../../PowerControl/GameTitlePolicy.json';\nexport default gameTitlePolicy;\n");f.write(sourceData,data);
 for(const name of opaque)f.write(name,'SYNTHETIC DO NOT READ');
 withoutExcludedContent(opaque.map(name=>path.join(f.repo,name)),()=>{
  const result=captured(f),row=result.files.find(item=>item.path===sourceData);
  assert(row);assert.equal(row.sha256,hash(Buffer.from(data)));assert.equal(fs.readFileSync(path.join(f.out,sourceData),'utf8'),data);
  assert(result.files.some(item=>item.path===sourceImport));
  for(const name of opaque){assert(result.excluded.some(item=>item.path===name&&item.rule==='powercontrol-nonsource'));assert(!fs.existsSync(path.join(f.out,name)));}
  assert.equal(f.compare().status,'CURRENT');
 });
 assert.equal(SNAPSHOT_POLICY.version,8);assert.deepEqual(SNAPSHOT_POLICY.retainedStaticSourceData.map(item=>[item.path,item.consumedBy]),[[sourceData,sourceImport],['PowerControl/settings-baseline.json','src/bridge/settingsBaseline.ts'],['PowerControl/settings-defaults-v1.json','tools/settings-baseline-contract.mjs']]);
});
test('changing imported static JSON invalidates current source without reading excluded operational settings',t=>{
 const f=fixture(t),data='PowerControl/GameTitlePolicy.json',opaque='PowerControl/runtime/settings.json';
 f.write(data,'{"schema":1,"launchSuffixes":[]}');f.write(opaque,'SYNTHETIC OLD');captured(f);
 f.write(opaque,'SYNTHETIC NEW');withoutExcludedContent([path.join(f.repo,opaque)],()=>assert.equal(f.compare().status,'CURRENT'));
 f.write(data,'{"schema":1,"launchSuffixes":["new-rule"]}');
 withoutExcludedContent([path.join(f.repo,opaque)],()=>{const result=f.compare();assert.equal(result.status,'STALE');assert.deepEqual(result.sourceDiff.changed.map(item=>item.path),[data]);});
});
test('static source-data removal or snapshot tampering cannot inherit a previous capture approval',t=>{
 for(const target of ['source','snapshot']){
  const f=fixture(t),data='PowerControl/GameTitlePolicy.json';f.write(data,'{"schema":1}');captured(f);
  if(target==='source')fs.unlinkSync(path.join(f.repo,data));else fs.writeFileSync(path.join(f.out,data),'altered static source data');
  const result=f.compare();assert.equal(result.status,'STALE');
  if(target==='source')assert(result.sourceDiff.removed.includes(data));else assert(result.snapshotDiff.changed.some(item=>item.path===data));
 }
});


for(const [mode,capture] of [['strict',createSourceSnapshot],['same-descriptor',createCapturedSourceSnapshot]])test('PowerControl Python source capture retains Python and only the exact build recipe ('+mode+')',t=>{
 const f=fixture(t);
 const python=['PowerControl/pawnio/YeManTdpCtl.py','PowerControl/Root.py','PowerControl/scripts/nested/helper.py','PowerControl/scripts/Mixed.PY','PowerControl/deps/pure/source.py'];
 const recipe='PowerControl/pawnio/YeManTdpCtl.spec';
 for(const file of python)f.write(file,Buffer.from('# controlled source, never run\r\nraise RuntimeError("MUST_NOT_EXECUTE")\r\n'));
 f.write(recipe,'a = Analysis(["YeManTdpCtl.py"], datas=[("_internal/RyzenSMU.bin", ".")])\n');
 const caches=['PowerControl/pawnio/__pycache__','PowerControl/deps/pure/__pycache__'];
 for(const dir of caches){f.write(dir+'/ignored.py','CACHE_SENTINEL_MUST_NOT_BE_READ');f.write(dir+'/nested/ignored.pyc','CACHE_SENTINEL_MUST_NOT_BE_READ');}
 const bytecode=['PowerControl/pawnio/standalone.pyc','PowerControl/pawnio/UPPER.PYC'];
 const opaque=['PowerControl/pawnio/runtime/settings.json','PowerControl/pawnio/runtime/lease.json','PowerControl/pawnio/session.log','PowerControl/pawnio/_internal/RyzenSMU.bin','PowerControl/pawnio/PawnIO_setup.exe','PowerControl/pawnio/YeManTdpCtl.exe','PowerControl/pawnio/native.pyd','PowerControl/pawnio/unrelated.spec','PowerControl/other/YeManTdpCtl.spec','PowerControl/pawnio/YeManTdpCtl.py.json','PowerControl/pawnio/unproven.pyw','PowerControl/pawnio/unproven.sh'];
 for(const file of [...bytecode,...opaque])f.write(file,'SYNTHETIC_OPERATIONAL_OR_UNPROVEN_BYTES_MUST_NOT_BE_READ');
 const guarded=[...caches,...bytecode,...opaque].map(file=>path.join(f.repo,file));
 const result=withoutExcludedContent(guarded,()=>{
  const r=capture({repoRoot:f.repo,outDir:f.out});assert.equal(r.status,'PASS',JSON.stringify(r.failure));
  for(const file of [...python,recipe]){const row=r.files.find(row=>row.path===file);assert(row,'Python/build recipe missing from fixed inputs: '+file);assert.equal(row.sha256,hash(fs.readFileSync(path.join(f.repo,file))));assert(fs.readFileSync(path.join(f.out,file)).equals(fs.readFileSync(path.join(f.repo,file))));}
  for(const file of [...caches,...bytecode]){assert(r.excluded.some(row=>row.path===file&&row.rule==='python-cache'),'Cache exclusion not explicit: '+file);assert(!fs.existsSync(path.join(f.out,file)));}
  for(const file of opaque){assert(r.excluded.some(row=>row.path===file&&row.rule==='powercontrol-nonsource'));assert(!fs.existsSync(path.join(f.out,file)));}
  assert.deepEqual(r.policy.retainedSourceBuildRecipes.map(row=>row.path),[recipe]);assert.equal(f.compare().status,'CURRENT');return r;
 });
 const originalManifest=fs.readFileSync(result.manifestFile);
 f.write('PowerControl/pawnio/runtime/settings.json','changed synthetic operational bytes');f.write('PowerControl/pawnio/standalone.pyc','changed synthetic cache');f.write(caches[0]+'/new.py','changed synthetic cache');
 assert.equal(withoutExcludedContent(guarded,()=>f.compare()).status,'CURRENT');assert(fs.readFileSync(result.manifestFile).equals(originalManifest));
});
test('PowerControl Python source edits invalidate captured currentness while cache/runtime churn cannot hide them',t=>{
 const f=fixture(t),script='PowerControl/pawnio/YeManTdpCtl.py';f.write(script,'# source before\n');f.write('PowerControl/pawnio/__pycache__/ignore.py','CACHE');f.write('PowerControl/pawnio/settings.json','SYNTHETIC_RUNTIME');const r=captured(f),before=fs.readFileSync(r.manifestFile);
 f.write(script,'# changed actual source bytes\n');f.write('PowerControl/pawnio/settings.json','CHANGED_SYNTHETIC_RUNTIME');f.write('PowerControl/pawnio/__pycache__/ignore.py','CHANGED_CACHE');
 const result=withoutExcludedContent([path.join(f.repo,'PowerControl/pawnio/__pycache__'),path.join(f.repo,'PowerControl/pawnio/settings.json')],()=>f.compare());assert.equal(result.status,'STALE');assert.deepEqual(result.sourceDiff.changed.map(row=>row.path),[script]);assert.equal(result.main.captured.sha256,result.main.current.sha256);assert(fs.readFileSync(r.manifestFile).equals(before));
});
for(const action of ['added','removed'])test('PowerControl Python source '+action+' is part of fixed identity',t=>{
 const f=fixture(t),script='PowerControl/scripts/changed.py';f.write('PowerControl/scripts/keep.py','# stable source\n');if(action==='removed')f.write(script,'# removed source\n');captured(f);if(action==='added')f.write(script,'# added source\n');else fs.unlinkSync(path.join(f.repo,script));const result=f.compare();assert.equal(result.status,'STALE');assert.deepEqual(result.sourceDiff[action],[script]);
});
test('PowerControl Python snapshot tampering is rejected without rewriting the original manifest',t=>{
 const f=fixture(t),script='PowerControl/pawnio/YeManTdpCtl.py';f.write(script,'# controlled source\n');const r=captured(f),before=fs.readFileSync(r.manifestFile),pin=hash(before);fs.writeFileSync(path.join(f.out,script),'# modified frozen source\n');const result=f.compare();assert.equal(result.status,'STALE');assert.deepEqual(result.snapshotDiff.changed.map(row=>row.path),[script]);assert.throws(()=>verifyCapturedSourceSnapshot({manifestFile:r.manifestFile,repoRoot:f.repo,expectedManifestSha256:pin}),/SNAPSHOT_CAPTURED_VERSION_CHANGED/);assert(fs.readFileSync(r.manifestFile).equals(before));
});
test('the exact Python build recipe is source identity while referenced binary payload stays excluded',t=>{
 const f=fixture(t),recipe='PowerControl/pawnio/YeManTdpCtl.spec',binary='PowerControl/pawnio/_internal/RyzenSMU.bin';f.write(recipe,'a = Analysis(["YeManTdpCtl.py"])\n');f.write(binary,'SYNTHETIC_BINARY');captured(f);f.write(recipe,'a = Analysis(["YeManTdpCtl.py"], optimize=0)\n');f.write(binary,'CHANGED_SYNTHETIC_BINARY');const result=withoutExcludedContent([path.join(f.repo,binary)],()=>f.compare());assert.equal(result.status,'STALE');assert.deepEqual(result.sourceDiff.changed.map(row=>row.path),[recipe]);
});
// Policy 7: synthetic, owned static-release fixtures only; no PowerControl runtime discovery.
function releaseFixture(f){
 const policy=SNAPSHOT_POLICY.retainedStaticRelease,manifest={id:policy.id,version:policy.version,themes:policy.themeNames.map(name=>({name,author:'synthetic',version:'1'})),files:[]};
 const add=(relative,bytes)=>{const data=Buffer.from(bytes);f.write(policy.assetRoot+'/'+relative,data);manifest.files.push({path:relative,sha256:hash(data),bytes:data.length});};
 for(const name of policy.themeNames){add(name+'/theme.json',JSON.stringify({name,manifest_version:1}));add(name+'/config_USER.json','{"active":true}');}
 add(policy.themeNames[0]+'/focus/shared.css','/* synthetic release */\n');add(policy.themeNames[0]+'/LICENSE','Synthetic release license\n');
 const save=()=>f.write(policy.manifest,JSON.stringify(manifest,null,2));save();return {policy,manifest,add,save};
}
for(const [mode,capture]of [['strict',createSourceSnapshot],['same-descriptor',createCapturedSourceSnapshot]])test('policy7: static release exact declared assets retain bytes/hash and complete identity ('+mode+')',t=>{
 const f=fixture(t),pack=releaseFixture(f),r=capture({repoRoot:f.repo,outDir:f.out});assert.equal(r.status,'PASS',JSON.stringify(r.failure));assert.equal(r.policy.version,8);assert.equal(r.policy.retainedStaticRelease.consumedBy,'PowerControl/steam-appearance/preset.ps1::Read-PresetManifest');
 for(const entry of pack.manifest.files){const rel=pack.policy.assetRoot+'/'+entry.path,row=r.files.find(row=>row.path===rel);assert(row);assert.equal(row.size,entry.bytes);assert.equal(row.sha256,entry.sha256);assert(fs.readFileSync(path.join(f.repo,rel)).equals(fs.readFileSync(path.join(f.out,rel))));}
 const expected=[pack.policy.manifest,...pack.manifest.files.map(row=>pack.policy.assetRoot+'/'+row.path)].sort();assert.deepEqual(r.files.filter(row=>row.path.startsWith('PowerControl/steam-appearance/')).map(row=>row.path).sort(),expected);
 assert.equal(r.copyVerifiedFiles,r.files.length);assert.equal(f.compare().status,'CURRENT');const pin=hash(fs.readFileSync(r.manifestFile));assert.equal(verifyCapturedSourceSnapshot({manifestFile:r.manifestFile,repoRoot:f.repo,expectedManifestSha256:pin}).status,'FROZEN_SOURCE_VERIFIED');assert.equal(hash(fs.readFileSync(r.manifestFile)),pin);
});
for(const [label,mutate,code]of [
 ['wrong release id',p=>p.manifest.id='other-release','SNAPSHOT_STATIC_RELEASE_MANIFEST_INVALID'],
 ['unknown release version',p=>p.manifest.version=2,'SNAPSHOT_STATIC_RELEASE_MANIFEST_INVALID'],
 ['renamed theme',p=>p.manifest.themes[0].name='Pretend Theme','SNAPSHOT_STATIC_RELEASE_MANIFEST_INVALID'],
 ['duplicate theme',p=>p.manifest.themes[0]=p.manifest.themes[1],'SNAPSHOT_STATIC_RELEASE_MANIFEST_INVALID'],
 ['missing required template declaration',p=>p.manifest.files=p.manifest.files.filter(r=>r.path!==p.policy.themeNames[0]+'/config_USER.json'),'SNAPSHOT_STATIC_RELEASE_INCOMPLETE'],
 ['duplicate member',p=>p.manifest.files.push({...p.manifest.files[0]}),'SNAPSHOT_STATIC_RELEASE_MEMBER_DUPLICATED'],
 ['case alias duplicate',p=>p.manifest.files.push({...p.manifest.files.find(r=>r.path.endsWith('.css')),path:p.policy.themeNames[0]+'/focus/SHARED.css'}),'SNAPSHOT_STATIC_RELEASE_MEMBER_DUPLICATED'],
 ['bad SHA',p=>p.manifest.files[0].sha256='0'.repeat(64),'SNAPSHOT_STATIC_RELEASE_HASH_DIFFER'],
 ['bad hash encoding',p=>p.manifest.files[0].sha256='not-a-hash','SNAPSHOT_STATIC_RELEASE_MEMBER_IDENTITY'],
 ['wrong byte length',p=>p.manifest.files[0].bytes++,'SNAPSHOT_STATIC_RELEASE_BYTES_DIFFER'],
 ['negative bytes',p=>p.manifest.files[0].bytes=-1,'SNAPSHOT_STATIC_RELEASE_MEMBER_IDENTITY'],
 ['member byte ceiling',p=>p.manifest.files[0].bytes=p.policy.maxMemberBytes+1,'SNAPSHOT_STATIC_RELEASE_MEMBER_IDENTITY'],
 ['total byte ceiling',p=>{for(const r of p.manifest.files)r.bytes=p.policy.maxMemberBytes;},'SNAPSHOT_STATIC_RELEASE_TOTAL_LIMIT'],
 ['member count ceiling',p=>{p.manifest.files=Array.from({length:p.policy.maxMembers+1},()=>p.manifest.files[0]);},'SNAPSHOT_STATIC_RELEASE_MEMBER_LIMIT'],
 ['unknown JSON resource',p=>p.manifest.files[0].path=p.policy.themeNames[0]+'/settings.json','SNAPSHOT_STATIC_RELEASE_RESOURCE_DENIED'],
 ['unknown binary resource',p=>p.manifest.files[0].path=p.policy.themeNames[0]+'/payload.dll','SNAPSHOT_STATIC_RELEASE_RESOURCE_DENIED'],
 ['runtime config impersonation',p=>p.manifest.files[0].path='../backend.path','SNAPSHOT_RELATIVE_PATH_INVALID'],
 ['parent traversal',p=>p.manifest.files[0].path=p.policy.themeNames[0]+'/../escape.css','SNAPSHOT_RELATIVE_PATH_INVALID'],
 ['dot traversal',p=>p.manifest.files[0].path=p.policy.themeNames[0]+'/./escape.css','SNAPSHOT_RELATIVE_PATH_INVALID'],
 ['absolute path',p=>p.manifest.files[0].path='C:/owned/no.css','SNAPSHOT_RELATIVE_PATH_INVALID'],
 ['UNC path',p=>p.manifest.files[0].path='//server/share.css','SNAPSHOT_RELATIVE_PATH_INVALID'],
 ['backslash alias',p=>p.manifest.files[0].path=p.policy.themeNames[0]+'\\escape.css','SNAPSHOT_RELATIVE_PATH_INVALID'],
 ['alternate data stream',p=>p.manifest.files[0].path=p.policy.themeNames[0]+'/shared.css:stream','SNAPSHOT_STATIC_RELEASE_PATH_INVALID'],
 ['trailing dot alias',p=>p.manifest.files[0].path=p.policy.themeNames[0]+'/name./a.css','SNAPSHOT_STATIC_RELEASE_PATH_INVALID'],
 ['reserved device alias',p=>p.manifest.files[0].path=p.policy.themeNames[0]+'/NUL.css','SNAPSHOT_STATIC_RELEASE_PATH_INVALID'],
 ['unknown theme root',p=>p.manifest.files[0].path='Other/theme.json','SNAPSHOT_STATIC_RELEASE_RESOURCE_DENIED'],
])test('policy7: static release rejects '+label+' without claiming a complete copy',t=>{const f=fixture(t),p=releaseFixture(f);mutate(p);p.save();const r=f.create();assert.equal(r.status,'FAIL');assert.equal(r.failure.code,code,JSON.stringify(r.failure));assert.equal(r.isCurrent,false);assert.equal(readJson(r.manifestFile).status,'FAIL');});
test('policy7: static release missing declared member fails closed',t=>{const f=fixture(t),p=releaseFixture(f),rel=p.policy.assetRoot+'/'+p.manifest.files[0].path;fs.unlinkSync(path.join(f.repo,rel));const r=f.create();assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'SNAPSHOT_STATIC_RELEASE_MEMBER_MISSING');});
test('policy7: static release assets cannot be captured without their manifest',t=>{const f=fixture(t),p=releaseFixture(f);fs.unlinkSync(path.join(f.repo,p.policy.manifest));const r=f.create();assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'SNAPSHOT_STATIC_RELEASE_MANIFEST_REQUIRED');});
test('policy7: static release oversized manifest fails before its bytes are opened',t=>{const f=fixture(t),p=releaseFixture(f);f.write(p.policy.manifest,' '.repeat(p.policy.maxManifestBytes+1));withoutExcludedContent([path.join(f.repo,p.policy.manifest)],()=>{const r=f.create();assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'SNAPSHOT_STATIC_RELEASE_MANIFEST_LIMIT');});});
for(const member of ['surprise.css','settings.json','cache/__pycache__/hidden.py'])test('policy7: unlisted static subtree resource '+member+' is rejected without reading it',t=>{const f=fixture(t),p=releaseFixture(f),rel=p.policy.assetRoot+'/'+p.policy.themeNames[0]+'/'+member;f.write(rel,'MUST NOT OPEN');const guarded=member.includes('/')?path.dirname(path.dirname(path.join(f.repo,rel))):path.join(f.repo,rel);withoutExcludedContent([guarded],()=>{const r=f.create();assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'SNAPSHOT_STATIC_RELEASE_UNDECLARED_RESOURCE');});});
test('policy7: static release member and ancestor reparse links are rejected before target reads',t=>{const f=fixture(t),p=releaseFixture(f),outside=path.join(f.base,'owned-external');fs.mkdirSync(outside);fs.writeFileSync(path.join(outside,'shared.css'),'MUST NOT READ');const theme=path.join(f.repo,p.policy.assetRoot,p.policy.themeNames[0]);fs.symlinkSync(outside,path.join(theme,'linked'),'junction');withoutExcludedContent([outside],()=>{const r=f.create();assert.equal(r.status,'FAIL');assert.match(r.failure.code,/REPARSE/);});});
test('policy7: release templates do not authorize operational configuration or traversal',t=>{
 const f=fixture(t),p=releaseFixture(f),operational=['PowerControl/steam-appearance/backend.path','PowerControl/steam-appearance/state.json','PowerControl/steam-appearance/settings/device.json','PowerControl/steam-appearance/logs/session.log','PowerControl/steam-appearance/runtime/session.json','PowerControl/settings.json'];for(const rel of operational)f.write(rel,'SYNTHETIC OPERATIONAL DO NOT READ');
 const denied=[operational[0],operational[1],...['settings','logs','runtime'].map(n=>'PowerControl/steam-appearance/'+n),operational.at(-1)].map(n=>path.join(f.repo,n));
 withoutExcludedContent(denied,()=>{const r=captured(f),original=fs.readFileSync(r.manifestFile);for(const rel of operational)assert(!r.files.some(row=>row.path===rel));for(const name of ['settings','logs','runtime'])assert(r.excluded.some(row=>row.path==='PowerControl/steam-appearance/'+name&&row.rule==='steam-appearance-runtime'));assert(r.excluded.some(row=>row.path===operational[0]&&row.rule==='powercontrol-nonsource'));
  for(const rel of operational)f.write(rel,'SYNTHETIC OPERATIONAL CHURN');assert.equal(f.compare().status,'CURRENT');assert(fs.readFileSync(r.manifestFile).equals(original));assert(r.files.some(row=>row.path.endsWith('/config_USER.json')));
 });
});
test('policy7: static asset edit and rehashed release manifest remain stale against old frozen identity',t=>{const f=fixture(t),p=releaseFixture(f),r=captured(f),original=fs.readFileSync(r.manifestFile),row=p.manifest.files.find(r=>r.path.endsWith('.css')),bytes='/* changed release */\n';f.write(p.policy.assetRoot+'/'+row.path,bytes);row.bytes=Buffer.byteLength(bytes);row.sha256=hash(Buffer.from(bytes));p.save();const compared=f.compare();assert.equal(compared.status,'STALE');assert(compared.sourceDiff.changed.some(r=>r.path===p.policy.manifest));assert(compared.sourceDiff.changed.some(r=>r.path===p.policy.assetRoot+'/'+row.path));assert(fs.readFileSync(r.manifestFile).equals(original));});
test('policy7: static snapshot asset tampering cannot inherit capture approval or rewrite original manifest',t=>{const f=fixture(t),p=releaseFixture(f),r=captured(f),original=fs.readFileSync(r.manifestFile),pin=hash(original),row=p.manifest.files.find(r=>r.path.endsWith('.css'));fs.writeFileSync(path.join(f.out,p.policy.assetRoot,row.path),'/* tampered */');assert.equal(f.compare().status,'FAIL');assert.throws(()=>verifyCapturedSourceSnapshot({manifestFile:r.manifestFile,repoRoot:f.repo,expectedManifestSha256:pin}),/STATIC_RELEASE/);assert(fs.readFileSync(r.manifestFile).equals(original));});
for(const phase of ['source-before','before-capture-file'])test('policy7: static release drift at '+phase+' is not promoted to complete capture',t=>{const f=fixture(t),p=releaseFixture(f),row=p.manifest.files[0];let changed=false;const r=(phase==='source-before'?createSourceSnapshot:createCapturedSourceSnapshot)({repoRoot:f.repo,outDir:f.out,onProgress:e=>{if(!changed&&e.phase===phase&&(phase!=='before-capture-file'||e.path===p.policy.assetRoot+'/'+row.path)){changed=true;f.write(p.policy.assetRoot+'/'+row.path,'changed release bytes');}}});assert(changed);assert.equal(r.status,'FAIL');assert.equal(r.isCurrent,false);assert.match(r.failure.code,/STATIC_RELEASE|CHANGED_DURING_COPY|SOURCE_OR_COPY_DRIFT/);});

// Policy 8 includes only the two immutable config inputs, never personal state.
test('policy8: exact configuration baseline/default bytes freeze without reading mutable lookalikes',t=>{
 const f=fixture(t),immutable=['PowerControl/settings-baseline.json','PowerControl/settings-defaults-v1.json'];
 for(const rel of immutable)f.write(rel,'{"fixture":"'+rel+'"}\n');
 const mutable=['PowerControl/yeman-settings.json','PowerControl/yeman-settings.json.bak','PowerControl/settings-baseline.json.bak','PowerControl/settings-defaults-v9.json','PowerControl/ui-background/dynamic.json'];
 for(const rel of mutable)f.write(rel,'PRIVATE FIXTURE MUST NOT READ');
 withoutExcludedContent(mutable.map(rel=>path.join(f.repo,rel)),()=>{
  const r=captured(f);assert.equal(r.policy.version,8);
  for(const rel of immutable){const row=r.files.find(row=>row.path===rel);assert(row);assert.equal(row.sha256,hash(fs.readFileSync(path.join(f.repo,rel))));assert(fs.readFileSync(path.join(f.out,rel)).equals(fs.readFileSync(path.join(f.repo,rel))));}
  for(const rel of mutable)assert(!r.files.some(row=>row.path===rel));
 });
});
test('policy8: changing the configuration contract makes an old frozen build stale',t=>{
 const f=fixture(t),rel='PowerControl/settings-baseline.json';f.write(rel,'{"revision":1}');const r=captured(f),original=fs.readFileSync(r.manifestFile);f.write(rel,'{"revision":2}');
 const result=f.compare();assert.equal(result.status,'STALE');assert(result.sourceDiff.changed.some(row=>row.path===rel));assert(fs.readFileSync(r.manifestFile).equals(original));
});
