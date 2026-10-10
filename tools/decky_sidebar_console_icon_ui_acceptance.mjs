// Read-only QAM icon layout: production TSX under actual Steam CSS. No business commands.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {root,task} from './fixtures/decky_task_paths.mjs';
import {targets,connect} from './fixtures/decky_cdp.mjs';
const require=createRequire(path.join(root,'package.json'));
const {transformSync}=require('esbuild');
const source=fs.readFileSync(path.join(root,'decky-plugin/src/index.tsx'),'utf8');
const jsx=source.match(/const consoleIcon = ([\s\S]*?);\r?\nconst placement/)?.[1];
assert.ok(jsx,'Production icon expression must exist');
const module={exports:null};
vm.runInNewContext(transformSync('module.exports = '+jsx,{loader:'tsx',format:'cjs',jsxFactory:'SP_REACT.createElement'}).code,{module,SP_REACT:{createElement:(type,props,...children)=>({type,props:props??{},children})}});
const spec=module.exports;
assert.equal(spec.type,'svg');
const probe=process.argv.includes('--probe');
const report={actualSteam:true,sourceIconRendered:true,businessCommands:0,hardwareWrites:0,steamRestarted:false,credentialsRecorded:false,probeOnly:probe,steps:[]};
const out=path.join(task,'validation',probe?'ICON-NATIVE-CSS-PROBE.json':'ICON-ACTUAL-PRODUCT.json');
const save=()=>fs.writeFileSync(out,JSON.stringify(report,null,2));
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
let q,c;
const read=`(()=>{const box=e=>e.getBoundingClientRect().toJSON();return Array.from(document.querySelectorAll('[role=tab]')).filter(e=>!e.closest('[data-ymcc-icon-probe]')).map(e=>{const icon=e.querySelector('[aria-label="YMCC 控制台"]')||e.firstElementChild;const style=icon&&getComputedStyle(icon);let f=e[Object.keys(e).find(k=>k.startsWith('__reactFiber$'))],key=null;for(let i=0;f&&i<20;i++,f=f.return){if(key===null&&f.key!=null)key=String(f.key);}return {key,label:e.getAttribute('aria-label'),tab:box(e),icon:icon?box(icon):null,iconTag:icon?.tagName,selected:e.getAttribute('aria-selected'),iconStyle:style?{width:style.width,height:style.height,margin:style.margin,fill:style.fill}:null};});})()`;
function assertLayout(rows){
 const own=rows.find(t=>t.key==='100019');
 assert.ok(own?.icon,'Single YMCC icon must exist');
 assert.equal(rows.filter(t=>t.key==='100019').length,1);
 assert.equal(own.iconTag.toLowerCase(),'svg');
 const others=rows.filter(t=>t.key!=='100019');
 assert.ok(others.length>=2);
 assert.equal(others.filter(t=>t.key==='999').length,1,'Keep exactly one original Decky home tab');
 assert.equal(new Set(rows.map(t=>t.key)).size,rows.length,'No duplicate native tabs');
 for(const native of others){assert.ok(Math.abs(own.tab.height-native.tab.height)<1,'Keep native tab height');assert.ok(Math.abs(own.tab.width-native.tab.width)<1,'Keep native tab width');}
 const cx=r=>r.left+r.width/2,cy=r=>r.top+r.height/2;
 assert.ok(Math.abs(cx(own.tab)-cx(own.icon))<1,'Y must be horizontally centered');
 assert.ok(Math.abs(cy(own.tab)-cy(own.icon))<1,'Y must be vertically centered');
 const native=others.find(t=>t.iconTag?.toLowerCase()==='svg');
 assert.ok(native);
 assert.ok(Math.abs(own.icon.width-native.icon.width)<1);
 assert.ok(Math.abs(own.icon.height-native.icon.height)<1);
 return {centerOffsetX:cx(own.icon)-cx(own.tab),centerOffsetY:cy(own.icon)-cy(own.tab),tabHeight:own.tab.height,iconHeight:own.icon.height,order:rows.map(t=>t.key)};
}
async function open(id){await c.evaluate(`DFL.Navigation.OpenQuickAccessMenu(${id});true`);await sleep(300);q?.close();const t=(await targets()).find(t=>t.title.startsWith('QuickAccess'));assert.ok(t);q=await connect(t.webSocketDebuggerUrl);}
try{
 const list=await targets();const qt=list.find(t=>t.title.startsWith('QuickAccess'));assert.ok(qt);q=await connect(qt.webSocketDebuggerUrl);
 report.before=await q.evaluate(read);
 if(probe){
  report.probes=await q.evaluate(`(()=>{const spec=${JSON.stringify(spec)},make=s=>{const e=document.createElementNS('http://www.w3.org/2000/svg',s.type);for(const [key,value] of Object.entries(s.props))e.setAttribute(key,String(value));for(const child of s.children)if(child&&typeof child==='object')e.appendChild(make(child));return e;};const icon=document.querySelector('[aria-label="YMCC 控制台"]'),own=icon?.closest('[role=tab]'),native=Array.from(document.querySelectorAll('[role=tab]')).find(e=>e!==own);if(!own||!native)throw Error('Native QAM tabs unavailable');const clones=[];try{return [own,native].map((original,index)=>{const clone=original.cloneNode(false);clone.removeAttribute('id');clone.removeAttribute('tabindex');clone.setAttribute('data-ymcc-icon-probe',String(index));clone.setAttribute('aria-hidden','true');clone.style.position='absolute';clone.style.left='-5000px';clone.style.top='0px';clone.style.pointerEvents='none';clone.style.width=getComputedStyle(original).width;clone.appendChild(make(spec));original.parentElement.appendChild(clone);clones.push(clone);const svg=clone.firstElementChild,rect=e=>e.getBoundingClientRect().toJSON();return {state:index===0?'selected':'unselected',tab:rect(clone),icon:rect(svg),iconTag:svg.tagName,nativeTab:rect(original),iconStyle:{margin:getComputedStyle(svg).margin,width:getComputedStyle(svg).width,height:getComputedStyle(svg).height}};});}finally{for(const clone of clones)clone.remove();}})()`);
  for(const row of report.probes){const cy=r=>r.top+r.height/2,cx=r=>r.left+r.width/2;assert.ok(Math.abs(cy(row.tab)-cy(row.icon))<1);assert.ok(Math.abs(cx(row.tab)-cx(row.icon))<1);assert.ok(Math.abs(row.tab.height-row.nativeTab.height)<1);assert.ok(Math.abs(row.icon.height-24)<1);}
  assert.deepEqual(await q.evaluate(read),report.before,'Probe must preserve every original tab');
  report.steps.push('Production SVG centered under selected and unselected native Steam styles','Native tab heights and original UI preserved; temporary probes removed');
 }else{
  c=await connect(list.find(t=>t.title==='SharedJSContext').webSocketDebuggerUrl);
  await open(100019);report.selected=await q.evaluate(read);report.selectedMetrics=assertLayout(report.selected);
  await open(0);report.unselected=await q.evaluate(read);report.unselectedMetrics=assertLayout(report.unselected);
  assert.deepEqual(report.unselectedMetrics.order,report.selectedMetrics.order,'No tab order change');
  await c.evaluate('DFL.Navigation.CloseSideMenus();true');await sleep(200);await open(100019);
  report.reopened=await q.evaluate(read);report.reopenedMetrics=assertLayout(report.reopened);
  assert.deepEqual(report.reopenedMetrics.order,report.selectedMetrics.order);
  const image=await q.send('Page.captureScreenshot',{format:'png',fromSurface:true});
  report.screenshot=path.join(task,'validation/ICON-ACTUAL-PRODUCT.png');fs.writeFileSync(report.screenshot,Buffer.from(image.data,'base64'));
  report.steps.push('Actual installed Y centered with native dimensions while selected','Y stays centered after selecting the notifications tab','Close/reopen retains icon alignment, one YMCC tab and original tab order');
 }
 report.pass=true;save();console.log('Actual Steam icon layout PASS '+report.steps.length);
}catch(error){report.failure=error.stack;save();throw error;}finally{q?.close();c?.close();}
