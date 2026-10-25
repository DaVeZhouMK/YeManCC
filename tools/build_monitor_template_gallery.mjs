// Offline production-component gallery and full-viewport QA harness. Never opens
// the native app, Steam, a game, sensors or a process. Fixture data is labelled.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { monitorHudVuePlugin } from './monitor_hud_vue_build.mjs';
const root = process.cwd(), { build } = createRequire(path.join(root, 'package.json'))('esbuild');
const out = path.resolve(process.env.YMCC_MONITOR_GALLERY_OUT || path.join(root, '../../../Build/Tasks/YMCC-Steam-HUD-Probe/tech-templates-20261013/gallery'));
fs.mkdirSync(out, { recursive: true });
const entry = `
import {createApp,h,ref,computed,nextTick,onMounted,onBeforeUnmount,watch} from 'vue';
import Hud from './src/components/MonitorHudTemplate.vue';
import {mountMonitorHud} from './src/bridge/monitorHudRenderer.ts';
import {prepareHudUnderlayFile} from './src/bridge/monitorHudUnderlayImport.ts';
import {makeHudFixture,HUD_CROSSHAIRS,HUD_LAYOUTS,HUD_DATA_PRESETS,HUD_UNDERLAY_PRESETS,hudDataPresetFields,normalizeMonitorOutput} from './src/bridge/monitorHudModel.ts';
const sample=makeHudFixture(),preset=ref('standard'),scale=ref(1),opacity=ref(.86),emphasized=ref(true),imagePreset=ref('none'),imageTransparency=ref(72),customImage=ref(''),imageError=ref(''),imageInput=ref(null),selected=ref(new URLSearchParams(location.search).has('new')?'new':'all');
const crosshair=ref('none');
const fields=computed(()=>hudDataPresetFields(preset.value));
const appearance=computed(()=>({...normalizeMonitorOutput().ymcc,scale:scale.value,opacity:opacity.value,fpsEmphasis:emphasized.value,underlay:imagePreset.value==='custom'&&!customImage.value?'none':imagePreset.value,underlayPreset:imagePreset.value,underlayImage:customImage.value,underlayOpacity:1-imageTransparency.value/100}));
const style=document.createElement('style');style.textContent=Hud.__ymccHudCss;document.head.appendChild(style);
const descriptions={corner:'FPS重点 · CPU/MEM与GPU/BATT并列',bar:'自适应内容宽度 · 一行优先，空间不足换行',capsule:'紧凑双列 · 同类数据轻量排布',card:'独立数据卡片 · 区块分明',dashboard:'三列仪表布局 · 大FPS与底部电池',glass:'静态玻璃质感 · 无实时模糊/动画',split:'左右双栏 · CPU/内存与GPU/电池分离',cinema:'电影黑条 · 较薄图块与曲线',neon:'霓光分区 · 三列矩阵与内侧微光',circuit:'电路走线 · 侧置标题与连接节点',prism:'棱镜切角 · 大FPS与三个仪表模块',holo:'全息光轨 · 双细光轨与横向排布',tactical:'战术座舱 · 双仪表栈与四角定位线'};
function chooseImage(value){imagePreset.value=value;if(value==='custom'&&!customImage.value)imageInput.value?.click();}
async function importImage(event){const input=event.target,file=input.files?.[0];input.value='';if(!file)return;imageError.value='';try{customImage.value=await prepareHudUnderlayFile(file);imagePreset.value='custom';}catch(e){imageError.value=e.message||'图片导入失败';}}
// Gallery-only screen thumbnail; it mounts the actual shared output renderer.
const CrosshairScreen={setup(){const host=ref(null),surface=ref(null);let renderer,observer;
 const update=()=>{if(renderer)renderer.update(normalizeMonitorOutput({enabled:true,output:'ymcc',ymcc:{...appearance.value,fields:fields.value,crosshair:crosshair.value,layout:['all','new'].includes(selected.value)?'corner':selected.value}}),sample);};
 const resize=()=>{if(!host.value||!surface.value)return;const factor=host.value.clientWidth/1920;surface.value.style.transform='scale('+factor+')';host.value.style.height=(1080*factor)+'px';};
 onMounted(()=>{renderer=mountMonitorHud(surface.value,{preferences:normalizeMonitorOutput({ymcc:{crosshair:crosshair.value}}),data:sample,viewport:{width:1920,height:1080},onSize:()=>{}});observer=new ResizeObserver(resize);observer.observe(host.value);resize();update();});
 watch([appearance,fields,crosshair,selected],update);onBeforeUnmount(()=>{observer?.disconnect();renderer?.dispose();});
 return()=>h('section',{class:'crosshair-preview'},[h('div',{class:'crosshair-caption'},['屏幕准星 · 1920 × 1080 屏幕示意',h('span','下方为等比缩小预览；游戏中的准星固定在屏幕中心')]),h('div',{ref:host,class:'crosshair-screen'},[h('div',{ref:surface,style:{width:'1920px',height:'1080px',transformOrigin:'0 0',position:'absolute',left:'0',top:'0'}})])]);
}};
const controls=()=>h('nav',{class:'gallery-controls','aria-label':'预览调节'},[
 h('label',['数据预设',h('select',{'aria-label':'数据预设',value:preset.value,onChange:e=>preset.value=e.target.value},HUD_DATA_PRESETS.map(p=>h('option',{value:p.value},p.label)))]),
 h('label',['查看模板',h('select',{'aria-label':'查看模板',value:selected.value,onChange:e=>selected.value=e.target.value},[h('option',{value:'all'},'全部13个'),h('option',{value:'new'},'新增5种'),...HUD_LAYOUTS.map(p=>h('option',{value:p.value},p.label))])]),
 h('label',['屏幕准星',h('select',{'aria-label':'屏幕准星',value:crosshair.value,onChange:e=>crosshair.value=e.target.value},HUD_CROSSHAIRS.map(p=>h('option',{value:p.value},p.label)))]),
 h('label',['缩放',h('select',{'aria-label':'缩放',value:scale.value,onChange:e=>scale.value=Number(e.target.value)},[.8,1,1.4,2].map(v=>h('option',{value:v},Math.round(v*100)+'%')))]),
 h('label',['背景不透明度',h('select',{'aria-label':'背景不透明度',value:opacity.value,onChange:e=>opacity.value=Number(e.target.value)},[0,.35,.86,1].map(v=>h('option',{value:v},Math.round(v*100)+'%')))]),
 h('button',{type:'button','aria-pressed':emphasized.value,onClick:()=>emphasized.value=!emphasized.value},'FPS 突出显示'),
 h('label',['图片背景',h('select',{'aria-label':'图片背景',value:imagePreset.value,onChange:e=>chooseImage(e.target.value)},HUD_UNDERLAY_PRESETS.map(p=>h('option',{value:p.value},p.label)))]),
 h('label',['图片透明度',h('div',{class:'image-transparency'},[h('input',{type:'range',min:0,max:100,step:1,'aria-label':'图片透明度',value:imageTransparency.value,onInput:e=>imageTransparency.value=Number(e.target.value)}),h('output',imageTransparency.value+'%')])]),
 imagePreset.value==='custom'?h('button',{type:'button',onClick:()=>imageInput.value?.click()},'选择图片'):null,
 h('input',{ref:imageInput,type:'file',accept:'image/png,image/jpeg,image/webp',hidden:true,onChange:importImage}),
 imageError.value?h('span',{role:'alert'},imageError.value):null,
 h('span',{class:'field-count'},Object.values(fields.value).filter(Boolean).length+' 项 · 各模板字段一致')
]);
const app=createApp({setup:()=>()=>h('main',{class:'gallery'},[
 h('header',{class:'gallery-head'},[h('div',[h('h1','YMCC 监控 · 十三种外观'),h('p','默认采用本轮截图标准数据。仅示例，不是本机读数、Steam或游戏验收。')]),h('span',{class:'gallery-note'},'生产共用渲染组件')]),
 controls(),crosshair.value!=='none'?h(CrosshairScreen):null,h('section',{class:['gallery-grid',!['all','new'].includes(selected.value)?'gallery-grid--solo':'']},HUD_LAYOUTS.filter(p=>selected.value==='all'||selected.value===p.value||(selected.value==='new'&&['neon','circuit','prism','holo','tactical'].includes(p.value))).map((layout,index)=>h('article',{class:'gallery-card','data-layout':layout.value},[
  h('div',{class:'gallery-card-head'},[h('h2',String(index+1).padStart(2,'0')+'  '+layout.label),h('span',descriptions[layout.value])]),
  h('div',{class:'stage'},[h('div',{class:'stage-surface'},[h(Hud,{appearance:{...appearance.value,layout:layout.value},fields:fields.value,data:sample})])])
 ])))
])});if(!new URLSearchParams(location.search).has('qa'))app.mount('#app');
window.__gallery={layouts:HUD_LAYOUTS.map(x=>x.value),standardFields:Object.keys(hudDataPresetFields('standard')).filter(k=>hudDataPresetFields('standard')[k]),async set(p='standard',s=1,o=.86){preset.value=p;scale.value=s;opacity.value=o;await nextTick()}};
if(new URLSearchParams(location.search).has('qa')){
 document.body.classList.add('qa');const container=document.querySelector('#qa-surface');let prefs=normalizeMonitorOutput(),data=sample,sizeEvents=0;
 const renderer=mountMonitorHud(container,{preferences:prefs,data,onSize:()=>sizeEvents++});
 window.__qa={async set(layout,preset='standard',scale=1,opacity=.86,values=null,selectedIds=null,hide=false,emphasis=true,underlay='none',underlayImage='',underlayOpacity=.28){prefs=normalizeMonitorOutput({ymcc:{layout,underlay,underlayImage,underlayOpacity,fpsEmphasis:emphasis,fields:selectedIds?Object.fromEntries(Object.keys(hudDataPresetFields('detailed')).map(k=>[k,selectedIds.includes(k)])):hudDataPresetFields(preset),dataPreset:preset,scale,opacity,hideUnavailable:hide}});data=makeHudFixture();if(values!==null){data.values={...data.values,...values};if(Object.keys(values).length>40&&Object.values(values).every(v=>v===null)){data.coreUsage=[];data.frameTimes=[];}}renderer.update(prefs,data);await nextTick();return structuredClone(prefs)},inspect:()=>({prefs:structuredClone(prefs),sizeEvents}),async updateSame(){const n=sizeEvents;for(let i=0;i<100;i++){renderer.update(prefs,data);await nextTick()}return sizeEvents-n},dispose:()=>renderer.dispose()};
}
`;
const bundle = await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},bundle:true,platform:'browser',format:'iife',write:false,define:{'process.env.NODE_ENV':'"production"'},plugins:[monitorHudVuePlugin(root)]});
const css = `
.crosshair-preview{margin-bottom:18px;padding:14px;border:1px solid #33495b;border-radius:12px;background:#141e29}.crosshair-caption{display:flex;justify-content:space-between;gap:12px;font-size:13px;color:#d8e4ee;margin-bottom:10px}.crosshair-caption span{font-size:11px;color:#8fabc2}.crosshair-screen{position:relative;width:100%;overflow:hidden;background:radial-gradient(ellipse at center,#263949,#0b141d 75%);border-radius:7px}

*{box-sizing:border-box}html,body{margin:0;background:#0c121a;color:#eaf1f8;font-family:'Segoe UI','Microsoft YaHei UI',sans-serif}.gallery{max-width:1680px;margin:auto;padding:26px 30px 36px}.gallery-head{display:flex;justify-content:space-between;align-items:center;gap:24px;margin:0 0 20px}.gallery-head h1{font-size:25px;margin:0;font-weight:650}.gallery-head p{font-size:13px;color:#9cabbc;margin:9px 0 0}.gallery-note{padding:8px 12px;border:1px solid #33495d;border-radius:100px;font-size:12px;color:#9dd8f0;white-space:nowrap}.gallery-controls{display:flex;align-items:end;gap:18px;flex-wrap:wrap;margin-bottom:20px;padding:12px 16px;border:1px solid #263748;border-radius:10px;background:#151f2b}.gallery-controls label{display:flex;flex-direction:column;gap:5px;font-size:11px;color:#a6bbcf}.gallery-controls select{padding:6px 9px;background:#0e1823;border:1px solid #33495b;border-radius:6px;color:#eef3fa;font-size:12px;min-width:95px}.image-transparency{display:flex;align-items:center;gap:8px;min-width:150px;height:30px}.image-transparency input{width:115px;accent-color:#62b9de}.image-transparency output{min-width:34px;color:#b5e4f7;font-size:12px}.field-count{margin:auto 0 6px auto;font-size:12px;color:#87c6db}.gallery-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px;align-items:start}.gallery-card{padding:14px;border:1px solid #253646;border-radius:12px;background:#141e29;min-width:0}.gallery-card-head{display:flex;flex-direction:column;gap:5px;margin-bottom:10px}.gallery-card-head h2{font-size:15px;margin:0}.gallery-card-head span{font-size:11px;color:#8fabc2}.stage{padding:14px;overflow:auto;border-radius:8px;border:1px solid #22313e;background-color:#17222d;background-image:linear-gradient(45deg,#1c2935 25%,transparent 25%,transparent 75%,#1c2935 75%),linear-gradient(45deg,#1c2935 25%,transparent 25%,transparent 75%,#1c2935 75%);background-size:24px 24px;background-position:0 0,12px 12px}.stage-surface{width:100%;min-width:0}.gallery-grid--solo{grid-template-columns:1fr}.gallery-grid--solo .stage{min-height:300px}.gallery-controls button{padding:7px 10px;border:1px solid #33495b;border-radius:6px;background:#0e1823;color:#a6bbcf;cursor:pointer}.gallery-controls button[aria-pressed=true]{border-color:#62b9de;color:#b5e4f7;background:#203645}.gallery-controls button:focus-visible{outline:2px solid #62b9de;outline-offset:2px}.gallery-note,.gallery-controls{box-shadow:none}#qa-surface{display:none}.qa #app{display:none}.qa #qa-surface{display:block;width:100%;min-height:100vh;padding:12px;background:transparent}body.qa{background:transparent}@media(max-width:1430px){.gallery-grid{grid-template-columns:1fr}.gallery{max-width:1150px}}@media(max-width:700px){.gallery{padding:14px}.gallery-head{align-items:start;flex-direction:column}.field-count{margin:0}.gallery-head h1{font-size:21px}}
`;
const html = `<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data:; connect-src 'none'; base-uri 'none'"><title>YMCC 监控十三种外观 · 标准数据</title><style>${css}</style></head><body><div id="app"></div><div id="qa-surface"></div><script>${bundle.outputFiles[0].text.replaceAll('</script','<\\/script')}</script></body></html>`;
fs.writeFileSync(path.join(out,'index.html'),html);
const files=['src/bridge/monitorHudModel.ts','src/components/MonitorHudTemplate.vue','src/bridge/monitorHudRenderer.ts','src/bridge/monitorHudViewport.ts','src/components/MonitorCrosshair.vue'];
fs.writeFileSync(path.join(out,'GALLERY-INFO.json'),JSON.stringify({generatedAt:new Date().toISOString(),defaultPreset:'standard',fixtureOnly:true,sourceHashes:Object.fromEntries(files.map(f=>[f,crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex')]))},null,2));
console.log(path.join(out,'index.html'));
