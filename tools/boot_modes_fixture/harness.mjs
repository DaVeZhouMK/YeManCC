import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {createHash,randomInt} from 'node:crypto';
import {pathToFileURL} from 'node:url';
export const repo=path.resolve(import.meta.dirname,'../..');
export const require=createRequire(path.join(repo,'package.json'));
export const fixture=path.join(repo,'tools/boot_modes_fixture');
export const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
export function evidenceInputs(metafile){return Object.keys(metafile.inputs).filter(p=>!p.startsWith('boot-')&&!p.startsWith('<')&&!p.includes('node_modules')).map(p=>path.isAbsolute(p)?path.relative(repo,p):p).map(p=>p.replaceAll('\\','/')).sort();}
export const fingerprints=inputs=>Object.fromEntries(inputs.map(p=>[p,hash(path.join(repo,p))]));
export function outputDirectory(args,kind){const index=args.indexOf('--out-dir');const dir=index>=0?args[index+1]:args[0];if(!dir)throw new Error('An evidence directory is required: --out-dir <directory>');fs.mkdirSync(path.resolve(dir),{recursive:true});return path.resolve(dir);}
export async function runtimeBundle(out,options={}){
 const ports=path.join(fixture,'runtimePorts.ts');
 const native=new Set(['settingsRepository','performanceSchedule','monitorOutput','screenTouchpads','gameproc','batteryCharge','steamUi','steamAppearance','uiSettings','theme','trayResident','api','fanFeature','fanHost','sleepRecovery','gamePolicyTarget','quickapp','yeman','ipc','bootModePlatform','deckySidebar']);
 if(options.diskSettingsPort)native.delete('settingsRepository');
 const result=await require('esbuild').build({stdin:{contents:`export * as model from './src/bridge/bootModeModel.ts'; export * as boot from './src/bridge/bootModes.ts';export * as desktopStartup from './src/bridge/desktopModeStartup.ts';export * as ports from './tools/boot_modes_fixture/runtimePorts.ts';export * as shortcuts from './src/bridge/controllerShortcutRules.ts';${options.diskSettingsPort?"export * as repository from './src/bridge/settingsRepository.ts';":''}`,resolveDir:repo,sourcefile:'boot-runtime-entry.ts',loader:'ts'},absWorkingDir:repo,bundle:true,platform:'node',format:'esm',write:false,metafile:true,alias:{'@':path.join(repo,'src')},plugins:[{name:'boot-memory-native-boundary',setup(build){build.onResolve({filter:/.*/},args=>{const name=args.path.split('/').at(-1).replace(/\.ts$/,'');if(options.diskSettingsPort&&name==='api'&&args.importer.replaceAll('\\','/').endsWith('/src/bridge/settingsRepository.ts'))return {path:options.diskSettingsPort};if(native.has(name)&&(args.path.startsWith('./')||args.path.startsWith('@/bridge/')))return {path:ports};});}}]});
 const file=path.join(out,'boot-runtime.mjs');fs.writeFileSync(file,result.outputFiles[0].text);return {...await import(pathToFileURL(file).href+'?'+Date.now()),inputs:[...new Set([...evidenceInputs(result.metafile),'package.json'])].sort(),mockedModules:[...native].sort()};
}


// Explicit high loopback ports avoid browser-blocked service ports when Windows
// has a customized ephemeral range. At most 32 binds, with no protection bypass.
export async function listenBootFixture(server){
 for(let attempt=0;attempt<32;attempt++){
  try{await new Promise((resolve,reject)=>{const failed=error=>{server.removeListener('listening',ready);reject(error);};const ready=()=>{server.removeListener('error',failed);resolve();};server.once('error',failed);server.once('listening',ready);server.listen(randomInt(49152,65536),'127.0.0.1');});return;}
  catch(error){if(!['EADDRINUSE','EACCES'].includes(error.code))throw error;}
 }
 throw Error('No available isolated high loopback port');
}
