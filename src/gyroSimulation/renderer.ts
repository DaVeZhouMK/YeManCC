import {targetX,targetPose,type RangeState} from './model';
import type {Range3DRenderer} from './range3d';
type V3={x:number;y:number;z:number};
const TAU=Math.PI*2;
let lensCanvas:HTMLCanvasElement|null=null;
function polygon(ctx:CanvasRenderingContext2D,points:{x:number;y:number}[],fill:string,stroke?:string){
 if(!points.length)return;ctx.beginPath();points.forEach((p,i)=>i?ctx.lineTo(p.x,p.y):ctx.moveTo(p.x,p.y));ctx.closePath();ctx.fillStyle=fill;ctx.fill();if(stroke){ctx.strokeStyle=stroke;ctx.stroke();}
}
function roundRect(ctx:CanvasRenderingContext2D,x:number,y:number,w:number,h:number,r:number,fill:string){ctx.beginPath();ctx.roundRect(x,y,w,h,r);ctx.fillStyle=fill;ctx.fill();}
function vignette(ctx:CanvasRenderingContext2D,w:number,h:number){const g=ctx.createRadialGradient(w*.5,h*.45,h*.1,w*.5,h*.5,Math.max(w,h)*.7);g.addColorStop(0,'#060b1000');g.addColorStop(1,'#030911a0');ctx.fillStyle=g;ctx.fillRect(0,0,w,h);}
export function renderRange(ctx:CanvasRenderingContext2D,w:number,h:number,state:RangeState,lensPass=false,weapon?:Range3DRenderer){
 const f=w*(lensPass?1.72:.86),cp=Math.cos(state.pitch-state.kick),sp=Math.sin(state.pitch-state.kick),cy=Math.cos(state.yaw+state.kickYaw),sy=Math.sin(state.yaw+state.kickYaw);
 const camera=(p:V3)=>{const px=p.x-state.playerX,pz=p.z-state.playerZ,x=px*cy-pz*sy,z=px*sy+pz*cy,y=p.y-1.62;return {x,y:y*cp+z*sp,d:z*cp-y*sp};};
 const screen=(p:{x:number;y:number;d:number})=>({x:w/2+p.x*f/Math.max(.08,p.d),y:h*.47-p.y*f/Math.max(.08,p.d),d:p.d});
 const project=(p:V3)=>screen(camera(p));
 const poly=(p:V3[],fill:string,stroke?:string)=>{
  const points=p.map(camera),clipped:typeof points=[];
  for(let i=0;i<points.length;i++){const a=points[i],b=points[(i+1)%points.length],inside=a.d>=.3,next=b.d>=.3;
   if(inside)clipped.push(a);if(inside!==next){const t=(.3-a.d)/(b.d-a.d);clipped.push({x:a.x+(b.x-a.x)*t,y:a.y+(b.y-a.y)*t,d:.3});}}
  if(clipped.length>2)polygon(ctx,clipped.map(screen),fill,stroke);
 };
 const line=(a:V3,b:V3,color:string,width=1)=>{
  let aa=camera(a),bb=camera(b);if(aa.d<.3&&bb.d<.3)return;
  if(aa.d<.3||bb.d<.3){const t=(.3-aa.d)/(bb.d-aa.d),edge={x:aa.x+(bb.x-aa.x)*t,y:aa.y+(bb.y-aa.y)*t,d:.3};if(aa.d<.3)aa=edge;else bb=edge;}
  const pa=screen(aa),pb=screen(bb);ctx.strokeStyle=color;ctx.lineWidth=width;ctx.beginPath();ctx.moveTo(pa.x,pa.y);ctx.lineTo(pb.x,pb.y);ctx.stroke();
 };
 const box=(x:number,y:number,z:number,sx:number,sz:number,sy:number,color:string)=>{
  poly([{x,y,z},{x:x+sx,y,z},{x:x+sx,y:y+sy,z},{x,y:y+sy,z}],color,'#0b151526');
  poly([{x:x+sx,y,z},{x:x+sx,y,z:z+sz},{x:x+sx,y:y+sy,z:z+sz},{x:x+sx,y:y+sy,z}],'#697478');
  poly([{x,y:y+sy,z},{x:x+sx,y:y+sy,z},{x:x+sx,y:y+sy,z:z+sz},{x,y:y+sy,z:z+sz}],'#a4a592');
 };
 ctx.fillStyle='#3a4240';ctx.fillRect(0,0,w,h);
 // Concrete indoor range, acoustic ceiling and suspended target rails.
 poly([{x:-11,y:0,z:-5},{x:11,y:0,z:-5},{x:11,y:0,z:70},{x:-11,y:0,z:70}],'#737970');
 poly([{x:-11,y:5.6,z:-5},{x:11,y:5.6,z:-5},{x:11,y:5.6,z:70},{x:-11,y:5.6,z:70}],'#424c4a');
 poly([{x:-11,y:0,z:-5},{x:-11,y:0,z:70},{x:-11,y:5.6,z:70},{x:-11,y:5.6,z:-5}],'#777f74');
 poly([{x:11,y:0,z:-5},{x:11,y:0,z:70},{x:11,y:5.6,z:70},{x:11,y:5.6,z:-5}],'#636f69');
 box(-11,0,66,22,1,5.6,'#3d4945');box(-11,0,-5,22,.8,5.6,'#616e66');
 for(let z=0;z<66;z+=6){
  line({x:-11,y:0,z},{x:11,y:0,z},'#8c94845c');
  line({x:-11,y:5.4,z},{x:11,y:5.4,z},'#26322f',2);
  for(const x of [-11,11]){line({x,y:0,z},{x,y:5.5,z},'#404f49',2);line({x,y:2.3,z},{x,y:2.3,z:z+6},'#c8bf8066',2);}
  for(const x of [-7,0,7]){
   poly([{x:x-.08,y:5.35,z:z+1},{x:x+.08,y:5.35,z:z+1},{x:x+.08,y:5.35,z:z+3},{x:x-.08,y:5.35,z:z+3}],'#e0e6d5');
   line({x,y:5.32,z:z+1},{x,y:5.32,z:z+3},'#f3f6d8',2.3);
  }
 }
 for(let x=-9;x<=9;x+=3){line({x,y:0,z:-2},{x,y:0,z:66},'#b0b39b66');line({x,y:5.39,z:-2},{x,y:5.39,z:66},'#2e3c36',1.4);}
 for(const z of [10,25,50]){line({x:-10,y:.02,z},{x:10,y:.02,z},'#c6c29a',2);const p=project({x:-8.5,y:.1,z});if(p.d>1){ctx.fillStyle='#e1dcc0';ctx.font='600 10px sans-serif';ctx.fillText(`${z} m`,p.x,p.y);}}
 // Ceiling targets travel on cross rails, not along the floor silhouettes.
 for(const z of [22,38]){line({x:-9,y:4.65,z},{x:9,y:4.65,z},'#a0aca4',4);line({x:-9,y:4.8,z},{x:9,y:4.8,z},'#263b34',2);}
 for(let x=-8;x<=8;x+=4)box(x-1.3,0,65,2.6,.2,4,'#263931');
 box(-9,0,7,1.8,.8,1.3,'#7d846d');box(7.5,0,11,1.8,.8,1.3,'#646e5c');
 box(-7.6,0,18,1.5,.9,.8,'#8b8871');box(7.1,0,30,1.9,1.2,.8,'#747b66');
 for(const t of [...state.targets].sort((a,b)=>b.z-a.z)){
  const x=targetX(t,state.time),pose=targetPose(t,state.time),pivot=t.y-.64;
  const point=(px:number,py:number):V3=>t.rail?{x:x+px,y:t.y+py+pose.lift,z:t.z}:
   {x:x+px,y:pivot+(py+.64)*Math.cos(pose.fold),z:t.z+(py+.64)*Math.sin(pose.fold)};
  line({x,y:t.rail?4.7:0,z:t.z},{x,y:t.rail?Math.min(4.7,t.y+.56+pose.lift):pivot,z:t.z},t.rail?'#aab6a6':'#414b46',t.rail?2:3);
  if(t.rail)box(x-.23,4.68,t.z-.12,.46,.24,.2,'#4f6058');
  const face=(points:{x:number;y:number}[],fill:string,stroke?:string)=>{
   // The overhead carriage masks the plate as it retracts into its cassette.
   let vertices=points.map(p=>point(p.x,p.y));
   if(t.rail){const clipped:V3[]=[];for(let i=0;i<vertices.length;i++){const a=vertices[i],b=vertices[(i+1)%vertices.length],inside=a.y<4.7,next=b.y<4.7;if(inside)clipped.push(a);if(inside!==next){const v=(4.7-a.y)/(b.y-a.y);clipped.push({x:a.x+(b.x-a.x)*v,y:4.7,z:a.z+(b.z-a.z)*v});}}vertices=clipped;}
   if(vertices.length>2){ctx.save();ctx.lineWidth=.7;poly(vertices,fill,stroke);ctx.restore();}
  };
  if(t.kind==='plate'){
   for(const [radius,color] of [[.4,'#e9e7d8'],[.28,'#a9633f'],[.18,'#e9e7d8'],[.07,'#a9633f']] as const)
    face(Array.from({length:24},(_,i)=>({x:Math.cos(i*TAU/24)*radius,y:Math.sin(i*TAU/24)*radius})),color);
  }else{
   face([{x:-.38,y:-.63},{x:.38,y:-.63},{x:.42,y:.22},{x:.21,y:.34},{x:.16,y:.56},{x:-.16,y:.56},{x:-.21,y:.34},{x:-.42,y:.22}],'#d9a061','#6c533c');
   face([{x:-.18,y:-.22},{x:.18,y:-.22},{x:.18,y:.2},{x:-.18,y:.2}],'#9f774c','#4e4942');
   face(Array.from({length:18},(_,i)=>({x:Math.cos(i*TAU/18)*.075,y:Math.sin(i*TAU/18)*.075})),'#473e33');
  }
 }
 for(const impact of state.impacts){const p=project(impact);ctx.fillStyle=`rgba(245,217,159,${Math.min(1,impact.life*3)})`;ctx.beginPath();ctx.arc(p.x,p.y,impact.life<.3?4:2,0,TAU);ctx.fill();}
 // Soft side bounce keeps the existing room readable without washing out targets.
 const glow=ctx.createLinearGradient(0,0,w,0);glow.addColorStop(0,'#f3d2a81c');glow.addColorStop(.24,'#f3d2a800');glow.addColorStop(.76,'#c4e5ff00');glow.addColorStop(1,'#c4e5ff20');ctx.fillStyle=glow;ctx.fillRect(0,0,w,h);
 if(lensPass)return;
 let lens:CanvasRenderingContext2D|null=null;
 const scopeX=w/2,scopeY=h*.47,radius=Math.min(w,h)*.32,dpr=ctx.getTransform().a;
 if(state.ads>.01){
  lensCanvas??=document.createElement('canvas');const width=Math.round(w*dpr),height=Math.round(h*dpr);
  if(lensCanvas.width!==width||lensCanvas.height!==height){lensCanvas.width=width;lensCanvas.height=height;}
  lens=lensCanvas.getContext('2d');if(lens){lens.setTransform(dpr,0,0,dpr,0,0);renderRange(lens,w,h,state,true);}

 }
 // A perspective model: receiver near the eye, barrel extending into depth.
 // The muzzle and optic converge forward instead of a flat rifle pointing up the screen.
 if(weapon)weapon.render(ctx,w,h,state);else drawRifle(ctx,w,h,state);
 if(state.ads<.15){ctx.strokeStyle='#eff7e9';ctx.lineWidth=1;const cx=w/2,cy=h*.47,gap=5+state.recoil*250;ctx.beginPath();ctx.moveTo(cx-14-gap,cy);ctx.lineTo(cx-gap,cy);ctx.moveTo(cx+gap,cy);ctx.lineTo(cx+14+gap,cy);ctx.moveTo(cx,cy-14-gap);ctx.lineTo(cx,cy-gap);ctx.moveTo(cx,cy+gap);ctx.lineTo(cx,cy+14+gap);ctx.stroke();}
 if(lens&&lensCanvas){
  ctx.save();ctx.globalAlpha=state.ads;ctx.fillStyle='#03101424';ctx.fillRect(0,0,w,h);
  // Generous field of view. Only a thin scope rim and subtle off-axis shading.
  const r=radius*(.6+state.ads*.4),parallax=Math.min(5,state.recoil*75);
  ctx.fillStyle='#111c20';ctx.beginPath();ctx.arc(scopeX,scopeY,r+8,0,TAU);ctx.fill();
  ctx.strokeStyle='#7c8f87';ctx.lineWidth=2;ctx.beginPath();ctx.arc(scopeX,scopeY,r+6,0,TAU);ctx.stroke();
  ctx.save();ctx.beginPath();ctx.arc(scopeX,scopeY,r,0,TAU);ctx.clip();
  ctx.drawImage(lensCanvas,(scopeX-r)*dpr,(scopeY-r)*dpr,r*2*dpr,r*2*dpr,scopeX-r+parallax*.35,scopeY-r+parallax,r*2,r*2);
  weapon?.renderScopeFlash(ctx,w,h,state);
  const edge=ctx.createRadialGradient(scopeX-parallax,scopeY+parallax,r*.7,scopeX,scopeY,r);edge.addColorStop(0,'#02141700');edge.addColorStop(.83,'#03121a08');edge.addColorStop(1,'#020c1642');ctx.fillStyle=edge;ctx.fillRect(scopeX-r,scopeY-r,r*2,r*2);
  ctx.strokeStyle='#9d2929';ctx.lineWidth=1.1;ctx.beginPath();ctx.arc(scopeX,scopeY,r*.17,0,TAU);ctx.moveTo(scopeX-r*.38,scopeY);ctx.lineTo(scopeX-r*.06,scopeY);ctx.moveTo(scopeX+r*.06,scopeY);ctx.lineTo(scopeX+r*.38,scopeY);ctx.moveTo(scopeX,scopeY+r*.06);ctx.lineTo(scopeX,scopeY+r*.6);ctx.stroke();
  for(let i=1;i<5;i++){ctx.beginPath();ctx.moveTo(scopeX-i*2.8,scopeY+r*(.18+i*.08));ctx.lineTo(scopeX+i*2.8,scopeY+r*(.18+i*.08));ctx.stroke();}
  ctx.fillStyle='#ee5145';ctx.beginPath();ctx.arc(scopeX,scopeY,1.6,0,TAU);ctx.fill();ctx.restore();
  ctx.fillStyle='#e2e9dd';ctx.font='600 10px sans-serif';ctx.textAlign='center';ctx.fillText('2×',scopeX,scopeY+r+21);ctx.textAlign='left';ctx.restore();
 }
 if(state.hitFlash>0){ctx.strokeStyle='#fff2cb';ctx.lineWidth=2;const cx=w/2,cy=h*.47;ctx.beginPath();for(const dx of [-1,1])for(const dy of [-1,1]){ctx.moveTo(cx+dx*7,cy+dy*7);ctx.lineTo(cx+dx*13,cy+dy*13);}ctx.stroke();}
 vignette(ctx,w,h);
}
export function weaponPoint(state:RangeState,w:number,h:number,point:V3):{x:number;y:number;depth:number} {
 const aim=state.ads,lower=state.reload>0?Math.sin(Math.PI*(1-state.reload/1.35)):0;
 const x=point.x+.18*(1-aim)*(1-point.z/24);
 const y=point.y-.23*(1-aim)*(1-point.z/24)-.09*aim-.19*lower+state.recoil*.5;
 const depth=Math.max(.12,point.z-state.recoil*.35),f=w*.83;
 return {x:w/2+x*f/depth,y:h*.47-y*f/depth,depth};
}
function drawRifle(ctx:CanvasRenderingContext2D,w:number,h:number,state:RangeState){
 const faces:{points:V3[];color:string;depth:number}[]=[];
 const face=(points:V3[],color:string)=>faces.push({points,color,depth:points.reduce((n,p)=>n+p.z,0)/points.length});
 const box=(x:number,y:number,z:number,sx:number,sy:number,sz:number,top:string,side:string,front='#1d2828')=>{
  const a={x,y,z},b={x:x+sx,y,z},c={x:x+sx,y:y+sy,z},d={x,y:y+sy,z},aa={x,y,z:z+sz},bb={x:x+sx,y,z:z+sz},cc={x:x+sx,y:y+sy,z:z+sz},dd={x,y:y+sy,z:z+sz};
  face([a,b,c,d],front);face([aa,bb,cc,dd],front);face([a,aa,dd,d],side);face([b,bb,cc,c],side);face([d,c,cc,dd],top);face([a,b,bb,aa],side);
 };
 const tube=(x:number,y:number,z:number,length:number,radius:number,color:string,end:string)=>{
  const ring=(zz:number)=>Array.from({length:16},(_,i)=>({x:x+Math.cos(i*TAU/16)*radius,y:y+Math.sin(i*TAU/16)*radius,z:zz}));
  const back=ring(z),front=ring(z+length);
  face(front,end);for(let i=0;i<16;i++)face([back[i],back[(i+1)%16],front[(i+1)%16],front[i]],i<8?color:'#1e2828');face(back,end);
 };
 // Two gloved hands support the receiver and angled foregrip.
 box(.015,-.13,.3,.105,.1,.19,'#8d8b70','#706f58','#71715c');
 box(-.085,-.09,.64,.11,.075,.17,'#96927b','#77755f','#77745d');
 box(-.025,-.16,.59,.045,.16,.055,'#303b35','#202b28');
 // M4 receiver, magazine well, handguard and upper rail.
 box(-.035,-.055,.31,.07,.085,.31,'#515d58','#303d3a');
 box(-.021,-.17,.36,.042,.13,.09,'#374741','#273630');
 box(-.03,-.03,.62,.06,.075,.39,'#56635c','#35413c');
 for(let z=.64;z<1.01;z+=.035){box(-.035,.047,z,.07,.012,.015,'#738075','#3d4d43');box(.031,-.012,z,.006,.021,.017,'#101c1b','#111d1c');}
 tube(0,0,1.01,.2,.012,'#68766c','#303b37');
 tube(0,0,1.21,.19,.024,'#414e48','#192724');
 // Side-mounted laser, rail clamp and scope mounts.
 box(.035,.008,.8,.034,.034,.13,'#5c6c59','#354834');
 box(-.025,.046,.39,.05,.032,.05,'#6a786e','#3b4943');box(-.025,.046,.58,.05,.032,.05,'#6a786e','#3b4943');
 tube(0,.096,.35,.34,.036,'#69776b','#1d3035');
 tube(0,.096,.34,.028,.043,'#46564b','#0b1c24');
 tube(0,.096,.342,.002,.034,'#5a807c','#32646a');
 box(-.013,.13,.48,.026,.023,.035,'#7a8679','#4d5e50');box(.032,.08,.49,.022,.026,.04,'#6b7866','#485b47');
 for(const item of faces.sort((a,b)=>b.depth-a.depth))polygon(ctx,item.points.map(p=>weaponPoint(state,w,h,p)),item.color,'#101b1b44');
 const muzzle=weaponPoint(state,w,h,{x:0,y:0,z:1.41});
 if(state.flash>0){
  ctx.strokeStyle='#ffefbf91';ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(muzzle.x,muzzle.y);ctx.lineTo(w/2,h*.47);ctx.stroke();
  ctx.save();ctx.translate(muzzle.x,muzzle.y);ctx.fillStyle='#ffc65f';ctx.beginPath();for(let i=0;i<12;i++){const angle=i*TAU/12,r=i%2?7:24;ctx.lineTo(Math.cos(angle)*r,Math.sin(angle)*r);}ctx.closePath();ctx.fill();ctx.fillStyle='#fff7cf';ctx.beginPath();ctx.arc(0,0,6,0,TAU);ctx.fill();ctx.restore();
 }
}
