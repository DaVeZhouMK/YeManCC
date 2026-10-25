// Offline pure-header regression. Does not enumerate HID, construct HC, or run
// application/Host/driver/power operations. All generated files stay in OneXProtocol.
// node tools/hc_onex_x2_protocol_selftest.mjs [--out <OneXProtocol child>] [--vcvars <local batch>]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn, spawnSync } from 'node:child_process';
import { deriveOracle, cppOracle, sha } from './fixtures/hc_onex_x2_protocol/oracle.mjs';

const runner = fileURLToPath(import.meta.url), repo = path.resolve(path.dirname(runner), '..');
const fixtures = path.join(repo, 'tools/fixtures/hc_onex_x2_protocol');
let workspace = repo;
while (!fs.existsSync(path.join(workspace, 'Docs/Research/PcCapabilities-HC-CTW-20261014/upstream/source-manifest.json'))) {
  const next = path.dirname(workspace); if (next === workspace) throw Error('Fixed local HC inputs unavailable'); workspace = next;
}
const options = {}, args = process.argv.slice(2);
for (let i=0; i<args.length; ++i) {
  if (['--out','--vcvars'].includes(args[i]) && args[i+1]) options[args[i].slice(2)] = args[++i];
  else throw Error(`Unknown/incomplete argument ${args[i]}`);
}
const validation = path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/OneXProtocol');
const out = path.resolve(options.out || path.join(validation,new Date().toISOString().replace(/[:.]/g,'-')));
const inside = (base,target) => {const relative=path.relative(base,target);return relative==='' || (!path.isAbsolute(relative) && relative!=='..' && !relative.startsWith(`..${path.sep}`));};
if (!inside(validation,out)) throw Error('Output must stay under Build/Validation/HC-ModelCapabilities-20261017/OneXProtocol');
// Resolve existing parents before creating output, so junctions cannot redirect it.
fs.mkdirSync(validation,{recursive:true});
const realRoot=fs.realpathSync(validation);
if (!inside(path.resolve(validation),realRoot)) throw Error('Validation root was redirected');
let parent=out;
while (!fs.existsSync(parent)) parent=path.dirname(parent);
if (!inside(realRoot,fs.realpathSync(parent))) throw Error('Output parent was redirected');
fs.mkdirSync(out,{recursive:true});
if (fs.existsSync(path.join(out,'source-bindings.json'))) throw Error('Choose a fresh output directory to preserve earlier evidence');
const receipt={schemaVersion:1,status:'running',startedAt:new Date().toISOString(),scope:'pure protocol header only',
  hardwareIo:false,network:false,hcConstructed:false,runtimeEnabled:false,configurations:[],mutations:[]};
