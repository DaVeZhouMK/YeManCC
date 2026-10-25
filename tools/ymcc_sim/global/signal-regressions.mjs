/** Fixed entry for real-product fan/input signal regression suites. */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath,pathToFileURL } from 'node:url';
import { SIGNAL_CONSUMERS } from './signal-replay.mjs';
import { sourcePath } from './runner.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
export async function runSignalRegressions({domain,repoRoot=root,workDir}={}) {
  if (!Object.hasOwn(SIGNAL_CONSUMERS,domain)) throw Error('INVALID_SIGNAL_DOMAIN');
  const spec=SIGNAL_CONSUMERS[domain],name={fan:'runFanSignalRegressions',input:'runInputSignalRegressions',performance:'runGpuLogRegressions'}[domain];
  const fn=(await import(pathToFileURL(sourcePath(repoRoot,spec.file)).href))[name];
  if(typeof fn!=='function') throw Error('SIGNAL_REGRESSION_NOT_CONNECTED');
  const result=await fn({repoRoot,workDir});
  const cases=result?.cases??result?.inputConsumerStateFeedback;
  if(!result||!['PASS','FAIL','BLOCKED','INCOMPLETE','BLOCKED_COMPATIBILITY_DIFFERENCE'].includes(result.status)||!Array.isArray(cases)||!cases.length)throw Error('REGRESSION_RECEIPT_INVALID');
  return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
  let output;
  try{
    let domain;for(let i=2;i<process.argv.length;i+=2){const key=process.argv[i],value=process.argv[i+1];if(!value)throw Error('ARGUMENT_MISSING');if(key==='--domain')domain=value;else if(key==='--output')output=path.resolve(value);else throw Error('ARGUMENT_UNKNOWN');}
    if(!output)throw Error('OUTPUT_MISSING');
    const result=await runSignalRegressions({domain,workDir:path.join(path.dirname(output),'work')});fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output,JSON.stringify(result,null,2));
    console.log(`SIGNAL_REGRESSIONS ${domain} ${result.status}; cases=${(result.cases??result.inputConsumerStateFeedback).length}`);process.exitCode=result.status==='PASS'?0:result.status==='FAIL'?2:10;
  }catch(error){if(output){fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output,JSON.stringify({status:'BLOCKED',code:/^[A-Z_]{1,80}$/.test(error.message)?error.message:'SIGNAL_REGRESSION_EXECUTION_FAILED'},null,2));}console.error('SIGNAL_REGRESSIONS BLOCKED');process.exitCode=10;}
}
