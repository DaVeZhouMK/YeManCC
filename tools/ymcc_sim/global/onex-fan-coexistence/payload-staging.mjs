import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const key=file=>path.resolve(file).replaceAll('\\','/').toLowerCase();
const required=['YeManFanHost.exe','YeManFanHost.dll','YeManFanBackend.dll','YeManFanHost.deps.json','YeManFanHost.runtimeconfig.json','YeManFanHost.json'];
const fail=message=>{throw Error('ONEX_DECLARED_PAYLOAD: '+message);};
function pin(value,label){if(typeof value!=='string'||!/^[a-f0-9]{64}$/.test(value))fail('An explicit SHA256 pin is required: '+label);return value;}
function relative(value){if(typeof value!=='string'||!value||path.isAbsolute(value)||value.includes(':')||value.includes('\0'))fail('Unsafe member path');const result=value.replaceAll('\\','/'),parts=result.split('/');if(parts.some(part=>!part||part==='.'||part==='..'||/[ .]$/.test(part)||/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(part)))fail('Unsafe member path: '+value);return result;}
function plain(file){for(let current=path.resolve(file);;current=path.dirname(current)){if(fs.existsSync(current)&&fs.lstatSync(current).isSymbolicLink())fail('Reparse/symlink path: '+current);if(path.dirname(current)===current)break;}}
function regular(root,member){const file=path.resolve(root,relative(member));if(!key(file).startsWith(key(root)+'/'))fail('Member escaped root');plain(file);if(!fs.existsSync(file)||!fs.lstatSync(file).isFile())fail('Missing regular member: '+member);return file;}
function exact(file,expected,label){pin(expected,label);plain(file);if(!fs.existsSync(file)||!fs.lstatSync(file).isFile()||hash(file)!==expected)fail('Missing or changed '+label+': '+file);return {path:path.resolve(file),sha256:expected};}
function members(document,root,label){if(!Array.isArray(document.files)||!document.files.length)fail('Empty '+label+' members');const seen=new Set();return document.files.map(row=>{const member=relative(label==='payload'?row.path:(row.packagedPath??row.path)),id=member.toLowerCase();if(seen.has(id))fail('Duplicate '+label+' member: '+member);seen.add(id);return {...exact(regular(root,member),row.sha256,label+' member'),relativePath:member};});}
function allFiles(root){plain(root);return fs.readdirSync(root,{withFileTypes:true}).flatMap(row=>{const file=path.join(root,row.name);if(row.isSymbolicLink())fail('Reparse/symlink member');return row.isDirectory()?allFiles(file):[file];});}
function strictPlan(options,buildRoot,candidateRoot){
 const {manifestPath,manifestSha256,candidateHostSha256,runtimeRoot,sharedRuntimeRoot,runtimeManifestSha256,sharedManifestSha256}=options;
 for(const [name,value]of Object.entries({buildRoot,candidateRoot,manifestPath,runtimeRoot,sharedRuntimeRoot}))if(typeof value!=='string'||!path.isAbsolute(value))fail('Explicit absolute path required: '+name);
 plain(buildRoot);plain(candidateRoot);plain(runtimeRoot);plain(sharedRuntimeRoot);
 const manifestPin=exact(manifestPath,manifestSha256,'payload manifest'),document=read(manifestPath);
 if(document.schemaVersion!==2||typeof document.runtimeId!=='string'||typeof document.runtimeManifest!=='string')fail('Original schemaVersion=2 runtime contract required');
 if(relative(document.runtimeId).includes('/')||document.runtimeManifest.replaceAll('\\','/')!=='../handheldcompanion-runtime/'+document.runtimeId+'/HandheldCompanion.runtime.json')fail('Original relative runtime manifest layout required');
 const runtimeManifest=path.join(runtimeRoot,'HandheldCompanion.runtime.json'),sharedManifest=path.join(sharedRuntimeRoot,'manifest.json');
 if(key(path.resolve(candidateRoot,document.runtimeManifest.replaceAll('\\','/')))!==key(runtimeManifest))fail('Declared runtime path does not match the explicitly bound runtime');
 if(key(path.resolve(runtimeRoot,'../../ymcc-shared-runtime'))!==key(sharedRuntimeRoot))fail('Shared runtime must keep its original PowerControl sibling layout');
 const runtimePin=exact(runtimeManifest,runtimeManifestSha256,'runtime manifest'),sharedPin=exact(sharedManifest,sharedManifestSha256,'shared manifest'),runtime=read(runtimeManifest),shared=read(sharedManifest);
 if(runtime.schemaVersion!==1||shared.schemaVersion!==1||runtime.runtimeId!==document.runtimeId)fail('Runtime/shared manifest identity mismatch');
 const runtimeFiles=members(runtime,runtimeRoot,'runtime'),sharedFiles=members(shared,sharedRuntimeRoot,'shared'),files=members(document,buildRoot,'payload'),seen=new Set(files.map(row=>row.relativePath.toLowerCase()));
 for(const name of required)if(!seen.has(name.toLowerCase()))fail('Required complete Host member omitted: '+name);
 if(seen.has('yemanfanhost.payload.json'))fail('Payload manifest cannot list itself');
 const extra=allFiles(buildRoot).filter(file=>key(file)!==key(path.join(buildRoot,'YeManFanHost.payload.json'))&&!seen.has(path.relative(buildRoot,file).replaceAll('\\','/').toLowerCase()));if(extra.length)fail('Undeclared build member: '+path.relative(buildRoot,extra[0]));
 const candidate=regular(buildRoot,'YeManFanHost.dll');exact(candidate,candidateHostSha256,'candidate Host');
 const deps=read(regular(buildRoot,'YeManFanHost.deps.json'));
 if(!deps.targets||!Object.keys(deps.targets).length)fail('Host deps graph is empty');
 for(const target of Object.values(deps.targets))for(const library of Object.values(target))for(const bucket of ['runtime','native','runtimeTargets'])for(const member of Object.keys(library[bucket]??{})){if(/\.(dll|exe)$/i.test(member)&&!seen.has(relative(member).toLowerCase()))fail('Deps graph member omitted: '+member);}
 return {manifestPin,runtimePin,sharedPin,document,files,runtimeFiles,sharedFiles,candidateHostSha256,inputPins:[manifestPin,runtimePin,sharedPin,...files,...runtimeFiles,...sharedFiles]};
}
export function verifyDeclaredHostPayload(options){
 const root=path.resolve(options.payloadRoot);
 if(key(options.manifestPath)!==key(path.join(root,'YeManFanHost.payload.json')))fail('Verification must bind the actual embedded payload manifest');
 const plan=strictPlan(options,root,root);
 return {status:'PASS_STRICT_DECLARED_OWNED_PAYLOAD',scope:'Complete immutable test payload and runtime/shared metadata only; no execution or source-template approval',payloadRoot:root,manifestPath:plan.manifestPin.path,manifestSha256:plan.manifestPin.sha256,candidateHostSha256:plan.candidateHostSha256,runtimeRoot:path.resolve(options.runtimeRoot),sharedRuntimeRoot:path.resolve(options.sharedRuntimeRoot),payloadMembers:plan.files,runtimeMembers:plan.runtimeFiles,sharedMembers:plan.sharedFiles,inputPins:plan.inputPins,hostMainStarted:false,hardwareIo:false};
}
export function stageDeclaredHostPayload(options){
 if(!options.manifestPath)return stageLegacyDeclaredHostPayload(options);
 const {buildRoot,targetRoot}=options;if(typeof targetRoot!=='string'||!path.isAbsolute(targetRoot))fail('Explicit absolute fresh target required');
 if(fs.existsSync(targetRoot))fail('Candidate staging requires a fresh target');
 if(key(targetRoot)===key(buildRoot)||key(targetRoot).startsWith(key(buildRoot)+'/'))fail('Stage target must be separate from the build');
 const plan=strictPlan(options,buildRoot,targetRoot); // No output directory or child can precede complete validation.
 fs.mkdirSync(targetRoot,{recursive:true});
 for(const row of plan.files){const target=path.join(targetRoot,row.relativePath);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(row.path,target);exact(target,row.sha256,'staged member');}
 fs.copyFileSync(options.manifestPath,path.join(targetRoot,'YeManFanHost.payload.json'));
 for(const row of plan.inputPins)exact(row.path,row.sha256,'source unchanged after stage');
 const verified=verifyDeclaredHostPayload({...options,payloadRoot:targetRoot,manifestPath:path.join(targetRoot,'YeManFanHost.payload.json')});
 return {...verified,status:'PASS_STRICT_OWNED_PAYLOAD_STAGED',manifestOrigin:'explicit-owned-test-manifest',buildRoot:path.resolve(buildRoot),targetRoot:path.resolve(targetRoot),inputPins:plan.inputPins,sourceTemplateUsed:false};
}

