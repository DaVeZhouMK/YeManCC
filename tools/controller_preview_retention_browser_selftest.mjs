// Actual editor DOM retention regression. All settings/IPC writes are memory-only.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
import { buildEditorPreview } from './controller_shortcut_editor_preview.mjs';
const repo=process.cwd(),out=path.resolve(process.argv[2]||'G:/YeManCC-Work/Build/Tasks/ControllerEditorLag-20261025/retention');
fs.mkdirSync(out,{recursive:true});
const settingsPath=path.join(out,'memory-settings.json');
fs.writeFileSync(settingsPath,JSON.stringify({input:{},gamepad:{enabled:true,bDoubleMinimize:false,startDoubleF7:false,tdpShortcut:false,fpsShortcut:false,killGame:false,openKeyboard:false,returnDesktop:false,mouseToggle:false,mouseBackend:'joyxoff'},quickApps:{apps:[]}}));
await buildEditorPreview({settingsPath,out:path.join(out,'actual-editor')});
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_retention.cjs')('playwright');
const browser=await chromium.launch({channel:'msedge',headless:true}),checks=[],errors=[];
function check(ok,name){assert(ok,name);checks.push(name)}
try{
 const page=await browser.newPage({viewport:{width:1280,height:720},reducedMotion:'no-preference'});page.on('pageerror',error=>errors.push(String(error)));
 await page.goto(pathToFileURL(path.join(out,'actual-editor/editor.html')).href);await page.locator('.rule-row').first().waitFor();
 await page.evaluate(async()=>{const p=window.__editorPreview;p.settings.input.buttonMapping.rules.shortcutRules=[
 {id:'pad',inputs:[{source:'controller',code:'a'}]},
 {id:'key1',inputs:[{source:'keyboard',code:'ControlLeft'},{source:'keyboard',code:'KeyK'}]},
 {id:'key2',inputs:[{source:'keyboard',code:'ControlLeft'},{source:'keyboard',code:'KeyJ'}]},
 ].map(rule=>({...rule,origin:'custom',enabled:true,actionId:'keyboard.letter',params:{key:'KeyJ'},trigger:'press',holdMs:500,doubleWindowMs:500,intervalMs:150}));await p.setCatalog('two')});
 const customRows=page.locator('.rule-row').filter({hasText:'键盘 · J'});
 const select=async index=>{await customRows.nth(index).click();await page.locator('.action-manage-cancel').click();await page.mouse.move(1,1);await page.evaluate(()=>document.activeElement.blur())};
 const savedBefore=await page.evaluate(()=>JSON.stringify(window.__editorPreview.settings));
 for(const height of [720,800]){
  await page.setViewportSize({width:1280,height});
  for(const persona of ['steamdeck','dualsense-edge','elite']){
   await page.evaluate(persona=>window.__editorPreview.setPersona(persona),persona);await select(0);
   const result=await page.evaluate(async()=>{const diagram=document.querySelector('.device-diagram'),keys=[...diagram.querySelectorAll('.device-key')],root=diagram.querySelector('.device-vector');
    const styles=new Map(keys.map(key=>[key,key.getAttribute('style')]));let writes=0;const observer=new MutationObserver(records=>{writes+=records.filter(record=>record.type==='attributes'&&record.attributeName==='style').length});observer.observe(diagram,{attributes:true,subtree:true});
    for(let i=0;i<80;i++){window.__editorPreview.state({axes:[Math.sin(i*.2)*.8,Math.cos(i*.2)*.8,0,0]});await Promise.resolve();await Promise.resolve()}
    const moved=diagram.querySelector('[data-stick="0"]').getAttribute('transform');window.__editorPreview.state({});await Promise.resolve();await Promise.resolve();observer.disconnect();
    return{sameKeys:keys.every(key=>key.isConnected&&styles.get(key)===key.getAttribute('style')),sameSvg:root===diagram.querySelector('.device-vector'),styleWrites:writes,moved,neutral:diagram.querySelector('[data-stick="0"]').getAttribute('transform')};});
   check(result.sameKeys&&result.sameSvg,height+' '+persona+' continuous axis updates preserve original paths and hit targets');
   check(result.styleWrites===0,height+' '+persona+' axis-only samples never change fixed button geometry');
   check(result.moved!==result.neutral,height+' '+persona+' stick still moves and returns to neutral');
   await page.evaluate(()=>{const buttons=Array(18).fill(false);buttons[0]=true;window.__editorPreview.state({buttons})});
   await page.waitForFunction(()=>document.querySelector('.device-key[data-control="a"]')?.classList.contains('live'));
   check(true,height+' '+persona+' memoized hit targets still receive live button input');
   await page.evaluate(()=>window.__editorPreview.state({}));await page.waitForFunction(()=>!document.querySelector('.device-key[data-control="a"]')?.classList.contains('live'));
   check(true,height+' '+persona+' release clears live button feedback');
  }
  await select(1);
  await page.locator('.mirror-stage .keyboard-asset').waitFor({timeout:3000});
  await page.evaluate(()=>{const asset=document.querySelector('.mirror-stage .keyboard-asset');window.__retainedKeyboard=asset.firstElementChild;window.__retainedGlyphs=[...asset.querySelectorAll('path')];window.__retainedBlink=asset.querySelector('[data-key-code="ControlLeft"]').getAnimations()[0]});
  await customRows.nth(2).focus();
  const shifted=await page.evaluate(()=>{const asset=document.querySelector('.mirror-stage .keyboard-asset');return{sameSvg:window.__retainedKeyboard===asset.firstElementChild,samePaths:window.__retainedGlyphs.every(path=>path.isConnected),sameBlink:window.__retainedBlink===asset.querySelector('[data-key-code="ControlLeft"]').getAnimations()[0],newKey:asset.querySelector('[data-key-code="KeyJ"]').dataset.selected,oldKey:asset.querySelector('[data-key-code="KeyK"]').dataset.selected}});
  check(shifted.sameSvg&&shifted.samePaths,height+' keyboard focus changes preserve every original SVG path');
  check(shifted.sameBlink,height+' unchanged selected modifier retains its running blink');
  check(shifted.newKey==='true'&&shifted.oldKey==='false',height+' changed selected keys update immediately');
  await page.keyboard.down('KeyJ');check(await page.locator('.mirror-stage path[data-key-code="KeyJ"]').getAttribute('data-live')==='true',height+' real keyboard keydown still receives solid feedback');
  await page.keyboard.up('KeyJ');await page.waitForFunction(()=>document.querySelector('.mirror-stage path[data-key-code="KeyJ"]')?.getAttribute('data-live')==='false');
  check(await page.evaluate(()=>window.__retainedKeyboard===document.querySelector('.mirror-stage .keyboard-asset svg')),height+' release retains the keyboard SVG');
  await select(1);await page.locator('.output-summary').click();await page.locator('.keyboard-output-dialog').waitFor();
  const keyboard=page.locator('.keyboard-output-dialog');await keyboard.locator('.keyboard-hit-layer button[data-key-code="KeyJ"]').focus();
  await page.evaluate(()=>window.__outputSvg=document.querySelector('.keyboard-output-dialog .keyboard-asset svg'));
  await page.keyboard.press('ArrowRight');check(await page.evaluate(()=>window.__outputSvg===document.querySelector('.keyboard-output-dialog .keyboard-asset svg')),height+' controller-operated output keyboard retains its original artwork');
  check(await keyboard.locator('path[data-key-focus="true"]').count()===1,height+' keyboard direction marks one focused output key');
  await page.keyboard.press('Escape');await keyboard.waitFor({state:'detached'});
 }
 check(await page.evaluate(()=>JSON.stringify(window.__editorPreview.settings))===savedBefore,'all preview input/selection/navigation preserves original rules');
 check(await page.evaluate(()=>window.__editorPreview.writes===0&&window.__editorPreview.nativeWrites===0),'retention checks never write production settings or inject native input');
 check(errors.length===0,'no runtime exceptions');
 const sources=['src/components/ControllerKeyboardPreview.vue','src/components/ControllerDeviceDiagram.vue'];fs.writeFileSync(path.join(out,'ACCEPTANCE.json'),JSON.stringify({pass:true,checks,errors,resolutions:[[1280,720],[1280,800]],settingsWritten:false,nativeInputInjected:false,sources:Object.fromEntries(sources.map(file=>[file,crypto.createHash('sha256').update(fs.readFileSync(path.join(repo,file))).digest('hex')]))},null,2));
 console.log('PASS '+checks.length+' actual editor static-artwork, live-input and focus retention checks');
}finally{await browser.close()}
