import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {discoverTools} from './toolchain.mjs';
export const MODULE='tools/ymcc_sim/global/fan-api-zip-regression';
export const SOURCE_INPUTS=[
 ...['adapter.mjs','run.mjs','assertions.mjs','toolchain.mjs','observe-tools.cjs','write-fixture.py','README.md'].map(file=>MODULE+'/'+file),
 'tools/ymcc_sim/global/cli.mjs','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/signal-replay.mjs',
 'tools/ymcc_sim/global/log-corpus.mjs','tools/ymcc_sim/global/log-zip-reader.py','tools/ymcc_sim/global/fan-api-corpus.mjs',
 'tools/ymcc_sim/global/fan-api-log-consumer/fixture.mjs','tools/ymcc_sim/global/fan-api-log-consumer/normalize.mjs',
 'tools/ymcc_sim/global/fan-log-regression.mjs','tools/ymcc_sim/global/consumer/fan-signal-consumer.mjs','tools/ymcc_sim/global/consumer/fan-signal-fixture.mjs',
 'src/bridge/fanHost.ts','src/bridge/fanApi.ts','src/bridge/fanDiagnostics.ts','src/bridge/fanSessionEvidence.ts',
 'src/bridge/hardwareCoordinator.ts','src/bridge/aiFanMock.ts','src/bridge/fanHostInstallerRepair.ts','src/bridge/fanHostInstallerAsset.json','tools/fixtures/fan_no_io_host.ts',
];
export function getFanApiZipRegressionAdapters({repoRoot=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..'),pythonPath}={}) {
 let tools,blockedReason;
 try{tools=discoverTools(repoRoot,pythonPath);}catch(error){blockedReason=error.message;}
 return [{id:'fan.api-ordinary-zip-regression',domain:'fan',title:'Ordinary fan-api ZIP to actual product consumer with admission and output guards',
  kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/zip-regression',...(tools?['--python',tools.python]:[])],
  profiles:['desktop','full'],scope:'behavioral-mock',timeoutMs:180000,blockedExitCodes:[10],
  resultFiles:['zip-regression/fan-api-zip-regression-result.json'],evidenceInputs:SOURCE_INPUTS.filter(file=>file!==MODULE+'/run.mjs'),
  artifactInputs:tools?[...new Set(tools.rows.map(row=>row.path))]:[],...(blockedReason?{blockedReason}:{}),
 }];
}
