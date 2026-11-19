// Explicit prepare-only sibling of the original paired input_frame_pipe_issue.
// No subprocess imports, compiler invocation, test execution or implicit all.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=(file,bytes=fs.readFileSync(file))=>({path:path.resolve(file),bytes:bytes.length,sha256:sha(bytes)});

export function prepareRealPipe({options,seen,repo,mainFile,headerRoot,runnerPath}){
 assert(options.prepareOnly,'--real-pipe-case requires --prepare-only; execute through existing owned executor');
 assert(seen.has('out'),'explicit --out required for private real-pipe preparation');
 assert(!seen.has('caseGroup')&&!options.cases.length,'--real-pipe-case is exclusive with --case-group and --case');
 const variant=options.realPipeVariant??'candidate';
 assert(['candidate','old'].includes(variant),'unknown real-pipe variant');
 const here=path.dirname(fileURLToPath(import.meta.url));
 const sourceFile=path.join(here,'fixtures/input_frame_pipe_issue/real_pipe_cases.cpp');
 const sourceBytes=fs.readFileSync(sourceFile),sourceText=sourceBytes.toString('utf8');
 // The CPP remains the sole authority for manual names; no second case table.
 const names=[...sourceText.matchAll(/selected == L"([^"\r\n]+)"/g)].map(m=>m[1]);
 assert(names.length&&new Set(names).size===names.length,'source selector names are missing/duplicated');
 assert(names.includes(options.realPipeCase),'unknown/empty real-pipe case');
 if(variant==='old')assert(/^delay-(5|10|60)s$/.test(options.realPipeCase),'old control is limited to explicit delayed-read negative cases');
 const headerFile=path.join(headerRoot,'input_host_pipe_transport.h');
 const headerBytes=fs.readFileSync(headerFile),headerText=headerBytes.toString('utf8');
 const limit=headerText.match(/\bkMaxSchedulingReadMs\s*=\s*(\d+)\s*;/);
 if(variant==='candidate'){
  assert(headerText.includes('struct SlowReadAllowance')&&headerText.includes('schedulingGraceEntered')&&
    /Cancellation\s+cancellation=\{\}\s*,\s*SlowReadAllowance\s+slowRead=\{\}/.test(headerText),'candidate transport API mismatch');
  assert(limit,'candidate scheduling max missing');
  if(options.realPipeCase==='delay-60s')assert.equal(Number(limit[1]),65000,'delay-60s requires actual candidate max65000');
 }else assert(!headerText.includes('struct SlowReadAllowance'),'old variant requires the real pre-allowance header');
 // All validation above precedes output creation and imports no original
 // fake fixture preparation, default snapshots or execution modules.
 const run=path.resolve(options.out),outputAncestor=path.dirname(run);
 assert(!fs.existsSync(run),'fresh owned --out required');
 const inputs=[pin(sourceFile,sourceBytes),pin(headerFile,headerBytes),pin(mainFile),pin(runnerPath),pin(fileURLToPath(import.meta.url))];
 fs.mkdirSync(outputAncestor,{recursive:true});fs.mkdirSync(run);
 const write=(relative,bytes)=>{
  const file=path.join(run,relative);fs.mkdirSync(path.dirname(file),{recursive:true});
  fs.writeFileSync(file,bytes,{flag:'wx'});return pin(file,Buffer.isBuffer(bytes)?bytes:Buffer.from(bytes));
 };
 const source=write('source/real_pipe_cases.cpp',sourceBytes);
 const header=write('source/input_host_pipe_transport.h',headerBytes);
 assert(!fs.existsSync(path.join(run,'source/windows.h')),'fake Windows header prohibited');
 const owned=path.join(run,'owned');fs.mkdirSync(owned);
 const exe=path.join(owned,'real_pipe_cases.exe');
 const compilerArguments=['/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/permissive-',
  '/O2','/MT','/DWIN32_LEAN_AND_MEAN','/DNOMINMAX','/D_WIN32_WINNT=0x0A00',
  '/DYMCC_REAL_PIPE_WITH_SLOW_READ_ALLOWANCE='+(variant==='old'?'0':'1'),
  '/I'+path.dirname(header.path),source.path,'/Fo'+path.join(owned,'real_pipe_cases.obj'),'/Fe'+exe,
  '/sourceDependencies',path.join(owned,'deps.json'),'/link','/INCREMENTAL:NO','/VERBOSE:LIB','kernel32.lib'];
 const executor={kind:'EXISTING_OWNED_EXECUTOR_REQUIRED_NOT_EXECUTED',
  existingHelper:path.join(repo,'tools/ymcc_sim/global/host-process.ps1'),
  existingRawAdapter:path.join(repo,'tools/fixtures/legion_motion_consumer/raw_packet_owned.py'),
  compiler:{arguments:compilerArguments,program:'OWNER_MUST_RESOLVE_AND_PIN_EXISTING_MSVC_CL_EXE',expectedBusinessExit:0},
  business:{program:exe,arguments:['--case',options.realPipeCase],expectedBusinessExit:variant==='old'?1:0},
  constraints:['Use original caller remaining budget; no auto compile/run and no timeout renewal.',
   'Fix actual compiler/SDK/tool scope/environment and full source pins before compile; collect real include and link graph.',
   'Preserve independent raw, expected business exit, exact argv, macro and actual image.',
   'Verify child/Job0 before disposal and runtime pending/handles0; limited pinned vctip cleanup is not natural product exit.',
   'Read JSONL line by line and check exactly one selected result; default/all must not run.']};
 write('OWNED-EXECUTION.json',JSON.stringify(executor,null,2)+'\n');
 for(const p of inputs){const b=fs.readFileSync(p.path);assert(b.length===p.bytes&&sha(b)===p.sha256,'source changed during preparation '+p.path);}
 const report={schemaVersion:1,status:'PREPARED_NOT_COMPILED_NOT_EXECUTED',originalEntry:'tools/input_frame_pipe_issue_selftest.mjs',
  run,repo,mainFile,headerRoot,manualRealPipe:true,selectedCase:options.realPipeCase,selectedCases:[options.realPipeCase],selectedCount:1,
  variant,candidateMaximum:limit?Number(limit[1]):null,defaultAllExpanded:false,newTestId:false,minimumChanged:false,
  inputs,source,header,preparedRecipeCount:1,compilerInvocations:0,businessExecutions:0,
  compiled:false,executed:false,sourceReady:false,mainBoundButNotExtracted:true,
  scope:'Real Win32 private child/transport only; source is the exact sealed r2 CPP. No Host/SDK/device or production runtime self-heal claim.',
  executorManifest:path.join(run,'OWNED-EXECUTION.json')};
 write('PREPARED.json',JSON.stringify(report,null,2)+'\n');
 console.log(JSON.stringify({status:report.status,run,selectedCases:report.selectedCases,variant,compilerInvocations:0,businessExecutions:0}));
}
