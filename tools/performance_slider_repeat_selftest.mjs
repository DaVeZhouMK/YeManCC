import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
const root=path.resolve(import.meta.dirname,'..'),require=createRequire(path.join(root,'package.json'));
const {buildSync,transformSync}=require('esbuild'),ts=require('typescript');
const source=fs.readFileSync(path.join(root,'src/gamepad/sliderRepeat.ts'),'utf8');
const compiled=buildSync({entryPoints:[path.join(root,'src/gamepad/sliderRepeat.ts')],bundle:true,write:false,platform:'node',format:'cjs'}).outputFiles[0].text;
const module={exports:{}};vm.runInNewContext(compiled,{module,exports:module.exports});
const {editRepeatInterval:interval}=module.exports;
const engine=fs.readFileSync(path.join(root,'src/gamepad/engine.ts'),'utf8');
const ast=ts.createSourceFile('engine.ts',engine,ts.ScriptTarget.Latest,true);
const functions=ast.statements.filter(n=>ts.isFunctionDeclaration(n)&&['resetInlineEditCadence','fanNodeEditRepeatInterval','adjustSlider'].includes(n.name?.text)).map(n=>n.getText(ast)).join('\n');
assert.equal(ast.statements.filter(n=>ts.isFunctionDeclaration(n)&&['resetInlineEditCadence','fanNodeEditRepeatInterval','adjustSlider'].includes(n.name?.text)).length,3);
const fanSource='let inlineEditElement=null,inlineEditDirectionKey="",inlineEditStartedAt=0,inlineEditLastRepeatAt=0; const FAN_NODE_EDIT_REPEAT=55;'+functions;
const context=vm.createContext({editRepeatInterval:interval,document:{activeElement:null},Event:class{constructor(type){this.type=type}},calls:[]});
vm.runInContext(transformSync(fanSource,{loader:'ts',target:'es2020'}).code,context);
const fanFile=fs.readFileSync(path.join(root,'src/views/FanView.vue'),'utf8');
const fanScript=/<script setup[^>]*>([\s\S]*?)<\/script>/.exec(fanFile)[1];
const fanAst=ts.createSourceFile('FanView.ts',fanScript,ts.ScriptTarget.Latest,true);
const fanMethods=fanAst.statements.filter(n=>ts.isFunctionDeclaration(n)&&['normalizeEditedNodes','onGraphSpatialEdit','stopGraphNodeEditing','onDocumentVisibilityChange'].includes(n.name?.text)).map(n=>n.getText(fanAst)).join('\n');
const fanDeactivate=fanAst.statements.find(n=>ts.isExpressionStatement(n)&&ts.isCallExpression(n.expression)&&n.expression.expression.getText(fanAst)==='onDeactivated');
assert(fanDeactivate,'Actual fan deactivation hook is required');
const fanCurveModule={exports:{}};
vm.runInNewContext(buildSync({entryPoints:[path.join(root,'src/bridge/fanCurve.ts')],bundle:true,write:false,platform:'node',format:'cjs'}).outputFiles[0].text,{module:fanCurveModule,exports:fanCurveModule.exports});
const fanContext=vm.createContext({
 normalizeFanNodes:fanCurveModule.exports.normalizeFanNodes,graphDragArmed:{value:1},
 nodes:{value:[{tempC:0,dutyPercent:0},{tempC:40,dutyPercent:25},{tempC:70,dutyPercent:50},{tempC:100,dutyPercent:100}]},
 selectNode(){},syncGraphNodeEditMarker(){},emitNativeEvent(event,data){thisFanEvents.push({event,data})},
 document:{hidden:false},viewVisible:true,viewActive:true,stopTelemetry(){},stopBusyTicker(){},
 syncControlSwitchLock(){},hasCurrentControlCommit:()=>false,controlActive:{value:false},startBusyTicker(){},startTelemetry(){},
});
const thisFanEvents=[];
vm.runInContext(transformSync(fanMethods+'\nvar deactivateFan='+fanDeactivate.expression.arguments[0].getText(fanAst)+';',{loader:'ts',target:'es2020'}).code,fanContext);
const cases=[];
function check(name,run){try{run();cases.push({name,pass:true})}catch(error){cases.push({name,pass:false,error:String(error.stack||error)})}}
check('Edit cadence starts at the actual fan reference: 55ms',()=>{
 for(const held of [0,1,100,399,400])assert.equal(interval(held),55);
});
check('Only the interval changes, linearly from 55ms to 25ms',()=>{
 for(let held=0;held<=2500;held++)assert.equal(interval(held),55-Math.floor(Math.min(1600,Math.max(0,held-400))*30/1600));
 assert.deepEqual([400,800,1200,1600,2000].map(t=>interval(t)),[55,48,40,33,25]);
});
check('Long holds remain bounded; disabling acceleration retains base cadence',()=>{
 for(const held of [2000,10000,Number.MAX_SAFE_INTEGER])assert.equal(interval(held),25);
 for(const held of [0,1000,20000])assert.equal(interval(held,55,false),55);
});
check('Invalid time/base cannot cause runaway zero-delay repetition',()=>{
 for(const held of [NaN,Infinity,-100])assert.equal(interval(held),55);
 for(const base of [NaN,Infinity,-55,0])assert.equal(interval(0,base),55);
 for(const base of [1,20,55,150,10000])for(const held of [0,1000,10000])assert(interval(held,base)>=5&&interval(held,base)<=220);
});
check('Actual fan browser consumer accelerates interval, preserving one-step edits',()=>{
 const node={dataset:{gpInlineEditRepeatMs:'55'}};
 const observed=[0,400,800,1200,1600,2000].map(now=>context.fanNodeEditRepeatInterval(node,now,'x:1'));
 assert.deepEqual(observed,[55,55,48,40,33,25]);
});
check('Fan direction/axis/selected node changes reset acceleration',()=>{
 context.resetInlineEditCadence();const a={dataset:{gpInlineEditRepeatMs:'55'}},b={dataset:{gpInlineEditRepeatMs:'55'}};
 context.fanNodeEditRepeatInterval(a,0,'x:1');assert.equal(context.fanNodeEditRepeatInterval(a,2500,'x:1'),25);
 assert.equal(context.fanNodeEditRepeatInterval(a,2501,'x:-1'),55);
 assert.equal(context.fanNodeEditRepeatInterval(a,5001,'x:-1'),25);
 assert.equal(context.fanNodeEditRepeatInterval(a,5002,'y:-1'),55);
 assert.equal(context.fanNodeEditRepeatInterval(a,8002,'y:-1'),25);
 assert.equal(context.fanNodeEditRepeatInterval(b,8003,'y:-1'),55);
});
check('Releasing fan editing or leaving the node resets the hold clock',()=>{
 const node={dataset:{gpInlineEditRepeatMs:'55'}};
 context.fanNodeEditRepeatInterval(node,10000,'x:1');assert.equal(context.fanNodeEditRepeatInterval(node,13000,'x:1'),25);
 context.fanNodeEditRepeatInterval(null,13001,'');assert.equal(context.fanNodeEditRepeatInterval(node,13002,'x:1'),55);
});
check('Actual slider consumer advances exactly one grid step even on arbitrarily many held events',()=>{
 const input={tagName:'INPUT',type:'range',dataset:{gpRepeatIntervalMs:'55'},min:'0',max:'10000',step:'1',value:'0',events:[],dispatchEvent(e){this.events.push({type:e.type,value:Number(this.value)})}};
 context.document.activeElement=input;
 for(let i=0;i<1000;i++){const before=Number(input.value);context.adjustSlider(1);assert.equal(Number(input.value),before+1);}
 for(let i=0;i<1000;i++){const before=Number(input.value);context.adjustSlider(-1);assert.equal(Number(input.value),before-1);}
 assert(input.events.every(e=>e.type==='input'));assert.equal(input.events.length,2000);
});
check('Actual off-grid control still reaches the adjacent grid point without skipping',()=>{
 const input={tagName:'INPUT',type:'range',dataset:{gpStep:'5',gpStepBase:'0',gpRepeatIntervalMs:'55'},min:'1',max:'100',step:'any',value:'13',dispatchEvent(){}};
 context.document.activeElement=input;context.adjustSlider(1);assert.equal(Number(input.value),15);
 input.value='13';context.adjustSlider(-1);assert.equal(Number(input.value),10);
});
check('Actual fan node consumer changes one degree/percent at a time without hardware commits',()=>{
 const edits=[{dx:1,dy:0,temp:41,duty:25},{dx:-1,dy:0,temp:40,duty:25},{dx:0,dy:-1,temp:40,duty:26},{dx:0,dy:1,temp:40,duty:25}];
 for(const {dx,dy,temp,duty} of edits){let prevented=false;fanContext.onGraphSpatialEdit(1,{detail:{dx,dy},preventDefault(){prevented=true}});assert(prevented);assert.equal(fanContext.nodes.value[1].tempC,temp);assert.equal(fanContext.nodes.value[1].dutyPercent,duty);}
 assert.equal(thisFanEvents.length,0);
});
check('Actual fan deactivation ends edit cadence but does not change the fan control intent',()=>{
 fanContext.graphDragArmed.value=1;const before=JSON.stringify(fanContext.nodes.value);fanContext.deactivateFan();
 assert.equal(fanContext.graphDragArmed.value,null);assert.equal(fanContext.viewActive,false);assert.equal(JSON.stringify(fanContext.nodes.value),before);
 assert.equal(thisFanEvents.at(-1).event,'gamepad.fan-node-edit');assert.equal(thisFanEvents.at(-1).data.active,false);
});
check('Actual hidden fan page ends editing, then ignores stale direction events',()=>{
 fanContext.viewActive=true;fanContext.document.hidden=true;fanContext.graphDragArmed.value=1;
 const before=JSON.stringify(fanContext.nodes.value);fanContext.onDocumentVisibilityChange();assert.equal(fanContext.graphDragArmed.value,null);
 fanContext.onGraphSpatialEdit(1,{detail:{dx:1,dy:0},preventDefault(){throw Error('A cancelled edit must not consume direction input')}});
 assert.equal(JSON.stringify(fanContext.nodes.value),before);assert.equal(thisFanEvents.at(-1).data.active,false);
});
check('Cadence policy adds no timer or poller; caller owns the existing input clock',()=>{
 assert.doesNotMatch(source,/\b(?:setInterval|setTimeout|requestAnimationFrame)\s*\(/);
 assert.doesNotMatch(functions,/sliderRepeatDelta|speedMultiplier|Math\.floor\(elapsed/);
});
const report={pass:cases.every(c=>c.pass),count:cases.length,cases,hardwareCalls:0,scope:'Production interval helper, actual browser fan cadence and actual slider adjustment; memory-only clock/DOM boundary.'};
const args=process.argv.slice(2),out=args[0]==='--out'?args[1]:args[0];if(!out)throw Error('An explicit evidence output directory is required');
fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'performance-slider-repeat.json'),JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify(report,null,2));if(!report.pass)process.exitCode=1;
