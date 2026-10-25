import {MOTORCYCLE_ROAD_HALF_WIDTH,type MotorcycleState} from './motorcycle';
interface Point{x:number;y:number;scale:number}
/** Original lightweight canvas artwork; no downloaded game, texture or model payload. */
export function renderMotorcycle(ctx:CanvasRenderingContext2D,w:number,h:number,s:MotorcycleState){
 const horizon=h*.30,vanish=w*.5;
 const sky=ctx.createLinearGradient(0,0,0,horizon);sky.addColorStop(0,'#53627d');sky.addColorStop(1,'#e1b19b');ctx.fillStyle=sky;ctx.fillRect(0,0,w,h);
 ctx.fillStyle='#f6d7ac';ctx.beginPath();ctx.arc(w*.76,h*.21,h*.052,0,Math.PI*2);ctx.fill();
 for(let layer=0;layer<2;layer++){ctx.beginPath();ctx.moveTo(0,horizon);for(let i=0;i<=24;i++){const x=i*w/24,y=horizon-12-layer*14-Math.sin(i*.57+layer)*18-Math.cos(i*.91)*9;ctx.lineTo(x,y);}ctx.lineTo(w,horizon+12);ctx.closePath();ctx.fillStyle=layer?'#656d70':'#4e5968';ctx.fill();}
 const project=(x:number,z:number):Point=>{const scale=1/(Math.max(-1,z)+5);return{x:vanish+(x-s.x*.6)*w*.53*scale+Math.sin((s.distance+z)*.002)*Math.min(z,140)*.15,y:horizon+(h-horizon)*5*scale,scale};};
 ctx.fillStyle='#757b5d';ctx.fillRect(0,horizon,w,h-horizon);
 for(let z=280;z>=0;z-=4){const near=project(0,z),far=project(0,z+4),a=project(-MOTORCYCLE_ROAD_HALF_WIDTH,z),b=project(MOTORCYCLE_ROAD_HALF_WIDTH,z),c=project(MOTORCYCLE_ROAD_HALF_WIDTH,z+4),d=project(-MOTORCYCLE_ROAD_HALF_WIDTH,z+4);const band=Math.floor((z+s.distance)/10)%2;
  poly(ctx,[a,b,c,d],band?'#363d48':'#39424b');
  for(const side of [-1,1]){const p=project(side*5.25,z),q=project(side*5.65,z),r=project(side*5.65,z+4),t=project(side*5.25,z+4);poly(ctx,[p,q,r,t],band?'#ded4be':'#b95d50');}
  if(Math.floor((z+s.distance)/6)%3<2)for(const lane of [-1.72,1.72]){const p=project(lane-.04,z),q=project(lane+.04,z),r=project(lane+.04,z+4),t=project(lane-.04,z+4);poly(ctx,[p,q,r,t],'#d8d1af');}
  if(far.y>h)continue;
 }
 // Roadside tree, lamp, sign and town silhouettes move past with depth.
 for(let i=22;i>=0;i--){const z=((i*18-s.distance%18)+400)%400+8,side=i%2?1:-1,p=project(side*(7+i%3),z),size=Math.min(h*.5,p.scale*w*.8);ctx.fillStyle='#4b5146';ctx.fillRect(p.x-size*.05,p.y-size*.8,size*.1,size*.8);poly(ctx,[{x:p.x-size*.34,y:p.y-size*.45},{x:p.x,y:p.y-size*1.5},{x:p.x+size*.34,y:p.y-size*.45}],'#364f48');if(i%5===0){ctx.fillStyle='#68717b';ctx.fillRect(p.x-size*.55,p.y-size*1.1,size*1.1,size*.36);ctx.fillStyle='#ead9bb';ctx.font=`600 ${Math.max(5,size*.16)}px sans-serif`;ctx.textAlign='center';ctx.fillText(i%2?'NORTH  07':'COAST  21',p.x,p.y-size*.86);}}
 const rivals=s.opponents.filter(r=>r.distance-s.distance>1&&r.distance-s.distance<260).sort((a,b)=>b.distance-a.distance);
 for(const r of rivals){const p=project(r.x,r.distance-s.distance);drawBike(ctx,p.x,p.y,Math.min(1.2,p.scale*w/85),Math.sin(s.time*1.2+r.id)*.07,r.color,r.health===0?1.3:r.stagger>0?.22:0,0,1);}
 const playerX=w*.5+s.x*w*.009,playerY=h*.79,unit=Math.min(w/500,h/470);ctx.fillStyle='#10131b66';ctx.beginPath();ctx.ellipse(playerX,playerY+unit*41,unit*22,unit*9,0,0,Math.PI*2);ctx.fill();
 drawBike(ctx,playerX,playerY,unit,s.lean,'#e8b74f',0,s.attackPose,s.attackSide);
 if(s.speed>145){ctx.strokeStyle='#e5ebf21c';ctx.lineWidth=1;for(let i=0;i<16;i++){const t=(s.time*1.4+i*.19)%1,x=i%2?w*(.02+i*.009):w*(.98-i*.009),y=horizon+(h-horizon)*t;ctx.beginPath();ctx.moveTo(x,y);ctx.lineTo(x+(i%2?-1:1)*15,y+17+t*16);ctx.stroke();}}
 if(s.hitFlash>0){ctx.fillStyle=`rgba(205,65,42,${s.hitFlash*.6})`;ctx.fillRect(0,0,w,h);}
 ctx.textAlign='left';
}
function poly(ctx:CanvasRenderingContext2D,p:{x:number;y:number}[],color:string){ctx.fillStyle=color;ctx.beginPath();p.forEach((v,i)=>i?ctx.lineTo(v.x,v.y):ctx.moveTo(v.x,v.y));ctx.closePath();ctx.fill();}
function drawBike(ctx:CanvasRenderingContext2D,x:number,y:number,scale:number,lean:number,color:string,fallen:number,attack:number,side:number){
 ctx.save();ctx.translate(x,y);ctx.scale(scale,scale);ctx.rotate(lean+fallen);
 ctx.fillStyle='#18202b';ctx.beginPath();ctx.ellipse(0,24,9,25,0,0,Math.PI*2);ctx.fill();ctx.strokeStyle='#8c98a2';ctx.lineWidth=2;ctx.stroke();ctx.fillStyle='#1f2831';ctx.fillRect(-6,-34,12,24);
 poly(ctx,[{x:-11,y:7},{x:11,y:7},{x:9,y:-28},{x:-9,y:-28}],color);ctx.fillStyle='#ee5544';ctx.fillRect(-7,10,14,5);
 ctx.strokeStyle='#1e2632';ctx.lineWidth=9;ctx.lineCap='round';ctx.beginPath();ctx.moveTo(-6,-9);ctx.lineTo(-15,7);ctx.moveTo(6,-9);ctx.lineTo(15,7);ctx.stroke();
 ctx.fillStyle='#42536a';ctx.beginPath();ctx.ellipse(0,-22,14,21,0,0,Math.PI*2);ctx.fill();ctx.fillStyle='#c5aa78';ctx.fillRect(-8,-30,16,9);
 ctx.strokeStyle='#343f53';ctx.lineWidth=8;ctx.beginPath();ctx.moveTo(-10,-30);ctx.lineTo(-20,-15);ctx.moveTo(10,-30);ctx.lineTo(20,-15);ctx.stroke();
 if(attack>0){const reach=Math.sin((1-attack)*Math.PI);ctx.strokeStyle='#56647b';ctx.lineWidth=8;ctx.beginPath();ctx.moveTo(side*11,-29);ctx.lineTo(side*(23+reach*27),-22-reach*8);ctx.stroke();ctx.fillStyle='#d4ae8c';ctx.beginPath();ctx.arc(side*(25+reach*27),-22-reach*8,5,0,Math.PI*2);ctx.fill();}
 ctx.fillStyle=color;ctx.beginPath();ctx.ellipse(0,-47,13,15,0,0,Math.PI*2);ctx.fill();ctx.fillStyle='#171f2e';ctx.fillRect(-11,-51,22,6);ctx.strokeStyle='#73849a';ctx.lineWidth=3;ctx.beginPath();ctx.moveTo(-20,-15);ctx.lineTo(20,-15);ctx.stroke();ctx.restore();
}
