import {getCompleteHostSelfTestAdapter} from './complete-host-selftest/adapter.mjs';
import fs from 'node:fs';
import path from 'node:path';
import {commonHidRuntimeProjection,commonHidRuntimeCases,commonHidRuntimeInputs,win5KeptWakeCase} from '../../lib/common_hid_runtime_fixture.mjs';
import {validateInputResumeOptions,configureInputResumeAdapter,INPUT_RESUME_ID} from './input-resume-options.mjs';
/** Explicit candidate inputs for the existing simulator consumers. No shell expansion. */
const routes = {
  'fan.runtime-connection': {
    runtimeRoot: '-RuntimeRoot', sharedRuntimeRoot: '-SharedRuntimeRoot', payloadRoot: '-HostPayloadRoot',
  },
  'lighting.signal-consumer': {
    runtimeRoot: '--runtime-root', sharedRuntimeRoot: '--shared-runtime-root', hcVersion: '--hc-version',
    hcProfile: '--hc-profile', lightingPayloadRoot: '--host-payload-root', expectedHelperSha256: '--expected-helper-sha256',
  },
  'fan.open-side-effect-isolation': {
    legacyHostRoot: '--legacy-host-root', runtimeRoot: '--hc-assembly',
  },
  'fan.onex-lighting-coexistence': {
    payloadRoot: '--candidate-host-root', runtimeRoot: '--hc-assembly', fanToolLoadPlan: '--tool-load-plan', fanToolLoadPlanSha256: '--tool-load-plan-sha256',
  },
  'lifecycle.runtime-connectivity': {
    runtimeRoot: '--runtime-root', sharedRuntimeRoot: '--shared-runtime-root', payloadRoot: '--host-payload-root',
  },
};
function runtimeFiles(root, manifest) {
  const directory=path.resolve(root);
  let names=[];try{names=fs.readdirSync(directory,{withFileTypes:true}).filter(row=>row.isFile()&&/\.dll$/i.test(row.name)).map(row=>row.name);}catch{}
  return [...new Set([manifest, ...names])].sort().map(name=>path.join(directory,name));
}
function artifactInputs(id,options) {
 const files=[];
 if(id==='fan.onex-lighting-coexistence'&&options.fanToolLoadPlan)files.push(path.resolve(options.fanToolLoadPlan));
 if(options.runtimeRoot)files.push(...runtimeFiles(options.runtimeRoot,'HandheldCompanion.runtime.json'));
 if(options.sharedRuntimeRoot)files.push(...runtimeFiles(options.sharedRuntimeRoot,'manifest.json'));
 if(['fan.runtime-connection','lifecycle.runtime-connectivity','fan.onex-lighting-coexistence'].includes(id)&&options.payloadRoot)files.push(...['YeManFanHost.exe','YeManFanHost.dll','YeManFanBackend.dll'].map(name=>path.resolve(options.payloadRoot,name)));
 if(id==='fan.open-side-effect-isolation'&&options.legacyHostRoot)files.push(...['YeManFanHost.dll','YeManFanBackend.dll'].map(name=>path.resolve(options.legacyHostRoot,name)));
 if(id==='lighting.signal-consumer'&&options.lightingPayloadRoot){
  const root=path.resolve(options.lightingPayloadRoot);const candidates=[path.join(root,'YeManLightSetter.dll'),path.join(root,'light-setter/YeManLightSetter.dll')];
  files.push(candidates.find(file=>fs.existsSync(file))??candidates[0]);
 }
 return [...new Set(files)];
}
export function validateCommonHidSelection(options){
 const requested=['commonHidRuntimeOnly','commonHidRuntimeCase',...(options.inputResumeOnly===true?[]:['MainFile','HeaderRoot'])].some(key=>options[key]!==undefined);
 if(!requested)return null;
 if(options.commonHidRuntimeOnly!==true)throw Error('COMMON_HID_RUNTIME_ONLY_REQUIRED');
 if(typeof options.commonHidRuntimeCase!=='string'||!commonHidRuntimeCases.includes(options.commonHidRuntimeCase))throw Error('COMMON_HID_EXPLICIT_ORIGINAL_CASE_REQUIRED');
 if(options.tests?.length!==1||options.tests[0]!==commonHidRuntimeProjection.originalCanonicalId||options.modules?.length||options.bugPlan||options.fullAcceptance||options.requireAll||options.capabilities||!['plan','run'].includes(options.mode))throw Error('COMMON_HID_REQUIRES_ONE_EXPLICIT_ORIGINAL_TEST');
 if(options.profile!==undefined&&!['desktop','full'].includes(options.profile))throw Error('COMMON_HID_PROFILE_EXCLUDES_ORIGINAL_TEST');
 if(options.domains?.length&&!options.domains.includes('controller'))throw Error('COMMON_HID_DOMAIN_EXCLUDES_ORIGINAL_TEST');
 const incompatible=[...new Set(Object.values(routes).flatMap(route=>Object.keys(route)))];
 if(incompatible.some(key=>options[key]!==undefined)||options.completeHostContract!==undefined||options.coordinatorCase!==undefined)throw Error('COMMON_HID_UNRELATED_CANDIDATE_INPUT');
 if(options.commonHidRuntimeCase===win5KeptWakeCase&&options.MainFile!==undefined)throw Error('WIN5_KEPT_WAKE_DOES_NOT_CONSUME_MAIN_FILE');
 for(const key of ['MainFile','HeaderRoot'])if(options[key]!==undefined){
  const value=options[key];if(typeof value!=='string'||!path.isAbsolute(value)||/[\0\r\n]/.test(value))throw Error('COMMON_HID_ABSOLUTE_INPUT_REQUIRED:'+key);
  const stat=fs.statSync(value);if(key==='MainFile'?!stat.isFile():!stat.isDirectory())throw Error('COMMON_HID_INPUT_TYPE:'+key);
 }
 return {caseName:options.commonHidRuntimeCase,mainFile:options.MainFile,headerRoot:options.HeaderRoot};
}
export function validateInputResumeSelection(options){
 return validateInputResumeOptions(options,[...new Set(Object.values(routes).flatMap(route=>Object.keys(route)))]);
}
export function configureCandidateAdapters(adapters, options = {},repoRoot=path.resolve(import.meta.dirname,'../../..')) {
  const inputResume=validateInputResumeSelection(options);
  const commonHid=validateCommonHidSelection(options);
  const pin = options.expectedHelperSha256;
  if (pin !== undefined && (typeof pin !== 'string' || !/^[a-f0-9]{64}$/i.test(pin))) throw Error('INVALID_HELPER_CANDIDATE_PIN');
  if (Boolean(options.lightingPayloadRoot) !== Boolean(pin)) throw Error('LIGHTING_CANDIDATE_REQUIRES_ROOT_AND_REVIEWED_PIN');
  for (const key of new Set(Object.values(routes).flatMap(route => Object.keys(route)))) {
    if (options[key] !== undefined && (typeof options[key] !== 'string' || !options[key] || /[\0\r\n]/.test(options[key]))) throw Error(`INVALID_CANDIDATE_INPUT:${key}`);
  }
  if(options.completeHostContract!==undefined && (typeof options.completeHostContract!=='string'||!path.isAbsolute(options.completeHostContract)||/[\0\r\n]/.test(options.completeHostContract)))throw Error('EXPLICIT_COMPLETE_HOST_CONTRACT_REQUIRED');
  if(options.completeHostContract && !options.payloadRoot)throw Error('COMPLETE_HOST_CONTRACT_REQUIRES_PAYLOAD_ROOT');
  if(Boolean(options.fanToolLoadPlan)!==Boolean(options.fanToolLoadPlanSha256))throw Error('FAN_TOOL_PLAN_AND_SHA_REQUIRED');
  if(options.fanToolLoadPlan&&(!path.isAbsolute(options.fanToolLoadPlan)||!/^[a-f0-9]{64}$/.test(options.fanToolLoadPlanSha256)))throw Error('FAN_TOOL_PLAN_IDENTITY_INVALID');
  return adapters.map(adapter => {
    if(inputResume&&adapter.id===INPUT_RESUME_ID)return configureInputResumeAdapter(adapter,inputResume,repoRoot);
    if(commonHid&&adapter.id===commonHidRuntimeProjection.originalCanonicalId){
      const win5=commonHid.caseName===win5KeptWakeCase,inputs=commonHidRuntimeInputs(commonHid.caseName);
      const selectedMain=commonHid.mainFile??path.join(repoRoot,'native/main.cpp');
      const selectedHeader=commonHid.headerRoot??path.join(repoRoot,'native');
      const relativeInputs=inputs.sources.filter(name=>name!=='native/main.cpp');
      const files=[...(win5?[]:[selectedMain]),...inputs.headers.map(name=>win5||fs.existsSync(path.join(selectedHeader,name))?path.join(selectedHeader,name):path.join(repoRoot,'native',name))];
      const external=[],sources=[...relativeInputs,'tools/ymcc_sim/global/candidate-options.mjs','tools/ymcc_sim/global/cli.mjs','tools/ymcc_sim/optimization/cli.mjs','tools/ymcc_sim/global/runner.mjs'];
      for(const file of files){const relative=path.relative(repoRoot,path.resolve(file));if(relative==='..'||relative.startsWith('..'+path.sep)||path.isAbsolute(relative))external.push(path.resolve(file));else sources.push(relative.replaceAll('\\','/'));}
      return {...adapter,title:'Original common-HID runtime projection: '+commonHid.caseName,path:'tools/input_frame_submission_selftest.mjs',
        args:['--out','{outDir}/controller-input','--common-hid-runtime-only','--common-hid-runtime-case',commonHid.caseName,...(win5?[]:['--MainFile',selectedMain]),'--HeaderRoot',selectedHeader],
        timeoutMs:300000,resultFiles:['controller-input/LATEST.json'],evidenceInputs:[...new Set(sources)],artifactInputs:[...new Set(external)],...(win5?{cacheAcceptance:false,inheritExecutionDeadline:true}:{}),
        originalProjection:{canonicalTestId:adapter.id,caseName:commonHid.caseName,defaultParentDispatched:false,wholeRuntimeAcceptance:false}};
    }
    if(adapter.id==='fan.complete-host-selftest' && options.completeHostContract){
      const contractPath=path.resolve(options.completeHostContract),contract=JSON.parse(fs.readFileSync(contractPath,'utf8').replace(/^\uFEFF/,''));
      const configured=getCompleteHostSelfTestAdapter({contractPath,nodeExecutable:process.execPath,payloadRoot:path.resolve(options.payloadRoot),contract});
      return {...configured,evidenceInputs:[...new Set([...configured.evidenceInputs,'tools/ymcc_sim/global/candidate-options.mjs','tools/ymcc_sim/global/cli.mjs','tools/ymcc_sim/domains/global/global_domain.ps1'])]};
    }
    const route = routes[adapter.id];
    if (!route) return adapter;
    const values = Object.entries(route).flatMap(([key, flag]) => {
      if(options[key]===undefined)return [];
      if(['fan.open-side-effect-isolation','fan.onex-lighting-coexistence'].includes(adapter.id) && key==='runtimeRoot')return [flag,path.join(options[key],'HandheldCompanion.dll')];
      return [flag,options[key]];
    });
    return { ...adapter,
      artifactInputs: [...new Set([...(adapter.artifactInputs??[]),...artifactInputs(adapter.id,options)])],
      args: [...adapter.args, ...values],
      evidenceInputs: [...new Set([...adapter.evidenceInputs, 'tools/ymcc_sim/global/candidate-options.mjs', 'tools/ymcc_sim/global/cli.mjs', 'tools/ymcc_sim/global/runner.mjs', 'tools/ymcc_sim/domains/global/global_domain.ps1'])],
    };
  });
}
