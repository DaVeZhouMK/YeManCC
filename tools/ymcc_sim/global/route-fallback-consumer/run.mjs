import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
import {CASE_ID,MODULE,PRODUCT,FILES,FAULTS,hash,need,write,sourcePins,verifySources,lockTools,verifyTools,runtimeTools} from './contract.mjs';
import {buildConsumer} from './build.mjs';
import {runCases,CASE_IDS} from './browser-cases.mjs';
import {checkUiBrowserTerminal} from '../ui-navigation/tool-inputs.mjs';
export async function main(args=process.argv.slice(2)) {
  const options={};for(let i=0;i<args.length;i+=2){need(['--repo-root','--evidence-dir'].includes(args[i])&&args[i+1]&&!Object.hasOwn(options,args[i]),'ARGUMENT_REJECTED');options[args[i]]=args[i+1];}
  const root=path.resolve(options['--repo-root']??path.resolve(import.meta.dirname,'../../../..'));
  const out=options['--evidence-dir'];need(typeof out==='string'&&path.isAbsolute(out),'EXPLICIT_EVIDENCE_REQUIRED');
  const relative=path.relative(root,out);need(!(!relative||(!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep))),'EVIDENCE_OUTSIDE_REPOSITORY_REQUIRED');
  need(!fs.existsSync(out),'FRESH_EVIDENCE_REQUIRED');fs.mkdirSync(out,{recursive:true});
  const started=Date.now(),normal=[],negative=[],builds={},errors=[];
  let sources,tools,browserServer,browser,terminal,browserProof,deadline,toolAfter,actualRuntime;
  const priorTemp=process.env.TEMP,priorTmp=process.env.TMP;
  let timedOut=false;
  try {
    sources=sourcePins(root);tools=lockTools(root);
    write(path.join(out,'source-before.json'),sources);
    write(path.join(out,'tool-before.json'),{artifacts:tools.artifacts,authority:tools.authority,uiManifest:tools.ui.manifestSha256});
    fs.writeFileSync(path.join(out,'actual-product.ts'),fs.readFileSync(path.join(root,PRODUCT)));
    for(const fault of [null,...FAULTS]){const phase=fault?.id??'normal';builds[phase]=await buildConsumer(root,path.join(out,phase),tools,fault);}
    const temp=path.join(out,'owned-browser-temp');fs.mkdirSync(temp);process.env.TEMP=temp;process.env.TMP=temp;
    const {chromium}=createRequire(import.meta.url)(tools.playwrightEntry);
    browserServer=await chromium.launchServer({executablePath:tools.ui.executables.edge,headless:true,
      host:'127.0.0.1',args:['--disable-background-networking','--disable-component-update','--no-first-run'],
      downloadsPath:path.join(temp,'downloads'),env:{...process.env,TEMP:temp,TMP:temp}});
    const child=browserServer.process();terminal=new Promise(resolve=>child.once('close',(exitCode,signal)=>resolve({exitCode,signal})));
    browser=await chromium.connect(browserServer.wsEndpoint());
    const remaining=29000-(Date.now()-started);need(remaining>0,'THIRTY_SECOND_SETUP_BUDGET_EXHAUSTED');
    deadline=setTimeout(()=>{timedOut=true;void browserServer.close();},remaining);
    normal.push(...await runCases(browser,path.join(out,'normal','consumer.js')));
    for(const fault of FAULTS){const rows=await runCases(browser,path.join(out,fault.id,'consumer.js'),{phase:fault.id,only:[fault.caseId]});
      const detected=rows.length===1&&rows[0].status==='FAIL'&&rows[0].assertionName==='AssertionError'&&rows[0].error.includes(fault.expected);
      const record={id:fault.id,status:'FAIL',expectedFailureDetected:detected,sourceMutation:builds[fault.id].product,designatedAssertion:fault.expected,cases:rows};
      write(path.join(out,fault.id,'raw-failure.json'),record);negative.push(record);
      console.log('MUTANT FAIL '+fault.id+' expectedFailureDetected='+detected);
    }
  }catch(error){errors.push({name:error.name,message:error.message,stack:error.stack});console.error('ROUTE_CONSUMER_ERROR '+error.stack);}
  finally {
    clearTimeout(deadline);
    try{await browser?.close();await browserServer?.close();if(browserServer&&terminal){browserProof=checkUiBrowserTerminal(tools.ui,browserServer.process(),await terminal);browserProof.command=browserServer.process().spawnargs;}}
    catch(error){errors.push({phase:'owned-browser-terminal',message:error.message});}
    if(priorTemp===undefined)delete process.env.TEMP;else process.env.TEMP=priorTemp;
    if(priorTmp===undefined)delete process.env.TMP;else process.env.TMP=priorTmp;
    try{if(tools){toolAfter=verifyTools(root,tools);actualRuntime=runtimeTools(tools);}if(sources)verifySources(root,sources);
      for(const build of Object.values(builds))for(const file of build.outputs)need(hash(file.path)===file.sha256,'COMPILED_CONSUMER_CHANGED');}
    catch(error){errors.push({phase:'postflight-identity',message:error.message});}
  }
  const elapsedMs=Date.now()-started;
  const status=!timedOut&&elapsedMs<=30000&&!errors.length&&normal.length===CASE_IDS.length&&normal.every(row=>row.status==='PASS')&&negative.length===FAULTS.length&&negative.every(row=>row.expectedFailureDetected)?'PASS':'FAIL';
  const report={schemaVersion:1,id:CASE_ID,status,pass:status==='PASS',budgetMs:30000,elapsedMs,timedOut,
    actualConsumer:{product:PRODUCT,productSha256:sources?.find(row=>row.path===PRODUCT)?.sha256,module:MODULE,files:FILES},
    sourcePins:sources,builds,cases:normal,negativeDetections:negative,errors,browserProof,
    toolScope:tools?{completeArtifacts:tools.artifacts,authority:tools.authority,uiManifestSha256:tools.ui.manifestSha256,postflight:toolAfter,actualRuntime}:null,
    effectBoundary:['Real DOM and event dispatch/focus/disabled click','Injected window proxy binds native event methods; location.reload only increments a memory counter','Only two imported bridge effects are controlled memory promises','Owned page remains about:blank; all page network requests aborted'],
    lifecycleReview:{persistence:'persistent',status:'INCOMPLETE',normalClearCase:'clear-returns-listener-inert-and-late-semantic',
      retainedByDesign:['startupFailure','recoveryBusy'],gaps:['No public force-exit/fallback-destruction API','No sleep/current-generation-ready/healthy-recovery API','Late detached callbacks must be observed, not hidden by page.close','Normal DOM cleanup is not full parent lifecycle acceptance'],
      fullPersistentAcceptance:false},
    fullProductAcceptance:false,hardwareAcceptance:false,installedConfigurationMutated:false,browserLocationMutated:false,
    realSleepPerformed:false,productHostStarted:false};
  write(path.join(out,'result.json'),report);
  write(path.join(out,'tool-after.json'),{toolAfter,actualRuntime,browserProof,sourceStable:!errors.some(row=>row.phase==='postflight-identity')});
  console.log(JSON.stringify({id:CASE_ID,status,cases:normal.length,passed:normal.filter(row=>row.status==='PASS').length,failed:normal.filter(row=>row.status==='FAIL').length,negativeDetected:negative.filter(row=>row.expectedFailureDetected).length,elapsedMs,budgetMs:30000,lifecycleAcceptance:'INCOMPLETE',result:path.join(out,'result.json')}));
  if(status==='PASS')console.log('PASS '+CASE_ID);
  return status==='PASS'?0:2;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))main().then(code=>process.exitCode=code).catch(error=>{console.error(error.stack);process.exitCode=2;});
