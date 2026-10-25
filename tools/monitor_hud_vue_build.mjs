// Reuse production Vue SFCs in the existing Decky plugin; not a second HUD.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {createRequire} from 'node:module';
export function monitorHudVuePlugin(root) {
 const {parse,compileScript,compileStyle}=createRequire(path.join(root,'package.json'))('vue/compiler-sfc');
 return {name:'ymcc-shared-hud-vue',setup(build){
  // Shared AppIcon assets must use the same inline bytes in every consumer.
  build.onLoad({filter:/\.png$/},args=>({contents:fs.readFileSync(args.path),loader:'dataurl'}));
  build.onResolve({filter:/^@\//},args=>({path:path.resolve(root,'src',args.path.slice(2)+(/\.[a-z]+$/i.test(args.path)?'':'.ts'))}));
  build.onLoad({filter:/\.vue$/},args=>{
   const {descriptor}=parse(fs.readFileSync(args.path,'utf8'),{filename:args.path});
   const id='ymcc-hud-'+crypto.createHash('sha256').update(path.relative(root,args.path).replaceAll('\\','/')).digest('hex').slice(0,12);
   const script=compileScript(descriptor,{id,inlineTemplate:true});
   const css=descriptor.styles.map(style=>{const compiled=compileStyle({source:style.content,filename:args.path,id:'data-v-'+id,scoped:style.scoped});if(compiled.errors.length)throw compiled.errors[0];return compiled.code}).join('\n');
   return {loader:'ts',resolveDir:path.dirname(args.path),contents:script.content.replace(/export default /,'const __component = ')+
    '\n__component.__scopeId='+JSON.stringify('data-v-'+id)+';__component.__ymccHudCss='+JSON.stringify(css)+';export default __component;'};
  });
 }};
}
