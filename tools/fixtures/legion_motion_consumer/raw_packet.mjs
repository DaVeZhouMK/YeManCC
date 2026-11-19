import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {extract} from './extract_main.mjs';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=p=>{const b=fs.readFileSync(p);return {path:p,bytes:b.length,sha256:sha(b)};};
export function runRawPacket(repo){
 const args=process.argv.slice(2),o={};
 for(let i=0;i<args.length;++i){const a=args[i];assert(['--raw-packet-only','--prepare-only','--out','--raw-python','--raw-declarations','--raw-deadline','--raw-phase'].includes(a),'Unsupported raw-only option '+a);assert(!Object.hasOwn(o,a),'Duplicate raw-only option '+a);if(['--raw-packet-only','--prepare-only'].includes(a))o[a]=true;else {assert(args[i+1]&&!args[i+1].startsWith('--'),'Missing value '+a);o[a]=args[++i];}}
 assert(o['--raw-phase']===undefined||['remaining-controls','final-bounds-suffix','mouse-payload-before','mouse-payload-admission','mouse-payload-controls'].includes(o['--raw-phase']),'Unknown raw continuation phase');
 assert(o['--raw-packet-only']&&o['--out'],'raw-only needs explicit fresh --out');
 const out=path.resolve(o['--out']);
 const approved=['G:/YeManCC-Work/Testing/YMCC/Evidence','G:/YeManCC-Work/Build/Validation'].map(p=>path.resolve(p).toLowerCase()+path.sep);
 assert(approved.some(p=>out.toLowerCase().startsWith(p)),'Use core Evidence or Validation root');
 if(!o['--prepare-only']){assert(o['--raw-python']&&o['--raw-declarations']&&o['--raw-deadline'],'Execution requires explicit Python, predeclared input graph and deadline');assert(Number.isFinite(+o['--raw-deadline'])&&+o['--raw-deadline']>Date.now()+30000,'Invalid/expired deadline');}
 assert(!fs.existsSync(out),'Fresh output required');fs.mkdirSync(out,{recursive:true});
 assert.equal(fs.realpathSync(out).toLowerCase(),out.toLowerCase(),'No output reparse point');
 const write=(name,b)=>fs.writeFileSync(path.join(out,name),b,{flag:'wx'});
 const fixture=path.join(repo,'tools/fixtures/legion_motion_consumer');
 const main=path.join(repo,'native/main.cpp'),body=fs.readFileSync(main,'utf8');
 const actual=extract(body,'gamepadDispatchRawInput');
 write('dispatcher.generated.h',actual.code+'\n');
 write('input_edge_evidence.h',fs.readFileSync(path.join(repo,'native/input_edge_evidence.h')));
 const harness=path.join(fixture,'raw_packet.cpp');write('raw_packet.cpp',fs.readFileSync(harness));
 const names=[...fs.readFileSync(harness,'utf8').matchAll(/\{"(raw-packet-[a-z-]+)",\w+\}/g)].map(m=>m[1]);assert.equal(names.length,8);assert.equal(new Set(names).size,8);
 const result={status:'PREPARED_ONLY',passed:false,actualCompilerStarts:0,actualBusinessRuns:0,old31Executed:false,oldMutantsExecuted:false,actualDispatcher:actual,main:pin(main),inputs:[...['raw_packet.mjs','raw_packet.cpp','raw_packet_driver.py','raw_packet_owned.py','extract_main.mjs'].map(n=>pin(path.join(fixture,n))),...['native/input_edge_evidence.h','tools/legion_motion_consumer_selftest.mjs','tools/lib/hc_controller_motion_fixture.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'].map(n=>pin(path.join(repo,n)))],selectedOriginalParameterizedCases:names,selectedMousePayloadCases:[...fs.readFileSync(harness,'utf8').matchAll(/\{\"(raw-mouse-[a-z-]+)\",\w+\}/g)].map(m=>m[1]),phase:o['--raw-phase']||'normal-and-controls',scope:'Full exact actual dispatcher, real Windows RAWINPUT ABI, controlled memory-only API/callback effects. No leaf internals, device, Host, clocks, timer or product proof.',diagnostics:o['--raw-phase']?.startsWith('mouse-payload-')?'Explicit mouse phase; Release/Checked, DIAGNOSTICS=0 execution, FAULT and directed negative scope per driver':'Release/Checked DIAGNOSTICS=1 with observed true/false; separate DIAGNOSTICS=0 compile only',generated:[pin(path.join(out,'dispatcher.generated.h')),pin(path.join(out,'raw_packet.cpp')),pin(path.join(out,'input_edge_evidence.h'))]};
 write('PREPARED.json',JSON.stringify(result,null,2)+'\n');
 if(o['--prepare-only']){console.log(JSON.stringify({status:result.status,passed:false,selected:result.phase.startsWith('mouse-payload-')?(result.phase==='mouse-payload-before'?['raw-mouse-diag-off']:result.selectedMousePayloadCases):names,actualCompilerStarts:0}));return;}
 const driver=path.join(fixture,'raw_packet_driver.py');
 const run=spawnSync(path.resolve(o['--raw-python']),[driver,out,o['--raw-deadline'],path.resolve(o['--raw-declarations']),o['--raw-phase']||'normal-and-controls'],{windowsHide:true,encoding:'utf8',timeout:Math.max(1000,+o['--raw-deadline']-Date.now()+5000),maxBuffer:16777216,env:{...process.env,TEMP:out,TMP:out,PYTHONDONTWRITEBYTECODE:'1'}});
 write('driver.stdout.log',run.stdout||'');write('driver.stderr.log',run.stderr||'');
 assert(!run.error&&run.signal===null&&run.status===0,'raw-only producer failed; retain raw receipts and stop');
 const final=JSON.parse(fs.readFileSync(path.join(out,'RESULT.json'),'utf8'));assert.equal(final.status,'PASSED_SCOPED_DISPATCHER');console.log(JSON.stringify(final));
}
