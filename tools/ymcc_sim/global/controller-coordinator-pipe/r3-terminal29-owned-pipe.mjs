import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {startOwnedRawProducer,rawProgramLauncherDeclaration,verifyRawProducerBudget} from '../decky-persistent-lifecycle/raw-owned-producer.mjs';
import {ORIGINAL_NATIVE_BUDGET_MS} from './r3-terminal22-observation-terminal.mjs';

const rawProducer=fileURLToPath(new URL('../decky-persistent-lifecycle/raw-owned-producer.mjs',import.meta.url));
const host=fileURLToPath(new URL('../host-process.ps1',import.meta.url));
const pin=file=>({path:path.resolve(file),sha256:createHash('sha256').update(fs.readFileSync(file)).digest('hex')});
const cleanupReserveMs=10000;
// Launcher startup comes before the held business process clock starts.
const launchAllowanceMs=5000;
const outerAllocationMs=launchAllowanceMs+ORIGINAL_NATIVE_BUDGET_MS+cleanupReserveMs;

export function native29HeldInterval(raw){
 const start=raw.business?.startFileTimeUtc,end=raw.businessExitFileTimeUtc;
 assert(typeof start==='string'&&typeof end==='string'&&/^[1-9]\d{16,19}$/.test(start)&&/^[1-9]\d{16,19}$/.test(end),'TERMINAL29_HELD_FILETIMES_REQUIRED');
 const ticks=BigInt(end)-BigInt(start);
 assert(ticks>=0n,'TERMINAL29_HELD_FILETIMES_REVERSED');
 return {durationMs:Number(ticks)/10000,elapsed100ns:ticks.toString(),
  withinOriginalBudget:ticks<=BigInt(ORIGINAL_NATIVE_BUDGET_MS)*10000n,
  basis:'GetProcessTimes creation to exit on the held business process handle'};
}

export async function runOwnedNative29({root,runtimeDirectory,nativeExe,managedDll,dotnet,powershell,inputLedger,captureManifest,env,record}){
 const launcher=rawProgramLauncherDeclaration({YMCC_SIM_RAW_PROGRAM_PWSH:powershell});
 const before=spawnSync(launcher.program,['-NoLogo','-NoProfile','-NonInteractive','-Command',
  '([Diagnostics.Process]::GetProcessById('+process.pid+')).StartTime.ToUniversalTime().Ticks.ToString()'],
  {encoding:'utf8',windowsHide:true,timeout:5000});
 assert.equal(before.status,0,'TERMINAL29_OWNER_PROCESS_IDENTITY_QUERY_FAILED');
 assert.match(before.stdout.trim(),/^\d{17,20}$/,'TERMINAL29_OWNER_BIRTH_REQUIRED');
 const sources=[inputLedger,captureManifest,pin(fileURLToPath(import.meta.url)),pin(host),pin(rawProducer)];
 const tools=[...new Set([nativeExe,managedDll,dotnet,...launcher.files])].map(pin);
 assert.deepEqual(pin(inputLedger.path),inputLedger,'TERMINAL29_OBSERVER_LEDGER_CHANGED');
 assert.deepEqual(pin(captureManifest.path),captureManifest,'TERMINAL29_CAPTURE_MANIFEST_CHANGED');
 const allocated=Date.now();
 const args=[runtimeDirectory,'full-pipe',managedDll];
 // RawProgram serializes the environment. Bind the existing runtime controls
 // without putting unrelated desktop credentials into the evidence.
 const processEnv=Object.fromEntries(Object.entries(env).filter(([name])=>
  /^(SystemRoot|windir|ComSpec|Path|PATHEXT|TEMP|TMP|DOTNET_ROOT|DOTNET_MULTILEVEL_LOOKUP|DOTNET_CLI_TELEMETRY_OPTOUT|YMCC_FIXTURE_DOTNET|YEMAN_POWER_CONTROL_DIR)$/i.test(name)));
 const proof=await startOwnedRawProducer({id:'terminal29-original-full-pipe',program:nativeExe,args,cwd:runtimeDirectory,env:processEnv,
  host,powershell:launcher.program,ownerStartTicks:before.stdout.trim(),directory:path.join(root,'owned-native'),
  stdoutPath:path.join(root,'stdout.log'),stderrPath:path.join(root,'stderr.log'),
  allocatedAtEpochMs:allocated,deadlineEpochMs:allocated+outerAllocationMs,parentStartedAtEpochMs:allocated,
  parentBudgetMs:outerAllocationMs,cleanupReserveMs,originalBudgetMs:outerAllocationMs,parentRemainingBeforeMs:outerAllocationMs,
  sourceInputs:sources,toolArtifacts:tools}).wait();
 record({process:proof,inputLedger,captureManifest});
 assert.equal(proof.error,null,'TERMINAL29_OWNED_PROCESS_NOT_COLLECTED');
 const budget=verifyRawProducerBudget({...proof,boundSourceInputs:sources,toolArtifacts:tools},outerAllocationMs,outerAllocationMs);
 const terminal=JSON.parse(fs.readFileSync(proof.rawExecution.terminal.path,'utf8'));
 const interval=native29HeldInterval(terminal);
 record({process:proof,inputLedger,captureManifest,budget,interval});
 assert.equal(interval.withinOriginalBudget,true,'NATIVE22_ORIGINAL_BUDGET_EXCEEDED');
 return {pid:proof.pid,exitCode:proof.exitCode,signal:proof.signal,timedOut:proof.timedOut,
  launchError:proof.error,outputTruncated:proof.outputTruncated,durationMs:interval.durationMs};
}
