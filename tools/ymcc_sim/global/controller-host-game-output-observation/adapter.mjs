import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {toolchainFiles} from '../inputhost-protocol/adapter.mjs';
export const MODULE='tools/ymcc_sim/global/controller-host-game-output-observation';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const CASE_ID='controller.host-game-output-observation';
export const PRODUCER='tools/inputhost_game_output_observation_selftest.mjs';
export function moduleSources(){return ['adapter.mjs','run.mjs','spec.json','README.md'].map(name=>MODULE+'/'+name);}
export function productSources(){return [...fs.readdirSync(path.join(REPO,'InputHost')).filter(name=>name.endsWith('.cs')).map(name=>'InputHost/'+name),'InputHost/YeManInputHost.csproj','capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs','capabilities/CONTROLLER-GAME-OUTPUT-OBSERVATION.json',PRODUCER,'tools/inputhost_game_output_observation_selftest.cs','tools/msi_steamdeck_release_fence_1726_selftest.mjs','tools/msi_steamdeck_release_fence_1726_selftest.cs','tools/inputhost_lifecycle_selftest.cs'];}
export function infrastructureSources(){return ['tools/ymcc_sim/global/inputhost-protocol/adapter.mjs','tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/execution-completion/index.mjs'];}
export function toolFiles(){return [...new Set([...toolchainFiles(),path.join(REPO,'deps/hidmaestro/HIDMaestro.Core.dll')])];}
export function getHostGameOutputObservationAdapters(){return [{id:CASE_ID,domain:'controller',title:'Actual Host output event, current tuple and bounded raw observation with compiled source fault controls',kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/output-observation'],timeoutMs:240000,profiles:['desktop','full'],scope:'behavioral-mock',evidenceInputs:[...productSources(),...moduleSources(),...infrastructureSources()],artifactInputs:toolFiles(),resultFiles:['output-observation/result.json'],boundary:'Actual Host lifecycle/parser/serializer and observation helper, inert matching SDK event surface and PnP boundaries. Raw coalesced observations only; no motor decode/forwarding, Host Main, actual SDK construction, device writes or CPU-reduction acceptance.'}];}
