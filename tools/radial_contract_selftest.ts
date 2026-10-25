import { radialItemIcon } from '../src/radial/presentation';
import { ICONS } from '../src/icons';
import assert from 'node:assert/strict';
import { ACTIONS, defaultRadialSettings, normalizeRadialSettings } from '../src/radial/model';
import { WHEEL, primarySectors, childSectors, contains, hit, RadialSelection, dialValue, sector } from '../src/radial/geometry';
let cases=0;const check=(value:unknown,message:string)=>{assert.ok(value,message);cases++;};
const defaults=defaultRadialSettings().pages[0].items;check(defaults[6].action==='fan','default north position is fan Off plus three presets');check(defaults.flatMap(item=>[item,...(item.children??[])]).filter(item=>item.action==='windows.browser').length===1,'default menu exposes one browser');check(!ACTIONS.some(action=>action.value==='fan.control'),'ephemeral fan choices cannot be persisted');
check(defaults[3].action==='input.pad'&&defaults[4].action==='input.gyro','default menu includes native-owned virtual-pad and gyro groups');check(!ACTIONS.some(action=>action.value==='input.control'),'ephemeral input choices cannot be persisted');
check(defaults[2].action==='monitor.templates'&&defaults[7].action==='steam.controls','final default menu contains monitor A/B and Steam utility groups');check(!ACTIONS.some(action=>action.value==='utility.control'),'ephemeral utility leaves cannot become persisted commands');
const config=defaultRadialSettings(),items=config.pages[0].items,primary=primarySectors(items),children=childSectors(primary[1]);
check(primary.length===8&&children.length===3,'8 sectors; 3 child leaves');
for(const s of [...primary,...children]){check(contains(s,s.x,s.y),'rendered label centre is inside actual hit geometry');check(!contains(s,248,248),'all sectors have a hollow centre');check(s.polygon.length>=18,'native polygon uses bounded arc samples');}
for(let x=183;x<314;x+=5)for(let y=183;y<314;y+=5)if(Math.hypot(x-248,y-248)<66)check(hit([...primary,...children],x,y)===null,'centre cannot select a parent or child');
const select=new RadialSelection();select.move(items,primary[1].x,primary[1].y);check(select.parent===1,'parent follows its real sector');
select.move(items,children[0].x,children[0].y);check(select.locked&&select.leaf===children[0].id,'real enabled child locks parent');
select.move(items,248+Math.cos(.7)*153,248+Math.sin(.7)*153);check(select.leaf===null&&select.locked,'gap cannot execute parent');
select.move(items,248+Math.cos(.7)*130,248+Math.sin(.7)*130);check(!select.locked&&!select.enteredChildren,'inner exit radius unlocks child');
select.move(items,248,248);check(select.leaf===null&&select.parent===null,'centre clears stale selection');
for(const leaf of children)for(const steps of [8,32,120]){
 select.reset();const from=primary[1];select.move(items,from.x,from.y);
 for(let n=1;n<=steps;n++)select.move(items,from.x+(leaf.x-from.x)*n/steps,from.y+(leaf.y-from.y)*n/steps);
 check(select.locked&&select.leaf===leaf.id,'continuous diagonal slide crosses the actual parent-child gap');
}
const disabled=structuredClone(items);disabled[1].children![0].enabled=false;select.move(disabled,primary[1].x,primary[1].y);select.move(disabled,children[0].x,children[0].y);check(!select.locked&&select.leaf===null,'disabled child cannot lock or fall back to parent');
select.reset();select.stick(items,0,1);check(select.leaf==='fanControls','native LY up selects default north fan controls');select.stick(items,0,0);check(select.leaf===null,'stick dead zone clears stale action');
for(const s of primary){const angle=(s.start+s.end)/2;const r=195;check(dialValue(248+Math.cos(angle-1.28)*r,248+Math.sin(angle-1.28)*r,angle,5,30)===5,'dial min at own arc');check(dialValue(248+Math.cos(angle+1.28)*r,248+Math.sin(angle+1.28)*r,angle,5,30)===30,'dial max wraps at +/-pi');}
check(dialValue(248,248,0,5,30)===null,'dial centre never changes value');check(dialValue(248-195,248,0,5,30)===null,'outside dial arc never changes value');
const malformed=normalizeRadialSettings({holdMs:NaN,pages:[{items:[{id:'x',action:'shell.raw'}]}]});check(malformed.holdMs===650&&malformed.pages[0].items.length===8,'invalid stored commands use known model, no raw shell');
check(normalizeRadialSettings({...config,holdMs:1}).holdMs===400,'hold duration bounded');

