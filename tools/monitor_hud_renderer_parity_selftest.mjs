// Actual production shared Vue renderer in two browser documents. Synthetic
// test readings; neither iframe is a Steam/native/game acceptance substitute.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {createRequire} from 'node:module';import {createServer} from 'node:http';
import {monitorHudVuePlugin} from './monitor_hud_vue_build.mjs';
const root=process.cwd(),req=createRequire(path.join(root,'package.json')),{build}=req('esbuild');
const out=path.resolve(process.env.YMCC_MONITOR_PARITY_OUT||path.join(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/product-output-20261010/renderer'));fs.mkdirSync(out,{recursive:true});
const entry=`import {mountMonitorHud} from './src/bridge/monitorHudRenderer.ts';import {makeHudFixture,normalizeMonitorOutput,HUD_METRICS} from './src/bridge/monitorHudModel.ts';
const prefs=normalizeMonitorOutput();let data=makeHudFixture();window.__testDataOnly=true;
const child=document.querySelector('iframe').contentDocument;child.open();child.write('<html><head><style>html,body{margin:0;background:#253041}#steam{width:1280px;pointer-events:none}</style></head><body><div id="steam"></div></body></html>');child.close();
const dimensions={desktop:[],steam:[]};const desktop=mountMonitorHud(document.querySelector('#desktop'),{preferences:prefs,data,onSize:(...v)=>dimensions.desktop.push(v)});const steam=mountMonitorHud(child.querySelector('#steam'),{preferences:prefs,data,onSize:(...v)=>dimensions.steam.push(v)});
window.__set=(layout,all,scale=1,opacity=.86)=>{const p=normalizeMonitorOutput({ymcc:{layout,scale,opacity}});if(all)for(const m of HUD_METRICS)p.ymcc.fields[m.id]=true;desktop.update(p,data);steam.update(p,data);};
window.__samples=()=>{for(let i=0;i<20;i++){data={...data,values:{...data.values,fps:100+i,cpuTemp:60+i%3}};desktop.update(prefs,data);steam.update(prefs,data)}};
window.__inspect=()=>({dimensions,styles:[document.querySelectorAll('[data-ymcc-hud-style]').length,child.querySelectorAll('[data-ymcc-hud-style]').length],hud:[document.querySelectorAll('[data-monitor-hud]').length,child.querySelectorAll('[data-monitor-hud]').length]});
window.__dispose=()=>{desktop.dispose();steam.dispose()};`;
const b=await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},bundle:true,platform:'browser',format:'iife',write:false,plugins:[monitorHudVuePlugin(root)]});
const html=`<!doctype html><meta charset="utf-8"><title>TEST ONLY — shared H5 renderer parity / synthetic readings</title><style>html,body{margin:0;background:#253041;color:#dae2ee;font-family:Segoe UI}header{padding:14px;font-size:13px}section{display:flex;gap:20px;padding:20px;align-items:flex-start}.surface{width:1280px}iframe{border:0;width:1280px;height:1200px}</style><header>测试用合成数据 · 两个浏览器文档 · 不是 Steam / 原生窗口 / 游戏验收</header><section><div class="surface"><div id="desktop" style="width:1280px"></div></div><iframe></iframe></section><script>${b.outputFiles[0].text.replaceAll('</script','<\\/script')}</script>`;
const htmlPath=path.join(out,'renderer-parity.html');fs.writeFileSync(htmlPath,html);
const server=createServer((req,res)=>{res.setHeader('Content-Type','text/html;charset=utf-8');res.end(html)});await new Promise(r=>server.listen(0,'127.0.0.1',r));
const runtime='C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';const {chromium}=createRequire(path.join(runtime,'_hud-parity.cjs'))('playwright');
let browser;const cases=[],images=[],errors=[];
try{
 browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});const page=await browser.newPage({viewport:{width:2640,height:1400}});page.on('pageerror',e=>errors.push(String(e)));
 await page.goto('http://127.0.0.1:'+server.address().port+'/');await page.waitForFunction(()=>window.__inspect?.().hud.every(n=>n===1));
 // Both documents must have the same supported 1280px viewport; a 700px
 // iframe would clip the new content-sized 200% capsule before screenshotting.
 const child=page.frames().find(f=>f!==page.mainFrame());assert(child);
 const shape=async(doc)=>doc.locator('[data-monitor-hud]').evaluate(el=>{const c=getComputedStyle(el),r=el.getBoundingClientRect();return {text:el.innerText,width:r.width,height:r.height,font:c.fontSize,zoom:Number(c.zoom),background:c.backgroundColor,color:c.color,opacity:c.opacity,pointer:c.pointerEvents,fields:[...el.querySelectorAll('[data-hud-metric]')].map(n=>[n.dataset.hudMetric,n.innerText]),bars:el.querySelectorAll('.hud-core-track').length}});
 for(const [layout,all,scale,opacity]of [['corner',false,1,.86],['capsule',false,1,.65],['bar',false,1,.35],['bar',true,1,0],['bar',true,1.4,.9],['corner',false,2,.86],['capsule',false,2,.86],['bar',false,2,.86],...['card','dashboard','glass','split','cinema','neon','circuit','prism','holo','tactical'].map(layout=>[layout,false,1,.86])]){
  await page.evaluate(([l,a,s,o])=>window.__set(l,a,s,o),[layout,all,scale,opacity]);await page.waitForFunction(l=>document.querySelector('[data-monitor-hud]').classList.contains('ymcc-hud--'+l),layout);await page.waitForTimeout(180);
  const desktop=await shape(page),steam=await shape(child);assert.deepEqual(steam,desktop);
  assert(!/YMCC|MONITOR|示例数据/.test(desktop.text));assert.equal(desktop.font,'13px');assert.equal(desktop.zoom,scale);assert.equal(desktop.opacity,'1');assert.equal(desktop.pointer,'none');assert.equal(desktop.bars,16);
  const key=layout+(all?'-all':'-default')+'-'+Math.round(scale*100)+'-'+Math.round(opacity*100),left=path.join(out,key+'-desktop.png'),right=path.join(out,key+'-steam-document.png');
  // Percentage fills/unknown dashed rails have subpixel edges. Keep exact
  // pixel comparison, but also align the compositor screen origin: translating
  // the iframe by 1300px changes Chromium's dash raster rounding by two pixels.
  // This only positions the test surfaces; production HUD/CSS is unchanged.
  await page.evaluate(()=>{const hud=document.querySelector('#desktop'),frame=document.querySelector('iframe'),r=hud.getBoundingClientRect();frame.dataset.savedStyle=frame.getAttribute('style')||'';frame.style.cssText+=';position:absolute;left:'+r.left+'px;top:'+r.top+'px;visibility:hidden';});
  await page.locator('[data-monitor-hud]').screenshot({path:left});
  await page.evaluate(()=>{document.querySelector('#desktop').style.visibility='hidden';document.querySelector('iframe').style.visibility='visible';});
  await child.locator('[data-monitor-hud]').screenshot({path:right});
  await page.evaluate(()=>{document.querySelector('#desktop').style.visibility='';const frame=document.querySelector('iframe');frame.setAttribute('style',frame.dataset.savedStyle);});
  const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');assert.equal(hash(left),hash(right),'Same renderer should produce identical pixels in these equal-DPI browser documents');
  cases.push(key+': same 41-field renderer, text, metrics, colors, natural size and exact screenshot pixels');images.push(left,right);
 }
 await page.evaluate(()=>window.__samples());await page.waitForFunction(()=>document.querySelector('[data-hud-metric=fps]').textContent.includes('119'));assert.deepEqual(await shape(child),await shape(page));cases.push('Shared renderers accept source updates without DOM ownership conflicts across documents');
 assert.deepEqual(await page.evaluate(()=>window.__inspect().styles),[1,1]);cases.push('Scoped styles installed only in each owned rendering document');
 await page.evaluate(()=>window.__dispose());assert.deepEqual(await page.evaluate(()=>window.__inspect().styles),[0,0]);assert.deepEqual(await page.evaluate(()=>window.__inspect().hud),[0,0]);cases.push('Both renderer instances dispose DOM, scoped styles and size observers');
 assert.deepEqual(errors,[]);cases.push('No browser errors, no native/game/desktop input or hardware collection');
 const report={passed:cases.length,cases,images,htmlPath,physicalMousePassVerified:false,actualSteam:false,actualNativeWindow:false,scope:'Exact production Vue/H5 renderer and SFC CSS in two owned headless browser documents with equal DPI and synthetic data; pixel parity is NOT Steam/WebView2/game compatibility acceptance.'};fs.writeFileSync(path.join(out,'RENDERER-PARITY-SELFTEST.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}catch(error){if(browser)for(const p of browser.contexts().flatMap(c=>c.pages()))await p.screenshot({path:path.join(out,'failure.png'),fullPage:true});throw error;}
finally{if(browser)await browser.close();await new Promise(r=>server.close(r));}
