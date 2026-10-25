import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
const root=process.cwd(),out=path.resolve(process.env.YMCC_APPEARANCE_TEST_OUT || 'G:/YeManCC-Work/Build/Validation/SteamButtons-20261013-145743/appearance-state');
fs.mkdirSync(out,{recursive:true});process.env.YMCC_APPEARANCE_TEST_OUT=out;
const source=fs.readFileSync(path.join(root,'tools/steam_appearance_browser_selftest.mjs'),'utf8');
const end=source.indexOf('let browser,activePage;');if(end<0)throw Error('fixture boundary missing');
let setup=source.slice(0,end)
 .replace('snapshot:{packAvailable:true,packPrepared:true,','snapshot:{targetId:"a".repeat(16),packOwned:true,packAvailable:true,packPrepared:true,')
 .replace('window.__fixture={calls:[]','window.__fixture={holdRead:location.search.includes("slow"),calls:[]')
 .replace("if(action==='Status'){", "if(action==='Status'){if(f().holdRead)await new Promise(resolve=>window.__releaseStatus=resolve);")
 .replace("if(!result)throw Error('Unexpected fixture action '+action);", "if(f().invalidResult)result=f().invalidResult;if(!result)throw Error('Unexpected fixture action '+action);");
// The designated negative run compiles the old full-count predicate into the real component.
if(process.env.YMCC_APPEARANCE_MASTER_NEGATIVE==='1'){
 setup=setup.replace("parse(fs.readFileSync(args.path,'utf8'),", "parse(readComponent(args.path),");
 setup += String.raw`
function readComponent(file){let text=fs.readFileSync(file,'utf8');if(file.endsWith('SteamAppearanceCard.vue')){const a=text.indexOf('const masterOn = computed('),b=text.indexOf('const backendMatches',a);if(a<0||b<0)throw Error('negative mutation anchor missing');text=text.slice(0,a)+'const masterOn = computed(() => enabled.value === STEAM_APPEARANCE_THEMES.length);\n'+text.slice(b);}return text;}
`;
}
const tail=String.raw`
let browser;const cases=[],errors=[];
try{
 browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 const newPage=async(suffix='')=>{const p=await browser.newPage({viewport:{width:720,height:900}});p.on('pageerror',e=>errors.push(e.message));await p.goto('http://127.0.0.1:'+server.address().port+'/'+suffix);return p};
 const initial=await newPage('?slow');await initial.waitForFunction(()=>window.__releaseStatus);
 assert.equal(await initial.locator('.appearance-count').textContent(),'8 / 8');assert.equal(await initial.locator('.appearance-master').getAttribute('aria-checked'),'true');assert.match(await initial.locator('.appearance-count').getAttribute('aria-label'),/默认开启.*待确认/);
 await initial.getByRole('button',{name:'编辑外观'}).click();assert.equal(await initial.locator('.appearance-theme-switch[aria-checked="true"]').count(),8);assert.match(await initial.locator('.appearance-editor-summary').textContent(),/默认开启.*待确认/);
 assert.equal(await initial.evaluate(()=>window.__fixture.calls.some(call=>call.action!=='Status')),false);assert.equal(await initial.evaluate(()=>window.__fixture.hiddenCalls),0);
 await initial.evaluate(()=>{window.__fixture.holdRead=false;window.__releaseStatus()});await initial.waitForFunction(()=>window.__draft.getState().snapshot);assert.equal(await initial.locator('.appearance-theme-switch[aria-checked="true"]').count(),8);
 cases.push('slow initial read displays all eight default switches on with an explicit unconfirmed label and only Status calls');await initial.close();
 const partial=await newPage();await partial.waitForFunction(()=>window.__draft.getState().snapshot);
 await partial.evaluate(async()=>{const s=window.__fixture.snapshot;s.backendReady=false;s.backendProcessCount=0;s.configuredMatchesBackend=false;for(const [i,row] of s.themes.entries()){row.installed=i<4;row.configured=i<4;row.backendEnabled=null;}await window.__draft.read();});
 assert.equal(await partial.locator('.appearance-count').textContent(),'4 / 8');assert.equal(await partial.locator('.appearance-master').getAttribute('aria-checked'),'true');
 assert.equal(await partial.locator('.appearance-master.mixed').count(),0);
 cases.push('partial custom selection displays 4/8 with the group enabled, never a centered mixed knob');
 await partial.locator('.appearance-master').click();assert.equal(await partial.locator('.appearance-count').textContent(),'0 / 8');assert.equal(await partial.locator('.appearance-master').getAttribute('aria-checked'),'false');
 cases.push('explicit master OFF disables the entire partly enabled group immediately');
 await partial.locator('.appearance-master').click();assert.equal(await partial.locator('.appearance-count').textContent(),'8 / 8');assert.equal(await partial.locator('.appearance-master').getAttribute('aria-checked'),'true');assert.match(await partial.locator('.appearance-count').getAttribute('aria-label'),/已选择，待应用/);
 await partial.getByRole('button',{name:'编辑外观'}).click();assert.equal(await partial.locator('.appearance-theme-switch[aria-checked="true"]').count(),8);assert.equal(await partial.locator('.appearance-editor-summary strong').textContent(),'8');assert.equal(await partial.evaluate(()=>window.__fixture.applyRequests.length),0);
 cases.push('pending master selection and editor count agree even for themes awaiting installation');await partial.close();
 const group=await newPage();await group.waitForFunction(()=>window.__draft.getState().snapshot);
 for(let count=7;count>=0;count--){
  await group.evaluate(async n=>{window.__fixture.snapshot.themes.forEach((row,i)=>{row.configured=i<n;row.backendEnabled=i<n});await window.__draft.read();},count);
  assert.equal(await group.locator('.appearance-count').textContent(),count+' / 8');
  assert.equal(await group.locator('.appearance-master').getAttribute('aria-checked'),String(count>0));
  assert.equal(await group.locator('.appearance-master.mixed').count(),0);
 }
 assert.equal(await group.evaluate(()=>window.__fixture.applyRequests.length),0);
 cases.push('external partial selections keep the group enabled from 7/8 to 1/8; 0/8 is off and passive reads never apply');
 await group.locator('.appearance-master').click();await group.evaluate(()=>window.__draft.flush(false));
 assert.equal(await group.locator('.appearance-count').textContent(),'8 / 8');assert.equal(await group.locator('.appearance-master').getAttribute('aria-checked'),'true');
 assert.equal(await group.evaluate(()=>window.__fixture.applyRequests.at(-1).states && Object.values(window.__fixture.applyRequests.at(-1).states).every(v=>v===true)),true);
 await group.evaluate(async()=>{window.__fixture.snapshot.themes.find(row=>row.name==='Darken Unfocused Games').configured=false;await window.__draft.read();});
 assert.equal(await group.locator('.appearance-count').textContent(),'7 / 8');assert.equal(await group.locator('.appearance-master').getAttribute('aria-checked'),'true');
 await group.locator('.steam-appearance-card').screenshot({path:path.join(out,'seven-of-eight-master-on.png')});
 await group.getByRole('button',{name:'编辑外观'}).click();assert.equal(await group.getByRole('switch',{name:'焦点突出',exact:true}).getAttribute('aria-checked'),'false');
 await group.getByRole('switch',{name:'焦点突出',exact:true}).click();assert.equal(await group.locator('.appearance-count').textContent(),'8 / 8');
 await group.getByRole('switch',{name:'焦点突出',exact:true}).click();assert.equal(await group.locator('.appearance-count').textContent(),'7 / 8');assert.equal(await group.locator('.appearance-master').getAttribute('aria-checked'),'true');
 cases.push('all-on applies actual booleans; disabling focus highlighting via editor returns 7/8 without turning off the group');await group.close();
 const page=await newPage();await page.waitForFunction(()=>window.__draft.getState().snapshot);await page.evaluate(()=>window.__baseline=structuredClone(window.__fixture.snapshot));
 for(const [label,patch] of [['Steam stopped',{steamRunning:false}],['unprepared pack',{packPrepared:false}],['foreign active themes',{foreignActiveCount:2}],['reload required',{backendReloadRequired:true}],['pack repair required',{packRepairRequired:true}]]){
  await page.evaluate(async patch=>{window.__fixture.snapshot={...structuredClone(window.__baseline),...patch};await window.__draft.read()},patch);
  assert.equal(await page.locator('.appearance-status-dot.ready').count(),0,label);cases.push(label+' cannot display a confirmed ready light');
 }
 await page.evaluate(async()=>{window.__fixture.snapshot=structuredClone(window.__baseline);window.__fixture.snapshot.themes[0].backendEnabled=false;await window.__draft.read()});assert.equal(await page.locator('.appearance-status-dot.ready').count(),0);assert.match(await page.locator('.appearance-status-line').textContent(),/配置待重新应用/);cases.push('aggregate backend match cannot override a per-theme mismatch');
 await page.evaluate(async()=>{window.__fixture.snapshot={...structuredClone(window.__baseline),backendIdle:true,backendReady:false,backendProcessCount:1,configuredMatchesBackend:false};await window.__draft.read()});assert.equal(await page.locator('.appearance-status-dot.ready').count(),0);assert.match(await page.locator('.appearance-status-line').textContent(),/退出状态暂未确认/);cases.push('a stale idle flag with a remaining CSS process cannot claim CSS has exited');
 await page.evaluate(async()=>{window.__fixture.snapshot={...structuredClone(window.__baseline),backendIdle:true,backendReady:false,backendProcessCount:0,configuredMatchesBackend:false};await window.__draft.read()});assert.equal(await page.locator('.appearance-status-dot.ready').count(),1);assert.match(await page.locator('.appearance-status-line').textContent(),/CSS 后台已退出/);cases.push('confirmed applied appearance stays ready after the temporary CSS backend exits');
 const invalid=await page.evaluate(async()=>{const s=window.__fixture.snapshot;const pack=window.__valid({...s,packError:{bad:true}});const results=[];for(const extra of [{saved:'false'},{message:{text:'bad'}},{snapshot:false}]){window.__fixture.invalidResult={ok:true,snapshot:structuredClone(s),...extra};try{await window.__service.read();results.push('accepted')}catch(e){results.push(e.message)}}delete window.__fixture.invalidResult;return {pack,results}});assert.equal(invalid.pack,false);assert(invalid.results.every(value=>value.includes('无效状态')));cases.push('malformed snapshot metadata and acknowledgement types fail validation');
 await page.screenshot({path:path.join(out,'confirmed-idle.png')});await page.close();
 const slow=await newPage('?slow');await slow.waitForFunction(()=>!!window.__releaseStatus);await slow.getByRole('button',{name:'编辑外观'}).click();assert.equal(await slow.locator('.appearance-editor-summary strong').textContent(),'8');
 const round=slow.getByRole('switch',{name:'界面圆角',exact:true});await round.click();assert.equal(await round.getAttribute('aria-checked'),'false');assert.equal(await slow.locator('.appearance-count').textContent(),'7 / 8');assert.equal(await slow.locator('.appearance-master').getAttribute('aria-checked'),'true');assert.equal(await slow.locator('.appearance-master.mixed').count(),0);await round.click();assert.equal(await round.getAttribute('aria-checked'),'true');assert.equal(await slow.locator('.appearance-count').textContent(),'8 / 8');assert.equal(await slow.evaluate(()=>window.__fixture.applyRequests.length),0);cases.push('first-read delay still shows each click immediately and never repeats the same toggle intent');await slow.close();
 assert.deepEqual(errors,[]);const sourceFiles=['src/components/SteamAppearanceCard.vue','src/components/SteamAppearanceEditor.vue','src/bridge/steamAppearanceDraft.ts','src/bridge/steamAppearance.ts','tools/steam_appearance_state_browser_selftest.mjs','tools/steam_appearance_browser_selftest.mjs'];const record={ok:true,passed:cases.length,cases,errors,sourceHashes:Object.fromEntries(sourceFiles.map(file=>[file,createHash('sha256').update(fs.readFileSync(path.join(root,file))).digest('hex')])),realSteamOperations:0,realCssOperations:0};fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(record,null,2));console.log(JSON.stringify(record,null,2));
}finally{await browser?.close();await new Promise(r=>server.close(r))}
`;
const runner=path.join(out,'state-runner.mjs');fs.writeFileSync(runner,setup+tail);await import(pathToFileURL(runner).href);
