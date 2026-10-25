import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {createRequire} from 'node:module';
import {deckyApiPlugins} from './decky_sidebar_decky_api_build.mjs';
import {verifyLocal as verifyOfficialLoader} from './decky-loader-contract.mjs';
import {verifyFrontendUpdateHeader} from './decky_sidebar_frontend_update_embed.mjs';
verifyFrontendUpdateHeader();
import {verifyHealthHeader} from './decky_sidebar_health_embed.mjs';
verifyHealthHeader();
import {monitorHudVuePlugin} from './monitor_hud_vue_build.mjs';
const root=path.resolve(import.meta.dirname,'..'),require=createRequire(path.join(root,'package.json')),{build}=require('esbuild');
const args=process.argv.slice(2),outIndex=args.indexOf('--out');
const outdir=outIndex>=0?path.resolve(args[outIndex+1]):path.join(root,'PowerControl/decky/plugins/ymcc-sidebar');
const styleIndex=args.indexOf('--panel-style'),panelStyle=styleIndex<0?'grouped':args[styleIndex+1];
if(!['grouped','minimal'].includes(panelStyle))throw Error('Unknown panel style; use grouped or minimal');
const check=args.includes('--check'),describe=args.includes('--describe'),hash=value=>crypto.createHash('sha256').update(value).digest('hex');
const options={entryPoints:[path.join(root,'decky-plugin/src/index.tsx')],bundle:true,minify:true,sourcemap:false,format:'esm',platform:'browser',target:'es2020',define:{__YMCC_DECKY_PANEL_STYLE__:JSON.stringify(panelStyle),__YMCC_HUD_PROBE_BUILD__:args.includes('--hud-probe')?'true':'false',__YMCC_DECKY_BUILD_ID__:JSON.stringify('0'.repeat(64))},jsxFactory:'SP_REACT.createElement',jsxFragment:'SP_REACT.Fragment',plugins:[...deckyApiPlugins(),monitorHudVuePlugin(root)],write:false};
const withFrontendVersion=(bytes,version)=>Buffer.concat([Buffer.from('// YMCC_DECKY_FRONTEND_BUILD_ID='+version+'\n'),Buffer.from(bytes)]);
const template=await build(options),buildId=hash(withFrontendVersion(template.outputFiles[0].contents,'0'.repeat(64)));
const output=await build({...options,define:{...options.define,__YMCC_DECKY_BUILD_ID__:JSON.stringify(buildId)}});
const expected=[{name:'dist/index.js',bytes:withFrontendVersion(output.outputFiles[0].contents,buildId)},...['package.json','plugin.json','LICENSE.decky-api','LICENSE.vue'].map(name=>({name,bytes:fs.readFileSync(path.join(root,'decky-plugin',name))}))];
const loader=path.join(root,'PowerControl/decky/PluginLoader_noconsole.exe');
// Consume the unmodified official artifact; no fork/repack policy is part of this build.
const officialLoader=verifyOfficialLoader(root);
const loaderLicense=path.join(root,'PowerControl/decky/LICENSE.decky-loader');if(!fs.existsSync(loaderLicense)||fs.statSync(loaderLicense).size!==18092)throw Error('Pinned Loader license missing');
// Describe recompiles in memory for the final archive gate; it never refreshes cached dist.
if(!describe)for(const item of expected){const target=path.join(outdir,item.name);if(check){if(!fs.existsSync(target)||hash(fs.readFileSync(target))!==hash(item.bytes))throw Error('Stale or mismatched plugin payload: '+item.name);}else{fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,item.bytes);}}
const files=expected.map(item=>({name:item.name,bytes:item.bytes.length,sha256:hash(item.bytes)}));
console.log(JSON.stringify({keyword:'野蛮系统控制台',sourceRoot:root,buildId,panelStyle,outdir:describe?null:outdir,verified:check,described:describe,files,totalBytes:files.reduce((total,item)=>total+item.bytes,0),externalLoaderBytes:fs.statSync(loader).size,loaderVersion:officialLoader.version,loaderOrigin:officialLoader.origin,loaderModified:officialLoader.modified,loaderIncludesPython:true,externalDlls:0,frontendSourceSDKs:1,deckyApiVersion:'1.1.3',nodeModulesInstalled:false,PythonBackend:false,steamStarted:false}));
