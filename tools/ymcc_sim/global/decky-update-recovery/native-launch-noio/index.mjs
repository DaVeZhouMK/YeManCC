import {selectedNativeTools,HOST_ENVIRONMENT_SOURCE_INPUTS} from '../../release-host-environment.mjs';
/** Explicit, reviewed NoIO fixture generation. No product, Steam or identity execution. */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';
const moduleRoot=path.dirname(fileURLToPath(import.meta.url));
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const key=file=>path.resolve(file).toLowerCase();
const inside=(root,file)=>{const rel=path.relative(root,file);return rel!==''&&!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep);};
const need=(ok,code)=>{if(!ok)throw Error('STEAM_NATIVE_NOIO_'+code);};
function plain(file){const resolved=path.resolve(file);need(key(fs.realpathSync(resolved))===key(resolved)&&!fs.lstatSync(resolved).isSymbolicLink(),'REPARSE_INPUT:'+file);return resolved;}
export const NATIVE_LAUNCH_NOIO_SOURCE_INPUTS=Object.freeze([...HOST_ENVIRONMENT_SOURCE_INPUTS,"tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/index.mjs","tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-case.mjs","tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json","tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/read-evidence.mjs","tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/reviewed-source.json","tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/reviewed-terminals.json","tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/selftest-entry.mjs","tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/selftest.ps1","tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/terminal-adapter.h","native/steam_native_launch.h","native/steam_native_launch_windows.h","native/decky_sidebar_launch.h","native/process_efficiency.h","deps/json/json.hpp","tools/steam_native_launch_selftest.cpp","tools/steam_native_launch_selftest.ps1","tools/steam_native_launch_regression.mjs","tools/steam_launch_admission_selftest.mjs","tools/steam_linked_launch_selftest.mjs","src/bridge/yeman.ts","src/bridge/api.ts","src/bridge/frameRateModel.ts","src/bridge/tdpRuntimeState.ts","src/bridge/gamingHomePolicy.ts","src/views/SteamView.vue","PowerControl/YeManSteam.vbs","PowerControl/YeManSteam.bat","PowerControl/YeManSteam2.bat","tools/ymcc_sim/global/runner.mjs","tools/ymcc_sim/global/bundled-node-tool-authority.mjs","tools/ymcc_sim/global/snapshot-node-tools.mjs","tools/ymcc_sim/global/source-snapshot.mjs","tools/fixtures/battery_ui_native_recovery/snapshot-node-binding.mjs","tools/ymcc_sim/global/execution-completion/index.mjs","package.json","pnpm-lock.yaml"]);
export function getNativeLaunchNoIoToolDeclaration(){try{const saved=selectedNativeTools(),powershell=process.env.YMCC_SIM_POWERSHELL||saved.powershell.path;need(key(powershell)===key(saved.powershell.path),'POWERSHELL_PATH_CHANGED');const rows=[...saved.files,saved.powershell];for(const row of rows)need(hash(fs.readFileSync(plain(row.path)))===row.sha256,'DECLARED_NATIVE_TOOL_CHANGED:'+row.path);return {artifactInputs:[...new Set(rows.map(row=>path.resolve(row.path)))]};}catch(error){return {artifactInputs:[],blockedReason:error.message};}}
export async function prepareNativeLaunchNoIoFixture(repoRoot,evidenceRoot){
 const repo=plain(repoRoot),out=plain(evidenceRoot);need(!inside(repo,out)&&key(repo)!==key(out),'OWNED_EVIDENCE_OUTSIDE_REPO_REQUIRED');
 const fixtureDir=path.join(out,'native-launch-noio-fixture');need(!fs.existsSync(fixtureDir),'FRESH_FIXTURE_REQUIRED');
 const reviewed=JSON.parse(fs.readFileSync(path.join(moduleRoot,'reviewed-source.json'),'utf8'));
 const review=JSON.parse(fs.readFileSync(path.join(moduleRoot,'reviewed-terminals.json'),'utf8'));
 need(review.status==='STATIC_TERMINAL_REVIEW_COMPLETE'&&hash(fs.readFileSync(path.join(moduleRoot,'terminal-adapter.h')))===review.adapterSha256,'REVIEWED_TERMINAL_CHANGED');
 const reads=[];const read=(file,role)=>{const actual=plain(file),bytes=fs.readFileSync(actual);reads.push({path:actual,sha256:hash(bytes),bytes:bytes.length,role});return bytes;};
 const sourceRows=reviewed.files.map(row=>{need(!path.isAbsolute(row.path)&&!row.path.split('/').includes('..'),'SOURCE_PATH');const source=path.join(repo,row.path),bytes=read(source,'reviewed current production/test source');need(hash(bytes)===row.sha256,'REVIEWED_SOURCE_CHANGED:'+row.path);return {relative:row.path,source,sha256:row.sha256,bytes};});
 const standard=await import(pathToFileURL(path.join(repo,'tools/ymcc_sim/global/bundled-node-tool-authority.mjs')));
 const nodeAuthority=standard.lockBundledNodeTools(repo);
 const authoritySources=standard.BUNDLED_AUTHORITY_SOURCES.map(relative=>({path:relative,sha256:hash(read(path.join(repo,relative),'current standard Node authority implementation'))}));
 const native=selectedNativeTools();
 for(const row of native.files)need(hash(read(row.path,'declared native tool before fixture'))===row.sha256,'NATIVE_TOOL_CHANGED:'+row.path);
 fs.mkdirSync(fixtureDir);const generated=[];
 const write=(relative,bytes)=>{const file=path.join(fixtureDir,relative);need(inside(fixtureDir,file),'OUTPUT_ESCAPE');fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes);generated.push({path:file,sha256:hash(bytes)});return file;};
 const json=(relative,value)=>write(relative,Buffer.from(JSON.stringify(value,null,2)));
 for(const name of ['native-case.mjs','selftest-entry.mjs','read-evidence.mjs','terminal-adapter.h','selftest.ps1'])write(name,read(path.join(moduleRoot,name),'portable reviewed fixture code'));
 for(const row of sourceRows)write('frozen/'+row.relative,row.bytes);
 json('SOURCE-INPUTS.json',{rows:sourceRows.map(({bytes,...row})=>row)});
 json('NATIVE-TOOLS.json',native);
 json('NODE-AUTHORITY.json',nodeAuthority);
 json('NODE-AUTHORITY-SOURCES.json',authoritySources);
 json('TERMINAL-REVIEW.json',{...review,adapter:{path:path.join(fixtureDir,'terminal-adapter.h'),sha256:review.adapterSha256},sourceApprovalGranted:false});
 const portableSources=fs.readdirSync(moduleRoot,{withFileTypes:true}).filter(row=>row.isFile()).map(row=>{const file=path.join(moduleRoot,row.name);return {path:file,sha256:hash(read(file,'portable tool source identity'))};});
 json('GENERATION-SOURCES.json',portableSources);
 const node={path:plain(process.execPath),sha256:hash(read(process.execPath,'explicit Node executable'))};
 const powershellPath=process.env.YMCC_SIM_POWERSHELL||'C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe';
 const powershell={path:plain(powershellPath),sha256:hash(read(powershellPath,'explicit PowerShell executable'))};need(key(powershell.path)===key(native.powershell.path)&&powershell.sha256===native.powershell.sha256,'REVIEWED_POWERSHELL_CHANGED');
 standard.verifyBundledToolScope(nodeAuthority,repo);
 json('GENERATION-READS.json',{meaning:'Actual generator reads; not product behavior or OS loaded-module coverage.',rows:reads});
 const fixture={schemaVersion:1,kind:'steam-native-launch-memory-terminal-fixture',sourceApprovalGranted:false,executionMode:'explicit-reviewed-NoIO-only',authorizedWriteRoot:out,entry:path.join(fixtureDir,'selftest-entry.mjs'),selftestScript:path.join(fixtureDir,'selftest.ps1'),node,powershell,files:generated};
 const manifest=path.join(fixtureDir,'FIXTURE.json');fs.writeFileSync(manifest,JSON.stringify(fixture,null,2));
 const binding={noIoFixture:manifest,expectedFixtureSha256:hash(fs.readFileSync(manifest))};fs.writeFileSync(path.join(fixtureDir,'FIXTURE-BINDING.json'),JSON.stringify(binding,null,2));
 return binding;
}
