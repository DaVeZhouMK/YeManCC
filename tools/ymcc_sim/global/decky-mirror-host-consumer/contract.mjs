import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
export const MAX_INPUTS=640;
export const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
export const hash=file=>sha(fs.readFileSync(file));
export const readJson=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
export const writeJson=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
export function need(ok,code,details){if(!ok)throw Object.assign(Error(code),{code,details});}
export function inside(root,file){const rel=path.relative(path.resolve(root),path.resolve(file));return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));}
export function safeRealPath(root,file){need(inside(root,file),'PATH_OUTSIDE_ROOT',{root,file});need(inside(fs.realpathSync(root),fs.realpathSync(file)),'PATH_REPARSE_ESCAPE',{root,file});return fs.realpathSync(file);}
export function freshEvidence(value,repo){need(typeof value==='string'&&path.isAbsolute(value)&&!/[\0\r\n]/.test(value),'EVIDENCE_ABSOLUTE_REQUIRED');const out=path.resolve(value);need(!inside(repo,out),'EVIDENCE_INSIDE_PRODUCT_REPOSITORY');need(!fs.existsSync(out),'FRESH_EVIDENCE_REQUIRED');let parent=path.dirname(out);while(!fs.existsSync(parent))parent=path.dirname(parent);need(path.resolve(fs.realpathSync(parent)).toLowerCase()===path.resolve(parent).toLowerCase(),'EVIDENCE_REPARSE_PARENT');return out;}
export function repoFile(root,relative){need(typeof relative==='string'&&!path.isAbsolute(relative)&&!relative.split(/[\\/]/).includes('..'),'REPO_RELATIVE_REQUIRED');return safeRealPath(root,path.join(root,relative));}
export function fingerprint(root,files){need(files.length<=MAX_INPUTS,'INPUT_BUDGET_EXCEEDED');return [...new Set(files)].sort().map(relative=>({path:relative,actualPath:repoFile(root,relative),sha256:hash(repoFile(root,relative))}));}
export function verifyRows(rows){for(const row of rows)need(hash(row.actualPath??row.path)===row.sha256,'INPUT_CHANGED',row);}
export function casesIn(text){return [...text.matchAll(/await check\('([^']+)'/g)].map(m=>({name:m[1],line:text.slice(0,m.index).split('\n').length}));}
export function mutateHost(text,fault){const start=text.indexOf('export function startDeckyMirrorHost(');need(start>=0,'FAULT_PRODUCT_FUNCTION_MISSING');const index=text.indexOf(fault.from,start);need(index>=start&&text.indexOf(fault.from,index+1)===-1,'FAULT_ANCHOR_NOT_UNIQUE');const candidate=text.slice(0,index)+fault.to+text.slice(index+fault.from.length);need(candidate!==text&&candidate.slice(0,index)===text.slice(0,index)&&candidate.slice(index+fault.to.length)===text.slice(index+fault.from.length),'FAULT_OUTSIDE_FUNCTION');return {candidate,offset:index,before:fault.from,after:fault.to,function:fault.function};}
export function faultVerdict({exit,stderr,trace,loads,suite,fault,override}){
 const expected=suite.cases.find(row=>row.name===fault.expectedName);need(expected,'FAULT_EXPECTATION_MISSING');
 need(exit.naturalExit&&exit.exitCode===1,'FAULT_NOT_NATURAL_ASSERTION_EXIT');
 need(trace.exitCode===1&&trace.compilerErrors.length===0&&trace.violations.length===0,'FAULT_ENVIRONMENT_OR_COMPILER_FAILURE');
 need(trace.transforms.some(row=>row.sourcePaths?.includes(override)&&row.succeeded),'FAULT_PRODUCT_NOT_TRANSFORMED');
 need(trace.vmExecutions?.some(row=>row.succeeded&&row.sourcePaths.includes(override)),'FAULT_PRODUCT_NOT_EXECUTED_IN_VM');
 need(loads.some(row=>row.repoRelative===suite.script),'FAULT_ORIGINAL_SUITE_NOT_LOADED');
 need(/AssertionError \[ERR_ASSERTION\]/.test(stderr),'FAULT_NOT_ORIGINAL_ASSERTION');
 const frames=[...stderr.matchAll(/file:\/\/\/[^\r\n)]+:(\d+):(\d+)/g)];
 const basename=path.basename(suite.script);const frame=frames.find(m=>decodeURIComponent(m[0]).includes(basename+':'));
 need(frame&&Number(frame[1])===expected.line,'FAULT_WRONG_ORIGINAL_ASSERTION',{expected,frames:frames.map(m=>m[0])});
 return {detected:true,failedName:fault.expectedName,originalScript:suite.script,line:expected.line,column:Number(frame[2]),exitCode:exit.exitCode,classification:'ORIGINAL_BUSINESS_ASSERTION',compileFailureCount:0};
}
export function sourceRows(repo,caseRoot,repoInputs,ownFiles){const rows=repoInputs.map(row=>{need(typeof row.path==='string'&&!path.isAbsolute(row.path)&&!row.path.split(/[\\\/]/).includes('..'),'REPO_RELATIVE_REQUIRED');return {path:row.path,sha256:row.sha256};});for(const file of ownFiles){safeRealPath(caseRoot,file);rows.push({path:path.resolve(file),sha256:hash(file)});}const result=[...new Map(rows.map(row=>[row.path,row])).values()];need(result.length<=MAX_INPUTS,'SOURCE_HASH_BUDGET_EXCEEDED');return result;}
export function checkClosure(trace,loads,repoRows,tools,ownFiles){
 const declared=new Map([...repoRows.map(row=>[path.resolve(row.actualPath).toLowerCase(),row.sha256]),...tools.map(row=>[path.resolve(row.path).toLowerCase(),row.sha256]),...ownFiles.map(file=>[path.resolve(file).toLowerCase(),hash(file)])]);
 for(const row of [...trace.reads,...loads]){need(declared.get(path.resolve(row.actualPath).toLowerCase())===row.sha256,'UNDECLARED_OR_CHANGED_ACTUAL_READ',row);}
 need(trace.compilerErrors.length===0&&trace.violations.length===0,'OBSERVER_EXECUTION_VIOLATION',trace.violations);
 return {actualReads:trace.reads.length,esmLoads:loads.length,declaredIdentityClosed:true};
}
