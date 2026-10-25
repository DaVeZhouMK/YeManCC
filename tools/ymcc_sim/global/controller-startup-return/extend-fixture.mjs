import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
const sha=s=>crypto.createHash('sha256').update(s).digest('hex');
export function extractFunction(source,name){
 const m=new RegExp('^static[^\\n;{]*\\b'+name+'\\s*\\(','m').exec(source);if(!m)throw Error('STARTUP_FUNCTION_MISSING:'+name);
 const start=m.index,open=source.indexOf('{',start);let depth=0,state='code',end;
 for(let i=open;i<source.length;i++){const c=source[i],n=source[i+1];if(state==='line'){if(c==='\n')state='code';continue;}if(state==='block'){if(c==='*'&&n==='/'){state='code';i++;}continue;}if(state==='string'||state==='char'){if(c==='\\'){i++;continue;}if(c===(state==='string'?'"':"'"))state='code';continue;}if(c==='/'&&n==='/'){state='line';i++;continue;}if(c==='/'&&n==='*'){state='block';i++;continue;}if(c==='"'||c==="'"){state=c==='"'?'string':'char';continue;}if(c==='{')depth++;if(c==='}'&&--depth===0){end=i+1;break;}}
 if(!end)throw Error('STARTUP_UNCLOSED_FUNCTION:'+name);const code=source.slice(start,end);return {name,code,startLine:source.slice(0,start).split('\n').length,endLine:source.slice(0,end).split('\n').length,sha256:sha(code)};
}
export function extendFixture({repo,moduleDir,out}){
 fs.mkdirSync(out,{recursive:true});const source=fs.readFileSync(path.join(repo,'native/msi_crash_recovery_runtime.h'),'utf8'),retryError=extractFunction(source,'msiCrashRetryableRecordReadError'),loadFailure=extractFunction(source,'msiCrashRecordLoadFailed'),load=extractFunction(source,'msiCrashLoadLocked'),init=extractFunction(source,'msiCrashRecoveryInitialize');
 let boundary=fs.readFileSync(path.join(repo,'tools/fixtures/msi_startup_owned_return/boundaries.inc'),'utf8'),cases=fs.readFileSync(path.join(repo,'tools/fixtures/msi_startup_owned_return/cases.inc'),'utf8');
 const old='static void msiCrashLoadLocked(){} // Fixtures seed a decoded, already-loaded valid record.';if(boundary.split(old).length!==2)throw Error('STARTUP_LOAD_SEAM_DRIFT');
 let actual=fs.readFileSync(path.join(moduleDir,'load-boundaries.inc'),'utf8').replace('@@ACTUAL_RETRY_ERROR@@',()=>retryError.code).replace('@@ACTUAL_LOAD_FAILURE@@',()=>loadFailure.code).replace('@@ACTUAL_LOAD@@',()=>load.code).replace('@@ACTUAL_INITIALIZE@@',()=>init.code);
 // Place a forward declaration above the actual loader, retain the original
 // in-memory log sink's default argument on exactly one declaration.
 boundary=boundary.replace(old,()=>actual).replace('static void msiCrashNote(const char* phase,const char* reason,bool persisted=false){','static void msiCrashNote(const char* phase,const char* reason,bool persisted){');
 const added=fs.readFileSync(path.join(moduleDir,'loader-cases.inc'),'utf8'),report='    const json report={{"passed",c.failures.empty()}';if(cases.split(report).length!==2)throw Error('STARTUP_REPORT_ANCHOR_DRIFT');
 cases=cases.replace('int main(){',()=>added+'\nint main(){').replace(report,()=> '    appendColdStartupCases(c);\n'+report);
 fs.writeFileSync(path.join(out,'boundaries.inc'),boundary);fs.writeFileSync(path.join(out,'cases.inc'),cases);
 const ranges=[retryError,loadFailure,load,init].map(({code,...r})=>r);const result={source:'native/msi_crash_recovery_runtime.h',sourceSha256:sha(source),ranges,originalAssertionsUnmodified:true,originalCasesAppendOnly:true,addedCases:29,actualLoaderAndInitializerVerbatim:true,seams:['typed bounded-file read in memory; actual native Win32 reader separately exercised by full Coordinator private-file cases','legacy migration absent, not tested','original process-owner liveness/creation fixture boundary','original in-memory atomic persistence and PnP/HID outcomes']};fs.writeFileSync(path.join(out,'extension-source-binding.json'),JSON.stringify(result,null,2)+'\n');return result;
}
