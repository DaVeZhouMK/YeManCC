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
const events=[];let validAccount=true,accountListener=null;const window={dispatchEvent:event=>{events.push(event.type);return true;}};window.top=window;window.document={querySelectorAll:()=>[],documentElement:{}};window.MutationObserver=class{observe(){}disconnect(){}};window.addEventListener=()=>{};window.removeEventListener=()=>{};const location={origin:'https://steamloopback.host'};window.location=location;const context=vm.createContext({window,location,Event:class{constructor(type){this.type=type;}}});
function inject(value){return vm.runInContext(render(value),context,{timeout:500});}
window.__YMCC_DECKY_ACCOUNT_WATCH__={version:1,read:account=>validAccount&&account===42,subscribe:fn=>{accountListener=fn;return ()=>{accountListener=null;}}};

check('first injection returns exact run ID',()=>assert.equal(inject(a),a.runId));
check('readback stores exact endpoint and credential, no file IO',()=>assert.equal(JSON.stringify(window.__YMCC_DECKY_MIRROR__),JSON.stringify(a)));
check('bootstrap binding cannot be overwritten accidentally',()=>{const desc=Object.getOwnPropertyDescriptor(window,'__YMCC_DECKY_MIRROR__');assert.equal(desc.writable,false);assert.equal(desc.enumerable,false);assert.equal(desc.configurable,true);});
check('bootstrap object is frozen',()=>assert.equal(Object.isFrozen(window.__YMCC_DECKY_MIRROR__),true));
check('explicit fresh session replaces previous credential safely',()=>{assert.equal(inject(b),b.runId);assert.equal(window.__YMCC_DECKY_MIRROR__.token,b.token);});
check('each explicit binding emits one event without polling',()=>assert.deepEqual(events,['ymcc-decky-bootstrap','ymcc-decky-bootstrap']));
check('same bootstrap can be reattached without redefining failure',()=>assert.equal(inject(b),b.runId));
check('foreign origin never receives a credential or event',()=>{delete window.__YMCC_DECKY_MIRROR__;location.origin='https://example.invalid';const count=events.length;assert.equal(inject(a),undefined);assert.equal(window.__YMCC_DECKY_MIRROR__,undefined);assert.equal(events.length,count);location.origin='https://steamloopback.host';});
check('child frame never receives a credential',()=>{window.top={};assert.equal(inject(a),undefined);assert.equal(window.__YMCC_DECKY_MIRROR__,undefined);window.top=window;});
check('offline account mismatch refuses before any credential or bootstrap event',()=>{const value={...a,account:43,accountSource:'verified-steam-ui'};const count=events.length;assert.equal(inject(value),'ymcc-decky-account-not-current');assert.equal(window.__YMCC_DECKY_MIRROR__,undefined);assert.equal(events.length,count);});
check('verified offline account installs the original bootstrap and an event-driven revocation guard',()=>{window.MutationObserver=class{observe(){}disconnect(){}};const value={...a,account:42,accountSource:'verified-steam-ui'};assert.equal(inject(value),value.runId);assert(window.__YMCC_DECKY_HEALTH__.accountCurrent());assert.equal(typeof accountListener,'function');});
check('logout/switch revokes the old account even if a stale settings object remains, with no automatic UI rebuild',()=>{let packet;window.__YMCC_DECKY_HEALTH_V1=value=>{packet=JSON.parse(value);};validAccount=false;accountListener();assert(!window.__YMCC_DECKY_HEALTH__.accountCurrent());assert.equal(window.__YMCC_DECKY_MIRROR__.revoked,true);assert.equal(window.__YMCC_DECKY_MIRROR__.token,undefined);assert.equal(window.__YMCC_DECKY_MIRROR__.endpoint,undefined);assert.deepEqual(packet,{runId:a.runId,code:'decky-account-changed'});validAccount=true;assert(!window.__YMCC_DECKY_HEALTH__.accountCurrent(),'revoked credentials cannot revive');window.__YMCC_DECKY_HEALTH__.dispose();assert.equal(accountListener,null);});
// Execute the exact native lifecycle mutation expression as well. Ownership
// checked in a previous CDP read can change before the mutation is evaluated.
const watchSource=fs.readFileSync(path.join(root,'native/decky_sidebar_bootstrap_watch.h'),'utf8');
const teardownLine=watchSource.split('\n').find(row=>row.includes('for(const method of'));
const teardownBegin=teardownLine.indexOf('"(() => {');
const teardownEnd=teardownLine.indexOf('},{"contextId"',teardownBegin);
const teardownNative=teardownLine.slice(teardownBegin,teardownEnd<0?undefined:teardownEnd).trim().replace(/;$/,'');
const teardownToken=/"(?:\\.|[^"\\])*"|Json\(runId\)\.dump\(\)/g;
const teardownParts=teardownNative.match(teardownToken);assert(teardownParts?.length);
assert.equal(teardownNative.replace(teardownToken,'').replace(/[+\s]/g,''),'');
const teardownExpression=teardownParts.map(part=>part[0]==='"'?JSON.parse(part):JSON.stringify(a.runId)).join('');
function teardownAt(mirror){
 const calls=[],events=[],host={__YMCC_DECKY_MIRROR__:mirror,dispatchEvent:event=>events.push(event.type),DeckyPluginLoader:{dismountAll:()=>calls.push('dismountAll'),deinit:()=>calls.push('deinit')}};
 const result=vm.runInNewContext(teardownExpression,{window:host,Event:class{constructor(type){this.type=type;}}},{timeout:500});return {result,calls,events};
}
check('exact current native teardown mutates the verified current binding once',()=>{const v=teardownAt(a);assert.equal(v.result,true);assert.deepEqual(v.calls,['dismountAll','deinit']);assert.deepEqual(v.events,['ymcc-decky-health-stop']);});
check('account revoked after prior read prevents actual lifecycle mutation',()=>{const v=teardownAt({runId:a.runId,revoked:true});assert.equal(v.result,false);assert.deepEqual(v.calls,[]);assert.deepEqual(v.events,[]);});
check('binding replaced after prior read prevents actual lifecycle mutation',()=>{const v=teardownAt(b);assert.equal(v.result,false);assert.deepEqual(v.calls,[]);assert.deepEqual(v.events,[]);});
fs.writeFileSync(path.resolve(task,'validation/BOOTSTRAP-EXPRESSION-SELFTEST.json'),JSON.stringify({project:'YMCC Decky 侧边栏',cases:cases.length,results:cases,actualSteam:false,sourceExtracted:true},null,2));console.log(`Bootstrap expression tests passed: ${cases.length}`);
