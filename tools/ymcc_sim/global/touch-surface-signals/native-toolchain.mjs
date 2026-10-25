import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {TOUCH_SOURCE_INPUTS} from './adapters.mjs';
import {calibrationToolchainPreamble} from '../../../fixtures/hc_reviewed_calibration/toolchain.mjs';
const exec=promisify(execFile);
export const digest=value=>crypto.createHash('sha256').update(value).digest('hex');
export async function compileTouchFixture({repoRoot,workDir,name,source,generated={}}){
 const hash=digest(fs.readFileSync(source)+JSON.stringify(generated)+JSON.stringify(collectTouchSources(repoRoot)));
 const dir=path.join(workDir,name+'-'+hash.slice(0,16)),executable=path.join(dir,name+'.exe');fs.mkdirSync(dir,{recursive:true});
 for(const [file,text] of Object.entries(generated))fs.writeFileSync(path.join(dir,file),text);
 if(!fs.existsSync(executable)){
  const vswhere=path.join(process.env['ProgramFiles(x86)']??'C:/Program Files (x86)','Microsoft Visual Studio/Installer/vswhere.exe');
  const found=await exec(vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],{windowsHide:true,timeout:10000});
  const setup=path.join(found.stdout.trim(),'VC/Auxiliary/Build/vcvars64.bat');
  const q=p=>{if(/["%\r\n]/.test(p))throw Error('UNSAFE_COMPILER_PATH');return '"'+p+'"';};
  fs.writeFileSync(path.join(dir,'compile.rsp'),['/nologo','/std:c++20','/DUNICODE','/D_UNICODE','/EHsc','/O1','/Gy','/MT','/utf-8',`/I${q(dir)}`,`/I${q(path.join(repoRoot,'native'))}`,`/I${q(path.join(repoRoot,'deps/json'))}`,`/I${q(repoRoot)}`,q(source),`/Fo:${q(path.join(dir,'fixture.obj'))}`,`/Fe:${q(executable)}`,'/link /OPT:REF /INCREMENTAL:NO /NODEFAULTLIB:user32.lib /NODEFAULTLIB:gdi32.lib /NODEFAULTLIB:shell32.lib'].join('\r\n'));
  fs.writeFileSync(path.join(dir,'compile.cmd'),calibrationToolchainPreamble(setup)+'cl.exe @compile.rsp\r\nif errorlevel 1 exit /b %errorlevel%\r\ndumpbin /imports "'+executable+'" > imports.txt\r\n');
  try{const r=await exec(process.env.ComSpec??'cmd.exe',['/d','/c','compile.cmd'],{cwd:dir,windowsHide:true,timeout:60000,maxBuffer:4e6});fs.writeFileSync(path.join(dir,'compile.log'),r.stdout+r.stderr);}
  catch(error){fs.writeFileSync(path.join(dir,'compile.log'),String(error.stdout??'')+String(error.stderr??''));throw Error('TOUCH_FIXTURE_BUILD_FAILED:'+path.join(dir,'compile.log'));}
 }
 const imports=fs.readFileSync(path.join(dir,'imports.txt'),'utf8');
 const forbidden=['user32.dll','win32u.dll','gdi32.dll','shell32.dll','ole32.dll','hid.dll','setupapi.dll','xinput1_4.dll','sendinput','injecttouchinput','setwindowshook','setwineventhook','getasynckeystate'].filter(x=>imports.toLowerCase().includes(x));
 if(forbidden.length)throw Error('TOUCH_UNSAFE_IMPORT:'+forbidden.join(','));
 return {dir,executable,executableSha256:digest(fs.readFileSync(executable)),importAudit:{status:'PASS',forbidden,sha256:digest(imports)}};
}
export function collectTouchSources(repoRoot){return [...TOUCH_SOURCE_INPUTS].sort().map(p=>({path:p,sha256:digest(fs.readFileSync(path.join(repoRoot,p)))}));}
export async function runTouchExecutable(compiled,args=[]){
 let stdout,exitCode=0;
 try{({stdout}=await exec(compiled.executable,args,{cwd:compiled.dir,windowsHide:true,timeout:30000,maxBuffer:8e6}));}
 catch(error){if(typeof error.code!=='number')throw error;stdout=error.stdout;exitCode=error.code;}
 return {stdout,exitCode};
}


