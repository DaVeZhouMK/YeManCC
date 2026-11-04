// Optional leaf of input_shortcut_toggle_selftest.mjs. Real function extraction,
// original fake Win32/worker ports and a frozen failing before control only.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {extract} from '../legion_motion_consumer/extract_main.mjs';
import {calibrationToolchainPreamble} from '../hc_reviewed_calibration/toolchain.mjs';
const base=path.dirname(fileURLToPath(import.meta.url));
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const pin=file=>({path:file,bytes:fs.statSync(file).size,sha256:hash(file)});

export async function runJoyXoffSessionCleanup({root,out,mainPath}) {
  // The owner supplies the original cumulative scope; there is no fresh budget.
  assert(process.env.YMCC_JOYXOFF_TOTAL_STARTED && process.env.YMCC_JOYXOFF_PRIOR_MS,
    'Original cumulative scope timing is required; no automatic new scope');
  const started=Number(process.env.YMCC_JOYXOFF_TOTAL_STARTED),prior=Number(process.env.YMCC_JOYXOFF_PRIOR_MS);
  assert(Number.isFinite(started)&&started>0&&started<=Date.now()&&Number.isFinite(prior)&&prior>=0&&prior<20000,'Invalid original cumulative scope');
  const deadline=started+20000-prior;
  const remaining=()=>{const ms=Math.floor(deadline-Date.now()-600);assert(ms>0,'Original JoyXoff scope exhausted');return ms;};
  remaining();
  const source=path.resolve(mainPath),cpp=path.join(base,'joyxoff-session-cleanup.fixture.cpp'),beforeControl=path.join(base,'joyxoff-session-cleanup.before.inc');
  assert.equal(hash(beforeControl),'77cd0a206c994b045ad34a3df7de92253f43fa09369c9a37fa088c3fdc8888c8','Frozen original negative-control bytes changed');
  const current=extract(fs.readFileSync(source,'utf8'),'closeJoyXoffAsync');
  fs.writeFileSync(path.join(out,'joyxoff-session-cleanup.current.inc'),current.code+'\n');
  const inputs=[source,fileURLToPath(import.meta.url),path.join(root,'tools/input_shortcut_toggle_selftest.mjs'),cpp,beforeControl,
    path.join(root,'tools/fixtures/legion_motion_consumer/extract_main.mjs'),path.join(root,'tools/lib/hc_controller_motion_fixture.mjs'),
    path.join(root,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'),path.join(root,'deps/json/json.hpp'),path.join(out,'joyxoff-session-cleanup.current.inc')];
  const before=inputs.map(pin),write=(name,value)=>fs.writeFileSync(path.join(out,name),JSON.stringify(value,null,2)+'\n');
  write('SOURCE-BINDINGS.json',{selection:'joyxoff-session-cleanup',before,current,negativeControl:pin(beforeControl),sourceClosureCacheable:false,fullParentExecuted:false});
  const vcvars=process.env.YMCC_SHORTCUT_VCVARS || 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
  const quote=value=>{assert(!/["%\r\n]/.test(value),'Unsupported compiler path');return '"'+value+'"';};
  const exe=path.join(out,'joyxoff-session-cleanup.exe'),batch=path.join(out,'compile.cmd');
  fs.writeFileSync(batch,calibrationToolchainPreamble(vcvars)+'set "VSLANG=1033"\r\nwhere cl > compiler-paths.log\r\nwhere link >> compiler-paths.log\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /Od /showIncludes /I'+quote(path.join(root,'deps/json'))+' /I'+quote(out)+' '+quote(cpp)+' /Fo'+quote(path.join(out,'joyxoff-session-cleanup.obj'))+' /Fe'+quote(exe)+'\r\nexit /b %errorlevel%\r\n');
  const stages=[],compileStarted=Date.now();
  // Run under the existing owner's process-tree budget. This module does not
  // create or reset an owned execution scope, or claim cleanup without its receipt.
  const compile=spawnSync('cmd.exe',['/d','/c',batch],{cwd:out,encoding:'utf8',timeout:remaining()});
  stages.push({stage:'compile',wallMs:Date.now()-compileStarted,exitCode:compile.status,error:compile.error?.message??null});
  fs.writeFileSync(path.join(out,'compile.stdout.log'),compile.stdout||'');fs.writeFileSync(path.join(out,'compile.stderr.log'),compile.stderr||'');
  if(compile.error||compile.status!==0){write('RESULT.json',{status:'COMPILE_FAILED_OR_TIMEOUT',testsExecuted:false,stages,before,identityClosureComplete:false});process.exitCode=1;return;}
  const executeStarted=Date.now(),run=spawnSync(exe,[],{cwd:out,encoding:'utf8',timeout:remaining()});
  stages.push({stage:'fake-only-native-leaf',wallMs:Date.now()-executeStarted,exitCode:run.status,error:run.error?.message??null});
  fs.writeFileSync(path.join(out,'execute.stdout.log'),run.stdout||'');fs.writeFileSync(path.join(out,'execute.stderr.log'),run.stderr||'');
  let body=null,parseError=null;
  try{body=JSON.parse(run.stdout.trim());}catch(error){parseError=error.message;}
  const after=inputs.map(pin),drift=before.filter((p,i)=>p.sha256!==after[i].sha256);
  const includes=[...new Set([...(compile.stdout||'').matchAll(/^Note: including file:\s*(.+)$/gm)].map(m=>m[1].trim()))];
  const toolPaths=fs.readFileSync(path.join(out,'compiler-paths.log'),'utf8').split(/\r?\n/).filter(p=>fs.existsSync(p));
  write('COMPILER-INPUTS.json',{method:'actual MSVC /showIncludes',includes:includes.filter(p=>fs.existsSync(p)).map(pin),missingIncludes:includes.filter(p=>!fs.existsSync(p)),tools:[process.execPath,...toolPaths].map(pin),executable:pin(exe),sourceClosureCacheable:false,identityClosureComplete:false});
  write('INPUT-PINS-AFTER.json',{after,drift});
  const passed=!run.error&&run.status===0&&body?.status==='PASS'&&body?.candidateCases===19&&body?.baseline?.status==='FAIL_EXPECTED'&&!parseError&&!drift.length;
  write('RESULT.json',{status:passed?'PASS_SELECTED_ORIGINAL_CONSUMER':'FAIL_OR_SOURCE_DRIFT',selection:'joyxoff-session-cleanup',actualCurrentFunction:current,body,parseError,before,after,drift,stages,
    testsExecuted:true,fullOriginalSuiteExecuted:false,permanentIdAdded:false,realProcessOperations:false,realDevices:false,identityClosureComplete:false,ownedTreeCleanup:'requires original owner receipt',unmeasured:['real Win32 APIs/threads','installed behavior and sleep','full original shortcut suite','full compiler/linker/runtime closure']});
  if(!passed)process.exitCode=1;
}
