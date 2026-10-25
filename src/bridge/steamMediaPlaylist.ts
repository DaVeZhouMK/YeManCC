// Expand a bounded, finished fMP4 HLS asset into the shared owner's download plan.
// Playback never depends on a remote playlist or segment URL.
export function parseHlsAsset(text:string,base:string,preferredHeight=720):{variant?:string;segments?:string[]} {
  if(text.length>1024*1024||!text.trimStart().startsWith('#EXTM3U'))throw Error('媒体播放列表无效');
  const lines=text.split(/\r?\n/).map(line=>line.trim()).filter(Boolean);
  const variants:Array<{url:string;height:number;bandwidth:number}>=[];
  for(let i=0;i<lines.length;i++)if(lines[i].startsWith('#EXT-X-STREAM-INF:')){
    const next=lines[i+1];if(!next||next.startsWith('#'))continue;
    const height=Number(/RESOLUTION=\d+x(\d+)/i.exec(lines[i])?.[1]||0);
    const bandwidth=Number(/(?:^|,)BANDWIDTH=(\d+)/i.exec(lines[i].slice(18))?.[1]||0);
    variants.push({url:new URL(next,base).href,height,bandwidth});
  }
  if(variants.length){const rank=(v:typeof variants[number])=>{const group=v.height===preferredHeight?0:preferredHeight>=1080&&v.height===720?1:preferredHeight>=720&&v.height===480?2:v.height>0&&v.height<preferredHeight?3:v.height>0?4:5;return [group,group===3?-v.height:v.height,-v.bandwidth];};
    variants.sort((a,b)=>{const x=rank(a),y=rank(b);return x[0]-y[0]||x[1]-y[1]||x[2]-y[2];});
    return {variant:variants[0].url};}
  if(!lines.includes('#EXT-X-ENDLIST')||lines.some(line=>line.startsWith('#EXT-X-KEY:')&&!/METHOD=NONE(?:,|$)/.test(line))||lines.some(line=>line.startsWith('#EXT-X-BYTERANGE:')))throw Error('仅支持完整未加密的离线媒体');
  const map=lines.find(line=>line.startsWith('#EXT-X-MAP:'));
  const init=map&&/URI="([^"]+)"/.exec(map)?.[1];if(!init||/BYTERANGE=/.test(map!))throw Error('此媒体需要先转换为可缓存成品');
  const fragments=lines.filter(line=>!line.startsWith('#'));
  if(!fragments.length||fragments.length>600)throw Error('媒体分片数量无效');
  return {segments:[init,...fragments].map(part=>new URL(part,base).href)};
}
