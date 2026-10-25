import path from 'node:path';
export const need=(value,code)=>{if(!value)throw Error(code);};
export function within(root,file){const relative=path.relative(path.resolve(root),path.resolve(file));return relative===''||(!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep));}
export function faultSource(text,fault){need(typeof text==='string'&&text.split(fault.before).length===2,'SLEEP_UI_FAULT_ANCHOR');return text.replace(fault.before,fault.after);}
export function verifyOriginalReport(report,spec){
 need(report?.suite===spec.expectedReportSuite&&report.passed===spec.expectedCaseNames.length&&Array.isArray(report.cases)&&JSON.stringify(report.cases)===JSON.stringify(spec.expectedCaseNames),'SLEEP_UI_ORIGINAL_CASES_INCOMPLETE');
 need(report.hardwareOperations===0&&report.liveSteamOperations===0,'SLEEP_UI_ORIGINAL_SCOPE');
 return {checks:report.cases.length,caseNames:report.cases,naturalOriginalEndpoint:true};
}
export function verifyBoundary(observation,trace,{expectedPages}={}){
 need(observation?.exitCode!==null&&observation.denied?.length===0&&observation.serversCreated===1&&observation.browserLaunched===1&&observation.browserClosed===1&&observation.pagesCreated===expectedPages&&observation.builds?.length===1&&observation.builds[0].observerPreservedProgramBytes===true,'SLEEP_UI_RUNTIME_BOUNDARY');
 need(observation.processes?.length===2&&observation.processes.filter(row=>row.compiler).length===1&&observation.processes.filter(row=>row.browser).length===1,'SLEEP_UI_PROCESS_BOUNDARY');
 need(Number.isInteger(observation.port)&&observation.port>0&&observation.browserRequests?.length>0&&observation.browserRequests.every(row=>row.allowed===true&&new URL(row.url).origin==='http://127.0.0.1:'+observation.port),'SLEEP_UI_BROWSER_ORIGIN');
 need(trace?.serverClosed===1&&trace.pageErrors?.length===0&&trace.snapshotErrors?.length===0&&trace.served?.length>0&&trace.served.every(row=>row.finished===true)&&trace.snapshots?.length>=expectedPages,'SLEEP_UI_PAGE_OR_CLOSE_EVIDENCE');
 for(const snapshot of trace.snapshots){
  need(Array.isArray(snapshot.calls)&&snapshot.calls.length>0&&snapshot.opened?.length===0&&snapshot.localStorageKeys?.length===0&&snapshot.sessionStorageKeys?.every(key=>key==='sleep-auto'),'SLEEP_UI_NATIVE_MEMORY_SNAPSHOT');
  for(const call of snapshot.calls){
   need(['sleepRecovery.autoGet','sleepRecovery.autoSet'].includes(call.cmd),'SLEEP_UI_UNEXPECTED_NATIVE_ACTION');
   need(call.args&&typeof call.args==='object'&&!Array.isArray(call.args),'SLEEP_UI_NATIVE_ARGUMENTS');
   need(call.cmd==='sleepRecovery.autoGet'?Object.keys(call.args).length===0:Object.keys(call.args).length===1&&typeof call.args.enabled==='boolean','SLEEP_UI_NATIVE_ARGUMENTS');
  }
 }
 return {pageErrors:0,serversClosed:1,browsersClosed:1,pagesCreated:expectedPages,memorySnapshots:trace.snapshots.length,nativeCommands:[...new Set(trace.snapshots.flatMap(row=>row.calls.map(call=>call.cmd)))].sort()};
}
export function verifyAssertionFailure(row,stderr,spec){
 const firstFrame=stderr.split(/\r?\n/).find(line=>/^\s+at /.test(line));
 need(row.exitCode===1&&!row.signal&&!row.error&&/^AssertionError \[ERR_ASSERTION\]:/m.test(stderr)&&firstFrame?.replaceAll('\\','/').includes('/'+path.basename(spec.original)+':'+spec.fault.expectedLine+':'),'SLEEP_UI_EXPECTED_ORIGINAL_ASSERTION');
 return {fault:spec.fault.id,line:spec.fault.expectedLine,expectedCase:spec.fault.expectedCase,firstFrame,compileSucceeded:true,originalAssertionDetected:true};
}