function stageLegacyDeclaredHostPayload({buildRoot,templateRoot,targetRoot}){
 if(fs.existsSync(targetRoot))throw Error('Candidate staging requires a fresh target');fs.mkdirSync(targetRoot,{recursive:true});
 const template=path.join(templateRoot,'YeManFanHost.payload.json'),manifest=JSON.parse(fs.readFileSync(template,'utf8').replace(/^\uFEFF/,''));
 if(manifest.schemaVersion!==2||!Array.isArray(manifest.files)||!manifest.files.length)throw Error('Current declared Host payload manifest is required');
 const support=new Set(['install-fan-host-payload.ps1','run-emergency-fan-restore.ps1','YeManFanHost.authorization.md']);
 const pins=[{path:template,sha256:hash(template)}],seen=new Set();
 for(const entry of manifest.files){
  const relative=entry.path;if(typeof relative!=='string'||!relative||path.isAbsolute(relative)||relative.includes(':')||relative.split(/[\\/]/).includes('..'))throw Error('Unsafe candidate staging member');
  const key=relative.replaceAll('\\','/').toLowerCase();if(seen.has(key))throw Error('Duplicate candidate staging member');seen.add(key);
  const source=path.join(support.has(relative)?templateRoot:buildRoot,relative);if(!fs.existsSync(source)||!fs.lstatSync(source).isFile()||fs.lstatSync(source).isSymbolicLink())throw Error('Declared current build/support member is missing: '+relative);
  const pin=hash(source),target=path.join(targetRoot,relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(source,target);
  if(hash(source)!==pin||hash(target)!==pin)throw Error('Candidate source changed during staging: '+relative);pins.push({path:source,sha256:pin});entry.sha256=pin;if(Object.hasOwn(entry,'bytes'))entry.bytes=fs.statSync(target).size;
 }
 for(const required of ['YeManFanHost.exe','YeManFanHost.dll','YeManFanBackend.dll','YeManFanHost.deps.json','YeManFanHost.runtimeconfig.json','YeManFanHost.json'])if(!seen.has(required.toLowerCase()))throw Error('Declared Host contract omits required current build member: '+required);
 fs.writeFileSync(path.join(targetRoot,'YeManFanHost.payload.json'),JSON.stringify(manifest,null,2));
 const unstagedBuildFiles=fs.readdirSync(buildRoot,{withFileTypes:true}).filter(item=>item.isFile()&&!seen.has(item.name.toLowerCase())).map(item=>item.name).sort();
 return{status:'PASS',scope:'Isolated candidate staged from exact declared Host build/support set; no publish or source/package pruning',buildRoot,templateRoot,targetRoot,stagedMembers:[...seen],unstagedBuildFiles,inputPins:pins};
}
