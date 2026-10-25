import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {ID, MODULE, REPO, HELPER, BASELINE, COMPONENT, evidenceInputs, reviewedSources, reviewedTools, toolManifest} from './adapter.mjs';
import {sha, need, privateOrderingSource, natural, verifyFeedback, CURRENT_SHA, BEFORE_SHA, HELPER_SHA, BASELINE_SHA, ORDERING_CONTRACT_EPOCH, HISTORICAL_CONTRACT} from './verify.mjs';
import {runProcess, sourcePath} from '../runner.mjs';

const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const write = (file, value) => fs.writeFileSync(file, JSON.stringify(value, null, 2) + '\n');
const hash = file => sha(fs.readFileSync(file));
const canonical = file => fs.realpathSync(file);
const slash = file => file.replaceAll('\\', '/');
const inside = (root, file) => { const rel=path.relative(root,file); return rel==='' || (rel!=='..' && !rel.startsWith('..'+path.sep) && !path.isAbsolute(rel)); };
const walk = root => fs.readdirSync(root, {withFileTypes:true}).flatMap(entry => {
  const file=path.join(root,entry.name); need(!entry.isSymbolicLink(), 'LIGHTING_EVIDENCE_LINK_REFUSED');
  return entry.isDirectory() ? walk(file) : [file];
});
export function verifyProbeIdentity(positive, fault, component, privateComponent) {
  const normalize = (text, file) => {
    const literal=JSON.stringify(file); need(text.split(literal).length===2, 'LIGHTING_PROBE_COMPONENT_ANCHOR');
    return text.replace(literal, '"<selected-component>"');
  };
  need(normalize(positive,component)===normalize(fault,privateComponent), 'LIGHTING_SHARED_PROBE_ASSERTIONS_CHANGED');
  return {assertionsIdentical:true, positiveSha256:sha(positive), faultSha256:sha(fault)};
}
export function verifyObservation(reports, {repo, root, out, component, sourceSha256, negative, sources, tools, manifest}) {
  const expectedExit=negative ? 1 : 0;
  need(reports.length===2 && new Set(reports.map(row=>row.pid)).size===2, 'LIGHTING_NODE_OBSERVATION_SET');
  const helper=reports.find(row=>row.entry===path.join(repo,HELPER));
  const probePath=path.join(out,'real-component-feedback-probe.mjs');
  const probe=reports.find(row=>row.entry===probePath);
  need(helper && probe, 'LIGHTING_ACTUAL_HELPER_AND_PROBE_REQUIRED');
  const expectedArgs=negative ? [path.join(repo,HELPER),out,'--component-source',component] : [path.join(repo,HELPER),out];
  need(JSON.stringify(helper.argv)===JSON.stringify(expectedArgs) && JSON.stringify(probe.argv)===JSON.stringify([probePath,out]), 'LIGHTING_DEFAULT_POSITIVE_OR_PRIVATE_FAULT_ARGUMENTS');
  const allowedSources=new Set(sources.map(file=>canonical(path.join(repo,file))));
  const allowedTools=new Set(tools.map(canonical));
  const childLedger=[];
  for(const report of reports) {
    natural({...report.terminal, pid:report.pid, startedUtc:report.startedUtc}, 'node-observer', expectedExit);
    need(report.schemaVersion===1 && report.terminal.event==='exit' && report.executable===manifest.node &&
      Array.isArray(report.drift) && report.drift.length===0 && Array.isArray(report.observationErrors) && report.observationErrors.length===0,
      'LIGHTING_OBSERVER_OR_INPUT_DRIFT');
    for(const [kind, allowed] of [['sources',allowedSources],['tools',allowedTools]]) {
      need(Array.isArray(report[kind]), 'LIGHTING_OBSERVED_INPUTS_MISSING');
      for(const row of report[kind]) need(allowed.has(canonical(row.path)) && hash(row.path)===row.sha256,
        `LIGHTING_UNREVIEWED_OR_CHANGED_${kind.toUpperCase()}:${row.path}`);
    }
    for(const row of report.generatedInputs) need(inside(root,canonical(row.path)) &&
      [probePath,component].includes(row.path) && hash(row.path)===row.sha256, 'LIGHTING_GENERATED_INPUT_NOT_REVIEWED');
    for(const child of report.children) {
      need(allowedTools.has(canonical(child.executable)) && child.terminal===true, 'LIGHTING_CHILD_NOT_REVIEWED_OR_UNFINISHED');
      natural(child,'actual-child',child.kind==='spawnSync' ? expectedExit : 0);
      childLedger.push({parentPid:report.pid,...child});
    }
  }
  need(helper.children.length===1 && helper.children[0].kind==='spawnSync' && helper.children[0].pid===probe.pid &&
    probe.ppid===helper.pid && helper.children[0].executable===manifest.node &&
    JSON.stringify(helper.children[0].args)===JSON.stringify([probePath,out]), 'LIGHTING_ACTUAL_PROBE_CHILD_REQUIRED');
  const compiler=probe.children.find(row=>row.executable===manifest.esbuildBinary);
  const browser=probe.children.find(row=>row.executable===manifest.browser);
  need(probe.children.length===2 && compiler && browser && compiler.args.some(arg=>arg.startsWith('--service=')) &&
    browser.args.includes('--headless') && browser.args.includes('--remote-debugging-pipe') &&
    browser.args.some(arg=>/^--user-data-dir=.*playwright_chromiumdev_profile-/.test(arg)), 'LIGHTING_ACTUAL_COMPILER_AND_OWNED_BROWSER_REQUIRED');
  need(helper.builds.length===0 && probe.builds.length===1, 'LIGHTING_ACTUAL_BUILD_REQUIRED');
  const build=probe.builds[0];
  need(build.outputs.length===1 && build.outputs[0].bytes>0 && hash(build.outputs[0].path)===build.outputs[0].sha256 &&
    hash(build.metafile)===build.metafileSha256, 'LIGHTING_COMPILER_OUTPUT_IDENTITY');
  const compilerSources=new Set();
  for(const input of build.inputs) if(!input.virtual) {
    const file=canonical(input.path);
    need(allowedSources.has(file) || allowedTools.has(file), `LIGHTING_COMPILER_INPUT_NOT_REVIEWED:${input.path}`);
    if(allowedSources.has(file)) compilerSources.add(slash(path.relative(repo,file)));
  }
  for(const file of reviewedSources(repo).filter(file=>!file.endsWith('.css'))) need(compilerSources.has(file), `LIGHTING_PRODUCT_COMPILER_INPUT_MISSING:${file}`);
  const actualReads=probe.sources.concat(probe.generatedInputs);
  need(actualReads.some(row=>row.path===component && row.sha256===sourceSha256), 'LIGHTING_ACTUAL_COMPONENT_BYTES_NOT_READ');
  need(probe.sources.some(row=>slash(path.relative(repo,row.path))==='src/styles/tokens.css'), 'LIGHTING_ACTUAL_CSS_READ_MISSING');
  return {nodeReports:reports,childLedger,compiled:build,executedProductModules:[...compilerSources].sort(),
    browser:{pid:browser.pid,executable:browser.executable,headless:true,ownedTemporaryProfile:true,exitCode:browser.exitCode}};
}
export async function runLightingFeedbackOrdering(evidenceDir, {repoRoot=REPO}={}) {
  const repo=canonical(repoRoot), root=path.resolve(evidenceDir), steps=[];
  need(!fs.existsSync(root), 'LIGHTING_EVIDENCE_DIRECTORY_ALREADY_EXISTS');
  fs.mkdirSync(root,{recursive:true});
  let sourceRows=[], toolRows=[];
  try {
    const sources=evidenceInputs(repo), tools=reviewedTools(repo), manifest=toolManifest(repo);
    need(tools.length<=640 && new Set(tools).size===tools.length && tools.includes(canonical(process.execPath)) && manifest.node===canonical(process.execPath), 'LIGHTING_TOOLCHAIN_NOT_REVIEWED');
    sourceRows=sources.map(relative=>{
      const file=sourcePath(repo,relative), copy=path.join(root,'input-sources',relative);
      fs.mkdirSync(path.dirname(copy),{recursive:true}); fs.copyFileSync(file,copy);
      return {path:relative,sha256:hash(file),copy};
    });
    toolRows=tools.map(file=>({path:file,sha256:hash(file)}));
    write(path.join(root,'inputs-before.json'),{sources:sourceRows,tools:toolRows});
    need(hash(path.join(repo,HELPER))===HELPER_SHA && hash(path.join(repo,BASELINE))===BASELINE_SHA, 'LIGHTING_SHARED_HELPER_OR_ORIGINAL_BASELINE_IDENTITY');
    const current=fs.readFileSync(path.join(root,'input-sources',COMPONENT));
    const historical=privateOrderingSource(current), privateComponent=path.join(root,'private-fault','ControllerFeedbackCapabilitiesCard.ordering-guards-disconnected.vue');
    fs.mkdirSync(path.dirname(privateComponent)); fs.writeFileSync(privateComponent,historical);
    write(path.join(root,'private-fault','proof.json'),{source:COMPONENT,currentSha256:sha(current),privateFaultSha256:sha(historical),
      sourceContractEpoch:ORDERING_CONTRACT_EPOCH,previousContract:HISTORICAL_CONTRACT,removedLines:2,
      retained:'request counter/increment and complete activation/deactivation lifecycle',
      mode:'only two ordering guards removed from current source; not a historical source image',consumerFixInjected:false,assertionsChanged:false});
    async function execute(label,negative) {
      const dir=path.join(root,label),out=path.join(dir,'out'),trace=path.join(dir,'trace'); fs.mkdirSync(dir);
      const component=negative ? privateComponent : path.join(repo,COMPONENT), sourceSha256=negative ? BEFORE_SHA : CURRENT_SHA;
      const configPath=path.join(dir,'config.json'), argumentsPath=path.join(dir,'arguments.json');
      write(configPath,{repo,trace,evidenceRoot:root,node:manifest.node,helper:path.join(repo,HELPER),
        helperArguments:negative ? [out,'--component-source',component] : [out],observer:path.join(repo,MODULE,'observe.cjs'),esbuildBinary:manifest.esbuildBinary});
      write(argumentsPath,[configPath]);
      const env={...process.env,NO_COLOR:'1'}; delete env.NODE_OPTIONS; delete env.NODE_PATH;
      const startedUtc=new Date().toISOString();
      const processResult=await runProcess(manifest.pwsh,['-NoProfile','-NonInteractive','-File',path.join(repo,'tools/ymcc_sim/global/host-process.ps1'),
        '-ScriptPath',path.join(repo,MODULE,'Run-Helper.ps1'),'-ArgumentsPath',argumentsPath],{cwd:repo,env,timeoutMs:90000});
      const finishedUtc=new Date().toISOString();
      fs.writeFileSync(path.join(dir,'stdout.log'),processResult.stdout || ''); fs.writeFileSync(path.join(dir,'stderr.log'),processResult.stderr || '');
      const {stdout,stderr,...terminal}=processResult; steps.push({label,startedUtc,finishedUtc,...terminal,ownedJob:true});
      write(path.join(root,'process-ledger.json'),{steps}); natural(steps.at(-1),label,negative ? 1 : 0);
      const feedback=read(path.join(out,'feedback-result.json'));
      const semantic=verifyFeedback({cases:read(path.join(out,'ui-feedback-cases.json')),ordering:read(path.join(out,'ordering-result.json')),feedback,
        browser:fs.existsSync(path.join(out,'browser-results.json')) ? read(path.join(out,'browser-results.json')) : null,
        stdout:feedback.stdout,stderr:feedback.stderr,exitCode:processResult.exitCode,component,sourceSha256,negative});
      const reports=fs.readdirSync(trace).filter(name=>/^node-\d+\.json$/.test(name)).map(name=>read(path.join(trace,name)));
      const observed=verifyObservation(reports,{repo,root,out,component,sourceSha256,negative,sources,tools,manifest});
      const actualProbe=observed.nodeReports.find(row=>row.entry===path.join(out,'real-component-feedback-probe.mjs'));
      const actualChild=observed.childLedger.find(row=>row.pid===actualProbe.pid);
      need(fs.readFileSync(actualChild.stdoutFile,'utf8')===feedback.stdout && fs.readFileSync(actualChild.stderrFile,'utf8')===feedback.stderr &&
        fs.readFileSync(path.join(out,'feedback-probe.log'),'utf8')===feedback.stdout+'\n'+feedback.stderr, 'LIGHTING_ORIGINAL_LOGS_NOT_PRESERVED');
      write(path.join(dir,'validated.json'),{semantic,observed});
      return {semantic,observed,probe:path.join(out,'real-component-feedback-probe.mjs'),component};
    }
    const positive=await execute('positive',false), fault=await execute('fault-current-ordering',true);
    const probeIdentity=verifyProbeIdentity(fs.readFileSync(positive.probe,'utf8'),fs.readFileSync(fault.probe,'utf8'),positive.component,fault.component);
    need(positive.observed.compiled.outputs[0].sha256!==fault.observed.compiled.outputs[0].sha256, 'LIGHTING_FAULT_NOT_COMPILED');
    const sourceAfter=sourceRows.map(row=>({path:row.path,sha256:hash(sourcePath(repo,row.path)),snapshotSha256:hash(row.copy)}));
    const toolAfter=toolRows.map(row=>({path:row.path,sha256:hash(row.path)}));
    write(path.join(root,'inputs-after.json'),{sources:sourceAfter,tools:toolAfter});
    need(sourceAfter.every((row,i)=>row.sha256===sourceRows[i].sha256 && row.snapshotSha256===sourceRows[i].sha256) &&
      toolAfter.every((row,i)=>row.sha256===toolRows[i].sha256) && hash(privateComponent)===BEFORE_SHA, 'LIGHTING_INPUTS_CHANGED_DURING_RUN');
    const result={schemaVersion:1,id:ID,status:'PASS',passed:true,sourceContractEpoch:ORDERING_CONTRACT_EPOCH,previousContract:HISTORICAL_CONTRACT,positive:positive.semantic,privateFault:fault.semantic,probeIdentity,
      sourceHashes:[...sourceRows.map(({path,sha256})=>({path,sha256})),...walk(root).map(file=>({path:file,sha256:hash(file)}))],
      toolchain:{files:toolRows},executedProductModules:positive.observed.executedProductModules,
      childLedger:[...positive.observed.childLedger,...fault.observed.childLedger],
      inputConsumerStateFeedback:{input:'Real mounted controls, two debounced brightness requests (53 then 74), newer reply resolved before older reply',
        consumer:'Current ControllerFeedbackCapabilitiesCard -> saveFeedback -> applyLed -> memory setLed -> rendered role=status',
        state:'Four existing receipt scenarios and four ordering scenarios at both 1280x720 and 1280x800',feedback:'Original DOM status and API arguments; eight exact historical failures retained'},
      boundary:{hardwareIo:false,physicalEffectVerified:false,productAppStarted:false,hostStarted:false,settingsAndBridge:'memory fixtures',browser:'owned headless Edge temporary profile'},
      remainingGaps:['Full navigation acceptance remains subject to the separate controller.ui-navigation gate; this leaf does not prove it.','Physical devices, Host startup and installed application acceptance remain separate.']};
    write(path.join(root,'result.json'),result); return result;
  } catch(error) {
    write(path.join(root,'failure.json'),{status:'FAIL',code:error.message,steps,originalFailurePreserved:true});
    throw error;
  }
}
if(process.argv[1] && path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
  try {
    const args=process.argv.slice(2); need(args.length===2 && args[0]==='--evidence-dir' && args[1], 'LIGHTING_ARGUMENTS_INVALID');
    const result=await runLightingFeedbackOrdering(args[1]);
    console.log(JSON.stringify({id:ID,status:result.status,originalReceipts:4,ordering:8,privateOrderingFailures:8}));
  } catch(error) { console.error(error.stack || error.message); process.exitCode=2; }
}
