import {retiredReadAssertionString} from './assert-format.mjs';
import path from 'node:path';
export const need=(value,code)=>{if(!value)throw Error(code);};
export function within(root,file){const rel=path.relative(path.resolve(root),path.resolve(file));return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));}
export function mutateSource(text,fault){need(text.split(fault.before).length===2,'FAN_FAILED_READ_FAULT_ANCHOR');return text.replace(fault.before,fault.after);}
export function verifyOriginalIdentityAssertion(text,spec){const lines=text.split(/\r?\n/);need(spec.fault.expectedOriginalAssertionLine===49&&lines[48]?.includes("assert.equal(failureReads,0,'already-retired failure still queried Host')")&&text.split("'already-retired failure still queried Host'").length===2,'FAN_FAILED_READ_ORIGINAL_ASSERTION_LINE_CHANGED');return {line:49,message:'already-retired failure still queried Host'};}
export function verifyReport(report,spec,{fault=false}={}){
 need(spec.fault.expectedOriginalAssertionLine===49&&spec.fault.expectedFailures.length===12&&spec.fault.expectedFailures.every(row=>row.assertionLine===49&&row.error===retiredReadAssertionString()),'FAN_FAILED_READ_FIXED_ASSERTION_FORMAT_OR_LINE');
 need(report&&JSON.stringify(Object.keys(report).sort())===JSON.stringify(['checks','ok','results','scope'])&&report.checks===45&&typeof report.ok==='boolean'&&report.scope===spec.originalScope&&Array.isArray(report.results)&&report.results.length===45,'FAN_FAILED_READ_FINAL45_REQUIRED');
 need(JSON.stringify(report.results.map(row=>row.name))===JSON.stringify(spec.expectedCaseNames),'FAN_FAILED_READ_ORDERED_NAMES_CHANGED');const failures=[];
 for(const [index,row] of report.results.entries()){
  need(typeof row.ok==='boolean','FAN_FAILED_READ_ROW_STATUS');
  if(row.ok)need(row.evidence&&typeof row.evidence==='object'&&!Array.isArray(row.evidence)&&!Object.hasOwn(row,'error'),'FAN_FAILED_READ_SUCCESS_EVIDENCE');
  else{need(typeof row.error==='string'&&row.error.startsWith('AssertionError [ERR_ASSERTION]:')&&Array.isArray(row.calls)&&Array.isArray(row.observations),'FAN_FAILED_READ_NONASSERTION_FAILURE');failures.push({index,name:row.name,error:row.error,assertionLine:49});}
 }
 if(fault){need(report.ok===false&&JSON.stringify(failures)===JSON.stringify(spec.fault.expectedFailures),'FAN_FAILED_READ_EXACT_PREDICTED_ASSERTIONS_NOT_DETECTED');for(const row of report.results.filter(row=>!row.ok))need(row.observations.length===1&&row.observations[0].failureReads===1&&row.observations[0].retiredAdoptions===0&&row.observations[0].when==='during-write','FAN_FAILED_READ_ORIGINAL_IDENTITY_EVIDENCE');}
 else need(report.ok===true&&failures.length===0,'FAN_FAILED_READ_ORIGINAL_FAILURE_PRESERVED');
 return {checks:45,passedRows:45-failures.length,failedRows:failures.length,exactFailures:failures,caseNames:report.results.map(row=>row.name),originalAssertionsUnchanged:true};
}
export function verifyExecution(raw,spec,{fault=false}={}){
 need(Number.isInteger(raw.exitCode)&&raw.exitCode===(fault?1:0)&&!raw.error&&!raw.signal,'FAN_FAILED_READ_PROCESS_NOT_COMPLETE');const b=raw.boundary,o=raw.observation;
 need(b?.kind==='fan-global-runtime-boundary'&&b.exitCode===raw.exitCode&&b.status===(fault?'FAIL':'PASS')&&b.explicitExitCalled===false&&Array.isArray(b.denied)&&b.denied.length===0,'FAN_FAILED_READ_RUNTIME_BOUNDARY');
 for(const key of ['realNetwork','realChildProcess','nativeLibraryLoad','writesOutsideEvidence','realHostMain','hardwareAccess','operatingSystemSleep'])need(b.boundaries?.[key]===false,'FAN_FAILED_READ_REAL_IO');
 need(o?.beforeExitObserved===true&&o.exitCode===raw.exitCode&&o.nodeWindowAbsentAtStart===true&&o.nodeChromeAbsentAtStart===true&&o.nodeWindowAbsentAtEnd===true&&o.nodeChromeAbsentAtEnd===true&&Array.isArray(o.activeResources)&&!o.activeResources.includes('Timeout'),'FAN_FAILED_READ_NATURAL_END_OR_NATIVE_ENVIRONMENT');
 let report;try{report=JSON.parse(raw.stdout.trim());}catch{throw Error('FAN_FAILED_READ_SINGLE_ORIGINAL_JSON_REQUIRED');}
 return {...verifyReport(report,spec,{fault}),naturalOriginalEndpoint:true,report};
}
export function summarizeActualCoverage(coverage,program){
 need(coverage&&Array.isArray(coverage.functions),'FAN_FAILED_READ_PROGRAM_COVERAGE_MISSING');const ranges=coverage.functions.flatMap(fn=>fn.ranges);
 const occurrences=[...program.matchAll(/webview\.postMessage\(/g)].map(match=>match.index);need(occurrences.length===1,'FAN_FAILED_READ_ACTUAL_NATIVE_DISPATCH_SITE');
 const dispatch=occurrences.map(offset=>{const candidates=ranges.filter(row=>row.startOffset<=offset&&row.endOffset>offset).sort((a,b)=>(a.endOffset-a.startOffset)-(b.endOffset-b.startOffset));need(candidates.length>0,'FAN_FAILED_READ_DISPATCH_COVERAGE_MISSING');return {offset,source:'webview.postMessage(',count:candidates[0].count,actualRange:candidates[0]};});need(dispatch.every(row=>row.count===0),'FAN_FAILED_READ_NATIVE_DISPATCH_EXECUTED');
 const functions=name=>coverage.functions.filter(fn=>fn.functionName===name).map(fn=>({name,startOffset:fn.ranges[0].startOffset,endOffset:fn.ranges[0].endOffset,count:fn.ranges[0].count}));
 return {kind:'actual-unmodified-program-v8-coverage',nativePostMessageSites:dispatch,nativePostMessageCalls:0,invokeFunctions:functions('invoke'),findExactFunctions:functions('findExact'),resyncFunctions:functions('resyncSessionFromHost'),sourceRewrittenForCoverage:false};
}
