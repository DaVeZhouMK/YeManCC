export interface AdjacentGame {appId:string;name:string;}
const valid=(raw:unknown)=>typeof raw==='string'||typeof raw==='number'&&Number.isSafeInteger(raw)?/^\d{1,20}$/.test(String(raw))&&BigInt(String(raw))>0n&&BigInt(String(raw))<=18446744073709551615n:false;
function cardSelection(root:Element,boundary:Element):AdjacentGame|null{
 const queue:Element[]=[root],found=new Map<string,AdjacentGame>();let visited=0;
 while(queue.length&&visited++<24){const node:any=queue.shift()!;const explicit=node.getAttribute?.('data-appid');
  if(valid(explicit))found.set(String(explicit),{appId:String(explicit),name:''});
  const key=Object.keys(node).find(key=>key.startsWith('__reactFiber$'));
  for(let fiber=key?node[key]:null,n=0;fiber&&n<12;fiber=fiber.return,n++){
   if(fiber.stateNode===boundary)break;const props=fiber.memoizedProps;if(!props||typeof props!=='object')continue;
   const app=props.overview||props.appOverview||props.app;const raw=app?.appid??props.appid??props.appId;
   if(valid(raw)){found.set(String(raw),{appId:String(raw),name:typeof app?.display_name==='string'?app.display_name:''});break;}
  }
  if(found.size>1)return null;
  for(const child of Array.from(node.children||[]) as Element[])if(queue.length<24)queue.push(child);
 }
 return found.size===1?[...found.values()][0]:null;
}
export function adjacentWallpaperGames(element:Element|null,current:string,previous=''):AdjacentGame[]{
 if(!element||!element.isConnected||!valid(current))return [];
 const route=element.closest('[data-ymcc-wallpaper-route="/library/home"]');if(!route)return [];
 for(let parent=element.parentElement,depth=0;parent&&parent!==route&&depth<8;parent=parent.parentElement,depth++){
  const children=Array.from(parent.children);if(children.length<2||children.length>64)continue;
  const cards=children.map(node=>({node,selection:cardSelection(node,parent!),rect:node.getBoundingClientRect()})).filter(card=>card.selection&&card.rect.width>0&&card.rect.height>0);
  const selected=cards.find(card=>card.selection!.appId===current&&card.node.contains(element));if(!selected)continue;
  const center=selected.rect.top+selected.rect.height/2;
  const row=cards.filter(card=>Math.abs(card.rect.top+card.rect.height/2-center)<=Math.max(8,Math.min(card.rect.height,selected.rect.height)*.4)).sort((a,b)=>a.rect.left-b.rect.left);
  if(row.length<2)continue;const index=row.indexOf(selected),old=row.findIndex(card=>card.selection!.appId===previous);
  const direction=old>=0&&old!==index?Math.sign(index-old):0;const offsets=direction?[direction,2*direction,-direction,-2*direction]:[1,-1,2,-2];
  const seen=new Set([current]),result:AdjacentGame[]=[];
  for(const offset of offsets){const item=row[index+offset]?.selection;if(item&&!seen.has(item.appId)){seen.add(item.appId);result.push(item);}}return result;
 }
 return [];
}
