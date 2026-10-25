import {SharedGameMedia,type GameMediaEntry} from '../src/bridge/sharedGameMedia';
let enabled=false,online=true,current=570,ymcc=570,serial=0,downloads=0,metadata=0;
const titles=new Map([[570,'Dota 2'],[1091500,'Cyberpunk 2077'],[1245620,'ELDEN RING']]);
const records=new Map<number,GameMediaEntry>();const videoUrls=new Map<number,string>();
const staticUrls=new Map<number,string>();
const $=(id:string)=>document.getElementById(id)!;const log=(text:string)=>{const row=document.createElement('li');row.textContent=text;$('events').prepend(row);while($('events').children.length>9)$('events').lastElementChild?.remove();};
async function makeFixtureVideo(){
  const canvas=document.createElement('canvas');canvas.width=960;canvas.height=540;const ctx=canvas.getContext('2d')!;
  const stream=canvas.captureStream(15);const chunks:BlobPart[]=[];const type=MediaRecorder.isTypeSupported('video/webm;codecs=vp8')?'video/webm;codecs=vp8':'video/webm';
  const recorder=new MediaRecorder(stream,{mimeType:type,videoBitsPerSecond:700000});recorder.ondataavailable=event=>{if(event.data.size)chunks.push(event.data);};
  const done=new Promise<string>(resolve=>{recorder.onstop=()=>{stream.getTracks().forEach(track=>track.stop());resolve(URL.createObjectURL(new Blob(chunks,{type:'video/webm'})));};});
  recorder.start();let frame=0;await new Promise<void>(resolve=>{const timer=setInterval(()=>{const t=frame++/30;const background=ctx.createLinearGradient(0,0,960,540);background.addColorStop(0,'#143a59');background.addColorStop(1,'#090d19');ctx.fillStyle=background;ctx.fillRect(0,0,960,540);for(let i=0;i<13;i++){ctx.fillStyle=`rgba(${30+i*5},${125+i*6},220,${0.035+i*0.004})`;ctx.beginPath();ctx.ellipse(480+Math.sin(t*2+i)*390,270+Math.cos(t*2+i)*130,220+i*8,35+i*3,t+i*.2,0,Math.PI*2);ctx.fill();}ctx.fillStyle='#8ab4cf';ctx.font='20px sans-serif';ctx.fillText('共享动态素材 · H5 内存演示',40,485);if(frame>=32){clearInterval(timer);recorder.stop();resolve();}},66);});const url=await done;return {url,poster:canvas.toDataURL('image/png')};
}
const owner=new SharedGameMedia({nativeBackground:false,enabled:()=>enabled,candidates:async target=>{metadata++;if(!online)throw Error('演示：外网断开');return {name:titles.get(target.appId)!,urls:['https://video.steamstatic.com/fixture/'+target.appId+'.webm'],poster:'https://shared.steamstatic.com/fixture/'+target.appId+'.jpg'};},installed:async()=>[],invoke:(async(command:string,args:any={})=>{
  const id=Number(args.appId)||Number(String(args.key).split('-')[1]);let entry=records.get(id);
  if(command==='gameMedia.resolve')return entry?{...entry}:{key:'steam-'+id,bindingRevision:0,status:'missing',ready:false};
  if(command==='gameMedia.begin'){
    if(entry?.taskId&&entry.status==='pending')return {...entry,acquired:false};
    entry=entry||{key:'steam-'+id,appId:id,name:titles.get(id),bindingRevision:1,status:'missing'};
    entry={...entry,status:'pending',taskId:'fixture-'+(++serial),taskSlots:['static','dynamic'].filter(slot=>!(entry as any)[slot+'Ready']),acquired:true};records.set(id,entry);render();return {...entry};
  }
  if(command==='gameMedia.install'){
    downloads++;log('准备 '+titles.get(id)+'：唯一任务 '+args.taskId);render();await new Promise(resolve=>setTimeout(resolve,650));
    if(records.get(id)?.bindingRevision!==args.bindingRevision)throw Error('media-stale-publication');
    const slot=/\.jpg$/.test(args.source)?'static':'dynamic';const asset={id:'fixture-'+id+'-'+serial+'-'+slot,revision:String(serial),kind:slot==='static'?'image':'video',mime:slot==='static'?'image/png':'video/webm',extension:slot==='static'?'png':'webm',bytes:240000} as const;entry={...entry!,status:'ready',ready:true,manual:false,identityVerified:true,bindingRevision:args.bindingRevision+1,url:slot==='static'?staticUrls.get(id):videoUrls.get(id),asset,assets:{...entry?.assets,[slot]:asset},[slot+'Ready']:true};records.set(id,entry);log('发布 '+titles.get(id)+'，YMCC / Steam 共用一个版本');render();return {...entry};
  }
  if(command==='gameMedia.finish'){entry={...records.get(id)!,taskId:'',status:'ready'};records.set(id,entry);return {...entry};}
  if(command==='gameMedia.fail'){entry={...entry!,status:'error',retryAt:Date.now()+60000,error:args.reason,ready:Boolean(entry?.ready)};records.set(id,entry);render();return {...entry};}
  if(command==='gameMedia.snapshot')return {entries:Object.fromEntries([...records.values()].map(value=>[value.key,value]))};
  if(command==='gameMedia.inventory')return [];
  throw Error('unsupported fixture '+command);
}) as any});
function bindVideo(id:string,entry:GameMediaEntry|undefined,show:boolean){const video=$(id) as HTMLVideoElement;const targetId=id==='steam-video'?current:ymcc;const parent=video.parentElement!;const mode=(entry as any)?.wallpaperMode||'dynamic';parent.style.backgroundImage=show&&mode==='static'?'url('+staticUrls.get(targetId)+')':'';parent.style.backgroundSize='cover';
  if(mode==='static'){video.pause();video.style.display='none';return;}
  if(!show||!entry?.ready||!entry.url||entry.deleted){video.pause();video.style.display='none';return;}
  video.style.display='block';if(video.dataset.asset!==entry.asset?.id){video.dataset.asset=entry.asset?.id;video.src=entry.url;}
  video.style.objectFit=entry.playback?.fit||'cover';if(document.visibilityState!=='hidden')void video.play().catch(()=>{});
}
function render(){
  ($('master') as HTMLInputElement).checked=enabled;$('selected-title').textContent=titles.get(current)!;$('ymcc-title').textContent=titles.get(ymcc)!;
  const currentEntry=records.get(current);$('steam-state').textContent=!enabled?'等待 YMCC 开启':currentEntry?.ready?'本地缓存 · 已就绪':currentEntry?.status==='pending'?'后台补齐中 · 保留原背景':currentEntry?.error||'保留原背景';
  bindVideo('steam-video',currentEntry,enabled);bindVideo('ymcc-video',records.get(ymcc),true);
  $('cache-count').textContent=String([...records.values()].filter(e=>e.ready&&!e.deleted).length);$('download-count').textContent=String(downloads);$('metadata-count').textContent=String(metadata);$('queue-count').textContent=String(owner.inspect().queued);
  const list=$('library');list.replaceChildren();for(const [id,title]of titles){const entry=records.get(id);if(!entry)continue;const button=document.createElement('button');button.className='library-row';button.innerHTML='<div class="wallpaper-thumb"><img alt="壁纸预览"></div><span></span><small></small>';button.querySelector('img')!.src=staticUrls.get(id)||'';button.querySelector('span')!.textContent=title;button.querySelector('small')!.textContent=entry.deleted?'已删除 · 自动补齐停用':entry.ready?'需处理 · 静态 / 动态已采集':'需处理 · '+(entry.status==='pending'?'准备中':'待补齐');button.addEventListener('click',()=>openEditor(id));list.appendChild(button);}
  document.querySelectorAll<HTMLButtonElement>('[data-game]').forEach(button=>button.classList.toggle('selected',Number(button.dataset.game)===current));
}
async function ensure(id:number,consumer:'steam'|'ymcc'='steam',priority=true){await owner.ensure({appId:id,name:titles.get(id)!,verified:true},consumer,priority,true);render();}
$('master').addEventListener('change',()=>{enabled=($('master') as HTMLInputElement).checked;owner.reconcilePreparation();log(enabled?'YMCC 开启：当前游戏优先，已知库后台补齐':'YMCC 关闭：Steam 停播，共享缓存保留');render();if(enabled){for(const id of [current,...[...titles.keys()].filter(id=>id!==current)])void ensure(id,'steam',id===current);void ensure(ymcc,'ymcc');}});
for(const button of document.querySelectorAll<HTMLButtonElement>('[data-game]'))button.addEventListener('click',()=>{current=Number(button.dataset.game);owner.selectSteamTarget({appId:current,name:titles.get(current)!,verified:true});render();if(enabled)void ensure(current);});
const demoPreparation=()=>{const game=($('demo-game-running') as HTMLInputElement).checked,saver=($('demo-saver') as HTMLInputElement).checked,disk=($('demo-disk-low') as HTMLInputElement).checked;owner.setPreparationState({backgroundBlocked:game||saver,storageBlocked:disk,reason:disk?'media-disk-space-low':saver?'battery-saver':game?'game-running':''});$('preparation-state').textContent=owner.preparationDescription()||'后台补齐可用';render();};
for(const id of ['demo-game-running','demo-saver','demo-disk-low'])$(id).addEventListener('change',demoPreparation);
owner.subscribe(reason=>{if(reason==='demand'||reason==='policy')render();});
$('network').addEventListener('click',()=>{online=!online;$('network').textContent=online?'模拟断网':'恢复外网';log(online?'外网已恢复':'外网已断开：缓存仍可播放');if(online)for(const entry of records.values())entry.retryAt=0;render();});
$('follow').addEventListener('click',()=>{ymcc=current;void ensure(ymcc,'ymcc');render();});
let editing=570;function openEditor(id:number){editing=id;const entry=records.get(id)!;$('editor-title').textContent=entry.name||titles.get(id)!;const dynamic=(entry as any).wallpaperMode!=='static';($('editor-static') as HTMLImageElement).src=staticUrls.get(id)||'';const preview=$('editor-video') as HTMLVideoElement;preview.src=entry.url||'';preview.style.display=dynamic?'block':'none';$('editor-static').style.display=dynamic?'none':'block';if(dynamic)void preview.play().catch(()=>{});for(const button of document.querySelectorAll<HTMLButtonElement>('[data-wallpaper-mode]'))button.classList.toggle('active',button.dataset.wallpaperMode===(dynamic?'dynamic':'static'));($('trim-start') as HTMLInputElement).value=String(entry.playback?.start||0);($('trim-end') as HTMLInputElement).value=String(entry.playback?.end||0);if(!($('editor') as HTMLDialogElement).open)($('editor') as HTMLDialogElement).showModal();}
$('editor-close').addEventListener('click',()=>{($('editor-video') as HTMLVideoElement).pause();($('editor') as HTMLDialogElement).close();});
for(const button of document.querySelectorAll<HTMLButtonElement>('[data-wallpaper-mode]'))button.addEventListener('click',()=>{const entry=records.get(editing)!;(entry as any).wallpaperMode=button.dataset.wallpaperMode;entry.bindingRevision++;log('游戏卡片壁纸切换为'+(button.dataset.wallpaperMode==='static'?'静态':'动态'));render();openEditor(editing);});
$('save-trim').addEventListener('click',()=>{const entry=records.get(editing)!;const start=Number(($('trim-start') as HTMLInputElement).value),end=Number(($('trim-end') as HTMLInputElement).value);if(start<0||end>0&&end<=start)return;entry.playback={start,end,fit:'cover'};entry.manual=true;entry.bindingRevision++;log('游戏库保存播放片段：两处输出共同更新');render();($('editor') as HTMLDialogElement).close();});
$('redownload').addEventListener('click',()=>{const entry=records.get(editing)!;entry.manual=false;entry.deleted=false;entry.refreshRequested=true;entry.taskId='';entry.bindingRevision++;entry.retryAt=0;($('editor') as HTMLDialogElement).close();void ensure(editing,'ymcc');});
$('remove').addEventListener('click',()=>{const entry=records.get(editing)!;entry.deleted=true;entry.ready=false;entry.manual=true;entry.bindingRevision++;log('游戏库删除：保留删除保护，后台补齐不会恢复');render();($('editor') as HTMLDialogElement).close();});
$('replace').addEventListener('change',event=>{const file=(event.target as HTMLInputElement).files?.[0];if(!file)return;const entry=records.get(editing)!;entry.url=URL.createObjectURL(file);entry.asset={id:'manual-'+(++serial),revision:String(serial),kind:'video',mime:file.type,extension:'mp4',bytes:file.size};entry.ready=true;entry.deleted=false;entry.manual=true;entry.bindingRevision++;log('游戏库替换本地视频：共享版本已更新');render();($('editor') as HTMLDialogElement).close();});
for(const key of ['steam-video','ymcc-video'])$(key).addEventListener('timeupdate',event=>{const video=event.currentTarget as HTMLVideoElement,range=records.get(key==='steam-video'?current:ymcc)?.playback;if(range&&(video.currentTime<range.start||range.end>range.start&&video.currentTime>=range.end))video.currentTime=Math.min(range.start,Math.max(0,video.duration-.1));});
document.addEventListener('visibilitychange',()=>{if(document.visibilityState==='hidden'){for(const id of ['steam-video','ymcc-video'])($(id) as HTMLVideoElement).pause();}else render();});
window.addEventListener('pagehide',()=>{owner.stop();for(const url of videoUrls.values())URL.revokeObjectURL(url);});
const fixtureMedia=await makeFixtureVideo();const fixture=fixtureMedia.url;for(const id of titles.keys()){videoUrls.set(id,fixture);staticUrls.set(id,fixtureMedia.poster);}$('fixture-state').textContent='H5 内存演示 · 不操作真实 Steam、游戏库或下载';
records.set(570,{key:'steam-570',appId:570,name:titles.get(570),status:'ready',ready:true,bindingRevision:1,url:fixture,asset:{id:'fixture-dota',revision:'1',kind:'video',mime:'video/webm',extension:'webm',bytes:240000},staticReady:true,dynamicReady:true,assets:{static:{id:'fixture-dota-static',revision:'1',kind:'image',mime:'image/png',extension:'png',bytes:20000},dynamic:{id:'fixture-dota',revision:'1',kind:'video',mime:'video/webm',extension:'webm',bytes:240000}}});render();
Object.assign(window,{__YMCC_WALLPAPER_PREVIEW__:{owner,records,inspect:()=>({enabled,online,current,ymcc,downloads,metadata,cache:[...records.values()]})}});
