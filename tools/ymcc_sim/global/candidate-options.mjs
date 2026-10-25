import {getCompleteHostSelfTestAdapter} from './complete-host-selftest/adapter.mjs';
import fs from 'node:fs';
import path from 'node:path';
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
export function configureCandidateAdapters(adapters, options = {}) {
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
