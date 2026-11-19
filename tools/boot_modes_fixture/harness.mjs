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
const compiledRuntimes=new Map();let runtimeInstance=0;
export async function runtimeBundle(out,options={}){
 if((options.bootModesSource||options.bootModeModelSource||options.settingsSources)&&options.reuseCompiled)throw Error('Original source substitution cannot reuse another compiled runtime');
 const file=path.join(out,'boot-runtime.mjs'),key=options.reuseCompiled&&options.diskSettingsPort?file+'|'+options.diskSettingsPort:'';
 const cached=key&&compiledRuntimes.get(key);
 if(cached){
  for(const [input,sha] of Object.entries(cached.sourceHashes))if(hash(path.resolve(repo,input))!==sha)throw Error('Runtime source changed during isolated file round '+input);
  if(hash(file)!==cached.bundleHash)throw Error('Compiled runtime changed during isolated file round');
  // Only compilation is reused. Every scene gets a fresh ESM instance and
  // fresh memory owners; disk state and all assertions still execute.
  return {...await import(pathToFileURL(file).href+'?instance='+(++runtimeInstance)),inputs:cached.inputs,mockedModules:cached.mockedModules};
 }
 const ports=path.join(fixture,'runtimePorts.ts');
 const sourceOverride=options.bootModesSource?{path:path.resolve(options.bootModesSource),sha256:hash(path.resolve(options.bootModesSource)),
  role:'Explicit original source body at the production module path; existing native memory aliases preserved'}:null;
 const modelSourceOverride=options.bootModeModelSource?{path:path.resolve(options.bootModeModelSource),sha256:hash(path.resolve(options.bootModeModelSource)),
  role:'Explicit original model body at the production module path; current dependencies and original assertions preserved'}:null;
 const settingsSourceOverrides=Object.entries(options.settingsSources??{}).map(([name,file])=>{if(!['settingsRepository','bootModeGlobalPreferences'].includes(name))throw Error('Unknown settings source substitution');const source=path.resolve(file);return {name,path:source,sha256:hash(source)};});
 const native=new Set(['settingsRepository','performanceSchedule','monitorOutput','screenTouchpads','gameproc','batteryCharge','steamUi','steamAppearance','uiSettings','theme','trayResident','api','fanFeature','fanHost','sleepRecovery','gamePolicyTarget','gamedetect','quickapp','yeman','ipc','bootModePlatform','deckySidebar']);
 if(options.diskSettingsPort)native.delete('settingsRepository');
 if(options.actualDisplayPlatform)native.delete('bootModePlatform');
 // Match the actual H5's production Vue runtime. Bundling Vue's Node entry
 // duplicates both builds and the unused template compiler in every fresh
 // scene; isolation needs fresh product state, not a fresh Babel parser.
 const result=await require('esbuild').build({stdin:{contents:`export * as inputReceipts from './src/bridge/inputApplicationReceipt.ts';export * as model from './src/bridge/bootModeModel.ts'; export * as boot from './src/bridge/bootModes.ts';export * as desktopStartup from './src/bridge/desktopModeStartup.ts';export * as gameBarStartup from './src/bridge/gameBarControllerStartup.ts';export * as ports from './tools/boot_modes_fixture/runtimePorts.ts';export * as shortcuts from './src/bridge/controllerShortcutRules.ts';${options.actualDisplayPlatform?"export * as platform from './src/bridge/bootModePlatform.ts';export {display as realDisplay} from './src/bridge/api.ts';":''}${options.diskSettingsPort?"export * as repository from './src/bridge/settingsRepository.ts';":''}`,resolveDir:repo,sourcefile:'boot-runtime-entry.ts',loader:'ts'},absWorkingDir:repo,bundle:true,platform:'node',format:'esm',write:false,metafile:true,alias:{'@':path.join(repo,'src'),'vue':require.resolve('vue/dist/vue.runtime.esm-bundler.js')},define:{'process.env.NODE_ENV':'"production"','__VUE_OPTIONS_API__':'true','__VUE_PROD_DEVTOOLS__':'false','__VUE_PROD_HYDRATION_MISMATCH_DETAILS__':'false'},plugins:[{name:'boot-memory-native-boundary',setup(build){if(sourceOverride)build.onLoad({filter:/bootModes\.ts$/},args=>({contents:fs.readFileSync(sourceOverride.path,'utf8'),loader:'ts',resolveDir:path.dirname(args.path)}));if(modelSourceOverride)build.onLoad({filter:/bootModeModel\.ts$/},args=>({contents:fs.readFileSync(modelSourceOverride.path,'utf8'),loader:'ts',resolveDir:path.dirname(args.path)}));for(const source of settingsSourceOverrides)build.onLoad({filter:new RegExp('[/\\\\]'+source.name+'\\.ts$')},args=>({contents:fs.readFileSync(source.path,'utf8'),loader:'ts',resolveDir:path.dirname(args.path)}));build.onResolve({filter:/.*/},args=>{const name=args.path.split('/').at(-1).replace(/\.ts$/,'');if(options.actualDisplayPlatform&&args.path==='./src/bridge/api.ts')return {path:path.join(repo,'src/bridge/api.ts')};if(options.diskSettingsPort&&name==='api'&&args.importer.replaceAll('\\','/').endsWith('/src/bridge/settingsRepository.ts'))return {path:options.diskSettingsPort};if(native.has(name)&&(args.path.startsWith('./')||args.path.startsWith('@/bridge/')))return {path:ports};});}}]});
 fs.writeFileSync(file,result.outputFiles[0].text);
 const inputs=[...new Set([...evidenceInputs(result.metafile),'package.json',...(sourceOverride?[path.relative(repo,sourceOverride.path)]:[]),...(modelSourceOverride?[path.relative(repo,modelSourceOverride.path)]:[]),...settingsSourceOverrides.map(source=>path.relative(repo,source.path))])].sort(),mockedModules=[...native].sort();
 if(key)compiledRuntimes.set(key,{sourceHashes:fingerprints(Object.keys(result.metafile.inputs).filter(input=>!input.startsWith('boot-')&&!input.startsWith('<'))),bundleHash:hash(file),inputs,mockedModules});
 return {...await import(pathToFileURL(file).href+'?instance='+(++runtimeInstance)),inputs,mockedModules,sourceOverride,modelSourceOverride,settingsSourceOverrides};
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