const save=()=>fs.writeFileSync(path.join(out,'source-bindings.json'),JSON.stringify(receipt,null,2)+'\n');
save();
const logResult=(log,result)=>{
  fs.writeFileSync(path.join(out,log),(result.stdout || '')+(result.stderr || ''));
  if(result.error)throw result.error;
  return result;
};
const run=(exe,argv,log,cwd=out,env=process.env,timeout=180000)=>{
  const result=logResult(log,spawnSync(exe,argv,{cwd,env,encoding:'utf8',windowsHide:true,timeout,maxBuffer:8*1024*1024}));
  if(result.status!==0)throw Error(`${log}: process exit ${result.status}`);
  return result.stdout;
};
async function compile(exe,argv,log,cwd,env) {
  const result=await new Promise((resolve,reject)=>{
    const child=spawn(exe,argv,{cwd,env,windowsHide:true,stdio:['ignore','pipe','pipe']});
    let stdout='',stderr=''; const timer=setTimeout(()=>{child.kill();reject(Error(`${log}: compile timeout`));},180000);
    child.stdout.on('data',b=>stdout+=b);child.stderr.on('data',b=>stderr+=b);
    child.on('error',e=>{clearTimeout(timer);reject(e);});
    child.on('close',status=>{clearTimeout(timer);resolve({status,stdout,stderr});});
  });
  logResult(log,result);
  if(result.status!==0)throw Error(`${log}: compiler exit ${result.status}`);
}
try {
  const original=path.join(repo,'native/onex_x2_protocol.h'), source=fs.readFileSync(original,'utf8');
  const fixturePaths=['source-pins.json','oracle.mjs','harness.cpp','README.md'].map(p=>path.join(fixtures,p));
  receipt.inputs=[runner,original,...fixturePaths].map(p=>({path:p,sha256:sha(fs.readFileSync(p))}));
  const includes=[...source.matchAll(/^\s*#\s*include\s*<([^>]+)>/gm)].map(m=>m[1]);
  if(JSON.stringify(includes)!==JSON.stringify(['array','cstddef','cstdint','optional','string_view']) || /^\s*#\s*include\s*"/m.test(source))
    throw Error('Header dependency boundary changed');
  const o=deriveOracle(workspace,fixtures);
  receipt.upstream={commit:o.commit,manifestSha256:o.manifestSha256,treeSha256:o.treeSha256,sourcePinsSha256:o.sourcePinsSha256,files:o.provenance};
  receipt.oracleSha256=sha(JSON.stringify(o));
  fs.writeFileSync(path.join(out,'expectations.json'),JSON.stringify(o,null,2)+'\n');
  fs.writeFileSync(path.join(out,'oracle.generated.h'),cppOracle(o));
  fs.writeFileSync(path.join(out,'onex_x2_protocol.h'),source);
  fs.copyFileSync(path.join(fixtures,'harness.cpp'),path.join(out,'harness.cpp'));
  fs.writeFileSync(path.join(out,'standalone.cpp'),'#include "onex_x2_protocol.h"\nstatic_assert(ymcc::onex_x2::clickOnly(ymcc::onex_x2::Button::Home));\nint main() { return 0; }\n');
  receipt.boundSourceSha256=sha(source); save();
  let vcvars=options.vcvars;
  if(!vcvars) {
    const vswhere=path.join(process.env['ProgramFiles(x86)'] || 'C:/Program Files (x86)','Microsoft Visual Studio/Installer/vswhere.exe');
    const install=run(vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],'toolchain-discovery.log').trim();
    vcvars=path.join(install,'VC/Auxiliary/Build/vcvars64.bat');
  }
  if(!fs.existsSync(vcvars)||/[\r\n"%]/.test(vcvars))throw Error('Local MSVC environment batch unavailable/unsupported');
  const envCmd=path.join(out,'compiler-environment.cmd');
  if(/[&|<>^%\r\n"]/.test(envCmd))throw Error('Unsupported batch path characters');
  fs.writeFileSync(envCmd,`@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\nset\r\n`);
  const envResult=spawnSync(process.env.ComSpec || 'cmd.exe',['/d','/c',envCmd],{encoding:'utf8',windowsHide:true,timeout:30000});
  if(envResult.error||envResult.status!==0)throw Error('Local MSVC environment setup failed');
  const env={...process.env};
  for(const line of envResult.stdout.split(/\r?\n/)) {
    const at=line.indexOf('=');if(at<=0)continue;
    const key=line.slice(0,at),existing=Object.keys(env).find(k=>k.toLowerCase()===key.toLowerCase());env[existing || key]=line.slice(at+1);
  }
  const compilerTemp=path.join(out,'compiler-temp');fs.mkdirSync(compilerTemp,{recursive:true});
  for(const name of ['TEMP','TMP']) {const existing=Object.keys(env).find(k=>k.toLowerCase()===name.toLowerCase());env[existing || name]=compilerTemp;}
  fs.writeFileSync(path.join(out,'compiler-environment.log'),`vcvars=${vcvars}\nVCToolsVersion=${env.VCToolsVersion}\nWindowsSDKVersion=${env.WindowsSDKVersion}\n`);
  receipt.compiler={vcvars,vcToolsVersion:env.VCToolsVersion,language:'C++17',includes};save();
  const common=['/nologo','/std:c++17','/EHsc','/utf-8','/W4','/WX','/permissive-'];
  const configurations=[['Release',['/O2','/DNDEBUG','/MD']],['Checked',['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MDd']]];
  for(const [name,flags] of configurations) {
    const dir=path.join(out,name);fs.mkdirSync(dir,{recursive:true});
    const exe=path.join(dir,'onex_protocol.exe');
    const argv=[...common,...flags,path.join(out,'harness.cpp'),`/Fo${path.join(dir,'harness.obj')}`,`/Fd${path.join(dir,'harness.pdb')}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO'];
    await compile('cl.exe',argv,`${name}/compile.log`,dir,env);
    const report=JSON.parse(run(exe,[],`${name}/run.log`,dir,env).trim());
    if(report.status!=='pass')throw Error(`${name}: semantic harness failure`);
    receipt.configurations.push({name,compileArguments:argv,executableSha256:sha(fs.readFileSync(exe)),...report});save();
    console.log(`${name}: PASS (${report.checks} checks)`);
  }
  const standalone=path.join(out,'Release/standalone.exe');
  run('cl.exe',[...common,'/O2',path.join(out,'standalone.cpp'),`/Fo${path.join(out,'Release/standalone.obj')}`,`/Fe${standalone}`],
    'Release/standalone-compile.log',path.join(out,'Release'),env);
  run(standalone,[],'Release/standalone-run.log',path.join(out,'Release'),env);
  receipt.standaloneHeader={status:'pass',executableSha256:sha(fs.readFileSync(standalone))};save();
  const controls=[
    {name:'oxp3-keyboard-instead-home',section:'button-roles',from:'model == Model::X2 ? Button::Keyboard : Button::Home',to:'model != Model::X2MiniPro ? Button::Keyboard : Button::Home'},
    {name:'lost-third-page-inheritance',section:'initialization',from:'0x25, 0x01, 0x21, 0x00, 0x00, 0x00',to:'0x25, 0x01, 0x24, 0x00, 0x00, 0x00'},
    {name:'65-byte-report-id-not-prefixed',section:'encoding',from:'reportLength == 65 ? 1u : 0u',to:'reportLength == 65 ? 0u : 0u'},
    {name:'guessed-input-report-id-offset',section:'logical-decoding',from:'result.buttonId = data[6];',to:'result.buttonId = data[7];'},
  ];
  const outcomes=await Promise.allSettled(controls.map(async control=>{
    if(source.split(control.from).length!==2)throw Error(`${control.name}: unique mutation anchor unavailable`);
    const dir=path.join(out,'mutations',control.name);fs.mkdirSync(dir,{recursive:true});
    const mutated=source.replace(control.from,control.to);
    fs.writeFileSync(path.join(dir,'onex_x2_protocol.h'),mutated);
    fs.copyFileSync(path.join(out,'harness.cpp'),path.join(dir,'harness.cpp'));
    fs.copyFileSync(path.join(out,'oracle.generated.h'),path.join(dir,'oracle.generated.h'));
    const exe=path.join(dir,'mutant.exe'),logBase=`mutations/${control.name}`;
    const argv=[...common,'/O2','/MD',path.join(dir,'harness.cpp'),`/Fo${path.join(dir,'harness.obj')}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO'];
    await compile('cl.exe',argv,`${logBase}/compile.log`,dir,env);
    const result=logResult(`${logBase}/run.log`,spawnSync(exe,[],{cwd:dir,env,windowsHide:true,encoding:'utf8',timeout:30000}));
    let report;try{report=JSON.parse(result.stdout.trim());}catch{throw Error(`${control.name}: missing structured semantic rejection`);}
    if(result.status===0||report.status!=='fail'||report.section!==control.section)throw Error(`${control.name}: wrong or absent semantic rejection`);
    return {name:control.name,status:'detected',mutatedHeaderSha256:sha(mutated),expectedSection:control.section,report,compileArguments:argv,executableSha256:sha(fs.readFileSync(exe))};
  }));
  receipt.mutations=outcomes.map((r,i)=>r.status==='fulfilled' ? r.value : {name:controls[i].name,status:'invalid-control',error:r.reason.message});save();
  if(outcomes.some(r=>r.status==='rejected'))throw Error('At least one mutation lacked compiled semantic rejection');
  for(const input of receipt.inputs) if(sha(fs.readFileSync(input.path))!==input.sha256)throw Error(`Input changed during validation: ${input.path}`);
  receipt.status='pass';receipt.finishedAt=new Date().toISOString();save();
  console.log(`Four semantic mutations rejected. Evidence: ${path.join(out,'source-bindings.json')}`);
} catch(error) {
  receipt.status='fail';receipt.error=error.message;receipt.finishedAt=new Date().toISOString();save();
  console.error(error.stack || error.message);process.exitCode=1;
}