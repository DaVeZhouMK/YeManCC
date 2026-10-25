import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
export const MODULE='tools/ymcc_sim/global/gyro-calibration-session';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const CASE_ID='controller.gyro-calibration-session';
export const PRODUCER='tools/gyro_calibration_session_selftest.ts';
export const PRODUCT='src/bridge/gyroCalibrationSession.ts';
export function moduleSources(){return ['adapter.mjs','run.mjs','worker.mjs','Run-Session.ps1','session-exit-selftest.ts'].map(name=>MODULE+'/'+name);}
export function productSources(){return [PRODUCER,PRODUCT,'src/bridge/inputConfigHash.ts','src/bridge/hcInputUtils.ts','package.json','tsconfig.json','tsconfig.node.json'];}
export function compilerTools(repoRoot=REPO){
 const require=createRequire(path.join(repoRoot,'package.json')),main=require.resolve('esbuild'),compilerRequire=createRequire(main);
 return [...new Set([path.resolve(process.execPath),main,require.resolve('esbuild/package.json'),compilerRequire.resolve('@esbuild/win32-x64/esbuild.exe'),compilerRequire.resolve('@esbuild/win32-x64/package.json'),
 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe'])];
}
export function getGyroCalibrationSessionAdapters(){return [{
 id:CASE_ID,domain:'controller',title:'Original gyro calibration session, safe-zero exit and original-assertion source faults in owned cwd',
 kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/calibration'],profiles:['desktop','full'],scope:'behavioral-mock',timeoutMs:120000,
 evidenceInputs:[...moduleSources(),...productSources(),'tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'],artifactInputs:compilerTools(),resultFiles:['calibration/result.json'],
}];}
