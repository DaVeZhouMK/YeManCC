// Declarations for the existing lighting leaf; no build, module import, Host or device execution.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const MODULE='tools/ymcc_sim/global/lighting-signals';
const need=(ok,code)=>{if(!ok)throw Error(code);};
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const hash=file=>digest(fs.readFileSync(file));
const key=file=>path.resolve(file).toLowerCase();
const inside=(root,file)=>{const relative=path.relative(path.resolve(root),path.resolve(file));return !path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep);};
const normalize=file=>file.replaceAll('\\','/');
function sources(root,relative,extensions){
 const dir=path.join(root,relative);if(!fs.existsSync(dir))return [];
 const result=[];
 for(const entry of fs.readdirSync(dir,{withFileTypes:true})){
  if(['bin','obj','.git','node_modules','__pycache__'].includes(entry.name))continue;
  const file=relative+'/'+entry.name;
  if(entry.isSymbolicLink())throw Error('LIGHTING_SOURCE_REPARSE:'+file);
  if(entry.isDirectory())result.push(...sources(root,file,extensions));else if(extensions.test(entry.name))result.push(file);
 }
 return result;
}
function resolveImport(repo,from,specifier){
 const base=specifier.startsWith('@/')?path.join(repo,'src',specifier.slice(2)):specifier.startsWith('.')?path.resolve(repo,path.dirname(from),specifier):null;
 if(!base)return null;need(inside(repo,base),'LIGHTING_SOURCE_IMPORT_ESCAPE');
 for(const file of [base,...['.ts','.js','.mjs','.vue','.json','/index.ts','/index.js'].map(ext=>base+ext)])if(fs.existsSync(file)&&fs.statSync(file).isFile())return normalize(path.relative(repo,file));
 throw Error('LIGHTING_SOURCE_IMPORT_MISSING:'+from+':'+specifier);
}
export function localImportClosure(repo,seeds){
 // The existing browser fixtures replace these complete modules with memory ports.
 const mocked=new Set(['@/bridge/ipc','@/bridge/settingsRepository','@/gamepad/focus']);
 const seen=new Set(),queue=[...seeds];
 while(queue.length){
  const file=queue.shift();if(seen.has(file))continue;seen.add(file);
  const absolute=path.resolve(repo,file);need(inside(repo,absolute),'LIGHTING_SOURCE_ESCAPE');if(!fs.existsSync(absolute))continue;
  if(!/\.(?:vue|m?js|ts)$/.test(file))continue;
  const text=fs.readFileSync(absolute,'utf8');
  const imports=[...text.matchAll(/(?:\bimport\s+(?:[^;'"\n]*?\s+from\s*)?|\bexport\s+[^;'"\n]*?\s+from\s*|\bimport\s*\(\s*)['"]([^'"]+)['"]/g)].map(match=>match[1]);
  for(const specifier of imports){if(mocked.has(specifier))continue;const target=resolveImport(repo,file,specifier);if(target)queue.push(target);}
 }
 return [...seen];
}
export function completeLightingSourceInputs(repo,legacy){
 const direct=['deps/ymcc-device-core-runtime/profile.json',MODULE+'/input-closure.mjs',MODULE+'/input-closure.test.mjs',MODULE+'/tool-inputs.json',
  'tools/fixtures/lightsetter_devicecore/ReplayCore.csproj','tools/fixtures/lightsetter_devicecore/AssemblyReferenceAudit.csproj',
  'FanLab/LightSetter/DeviceCore/YeManLightDeviceCore.csproj','FanLab/LightSetter/YeManLightSetter.json',
  'tools/oem_capability_evidence_matrix.py','deps/json/json.hpp'];
 const scanned=[...sources(repo,'FanLab/LightSetter',/\.(?:cs|csproj|json)$/),...sources(repo,'deps/handheldcompanion-runtime/source/Devices',/\.cs$/)];
 // ApplicationTrim.csproj's implicit compile includes only its root C# sources; its probe subprojects are excluded.
 const trim=path.join(repo,'tools/hc-application-dependency-trim');
 if(fs.existsSync(trim))for(const e of fs.readdirSync(trim,{withFileTypes:true}))if(e.isFile()&&e.name.endsWith('.cs'))direct.push('tools/hc-application-dependency-trim/'+e.name);
 for(const file of [...legacy,...direct,...scanned].filter(f=>f.endsWith('.csproj'))){
  for(let dir=path.dirname(path.resolve(repo,file));inside(repo,dir);dir=path.dirname(dir)){
   for(const name of ['Directory.Build.props','Directory.Build.targets','Directory.Packages.props','NuGet.Config','nuget.config','global.json']){const candidate=path.join(dir,name);if(fs.existsSync(candidate))direct.push(normalize(path.relative(repo,candidate)));}
   if(key(dir)===key(repo))break;
  }
 }
 const ui=localImportClosure(repo,['src/components/ControllerFeedbackCapabilitiesCard.vue','src/bridge/api.ts']);
 return [...new Set([...legacy,...direct,...scanned,...ui])];
}
export function resolveLightingRuntimeInputs(repo,options={}){
 const profilePath=path.join(repo,'deps/ymcc-device-core-runtime/profile.json');
 const runtimeRoot=options.runtimeRoot?path.resolve(options.runtimeRoot):path.join(repo,'deps/ymcc-device-core-runtime/bin');
 let sourceDirectory='deps/ymcc-device-core-runtime/PROFILE_SOURCE_REQUIRED';
 if(fs.existsSync(profilePath))sourceDirectory=read(profilePath).sourceDirectory;
 need(typeof sourceDirectory==='string'&&sourceDirectory.length>0,'LIGHTING_PROFILE_SOURCE_REQUIRED');
 const upstream=path.resolve(repo,sourceDirectory);need(inside(repo,upstream),'LIGHTING_PROFILE_SOURCE_ESCAPE');
 return {profilePath,runtimeRoot,hcCore:path.join(runtimeRoot,'HandheldCompanion.dll'),sharedDll:options.runtimeRoot?path.join(runtimeRoot,'Shared.dll'):path.join(upstream,'Shared.dll'),selection:options.runtimeRoot?'explicit-runtime-root':'repository-profile-overlay'};
}
export function describeLightingPayload(options={},{exists=fs.existsSync,hashFile=hash}={}){
 if(!options.hostPayloadRoot)return {requested:false,origin:'not-requested',file:null,actual:null,expectedReviewedSha256:null};
 const root=path.resolve(options.hostPayloadRoot),file=[path.join(root,'YeManLightSetter.dll'),path.join(root,'light-setter/YeManLightSetter.dll')].find(exists);
 return {requested:true,origin:'explicit-reviewed-external-payload',sourceBuildAcceptanceReused:false,file:file??null,actual:file?hashFile(file):null,expectedReviewedSha256:options.expectedHelperSha256?.toLowerCase()??null};
}
export function lightingToolDeclaration(repo,{environment=process.env}={}){
 const baselineFile=path.join(repo,MODULE,'tool-inputs.json');
 const selected=environment.YMCC_LIGHTING_TOOLS_MANIFEST,expected=environment.YMCC_LIGHTING_TOOLS_SHA256;
 need(Boolean(selected)===Boolean(expected),'LIGHTING_SELECTED_TOOL_PIN_REQUIRED');
 const manifestFile=selected?path.resolve(selected):baselineFile;
 if(selected)need(path.isAbsolute(selected)&&/^[a-f0-9]{64}$/.test(expected)&&hash(manifestFile)===expected,'LIGHTING_SELECTED_TOOL_MANIFEST_CHANGED');
 const manifest=read(manifestFile);
 if(selected)need(manifest.selectionStatus==='REVIEWED_HOST_DECLARATION'&&manifest.originalManifestSha256===hash(baselineFile),'LIGHTING_SELECTED_TOOL_REVIEW_REQUIRED');
 if(selected)need(typeof manifest.playwrightRequireAnchor==='string'&&key(manifest.playwrightRequireAnchor)===key(environment.YMCC_UI_TEST_REQUIRE_ANCHOR??'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_lighting.cjs'),'LIGHTING_BROWSER_MODULE_AUTHORITY_CHANGED');
 if(selected&&manifest.runnerImageVersion)need(environment.GITHUB_ACTIONS==='true'&&manifest.runnerImageVersion===environment.ImageVersion,'LIGHTING_SELECTED_RUNNER_IMAGE_CHANGED');
 need(manifest.schemaVersion===1&&manifest.kind==='ymcc-lighting-signal-selected-tools'&&manifest.maxArtifactInputs===3072&&Array.isArray(manifest.files),'LIGHTING_TOOL_MANIFEST_SCHEMA');
 const bindingFile=path.join(repo,'.ymcc-source-snapshot/node-tools-binding.json');
 let packageRepo=repo,binding,metadata=selected?[{path:baselineFile,sha256:hash(baselineFile)},{path:manifestFile,sha256:expected}]:[];
 if(fs.existsSync(bindingFile)){
  binding=read(bindingFile);
  need(binding.kind==='ymcc-snapshot-node-tools-binding'&&binding.status==='PASS'&&key(binding.snapshotRoot)===key(repo)&&binding.sourceApprovalGranted===false,'LIGHTING_NODE_BINDING_IDENTITY');
  need(binding.graphBeforeSha256===binding.graphAfterSha256&&digest(JSON.stringify(binding.graph))===binding.graphBeforeSha256,'LIGHTING_NODE_BINDING_GRAPH');
  const snapshotManifest=path.join(repo,'.ymcc-source-snapshot/manifest.json');
  need(hash(snapshotManifest)===binding.expectedSnapshotManifestSha256,'LIGHTING_NODE_SOURCE_MANIFEST_CHANGED');
  packageRepo=binding.sourceRoot;
  need(key(binding.graph.primaryRepo)===key(packageRepo),'LIGHTING_NODE_BINDING_ROOT');
  metadata=[...metadata,{path:bindingFile,sha256:hash(bindingFile)},{path:snapshotManifest,sha256:binding.expectedSnapshotManifestSha256},binding.graph.primaryPackageJson,...(binding.verifierSource?[binding.verifierSource]:[])];
 }
 const rows=manifest.files.map(row=>{
  need(/^[a-f0-9]{64}$/.test(row.sha256??''),'LIGHTING_TOOL_HASH_SCHEMA');
  if(row.repoRelative){need(row.repoRelative.startsWith('node_modules/')&&!row.repoRelative.split(/[\\/]/).includes('..'),'LIGHTING_NODE_TOOL_PATH');return {...row,path:path.resolve(packageRepo,row.repoRelative)};}
  need(typeof row.path==='string'&&path.isAbsolute(row.path)&&!/[\0\r\n]/.test(row.path),'LIGHTING_TOOL_ABSOLUTE_PATH');return {...row,path:path.resolve(row.path)};
 });
 const unique=new Map();for(const row of [...rows,...metadata]){need(!/[/\\]SOFT[/\\]YeMan(?:[/\\]|$)/i.test(row.path),'LIGHTING_INSTALLED_PAYLOAD_FORBIDDEN');const old=unique.get(key(row.path));need(!old||old.sha256===row.sha256,'LIGHTING_TOOL_CONFLICT');unique.set(key(row.path),row);}
 need(unique.size+2<=3072,'LIGHTING_TOOL_INPUT_LIMIT');
 return {manifestFile,manifest,files:[...unique.values()],packageRepo,binding:binding?{path:bindingFile,sha256:hash(bindingFile),graphSha256:binding.graphBeforeSha256,parentBindingStatus:'PASS',fullGraphReverifiedHere:false,sourceApprovalGranted:false}:null};
}
export function lightingArtifactInputs(repo){
 const runtime=resolveLightingRuntimeInputs(repo),tool=lightingToolDeclaration(repo);
 return [...new Set([runtime.hcCore,runtime.sharedDll,...tool.files.map(row=>row.path)])];
}
export function verifyLightingToolInputs(repo){
 const declaration=lightingToolDeclaration(repo);
 for(const row of declaration.files)need(fs.existsSync(row.path)&&fs.statSync(row.path).isFile()&&hash(row.path)===row.sha256,'LIGHTING_TOOL_INPUT_CHANGED:'+row.path);
 const runtime=resolveLightingRuntimeInputs(repo);
 for(const file of [runtime.hcCore,runtime.sharedDll])need(fs.existsSync(file)&&fs.statSync(file).isFile(),'LIGHTING_RUNTIME_INPUT_MISSING:'+file);
 const packageSnapshot=Object.fromEntries(declaration.files.map(row=>[row.path,row.sha256]));
 return {status:'PASS',toolFiles:declaration.files.length,scope:declaration.manifest.scope,nodeBinding:declaration.binding,runtimeSelection:runtime,executables:{node:declaration.manifest.node,dotnet:declaration.manifest.dotnet,python:declaration.manifest.python},packageSnapshot};
}
