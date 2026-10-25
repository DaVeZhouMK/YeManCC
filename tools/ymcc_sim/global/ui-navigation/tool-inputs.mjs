import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
export const UI_TOOL_INPUT_LIMIT=3072;
export const UI_REPO_ROOT=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const DEFAULT_MANIFEST=fileURLToPath(new URL('./toolchain-inputs.json',import.meta.url));
const need=(ok,message)=>{if(!ok)throw Error(message);};
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const key=p=>path.resolve(p).toLowerCase();
const badText=s=>[0,10,13].some(n=>s.includes(String.fromCharCode(n)));
const inside=(base,p)=>{const r=path.relative(path.resolve(base),path.resolve(p));return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
function resolved(spec,repoRoot){
 need(spec&&typeof spec==='object'&&Number(typeof spec.path==='string')+Number(typeof spec.repoRelative==='string')===1,'UI_TOOL_PATH_SCHEMA');
 if(spec.repoRelative!==undefined){need(spec.repoRelative.length>0&&!path.isAbsolute(spec.repoRelative)&&!/[\0\r\n]/.test(spec.repoRelative)&&!spec.repoRelative.split(/[\\/]/).includes('..'),'UI_TOOL_REPO_PATH_INVALID');const p=path.resolve(repoRoot,spec.repoRelative);need(inside(repoRoot,p),'UI_TOOL_REPO_ESCAPE');return p;}
 need(path.isAbsolute(spec.path)&&!/[\0\r\n]/.test(spec.path),'UI_TOOL_ABSOLUTE_PATH_REQUIRED');return path.resolve(spec.path);
}
export function validateToolManifest(manifest,{repoRoot=UI_REPO_ROOT}={}){
 need(manifest?.schemaVersion===1&&manifest.kind==='ymcc-ui-navigation-selected-toolchain'&&manifest.caseId==='controller.ui-navigation','UI_TOOL_MANIFEST_SCHEMA');
 need(manifest.maxArtifactInputs===UI_TOOL_INPUT_LIMIT&&Array.isArray(manifest.files)&&manifest.files.length>0&&manifest.files.length<=UI_TOOL_INPUT_LIMIT,'UI_TOOL_INPUT_LIMIT');
 const files=manifest.files.map(row=>{need(/^[a-f0-9]{64}$/.test(row.sha256??''),'UI_TOOL_HASH_SCHEMA');return {...row,path:resolved(row,repoRoot)};});
 const byPath=new Map(files.map(x=>[key(x.path),x]));need(byPath.size===files.length,'UI_TOOL_DUPLICATE_FILE');
 const executables={};for(const name of ['node','powershell','edge','esbuild']){const spec=manifest.executables?.[name],p=resolved(spec,repoRoot);need(path.extname(p).toLowerCase()==='.exe','UI_TOOL_EXECUTABLE_PATH_INVALID:'+name);need(/^[a-f0-9]{64}$/.test(spec.sha256??'')&&byPath.get(key(p))?.sha256===spec.sha256,'UI_TOOL_EXECUTABLE_NOT_DECLARED:'+name);executables[name]=p;}
 need(Array.isArray(manifest.directories)&&manifest.directories.length>0,'UI_TOOL_DIRECTORIES_REQUIRED');
 const directories=manifest.directories.map(x=>{need(x.membership==='complete-recursive-files-no-reparse','UI_TOOL_DIRECTORY_POLICY');return {...x,path:resolved(x,repoRoot)};});
 need(new Set(directories.map(x=>key(x.path))).size===directories.length,'UI_TOOL_DUPLICATE_DIRECTORY');
 need(typeof manifest.playwrightModuleRoot==='string'&&path.isAbsolute(manifest.playwrightModuleRoot)&&typeof manifest.playwrightCreateRequireAnchor==='string'&&path.dirname(manifest.playwrightCreateRequireAnchor)===manifest.playwrightModuleRoot,'UI_TOOL_BROWSER_RESOLVER_ANCHOR');
 for(const name of ['playwright','playwright-core'])need(directories.some(x=>key(x.path)===key(path.join(manifest.playwrightModuleRoot,name))),'UI_TOOL_BROWSER_PACKAGE_OMITTED:'+name);
 need(Array.isArray(manifest.sourceMembers)&&manifest.sourceMembers.includes('tools/ymcc_sim/global/ui-navigation/templates/build.mjs')&&manifest.sourceMembers.includes('tools/ymcc_sim/global/ui-navigation/tool-inputs.mjs'),'UI_TOOL_FORMAL_SOURCE_REPLACEMENT_REQUIRED');
 for(const relative of manifest.sourceMembers)resolved({repoRelative:relative},repoRoot);
 need(manifest.sourceReplacement?.historicalDObserverIsNotExecutedOrDeclaredAsAToolHere===true,'UI_TOOL_D_OBSERVER_NOT_REPLACED');
 return {manifest,repoRoot:path.resolve(repoRoot),files,byPath,executables,directories};
}
export function loadUiToolDeclaration({repoRoot=UI_REPO_ROOT,manifestPath=DEFAULT_MANIFEST}={}){
 const manifest=JSON.parse(fs.readFileSync(manifestPath,'utf8').replace(/^\uFEFF/,''));return {...validateToolManifest(manifest,{repoRoot}),manifestPath:path.resolve(manifestPath),manifestSha256:hash(manifestPath)};
}
// Descriptor enumeration validates only repository metadata. Runtime files may be absent
// when an unrelated selected case (for example sleep) is being planned.
export function getUiNavigationArtifactInputs(repoRoot=UI_REPO_ROOT){
 const manifest=JSON.parse(fs.readFileSync(DEFAULT_MANIFEST,'utf8').replace(/^\uFEFF/,''));
 return validateToolManifest(manifest,{repoRoot}).files.map(x=>x.path);
}
// Resolve only a package already fully declared by the UI tool manifest. This
// avoids requiring an unrelated alias in a captured source tree's node_modules.
export function resolveUiToolPackage({name,repoRoot=UI_REPO_ROOT,manifestPath=process.env.YMCC_UI_TOOL_MANIFEST??DEFAULT_MANIFEST}={}){
 need(typeof name==='string'&&/^(?:@[a-z0-9_.-]+\/)?[a-z0-9_.-]+$/i.test(name),'UI_TOOL_PACKAGE_NAME_INVALID');
 const d=loadUiToolDeclaration({repoRoot,manifestPath}),matches=[];
 for(const row of d.files){if(path.basename(row.path)!=='package.json')continue;verifyDeclaredFile(row);const p=JSON.parse(fs.readFileSync(row.path,'utf8'));if(p.name===name)matches.push(row);}
 need(matches.length===1,'UI_TOOL_PACKAGE_NOT_UNIQUELY_DECLARED:'+name);
 const entry=createRequire(matches[0].path).resolve(name),row=d.byPath.get(key(entry));
 need(row&&inside(path.dirname(matches[0].path),entry),'UI_TOOL_PACKAGE_ENTRY_UNDECLARED:'+name);verifyDeclaredFile(row);return entry;
}
export function verifyDeclaredFile(row){need(fs.existsSync(row.path)&&fs.lstatSync(row.path).isFile()&&!fs.lstatSync(row.path).isSymbolicLink()&&hash(row.path)===row.sha256,'UI_TOOL_FILE_CHANGED:'+row.path);return true;}
function walk(dir){need(fs.lstatSync(dir).isDirectory()&&!fs.lstatSync(dir).isSymbolicLink(),'UI_TOOL_DIRECTORY_INVALID:'+dir);const files=[];for(const e of fs.readdirSync(dir,{withFileTypes:true})){const p=path.join(dir,e.name);need(!e.isSymbolicLink(),'UI_TOOL_REPARSE_MEMBER:'+p);if(e.isDirectory())files.push(...walk(p));else{need(e.isFile(),'UI_TOOL_NONFILE_MEMBER:'+p);files.push(p);}}return files;}
export function verifyUiToolDeclaration(declaration){
 const {files,directories,byPath}=declaration;for(const row of files)verifyDeclaredFile(row);
 const membership=[];for(const dir of directories){const observed=walk(dir.path).map(key).sort(),expected=files.filter(x=>inside(dir.path,x.path)).map(x=>key(x.path)).sort();need(observed.length===expected.length&&observed.every((p,i)=>p===expected[i]),'UI_TOOL_DIRECTORY_MEMBERSHIP_CHANGED:'+dir.path);membership.push({path:dir.path,files:observed.length});}
 need(byPath.size===files.length,'UI_TOOL_DECLARATION_INCOMPLETE');return {verifiedFiles:files.length,membership,manifestSha256:declaration.manifestSha256};
}
function sourceHashes(declaration){const rows={};for(const relative of declaration.manifest.sourceMembers){const p=path.join(declaration.repoRoot,relative);need(fs.existsSync(p)&&fs.statSync(p).isFile(),'UI_TOOL_FORMAL_SOURCE_MISSING:'+relative);rows[relative]=hash(p);}return rows;}
function writeNew(p,value){fs.writeFileSync(p,JSON.stringify(value,null,2)+'\n',{flag:'wx'});}
// This product catalog is imported by the real game-title consumer outside src/.
// Keep the exception exact; other payload paths remain undeclared and rejected.
export function isUiProductBundleInput(sourceRoot,p){return inside(path.join(sourceRoot,'src'),p)||inside(path.join(sourceRoot,'public'),p)||key(p)===key(path.join(sourceRoot,'PowerControl/GameTitlePolicy.json'))||key(p)===key(path.join(sourceRoot,'PowerControl/settings-baseline.json'))||key(p)===key(path.join(sourceRoot,'decky-plugin/src/performanceModel.ts'));}
// Node resolves package junctions to their physical path. Admit only a file
// already pinned in this declaration, then verify both the declared and loaded
// path. Equal bytes at an unrelated path never grant package membership.
function canonicalTools(declaration){
 const files=new Map();for(const row of declaration.files){const physical=fs.realpathSync(row.path),existing=files.get(key(physical));need(!existing||existing.sha256===row.sha256,'UI_TOOL_CANONICAL_HASH_COLLISION');files.set(key(physical),row);}return files;
}
function admittedTool(declaration,physicalFiles,p){
 const row=declaration.byPath.get(key(p))||physicalFiles.get(key(p));if(!row)return;
 verifyDeclaredFile(row);need(key(fs.realpathSync(row.path))===key(fs.realpathSync(p))&&hash(p)===row.sha256,'UI_TOOL_LOADED_PATH_CHANGED:'+p);return row;
}
export function observeUiNodeStage({stage,requireCache,bundleInputs=[],sourceRoot,evidenceRoot}){
 need(['build','verify'].includes(stage),'UI_TOOL_STAGE_INVALID');
 const manifestPath=process.env.YMCC_UI_TOOL_MANIFEST;need(manifestPath&&process.env.YMCC_UI_TOOL_HELPER_URL,'UI_TOOL_STAGE_CONTEXT_REQUIRED');
 const declaration=loadUiToolDeclaration({repoRoot:sourceRoot,manifestPath});need(key(process.execPath)===key(declaration.executables.node),'UI_TOOL_NODE_EXECUTABLE_MISMATCH');
 const physicalFiles=canonicalTools(declaration);
 const cjs=[];for(const module of Object.values(requireCache)){if(!module?.filename||!fs.existsSync(module.filename))continue;const p=path.resolve(module.filename),row=admittedTool(declaration,physicalFiles,p);need(row,'UI_TOOL_UNDECLARED_CJS_MEMBER:'+p);verifyDeclaredFile(row);cjs.push({path:p,sha256:row.sha256});}
 const bundled=[];for(const input of bundleInputs){const p=path.resolve(sourceRoot,input);if(!fs.existsSync(p))continue;const tool=admittedTool(declaration,physicalFiles,p);if(tool)verifyDeclaredFile(tool);else need(!p.toLowerCase().includes('node_modules')&&(isUiProductBundleInput(sourceRoot,p)||inside(evidenceRoot,p)),'UI_TOOL_UNDECLARED_BUNDLE_MEMBER:'+p);bundled.push({path:p,sha256:hash(p),role:tool?'declared-tool':'current-source-or-owned-fixture'});}
 const report={schemaVersion:1,status:'PASS',stage,node:{path:process.execPath,pid:process.pid,sha256:hash(process.execPath)},manifestSha256:declaration.manifestSha256,actualCjsModules:cjs,actualBundleInputs:bundled,sourceHashes:sourceHashes(declaration),physicalHardware:false};writeNew(path.join(evidenceRoot,stage+'-tool-runtime.json'),report);return declaration;
}
export function verifyUiNodeAndCjs(declaration,requireCache){
 need(key(process.execPath)===key(declaration.executables.node),'UI_TOOL_NODE_EXECUTABLE_MISMATCH');
 const physicalFiles=canonicalTools(declaration);
 const rows=[];for(const module of Object.values(requireCache)){if(!module?.filename||!fs.existsSync(module.filename))continue;const row=admittedTool(declaration,physicalFiles,module.filename);need(row,'UI_TOOL_UNDECLARED_CJS_MEMBER:'+module.filename);verifyDeclaredFile(row);rows.push({path:path.resolve(module.filename),sha256:row.sha256});}return rows;
}
export function checkUiBrowserTerminal(declaration,browserProcess,terminal){need(browserProcess.pid>0&&key(browserProcess.spawnfile)===key(declaration.executables.edge)&&terminal.exitCode===0&&terminal.signal===null,'UI_TOOL_BROWSER_TERMINAL_OR_EXECUTABLE_MISMATCH');verifyDeclaredFile(declaration.byPath.get(key(declaration.executables.edge)));return {pid:browserProcess.pid,path:browserProcess.spawnfile,exitCode:terminal.exitCode,signal:terminal.signal,ownedHeadlessBrowser:true};}
function parseArgs(argv){const o={};for(let i=0;i<argv.length;i+=2){need(argv[i]?.startsWith('--')&&typeof argv[i+1]==='string','UI_TOOL_CLI_ARGUMENTS');const k=argv[i].slice(2);need(!Object.hasOwn(o,k),'UI_TOOL_DUPLICATE_ARGUMENT');o[k]=argv[i+1];}return o;}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 let output=null;try{
  const args=parseArgs(process.argv.slice(2));need(['pre','post'].includes(args.phase)&&path.isAbsolute(args['evidence-dir']??'')&&path.isAbsolute(args['repo-root']??''),'UI_TOOL_CLI_SCOPE');
  output=path.join(args['evidence-dir'],'tool-'+(args.phase==='pre'?'preflight':'postflight')+'.json');
  const d=loadUiToolDeclaration({repoRoot:args['repo-root'],manifestPath:args.manifest??DEFAULT_MANIFEST});need(!inside(d.repoRoot,args['evidence-dir']),'UI_TOOL_EVIDENCE_OUTSIDE_REPO_REQUIRED');
  need(key(process.execPath)===key(d.executables.node)&&key(args['powershell-path']??'')===key(d.executables.powershell),'UI_TOOL_ACTUAL_EXECUTABLE_MISMATCH');
  const result=verifyUiToolDeclaration(d);const report={schemaVersion:1,status:'PASS',phase:args.phase,...result,node:{path:process.execPath,pid:process.pid},powershell:args['powershell-path'],sourceHashes:sourceHashes(d),nativeOrBrowserBehaviorAcceptance:false};writeNew(output,report);console.log(JSON.stringify({status:'PASS',phase:args.phase,verifiedFiles:result.verifiedFiles}));
 }catch(error){if(output&&!fs.existsSync(output))writeNew(output,{schemaVersion:1,status:'FAIL',error:error.message});console.error(error.stack);process.exitCode=2;}
}
