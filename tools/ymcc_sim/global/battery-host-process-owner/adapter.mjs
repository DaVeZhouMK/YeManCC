import path from 'node:path';
import {ownerTools,SOURCE_MEMBERS} from './tools.mjs';
export const MODULE='tools/ymcc_sim/global/battery-host-process-owner';
export const ID='battery.host-process-owner';
export const SOURCES=Object.freeze([...new Set([
 ...SOURCE_MEMBERS,'deps/json/json.hpp','native/battery_host_process_owner.h',MODULE+'/adapter.mjs',MODULE+'/run.mjs',MODULE+'/tools.mjs',MODULE+'/native-fixture.cpp',
 'tools/fixtures/battery_native_host_roundtrip/contract.mjs',
 'tools/ymcc_sim/global/host-process.ps1',
])]);
// Candidate factory only: no central registration and no complete battery-parent contract.
export function getBatteryHostProcessOwnerAdapters(repo=path.resolve(import.meta.dirname,'../../../..')){
 return [{id:ID,domain:'battery',kind:'node',scope:'behavioral-mock',
  title:'Exact suspended BatteryHost process owner: owned Job, terminal generation and bounded observable software reap',
  path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/battery-host-process-owner'],
  timeoutMs:180000,profiles:['desktop','full','sleep'],blockedExitCodes:[10],
  evidenceInputs:[...SOURCES],artifactInputs:ownerTools(repo).files.map(row=>row.path),
  resultFiles:['battery-host-process-owner/result.json'],
  boundary:'New process-owner component only. Real source-built inert child/descendant and independent sentinel in original fixture Job; original MSVC/SDK tool pins and15s native subprocess bound. Exact CreateProcess(CREATE_SUSPENDED) PI only, Job kill-on-close and assignment before Resume. No attach/name kill, provider, journal deletion, real BatteryHost service, WMI/HID/EC, device, named battery pipe or OS sleep. Stop authority covers the entire shared Host, never an automatic single-request timeout. hardwareRestoreConfirmed=false; previous Controller FAILs and journal uncertainty remain.'}];
}