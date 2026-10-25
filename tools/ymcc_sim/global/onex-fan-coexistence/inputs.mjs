import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { BlockedInputError, resolveCecil } from '../fan-open-signals/inputs.mjs';
export { BlockedInputError };
export const baselineRelativeRoot = 'tools/ymcc_sim/global/onex-fan-coexistence/fixtures/host7f-light-e546';
export const baselinePins = Object.freeze({
  'YeManFanHost.dll': '7f4136c87fe97b10c375b1e2e53d2074f6fc1899e34a4b2a37203ea633adcc5f',
  'YeManFanBackend.dll': 'f8fdbd516d2a002fa65e1cad5bc02405f16ec7b04208d4f6a8c2d26f330dea09',
  'YeManLightSetter.dll': 'e5460cfcfd1e73ad70b48a0bf8be70176c2115e2f1c655b9105f9e4de33dbadf',
});
export const hcPin = '08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583';
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const isFile = file => fs.existsSync(file) && fs.statSync(file).isFile();
export function parseArgs(args) {
  const flags = new Map([['--parent-build-request','parentBuildRequest'],['--parent-build-request-sha256','parentBuildRequestSha256'],['--prepared-parent-build','preparedParentBuild'],['--prepared-parent-build-sha256','preparedParentBuildSha256'],['--evidence-dir','evidenceDir'], ['--candidate-host-root','candidateHostRoot'],
    ['--fixed-run-bindings','fixedRunBindings'], ['--fixed-run-bindings-sha256','fixedRunBindingsSha256'], ['--tool-load-plan','toolLoadPlan'], ['--tool-load-plan-sha256','toolLoadPlanSha256'], ['--hc-assembly','hcAssembly'], ['--cecil-path','cecilPath'], ['--legacy-root','legacyRoot']]);
  const result = {};
  for(let i=0;i<args.length;i++) {
    if(args[i]==='--prepare-parent-build-only'){if(result.prepareParentBuildOnly)throw Error('Duplicate argument: --prepare-parent-build-only');result.prepareParentBuildOnly=true;continue;}
    if(args[i]==='--prepare-only'){if(result.prepareOnly)throw Error('Duplicate argument: --prepare-only');result.prepareOnly=true;continue;}
    const flag=args[i],key=flags.get(flag); if(!key) throw Error('Unknown argument: '+flag);
    if(Object.hasOwn(result,key)) throw Error('Duplicate argument: '+flag);
    const value=args[++i];
    if(typeof value!=='string'||!value.trim()||value.startsWith('--'))throw Error('Missing value: '+flag);
    result[key]=value;
  }
  if(!result.evidenceDir)throw Error('--evidence-dir is required');
  if(result.prepareParentBuildOnly&&(result.prepareOnly||result.preparedParentBuild))throw Error('Parent build preparation cannot execute or resume');
  if(result.preparedParentBuild&&result.prepareOnly)throw Error('Parent build resume cannot use prepare-only');
  for(const [value,sha]of [['parentBuildRequest','parentBuildRequestSha256'],['preparedParentBuild','preparedParentBuildSha256']])if((result[value]===undefined)!==(result[sha]===undefined))throw Error('Parent build input and SHA are required together: '+value);
  if(result.parentBuildRequest!==undefined&&!result.prepareParentBuildOnly)throw Error('Parent build request requires preparation mode');
  if(result.prepareParentBuildOnly&&result.parentBuildRequest===undefined)throw Error('Parent build preparation requires exact request and SHA');
  return result;
}
export function prepareFreshEvidence(value) {
  if(typeof value!=='string'||!value.trim())throw Error('--evidence-dir is required');
  const out=path.resolve(value);
  if(fs.existsSync(out))throw Error('Evidence directory already exists: '+out);
  fs.mkdirSync(out,{recursive:true});return out;
}
export function resolveInputs({repoRoot,legacyRoot,hcAssembly,cecilPath,env,userHome}) {
  const fixed=path.join(repoRoot,baselineRelativeRoot),manifestPath=path.join(fixed,'provenance.json');
  if(!isFile(manifestPath))throw new BlockedInputError('ONEX_BASELINE_MISSING','Fixed pre-fix provenance is missing',{path:manifestPath});
  const provenance=JSON.parse(fs.readFileSync(manifestPath,'utf8').replace(/^\uFEFF/,''));
  if(provenance.schemaVersion!==1||provenance.id!=='host7f-light-e546'||provenance.files?.length!==3 ||
    Object.entries(baselinePins).some(([name,pin])=>provenance.files.filter(f=>f.path===name&&f.sha256===pin).length!==1))
    throw Object.assign(Error('Fixed baseline provenance identity mismatch'),{code:'ONEX_BASELINE_IDENTITY'});
  const root=path.resolve(legacyRoot??fixed),files=[];
  for(const [name,pin] of Object.entries(baselinePins)) {
    const file=path.join(root,name);
    if(!isFile(file))throw new BlockedInputError('ONEX_BASELINE_MISSING','Fixed old product DLL is missing',{path:file});
    if(hash(file)!==pin)throw Object.assign(Error('Fixed baseline identity mismatch: '+name),{code:'ONEX_BASELINE_IDENTITY'});
    files.push({path:file,sha256:pin});
  }
  const hc=path.resolve(hcAssembly??path.join(repoRoot,'PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1/HandheldCompanion.dll'));
  if(!isFile(hc))throw new BlockedInputError('ONEX_HC_MISSING','Exact HC PE reference is missing',{path:hc});
  if(hash(hc)!==hcPin)throw Object.assign(Error('HC PE identity mismatch'),{code:'ONEX_HC_IDENTITY'});
  const cecil=resolveCecil({repoRoot,cecilPath,env,userHome});
  return {baseline:{root,manifestPath,manifestSha256:hash(manifestPath),files},hc:{path:hc,sha256:hcPin},cecil};
}
export const writeBaselineRelativeRoot = 'tools/ymcc_sim/global/onex-fan-coexistence/fixtures/host-a678-write-failure';
export const writeBaselinePins = Object.freeze({
  'YeManFanHost.dll': 'a6788164463da89f7e7df6a6ab1edaf7c2d5eff69c27d127b0d3ce5c47d36539',
  'YeManFanBackend.dll': '6d2bcbd48498b7826db64d77b6d5d9d3f45bb91917d816b97c7a830a1eaa84db',
});
export function resolveWriteBaseline(repoRoot) {
  const root=path.join(repoRoot,writeBaselineRelativeRoot),manifestPath=path.join(root,'provenance.json');
  if(!isFile(manifestPath))throw new BlockedInputError('ONEX_WRITE_BASELINE_MISSING','Prior a678 write-failure baseline provenance is missing',{path:manifestPath});
  const provenance=JSON.parse(fs.readFileSync(manifestPath,'utf8').replace(/^\uFEFF/,''));
  if(provenance.schemaVersion!==1||provenance.id!=='host-a678-write-failure'||provenance.files?.length!==2||
    Object.entries(writeBaselinePins).some(([name,pin])=>provenance.files.filter(f=>f.path===name&&f.sha256===pin).length!==1))
    throw Object.assign(Error('Prior write-failure baseline provenance mismatch'),{code:'ONEX_WRITE_BASELINE_IDENTITY'});
  const files=[];
  for(const [name,pin]of Object.entries(writeBaselinePins)) {
    const file=path.join(root,name);
    if(!isFile(file))throw new BlockedInputError('ONEX_WRITE_BASELINE_MISSING','Prior write-failure baseline DLL is missing',{path:file});
    if(hash(file)!==pin)throw Object.assign(Error('Prior write-failure baseline identity mismatch: '+name),{code:'ONEX_WRITE_BASELINE_IDENTITY'});
    files.push({path:file,sha256:pin});
  }
  return {root,manifestPath,manifestSha256:hash(manifestPath),files};
}
