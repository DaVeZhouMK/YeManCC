import type { RadialItem } from './model';
// gap is a fixed distance in logical pixels, shared by both sides of a seam.
export const WHEEL = { size:496, center:248, inner:66, outer:149, childInner:158, childOuter:232, gap:4, enter:160, exit:140 } as const;
export interface Sector { id:string; item:RadialItem; index:number; parent:number|null; start:number; end:number; inner:number; outer:number; gap:number; path:string; polygon:[number,number][]; x:number; y:number }
const TAU=Math.PI*2;
export const angleDelta=(a:number,b:number)=>((a-b+Math.PI)%TAU+TAU)%TAU-Math.PI;
const point=(r:number,a:number):[number,number]=>[WHEEL.center+r*Math.cos(a),WHEEL.center+r*Math.sin(a)];
export function sector(item:RadialItem,index:number,parent:number|null,start:number,end:number,inner:number,outer:number,gap:number=WHEEL.gap):Sector {
  const span=end-start,halfGap=gap/2;
  if(![start,end,inner,outer,gap].every(Number.isFinite)||span<=0||span>=Math.PI||inner<=0||outer<=inner||gap<0||halfGap>=inner*Math.sin(span/2))throw new Error('Invalid radial sector');
  // Intersections of the two circles with lines offset from the nominal rays.
  // Angular padding would widen towards the outside and create triangular gaps.
  const outerInset=Math.asin(halfGap/outer),innerInset=Math.asin(halfGap/inner);
  const outerStart=start+outerInset,outerEnd=end-outerInset,innerStart=start+innerInset,innerEnd=end-innerInset;
  const a=point(outer,outerStart),b=point(outer,outerEnd),c=point(inner,innerEnd),d=point(inner,innerStart);
  const path=`M ${a} A ${outer},${outer} 0 0 1 ${b} L ${c} A ${inner},${inner} 0 0 0 ${d} Z`;
  const steps=Math.max(8,Math.ceil(span*24)),polygon:[number,number][]=[];
  for(let i=0;i<=steps;i++)polygon.push(point(outer,outerStart+(outerEnd-outerStart)*i/steps));
  for(let i=steps;i>=0;i--)polygon.push(point(inner,innerStart+(innerEnd-innerStart)*i/steps));
  const [x,y]=point((inner+outer)/2,(start+end)/2);
  return {id:item.id,item,index,parent,start,end,inner,outer,gap,path,polygon,x,y};
}
export function primarySectors(items:RadialItem[]):Sector[] {
  const step=TAU/items.length;
  return items.map((item,i)=>sector(item,i,null,i*step-step/2,i*step+step/2,WHEEL.inner,WHEEL.outer));
}
/** Children extend their parent; rendering, hit tests and native regions share
 * the same fixed-width cuts, with no new surface over the central hole. */
export function childSectors(parent:Sector):Sector[] {
  const children=parent.item.children??[],span=Math.max(1.36,Math.min(2.7,children.length*.45)),start=(parent.start+parent.end-span)/2,step=span/Math.max(1,children.length);
  return children.map((item,i)=>sector(item,i,parent.index,start+i*step,start+(i+1)*step,WHEEL.childInner,WHEEL.childOuter));
}
export function contains(s:Sector,x:number,y:number):boolean {
  const dx=x-WHEEL.center,dy=y-WHEEL.center,r=Math.hypot(dx,dy),halfSpan=(s.end-s.start)/2;
  const delta=angleDelta(Math.atan2(dy,dx),(s.start+s.end)/2),halfGap=s.gap/2;
  return r>=s.inner&&r<=s.outer&&Math.abs(delta)<=halfSpan&&
    r*Math.sin(halfSpan+delta)>=halfGap-1e-9&&r*Math.sin(halfSpan-delta)>=halfGap-1e-9;
}
export function hit(sectors:Sector[],x:number,y:number):Sector|null {return sectors.find(s=>s.item.enabled&&s.item.action!=='none'&&contains(s,x,y))??null;}
/** Locked parent only after an actual enabled child hit; no angular fallback. */
export class RadialSelection {
  parent:number|null=null; leaf:string|null=null; locked=false; enteredChildren=false;
  reset(){this.parent=null;this.leaf=null;this.locked=false;this.enteredChildren=false;}
  move(items:RadialItem[],x:number,y:number):string|null {
    const r=Math.hypot(x-WHEEL.center,y-WHEEL.center),primaries=primarySectors(items);
    if(r<WHEEL.inner){this.reset();return null;}
    if(this.parent!==null){
      const parent=primaries[this.parent];
      if(r<WHEEL.exit){this.locked=false;this.enteredChildren=false;}
      if(parent?.item.action==='submenu'&&r>=WHEEL.enter){
        this.enteredChildren=true;const child=hit(childSectors(parent),x,y);
        if(child)this.locked=true;this.leaf=child?.id??null;return this.leaf;
      }
      if(this.locked&&r>=WHEEL.exit){this.leaf=null;return null;}
    }
    const target=hit(primaries,x,y);
    // Keep the candidate while traversing the radial gap. Child fans are wider
    // than their parent: a continuous diagonal slide may leave the parent arc
    // before reaching childInner. This is not a lock and cannot execute parent.
    if(!target&&this.parent!==null&&primaries[this.parent]?.item.action==='submenu'&&r>=WHEEL.exit){this.leaf=null;return null;}
    this.parent=target?.index??null;this.leaf=target?.id??null;return this.leaf;
  }
  /** Stick coordinates from the existing admitted native frame. LY is up. */
  stick(items:RadialItem[],lx:number,ly:number,child=false):string|null {
    const m=Math.hypot(lx,ly);if(m<.26){this.leaf=null;return null;}
    const r=child?(WHEEL.childInner+WHEEL.childOuter)/2:(WHEEL.inner+WHEEL.outer)/2;
    return this.move(items,WHEEL.center+lx/m*r,WHEEL.center-ly/m*r);
  }
}
export function dialValue(x:number,y:number,centerAngle:number,min:number,max:number):number|null {
  const dx=x-WHEEL.center,dy=y-WHEEL.center,r=Math.hypot(dx,dy);
  if(r<WHEEL.childInner||r>WHEEL.childOuter||max<min)return null;
  const d=angleDelta(Math.atan2(dy,dx),centerAngle),halfGap=WHEEL.gap/2;
  if(Math.abs(d)>1.3||r*Math.sin(1.3-Math.abs(d))<halfGap-1e-9)return null;
  const middle=(WHEEL.childInner+WHEEL.childOuter)/2,halfSpan=1.3-Math.asin(halfGap/middle);
  const ratio=Math.max(0,Math.min(1,(d+halfSpan)/(2*halfSpan)));
  return Math.round(min+ratio*(max-min));
}

/** Top bubble is 4:3; its track shares the real renderer/native coordinates. */
export const RADIAL_SLIDER={left:128,top:0,width:240,height:180,start:154,end:342,trackY:126} as const;
export function sliderValue(x:number,y:number,min:number,max:number,dragging=false):number|null{
  if(![x,y,min,max].every(Number.isFinite)||max<min)return null;
  if(!dragging&&(x<142||x>354||y<104||y>150))return null;
  return min+Math.max(0,Math.min(1,(x-RADIAL_SLIDER.start)/(RADIAL_SLIDER.end-RADIAL_SLIDER.start)))*(max-min);
}
