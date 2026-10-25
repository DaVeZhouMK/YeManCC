// Verify retained SMT/control data through the actual two profile constructors.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
const require=createRequire(path.resolve('package.json')),{transformSync}=require('esbuild'),ts=require('typescript'),{reactive,toRaw}=require('vue');
function load(source,imports={},globals={}){const module={exports:{}};vm.runInNewContext(transformSync(source,{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,structuredClone,console,toRaw,require:key=>{if(key in imports)return imports[key];throw Error(key)},...globals});return module.exports}
const models=load(fs.readFileSync('src/bridge/performanceControls.ts','utf8'));
const panel=fs.readFileSync('src/components/GameCustomProfilePanel.vue','utf8').match(/<script setup[^>]*>([\s\S]*?)<\/script>/)[1];
const parsed=ts.createSourceFile('panel.ts',panel,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
const names=['clone','makeEntry'],functions=parsed.statements.filter(node=>ts.isFunctionDeclaration(node)&&names.includes(node.name?.text)).map(node=>node.getText(parsed)).join('\n');
const {makeEntry}=load(functions+'\nmodule.exports={makeEntry}',{},{gameName:{value:'Fixture'},isPadPersonaOverride:()=>true,isGyroOverride:()=>true});
const schedule={profiles:{ac:{performance:{}},dc:{performance:{}}},active:{ac:'performance',dc:'performance'}},controls={version:1,ac:models.defaultContinuousBank('ac'),dc:models.defaultContinuousBank('dc')};
const entry={controls,displayName:'Game',enabled:true,ac:{tdpMax:30},dc:{tdpMax:15},smtIndependent:true,hyperThreadPolicyEnabled:true,hyperThreadPolicy:'off',padPersona:'elite',gyroOverride:'fps',future:{marker:'preserve'}};
const normalized=value=>JSON.parse(JSON.stringify(value));
let next=makeEntry(reactive(entry),schedule,controls);assert.deepEqual(normalized(next),normalized({...entry,gyroPreset:'fps'}));assert.notEqual(next.controls,entry.controls);next.controls.ac.tdp=29;assert.equal(entry.controls.ac.tdp,55);
const mirror=load(fs.readFileSync('src/bridge/deckyGameActions.ts','utf8'),{'./gamePolicyHysteresis':{gamePolicyKey:()=> 'fixture.exe'},'./virtualTargetPolicyState':{currentVirtualTargetPolicy:()=>null},'./virtualTargetPolicy':{requireVirtualTargetAllowed:()=>{}}});
next=mirror.makeGameMirrorEntry(entry,schedule,'Game');assert.equal(next.hyperThreadPolicy,'off');assert.equal(next.hyperThreadPolicyEnabled,true);assert.equal(next.smtIndependent,true);assert.deepEqual(normalized(next.controls),normalized(controls));assert.deepEqual(normalized(next.future),entry.future);assert.notEqual(next.controls,entry.controls);
assert.match(fs.readFileSync('src/views/PerformanceScheduleView.vue','utf8'),/ContinuousPerformancePanel scope="effective"/);assert.match(fs.readFileSync('src/components/GameCustomProfilePanel.vue','utf8'),/scope="dedicated"/);
console.log(JSON.stringify({pass:true,count:3,cases:['顶部输入编辑保留独立SMT、连续AC/DC和扩展字段，Vue代理也能安全复制','Decky同一条目构造保留SMT和完整连续配置，不再重建时丢字段','性能页跟随生效配置、顶部编辑专属，复用同一面板'],hardwareWrites:0},null,2));
