import {begin,source,inputPlugin,bindBuild,generated,verify,receipt} from './fixtures/radial_private/scope.mjs';
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {createRequire} from 'node:module';
const root=path.resolve(import.meta.dirname,'..'),out=process.argv[2]&&path.resolve(process.argv[2]);
if(!out||out===root||fs.existsSync(out))throw Error('A fresh owned renderer directory is required');
const scope=begin(root,out,'tools/radial_renderer_fixture.mjs');
const require=createRequire(path.join(root,'package.json')),{build}=require('esbuild'),{parse,compileScript,compileStyle}=require('vue/compiler-sfc');
const sha=value=>crypto.createHash('sha256').update(value).digest('hex'),styles=[];
const result=await build({stdin:{contents:`import {createApp} from 'vue';import RadialOutput from './src/components/RadialOutput.vue';createApp(RadialOutput).mount('#radial');`,resolveDir:root,loader:'ts'},absWorkingDir:root,bundle:true,write:false,metafile:true,platform:'browser',format:'iife',alias:{'@':path.join(root,'src'),vue:require.resolve('vue/dist/vue.runtime.esm-bundler.js')},define:{'process.env.NODE_ENV':'"production"','import.meta.env.DEV':'false'},plugins:[inputPlugin(scope),{name:'actual-internal-radial-renderer',setup(b){b.onLoad({filter:/\.vue$/},a=>{
 source(scope,a.path);const text=fs.readFileSync(a.path,'utf8').replace(/^\ufeff/,''),{descriptor,errors}=parse(text,{filename:a.path});if(errors.length)throw Error(errors.join());
 const id='data-v-'+sha(a.path).slice(0,8),script=compileScript(descriptor,{id,inlineTemplate:true});
 for(const style of descriptor.styles){const compiled=compileStyle({source:style.content,filename:a.path,id,scoped:style.scoped});if(compiled.errors.length)throw Error(compiled.errors.join());styles.push(compiled.code);}
 return{contents:script.content.replace('export default','const _component=')+'\n_component.__scopeId='+JSON.stringify(id)+';export default _component;',loader:'ts',resolveDir:path.dirname(a.path)};
});}}]});
const sources=Object.fromEntries(Object.keys(result.metafile.inputs).filter(p=>p!=='<stdin>').map(p=>{const absolute=path.resolve(root,p);return[path.relative(root,absolute).replaceAll('\\','/'),sha(fs.readFileSync(absolute))];}));
bindBuild(scope,result,'radial');
fs.writeFileSync(path.join(out,'radial.js'),result.outputFiles[0].text);fs.writeFileSync(path.join(out,'radial.css'),styles.join('\n'));
fs.writeFileSync(path.join(out,'radial-output.html'),'<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="stylesheet" href="./radial.css"><div id="radial"></div><script src="./radial.js"></script></html>');
const hudStyles=[];
const hud=await build({entryPoints:[path.join(root,'src/monitorHudOutputEntry.ts')],absWorkingDir:root,outdir:path.join(out,'_virtual'),bundle:true,write:false,metafile:true,platform:'browser',format:'iife',loader:{'.svg':'dataurl','.png':'dataurl','.jpg':'dataurl','.webp':'dataurl','.woff2':'dataurl'},alias:{'@':path.join(root,'src'),vue:require.resolve('vue/dist/vue.runtime.esm-bundler.js')},define:{'process.env.NODE_ENV':'"production"','import.meta.env.DEV':'false'},plugins:[inputPlugin(scope),{name:'actual-monitor-native-renderer',setup(b){b.onLoad({filter:/\.vue$/},a=>{
 source(scope,a.path);const text=fs.readFileSync(a.path,'utf8').replace(/^\ufeff/,''),{descriptor,errors}=parse(text,{filename:a.path});if(errors.length)throw Error(errors.join());
 const id='data-v-'+sha(a.path).slice(0,8),script=compileScript(descriptor,{id,inlineTemplate:true});
 for(const style of descriptor.styles){const compiled=compileStyle({source:style.content,filename:a.path,id,scoped:style.scoped});if(compiled.errors.length)throw Error(compiled.errors.join());hudStyles.push(compiled.code);}
 return{contents:script.content.replace('export default','const _component=')+'\n_component.__scopeId='+JSON.stringify(id)+';export default _component;',loader:'ts',resolveDir:path.dirname(a.path)};
});}}]});
bindBuild(scope,hud,'hud');
for(const p of Object.keys(hud.metafile.inputs)){const absolute=path.resolve(root,p);sources[path.relative(root,absolute).replaceAll('\\','/')]=sha(fs.readFileSync(absolute));}
fs.writeFileSync(path.join(out,'hud.js'),hud.outputFiles.find(f=>f.path.endsWith('.js')).text);
fs.writeFileSync(path.join(out,'hud.css'),hudStyles.join('\n')+'\n'+(hud.outputFiles.find(f=>f.path.endsWith('.css'))?.text||''));
fs.writeFileSync(path.join(out,'hud-output.html'),'<!doctype html><html><meta charset="utf-8"><link rel="stylesheet" href="./hud.css"><div id="hud"></div><script src="./hud.js"></script></html>');
const outputHashes=Object.fromEntries(['radial.js','radial.css','radial-output.html','hud.js','hud.css','hud-output.html'].map(file=>[file,sha(fs.readFileSync(path.join(out,file)))]));
for(const name of Object.keys(outputHashes))generated(scope,path.join(out,name),'owned actual Vue/HUD renderer output');
const report={...receipt(scope),pass:true,toolProof:verify(scope),outputHashes,entry:'Actual RadialOutput with an explicit test-only bootstrap. Production entry/policy is not modified.',userDelivery:false,hardwareIo:false,ordinaryApplicationLaunched:false};
fs.writeFileSync(path.join(out,'renderer-fixture.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({pass:true,out,sourceFiles:Object.keys(sources).length}));