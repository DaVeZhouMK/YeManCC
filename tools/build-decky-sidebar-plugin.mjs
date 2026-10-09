import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {createRequire} from 'node:module';
import {deckyApiPlugins} from './decky_sidebar_decky_api_build.mjs';
const root=path.resolve(import.meta.dirname,'..'),require=createRequire(path.join(root,'package.json')),{build}=require('esbuild');
const args=process.argv.slice(2),outIndex=args.indexOf('--out');
const outdir=outIndex>=0?path.resolve(args[outIndex+1]):path.join(root,'PowerControl/decky/plugins/ymcc-sidebar');
const check=args.includes('--check'),hash=value=>crypto.createHash('sha256').update(value).digest('hex');
const output=await build({entryPoints:[path.join(root,'decky-plugin/src/index.tsx')],bundle:true,minify:true,sourcemap:false,format:'esm',platform:'browser',target:'es2020',jsxFactory:'SP_REACT.createElement',jsxFragment:'SP_REACT.Fragment',plugins:deckyApiPlugins(),write:false});
const expected=[{name:'dist/index.js',bytes:Buffer.from(output.outputFiles[0].contents)},...['package.json','plugin.json','LICENSE.decky-api'].map(name=>({name,bytes:fs.readFileSync(path.join(root,'decky-plugin',name))}))];
const loader=path.join(root,'PowerControl/decky/PluginLoader_noconsole.exe');
if(!fs.existsSync(loader)||hash(fs.readFileSync(loader))!=='1d8e06921ced35b0349e677207953d90e548ea254079150d99ced4d360381824')throw Error('Locked mainline Windows Loader is missing or changed');
const loaderLicense=path.join(root,'PowerControl/decky/LICENSE.decky-loader');if(!fs.existsSync(loaderLicense)||fs.statSync(loaderLicense).size!==18092)throw Error('Pinned Loader license missing');
for(const item of expected){const target=path.join(outdir,item.name);if(check){if(!fs.existsSync(target)||hash(fs.readFileSync(target))!==hash(item.bytes))throw Error('Stale or mismatched plugin payload: '+item.name);}else{fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,item.bytes);}}
const files=expected.map(item=>({name:item.name,bytes:item.bytes.length,sha256:hash(item.bytes)}));
console.log(JSON.stringify({keyword:'YMCC 控制台',sourceRoot:root,outdir,verified:check,files,totalBytes:files.reduce((total,item)=>total+item.bytes,0),externalLoaderBytes:fs.statSync(loader).size,externalDlls:0,frontendSourceSDKs:1,deckyApiVersion:'1.1.3',nodeModulesInstalled:false,PythonBackend:false,steamStarted:false}));
