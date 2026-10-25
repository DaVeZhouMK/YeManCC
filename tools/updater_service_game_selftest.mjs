import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const require=createRequire(path.join(root,'package.json')),{build}=require('esbuild');
const output=path.resolve(process.argv[2]||path.join(root,'../../Build/Validation/SelfTests/updater_service_game_selftest.mjs'));
fs.mkdirSync(path.dirname(output),{recursive:true});
await build({entryPoints:[path.join(root,'tools/updater_service_game_selftest.ts')],bundle:true,platform:'node',format:'esm',outfile:output,alias:{'@':path.join(root,'src')},plugins:[{name:'shared-game-memory-evidence',setup(b){
 b.onResolve({filter:/gamePolicyTarget$/},()=>({path:'gamePolicyTarget',namespace:'fixture'}));
 b.onLoad({filter:/.*/,namespace:'fixture'},()=>({contents:`const f=globalThis.__updaterGameFixture;export const getPolicyGame=()=>f.game;export const isPolicyGameInitialized=()=>f.known;export const subscribePolicyGameStatus=fn=>{f.listeners.add(fn);if(f.known)fn(f.game);return()=>f.listeners.delete(fn);};export const isPolicyTargetCurrent=()=>false;export const detectPolicyGame=async()=>f.game;export const gamePolicyKey=()=>'';export const samePolicyGame=(a,b)=>a===b;`,loader:'js'}));
}}]});
const result=spawnSync(process.execPath,[output],{cwd:root,stdio:'inherit'});
if(result.error)throw result.error;
process.exitCode=result.status??1;
