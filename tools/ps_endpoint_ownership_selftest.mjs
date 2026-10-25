import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {buildManagedFixture} from './input_lifecycle_managed_fixture.mjs';
const args=process.argv.slice(2);
if(args.length!==2||args[0]!=='--out'||!path.isAbsolute(args[1]))throw Error('Use --out ABS_NEW_DIRECTORY under ps-instance-ownership-audit01');
const out=path.resolve(args[1]);
if(fs.existsSync(out))throw Error('Fresh output required');
const parent=path.resolve('G:/YeManCC-Work/Build/Validation/VirtualController-Lifecycle-20261019/ps-instance-ownership-audit01');
const rel=path.relative(parent,out);if(!rel||rel==='..'||rel.startsWith('..'+path.sep)||path.isAbsolute(rel))throw Error('Ownership test output outside authorized audit directory');
const build=buildManagedFixture(out,{psOwnershipSelfTest:true});
const run=spawnSync('dotnet',[build.fixtureDll],{cwd:out,encoding:'utf8',windowsHide:true,timeout:45000,env:{...process.env,DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_NOLOGO:'1'}});
fs.writeFileSync(path.join(out,'stdout.log'),run.stdout??'',{flag:'wx'});fs.writeFileSync(path.join(out,'stderr.log'),run.stderr??'',{flag:'wx'});
let observed;try{observed=JSON.parse(run.stdout.trim());}catch{throw Error('Private helper/session result missing: '+out);}
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const manifest=JSON.parse(fs.readFileSync(build.manifestPath,'utf8'));
const changed=manifest.inputBindings.sources.filter(r=>hash(r.path)!==r.sha256||hash(r.snapshot)!==r.sha256).map(r=>r.relative);
const result={...observed,status:run.status===0&&observed.status==='PASS'&&changed.length===0?'PASS':'FAIL',exitCode:run.status,error:run.error?.message??null,changed,sourceStable:changed.length===0,helperCompiledVerbatim:true,realHostSessionConsumed:true,normalHostMainExecuted:false,realSdkLoaded:false,hardwareIo:false,buildManifest:build.manifestPath,buildManifestSha256:hash(build.manifestPath)};
fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({status:result.status,checks:result.checks,cases:result.cases.length,failures:result.failures,changed,result:path.join(out,'RESULT.json')},null,2));
process.exitCode=result.status==='PASS'?0:1;