// Distances across each shared cut must stay constant at both circle radii.
const near=(actual:number,expected:number,message:string)=>check(Math.abs(actual-expected)<1e-8,message);
const pt=(radius:number,angle:number):[number,number]=>[248+radius*Math.cos(angle),248+radius*Math.sin(angle)];
function parallelCuts(list:ReturnType<typeof primarySectors>,closed=false){
 for(let index=0;index<list.length-(closed?0:1);index++){
  const left=list[index],right=list[(index+1)%list.length],leftSteps=left.polygon.length/2-1;
  const lo=left.polygon[leftSteps],li=left.polygon[leftSteps+1],ro=right.polygon[0],ri=right.polygon.at(-1)!;
  near(Math.hypot(lo[0]-ro[0],lo[1]-ro[1]),WHEEL.gap,'outer seam uses fixed spacing');
  near(Math.hypot(li[0]-ri[0],li[1]-ri[1]),WHEEL.gap,'inner seam uses the same spacing');
  const lv=[lo[0]-li[0],lo[1]-li[1]],rv=[ro[0]-ri[0],ro[1]-ri[1]];
  near(lv[0]*rv[1]-lv[1]*rv[0],0,'adjacent cut edges are parallel');
  const angle=left.end,nx=-Math.sin(angle),ny=Math.cos(angle);
  for(const radius of [left.inner+1,(left.inner+left.outer)/2,left.outer-1])for(const offset of [-3,-2.1,-1.9,-1,0,1,1.9,2.1,3]){
   const [x,y]=pt(radius,angle),p=[x+nx*offset,y+ny*offset];
   check(contains(left,p[0],p[1])===(offset<=-WHEEL.gap/2),'left hit boundary follows its parallel cut');
   check(contains(right,p[0],p[1])===(offset>=WHEEL.gap/2),'right hit boundary follows its parallel cut');
  }
 }
}
parallelCuts(primary,true);parallelCuts(children);
const mergedA=sector(items[0],0,null,-Math.PI/8,Math.PI/8,66,149,0),mergedB=sector(items[1],1,null,Math.PI/8,3*Math.PI/8,66,149,0);
near(Math.hypot(mergedA.polygon[mergedA.polygon.length/2-1][0]-mergedB.polygon[0][0],mergedA.polygon[mergedA.polygon.length/2-1][1]-mergedB.polygon[0][1]),0,'zero-spacing sectors join exactly');
for(const sectorShape of [...primary,...children]){
 const sampleCount=sectorShape.polygon.length/2-1;
 const [outerStartX,outerStartY]=sectorShape.polygon[0], [innerStartX,innerStartY]=sectorShape.polygon.at(-1)!;
 const [outerEndX,outerEndY]=sectorShape.polygon[sampleCount],[innerEndX,innerEndY]=sectorShape.polygon[sampleCount+1];
 for(const [x,y]of [[outerStartX,outerStartY],[innerStartX,innerStartY]])near(-(x-248)*Math.sin(sectorShape.start)+(y-248)*Math.cos(sectorShape.start),WHEEL.gap/2,'start cut is offset by half the seam');
 for(const [x,y]of [[outerEndX,outerEndY],[innerEndX,innerEndY]])near((x-248)*Math.sin(sectorShape.end)-(y-248)*Math.cos(sectorShape.end),WHEEL.gap/2,'end cut is offset by half the seam');
}
for(const angle of primary.map(s=>(s.start+s.end)/2)){
 const shape=sector(items[0],0,0,angle-1.3,angle+1.3,158,232);
 for(const sign of [-1,1]){
  const p=pt(195,angle+sign*1.3);check(!contains(shape,p[0],p[1]),'dial cut is visually and interactively hollow');check(dialValue(...p,angle,5,150)===null,'dial spacing cannot change value');
  const inset=Math.asin(shape.gap/(shape.inner+shape.outer)),end=pt(195,angle+sign*(1.3-inset));
  check(dialValue(...end,angle,5,150)===(sign<0?5:150),'dial knob endpoints map to exact limits');
 }
}
for(const args of [[0,0,66,149,4],[0,Math.PI,66,149,4],[0,1,0,149,4],[0,1,149,66,4],[0,1,66,149,-1],[0,.01,66,149,4]]){
 assert.throws(()=>sector(items[0],0,null,...args as [number,number,number,number,number]),/Invalid radial sector/);cases++;
}
for(const action of ACTIONS)check(!!ICONS[radialItemIcon({id:'icon-check',label:action.label,action:action.value,enabled:true})],'all actions reuse an existing YMCC icon');
check(radialItemIcon({id:'legacy-tdp',label:'TDP',action:'tdp',enabled:true})==='gauge'&&radialItemIcon({id:'schedule',label:'性能调度',action:'page.schedule',enabled:true})==='gauge','TDP and performance use the local performance-page icon');

check(radialItemIcon({id:'pages',label:'性能调度',action:'submenu',enabled:true,children:[{id:'runtime',label:'TDP',action:'performance.control',enabled:true}]})==='gauge','expanded performance group keeps the original local performance icon');
console.log(JSON.stringify({pass:true,cases,productModules:['src/radial/model.ts','src/radial/geometry.ts'],hardwareValidated:false}));

check(radialItemIcon(items[6])==='fan','new default fan control uses the local fan icon');
