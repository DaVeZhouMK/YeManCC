import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';

const profilePath='tools/ymcc_sim/global/controller-host-exit/tool-profile.json';
const supplementPath='tools/ymcc_sim/global/controller-signal-evidence/tool-inputs.json';
const authorityPath='tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json';
const key=file=>path.win32.resolve(file).toLowerCase();
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
export const nativeExitPythonFlags=Object.freeze(['-I','-S','-B','-X','utf8']);

export function nativeExitChildArgs(repo,route,output,expectedMainSha256,{fixedBuild=false}={}){
 const args=[...nativeExitPythonFlags,path.join(repo,route.consumer),route.arg,output];
 if(fixedBuild)args.push('--fixed-native-build');
 if(expectedMainSha256!==undefined){
  assert.match(expectedMainSha256,/^[a-f0-9]{64}$/,'NATIVE_EXIT_EXPLICIT_FREEZE_ARG');
  args.push('--expected-main-sha256',expectedMainSha256);
 }
 return args;
}

function fileBytes(file){
 const info=fs.lstatSync(file);
 assert(info.isFile()&&!info.isSymbolicLink()&&key(fs.realpathSync.native(file))===key(file),'NATIVE_EXIT_TOOL_REDIRECTED:'+file);
 return fs.readFileSync(file);
}

function readJson(repo,relative,expected){
 const bytes=fileBytes(path.join(repo,relative)), hash=sha(bytes);
 if(expected!==undefined)assert.equal(hash,expected,'NATIVE_EXIT_TOOL_DECLARATION_CHANGED:'+relative);
 return {value:JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,'')),hash};
}

export function readNativeExitTools(repo){
 const {value:profile,hash:profileSha256}=readJson(repo,profilePath);
 assert(profile.schemaVersion===1&&profile.kind==='controller-host-exit-tool-profile','NATIVE_EXIT_TOOL_PROFILE');
 for(const hash of [profile.nativeAuthoritySha256,profile.supplementSha256])assert.match(hash,/^[a-f0-9]{64}$/);
 const {value:supplement}=readJson(repo,supplementPath,profile.supplementSha256);
 const {value:authority}=readJson(repo,authorityPath,profile.nativeAuthoritySha256);
 assert.equal(supplement.nativeAuthority,authorityPath,'NATIVE_EXIT_NATIVE_AUTHORITY_PATH');
 const python=supplement.primary.python, pythonRoot=path.dirname(python);
 assert.equal(supplement.python,python,'NATIVE_EXIT_PYTHON_PRIMARY');
 const relativePython=file=>path.win32.relative(pythonRoot,file).replaceAll('\\','/');
 const inPython=rel=>rel&&!path.posix.isAbsolute(rel)&&!path.win32.isAbsolute(rel)&&!rel.split('/').some(p=>p==='..'||p==='.'||p==='');
 const inheritedPython=supplement.files.filter(row=>inPython(relativePython(row.path)));
 const members=new Set(inheritedPython.map(row=>relativePython(row.path)));
 for(const name of Object.keys(profile.pythonChanges))assert(members.has(name),'NATIVE_EXIT_UNKNOWN_PYTHON_OVERRIDE:'+name);
 const pythonRows=inheritedPython.filter(row=>!['Lib/site.py','Lib/_sitebuiltins.py','Lib/site-packages/_distutils_hack/__init__.py'].includes(relativePython(row.path)))
  .map(row=>({...row,sha256:profile.pythonChanges[relativePython(row.path)]??row.sha256}));
 for(const [name,sha256] of Object.entries(profile.pythonAdditional)){
  assert(inPython(name)&&!members.has(name),'NATIVE_EXIT_INVALID_PYTHON_ADDITION:'+name);
  pythonRows.push({path:path.join(pythonRoot,name),sha256});
 }
 // Isolated Python still has executable-relative startup lookups before any script runs.
 for(const name of ['python312.zip','python312._pth','python._pth','pyvenv.cfg','../pyvenv.cfg']){
  assert.throws(()=>fs.lstatSync(path.join(pythonRoot,name)),{code:'ENOENT'},'NATIVE_EXIT_PYTHON_STARTUP_OVERRIDE:'+name);
 }
 const node=supplement.primary.node;
 const sharedNode=supplement.files.find(row=>key(row.path)===key(node));
 assert(sharedNode&&authority.files.some(row=>key(row.path)===key(node)&&row.sha256===sharedNode.sha256),'NATIVE_EXIT_NODE_DECLARATION_CONFLICT');
 const batches=supplement.files.filter(row=>/\\vcvars(?:64|all)\.bat$/i.test(row.path));
 assert.equal(batches.length,2,'NATIVE_EXIT_VCVARS_ENTRY_COUNT');
 const vcvars=batches.find(row=>/\\vcvars64\.bat$/i.test(row.path))?.path;
 const {native}=authority;
 const sdkVersion=native.include.map(dir=>/\\Include\\(\d+\.\d+\.\d+\.\d+)\\/i.exec(dir)?.[1]).find(Boolean);
 assert(sdkVersion,'NATIVE_EXIT_SDK_VERSION');
 const primary={node,python,vcvars,compiler:native.compiler,linker:native.linker,dumpbin:native.dumpbin,commandProcessor:native.commandProcessor};
 const seen=new Map();
 for(const row of [...authority.files,...batches,...pythonRows,...profile.checkedLibraries]){
  assert(path.win32.isAbsolute(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256),'NATIVE_EXIT_TOOL_ROW');
  const name=key(row.path);
  assert(!seen.has(name),'NATIVE_EXIT_DUPLICATE_TOOL:'+row.path);
  assert.equal(sha(fileBytes(row.path)),row.sha256,'NATIVE_EXIT_TOOL_CHANGED:'+row.path);
  seen.set(name,{path:row.path,sha256:row.sha256});
 }
 for(const [role,file] of Object.entries(primary))assert(file&&seen.has(key(file)),'NATIVE_EXIT_TOOL_ROLE:'+role);
 return {python,vcvars,primary,native,sdkVersion,files:[...seen.values()],profileSha256};
}

export function nativeExitEnvironment(ambient,tools,nodeExecutable){
 assert.equal(key(nodeExecutable),key(tools.primary.node),'NATIVE_EXIT_NODE_IDENTITY');
 const windows=path.dirname(path.dirname(tools.primary.commandProcessor));
 const allowed=['SystemDrive','OS','PROCESSOR_ARCHITECTURE','PROCESSOR_ARCHITEW6432','NUMBER_OF_PROCESSORS',
  'ProgramFiles','ProgramFiles(x86)','ProgramW6432','CommonProgramFiles','CommonProgramFiles(x86)','CommonProgramW6432',
  'ProgramData','LOCALAPPDATA','APPDATA','USERPROFILE','USERNAME','HOMEDRIVE','HOMEPATH','PATHEXT'];
 const parent=new Map(Object.entries(ambient).map(([k,v])=>[k.toLowerCase(),v]));
 const env=Object.fromEntries(allowed.filter(k=>parent.has(k.toLowerCase())).map(k=>[k,parent.get(k.toLowerCase())]));
 return {...env,SystemRoot:windows,windir:windows,ComSpec:tools.primary.commandProcessor,VSCMD_SKIP_SENDTELEMETRY:'1',
  PATH:[path.dirname(tools.python),path.dirname(tools.primary.compiler),path.join(windows,'System32'),windows].join(';')};
}
