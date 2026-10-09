import { task } from './fixtures/decky_task_paths.mjs';
// Verify the exact native bootstrap expression with an in-memory JS target only.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
const root=path.resolve(import.meta.dirname,'..');const source=fs.readFileSync(path.join(root,'native/decky_sidebar_bootstrap.h'),'utf8');
const line=source.split('\n').find(line=>line.includes('const std::string expression='));assert.ok(line);
const match=line.match(/expression="((?:\\.|[^"\\])*)"\+bootstrap\.dump\(\)\+"((?:\\.|[^"\\])*)";/);assert.ok(match,'C++ expression boundary changed; update verifier deliberately');
const decode=text=>JSON.parse('"'+text+'"');const before=decode(match[1]),after=decode(match[2]);
const cases=[];function check(name,fn){fn();cases.push({name,status:'passed'});}
const a={endpoint:'ws://127.0.0.1:12345/mirror',token:'a'.repeat(64),runId:'b'.repeat(32)};const b={...a,token:'c'.repeat(64),runId:'d'.repeat(32)};
const events=[];const window={dispatchEvent:event=>{events.push(event.type);return true;}};const context=vm.createContext({window,Event:class{constructor(type){this.type=type;}}});
function inject(value){return vm.runInContext(before+JSON.stringify(value)+after,context,{timeout:500});}
check('first injection returns exact run ID',()=>assert.equal(inject(a),a.runId));
check('readback stores exact endpoint and credential, no file IO',()=>assert.equal(JSON.stringify(window.__YMCC_DECKY_MIRROR__),JSON.stringify(a)));
check('bootstrap binding cannot be overwritten accidentally',()=>{const desc=Object.getOwnPropertyDescriptor(window,'__YMCC_DECKY_MIRROR__');assert.equal(desc.writable,false);assert.equal(desc.enumerable,false);assert.equal(desc.configurable,true);});
check('bootstrap object is frozen',()=>assert.equal(Object.isFrozen(window.__YMCC_DECKY_MIRROR__),true));
check('explicit fresh session replaces previous credential safely',()=>{assert.equal(inject(b),b.runId);assert.equal(window.__YMCC_DECKY_MIRROR__.token,b.token);});
check('each explicit binding emits one event without polling',()=>assert.deepEqual(events,['ymcc-decky-bootstrap','ymcc-decky-bootstrap']));
check('same bootstrap can be reattached without redefining failure',()=>assert.equal(inject(b),b.runId));
fs.writeFileSync(path.resolve(task,'validation/BOOTSTRAP-EXPRESSION-SELFTEST.json'),JSON.stringify({project:'YMCC Decky 侧边栏',cases:cases.length,results:cases,actualSteam:false,sourceExtracted:true},null,2));console.log(`Bootstrap expression tests passed: ${cases.length}`);
