// Bounded display geometry only. No collection, timers, input, or settings.
export interface NoticeSize {width:number;height:number;}
export interface NoticeRect extends NoticeSize {x:number;y:number;}
  const finite = (value: unknown): value is number => typeof value === 'number' && Number.isFinite(value);
  export const overlapsNotice = (a: NoticeRect, b: NoticeRect, gap = 0) => a.x < b.x + b.width + gap && a.x + a.width + gap > b.x && a.y < b.y + b.height + gap && a.y + a.height + gap > b.y;
  export function placeNotice(bounds: NoticeSize, size: NoticeSize, occupied: NoticeRect[], preferred: {x:number;y:number}, margin = 16, gap = 12): NoticeRect | null {
    if (![bounds.width,bounds.height,size.width,size.height,preferred.x,preferred.y,margin,gap].every(finite) || margin < 0 || gap < 0 || size.width <= 0 || size.height <= 0) return null;
    const maxX = bounds.width - margin - size.width, maxY = bounds.height - margin - size.height;
    if (maxX < margin || maxY < margin) return null;
    const blocks = occupied.slice(0,8).filter(r => [r.x,r.y,r.width,r.height].every(finite) && r.width > 0 && r.height > 0);
    const clamp = (v:number,lo:number,hi:number) => Math.min(hi,Math.max(lo,v));
    const xs = [preferred.x,margin,maxX,(bounds.width-size.width)/2], ys = [preferred.y,margin,maxY,(bounds.height-size.height)/2];
    for (const r of blocks) {xs.push(r.x-size.width-gap,r.x+r.width+gap);ys.push(r.y-size.height-gap,r.y+r.height+gap);}
    const unique = (values:number[],max:number) => [...new Set(values.map(v => Math.round(clamp(v,margin,max))))];
    let best: NoticeRect | null = null, score = Infinity;
    for (const y of unique(ys,maxY)) for (const x of unique(xs,maxX)) {
      const rect = {x,y,width:size.width,height:size.height};
      if (blocks.some(r => overlapsNotice(rect,r,gap))) continue;
      const next = (x-preferred.x)**2 + (y-preferred.y)**2;
      if (next < score) {best=rect;score=next;}
    }
    return best;
  }
