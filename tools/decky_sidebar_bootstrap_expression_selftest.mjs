import { task } from './fixtures/decky_task_paths.mjs';
// Verify the exact native bootstrap expression with an in-memory JS target only.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
const root=path.resolve(import.meta.dirname,'..');const source=fs.readFileSync(path.join(root,'native/decky_sidebar_bootstrap.h'),'utf8');
const line=source.split('\n').find(line=>line.includes('const std::string expression='));assert.ok(line);
// Reconstruct the complete native expression, including generated scripts and pinned version.
const native=line.slice(line.indexOf('expression=')+11).trim().replace(/;$/,'');
const token=/"(?:\\.|[^"\\])*"|bootstrap\.dump\(\)|std::string\((?:frontendUpdateScript\(\)|healthScript\(\)|ymcc::deckyidentity::version)\)/g;
const parts=native.match(token);assert(parts&&parts.length===9,'native expression inputs changed; review deliberately');
assert.equal(native.replace(token,'').replace(/\s/g,''),'+'.repeat(8));
const version=JSON.parse(fs.readFileSync(path.join(root,'decky-plugin/vendor/decky-loader.lock.json'),'utf8')).version;
const render=value=>parts.map(part=>part[0]==='"'?JSON.parse(part):part==='bootstrap.dump()'?JSON.stringify(value):part.includes('frontendUpdateScript')?fs.readFileSync(path.join(root,'native/decky_sidebar_frontend_update.js'),'utf8'):part.includes('healthScript')?fs.readFileSync(path.join(root,'native/decky_sidebar_health.js'),'utf8'):version).join('');
const cases=[];function check(name,fn){fn();cases.push({name,status:'passed'});}
const a={endpoint:'ws://127.0.0.1:12345/mirror',token:'a'.repeat(64),runId:'b'.repeat(32)};const b={...a,token:'c'.repeat(64),runId:'d'.repeat(32)};
const events=[];const window={dispatchEvent:event=>{events.push(event.type);return true;}};window.top=window;window.document={querySelectorAll:()=>[],documentElement:{}};window.MutationObserver=class{observe(){}disconnect(){}};window.addEventListener=()=>{};window.removeEventListener=()=>{};const location={origin:'https://steamloopback.host'};window.location=location;const context=vm.createContext({window,location,Event:class{constructor(type){this.type=type;}}});
function inject(value){return vm.runInContext(render(value),context,{timeout:500});}
check('first injection returns exact run ID',()=>assert.equal(inject(a),a.runId));
check('readback stores exact endpoint and credential, no file IO',()=>assert.equal(JSON.stringify(window.__YMCC_DECKY_MIRROR__),JSON.stringify(a)));
check('bootstrap binding cannot be overwritten accidentally',()=>{const desc=Object.getOwnPropertyDescriptor(window,'__YMCC_DECKY_MIRROR__');assert.equal(desc.writable,false);assert.equal(desc.enumerable,false);assert.equal(desc.configurable,true);});
check('bootstrap object is frozen',()=>assert.equal(Object.isFrozen(window.__YMCC_DECKY_MIRROR__),true));
check('explicit fresh session replaces previous credential safely',()=>{assert.equal(inject(b),b.runId);assert.equal(window.__YMCC_DECKY_MIRROR__.token,b.token);});
check('each explicit binding emits one event without polling',()=>assert.deepEqual(events,['ymcc-decky-bootstrap','ymcc-decky-bootstrap']));
check('same bootstrap can be reattached without redefining failure',()=>assert.equal(inject(b),b.runId));
check('foreign origin never receives a credential or event',()=>{delete window.__YMCC_DECKY_MIRROR__;location.origin='https://example.invalid';const count=events.length;assert.equal(inject(a),undefined);assert.equal(window.__YMCC_DECKY_MIRROR__,undefined);assert.equal(events.length,count);location.origin='https://steamloopback.host';});
check('child frame never receives a credential',()=>{window.top={};assert.equal(inject(a),undefined);assert.equal(window.__YMCC_DECKY_MIRROR__,undefined);window.top=window;});
fs.writeFileSync(path.resolve(task,'validation/BOOTSTRAP-EXPRESSION-SELFTEST.json'),JSON.stringify({project:'YMCC Decky 侧边栏',cases:cases.length,results:cases,actualSteam:false,sourceExtracted:true},null,2));console.log(`Bootstrap expression tests passed: ${cases.length}`);
