// Real diagnostic sender with inert IPC; no engine, UI actions or hardware.
import './mock-shell';
import { reportFrontendTrace, reportFrontendError, readFrontendErrorLog, frontendDiagnosticDeliveryFailures } from '../src/robust/frontendDiagnostics';
import { writeFileSync, mkdirSync } from 'node:fs';
import { resolve, join } from 'node:path';
const root=resolve(process.argv[2]);mkdirSync(root,{recursive:true});
const checks:Array<{name:string;pass:boolean}>=[];const calls:Array<{cmd:string;args:any}>=[];
let response='ok';(globalThis as any).__mockIpcResponder=(cmd:string,args:any)=>{calls.push({cmd,args});if(response==='throw')throw Error('fixture-IPC-failure');return {ok:response==='ok'};};
(globalThis as any).window.location={hash:'#/fixture'};
function check(name:string,value:unknown){checks.push({name,pass:!!value});}
async function settle(){await new Promise(r=>setImmediate(r));}
async function main(){
 const before=readFrontendErrorLog().length;
 reportFrontendTrace('gamepad-admission',{stage:'owner-start',action:'',epoch:1,qSeq:2,t:3});await settle();
 check('trace-uses-only-named-diagnostic-IPC-with-original-state',calls.length===1&&calls[0].cmd==='diagnostics.frontendTrace'&&JSON.parse(calls[0].args.message).qSeq===2&&calls[0].args.route==='#/fixture');
 check('state-trace-does-not-fill-persistent-error-ring',readFrontendErrorLog().length===before);
 reportFrontendError('vue-runtime',new Error('fixture-real-exception'));await settle();
 check('genuine-errors-still-use-error-endpoint-and-stack',calls[1].cmd==='diagnostics.frontendError'&&calls[1].args.message==='fixture-real-exception'&&calls[1].args.stack.includes('Error: fixture-real-exception'));
 let failures=frontendDiagnosticDeliveryFailures();response='false';reportFrontendTrace('gamepad-admission',{stage:'owner-stop'});await settle();check('native-sink-false-counted-not-claimed-durable',frontendDiagnosticDeliveryFailures()===failures+1);
 failures=frontendDiagnosticDeliveryFailures();response='throw';reportFrontendTrace('gamepad-admission',{stage:'owner-stop'});await settle();check('IPC-failure-counted-no-recursive-report',frontendDiagnosticDeliveryFailures()===failures+1&&calls.length===4);
 failures=frontendDiagnosticDeliveryFailures();const circular:any={stage:'bad-fixture'};circular.self=circular;reportFrontendTrace('gamepad-admission',circular);await settle();check('serialization-failure-never-throws-or-changes-control',frontendDiagnosticDeliveryFailures()===failures+1&&calls.length===4);
 failures=frontendDiagnosticDeliveryFailures();reportFrontendError('vue-runtime',new Error('fixture-original-error'));await settle();check('real-error-delivery-failure-also-counted',frontendDiagnosticDeliveryFailures()===failures+1&&calls[4].cmd==='diagnostics.frontendError');
 const report={checks,failures:checks.filter(c=>!c.pass).length,realProductOrHardwareExecuted:false,scope:'REAL_DIAGNOSTIC_SENDERS_INERT_IPC_NO_ENGINE_OR_USER_STORAGE'};writeFileSync(join(root,'report.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));process.exitCode=report.failures?1:0;
}
main().catch(e=>{console.error(e);process.exitCode=1;});
