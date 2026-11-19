import {strict as assert} from 'node:assert';
import {SharedGameMedia,wallpaperSourcePriority,reconcileMediaAliases} from '../src/bridge/sharedGameMedia';
import {downloadDescription,downloadInterruptionReason} from '../src/bridge/gameMediaDownloads';
let checks=0;const check=(x:unknown,label:string)=>{checks++;assert.ok(x,label);};
const shortcut={appId:100,name:'added game',executable:'G:/Games/Added.exe',verified:true,catalogSource:'steam-shortcut',steamAccount:'17',shortcutAppId:'2415919104',sourceTier:2};
const generic={appId:100,name:'generic alias',executable:'G:/Games/Other.exe',verified:true,catalogSource:'ymcc-confirmed',sourceTier:2};
for(const scenario of [
 {name:'library-error',installed:[generic],library:new Error('library read refused'),full:true,manual:true,send:true,ack:false,ids:[100]},
 {name:'steam-error',installed:new Error('installed read refused'),library:[shortcut],full:true,manual:true,send:true,ack:false,ids:[100]},
 {name:'both-errors',installed:new Error('installed read refused'),library:new Error('library read refused'),full:true,manual:true,send:false,ack:false,ids:[]},
 {name:'invalid-reply',installed:[generic],library:{ok:false},full:true,manual:true,send:true,ack:false,ids:[100]},
 {name:'successful-empty',installed:[],library:[],full:true,manual:true,send:true,ack:true,ids:[]},
 {name:'inventory-only',installed:[generic],library:[shortcut],full:false,manual:true,send:true,ack:false,ids:[100]},
 {name:'automatic-scan',installed:[],library:[],full:true,manual:false,send:true,ack:false,ids:[]},
]){
 const reads={installed:0,library:0};const sent:Array<{targets:Array<{appId:number}>;inventoryRequestId?:string}>=[];
 const owner=new SharedGameMedia({workAllowed:()=>true,backgroundEnabled:()=>true,installed:(async()=>{reads.installed++;if(scenario.installed instanceof Error)throw scenario.installed;return scenario.installed;}) as any,
   invoke:(async(command:string,args:any)=>{if(command==='gameMedia.inventory'){reads.library++;if(scenario.library instanceof Error)throw scenario.library;return scenario.library;}if(command==='gameMedia.background'){sent.push(args);return {};}throw Error(command);}) as any});
 try{
  await owner.supplement(scenario.full,scenario.manual,'manual-42');
  check(sent[0]?.inventoryRequestId===(scenario.ack?'manual-42':undefined),scenario.name+': only verified full manual inventory may acknowledge');
  check(sent.length===(scenario.send?1:0),scenario.name+': no pointless IPC or lost successful data');
  check(JSON.stringify((sent[0]?.targets||[]).map(x=>x.appId))===JSON.stringify(scenario.ids),scenario.name+': preserve successful targets');
  check(reads.installed===Number(scenario.full)&&reads.library===1,scenario.name+': keep existing bounded reads');
 }finally{owner.stop();}
}
for(const reverse of [false,true])for(const native of [false,true]){
 let reads=0;const calls:Array<{command:string,args:any}>=[];const library=[shortcut,generic,{...shortcut,appId:0,name:'unknown',executable:'G:/Games/Unknown.exe',verified:false,shortcutAppId:'2415919105'}];if(reverse)library.reverse();
 const owner=new SharedGameMedia({backgroundEnabled:()=>true,installed:async()=>{reads++;return native?[{appId:100,name:'native',gameDirectory:'G:/Steam/Common/Game',verified:true}]:[];},invoke:(async(command:string,args:any)=>{calls.push({command,args});if(command==='gameMedia.inventory')return library;if(command==='gameMedia.snapshot')return {entries:{'steam-100':{appId:100,identityVerified:true,name:'cached name',executable:'G:/Games/Stale.exe'}}};if(command==='gameMedia.background')return {};throw Error('renderer tried identity/network '+command);}) as any});
 await owner.supplement();const targets=calls.find(x=>x.command==='gameMedia.background')!.args.targets;
 const known=targets.find((x:any)=>x.appId===100),unknown=targets.find((x:any)=>x.appId===0);
 check(targets.length===2,'same store identity de-duplicates while unknown file remains eligible');
 check(known.catalogSource===(native?'steam-native':'steam-shortcut'),'preferred provenance survives library/stored merging');
 check(known.executable===(native?undefined:shortcut.executable),'lower priority alias cannot inject/replace preferred EXE');
 check(known.steamAccount===(native?undefined:'17'),'current-account evidence follows only preferred shortcut');
 check(unknown.catalogSource==='steam-shortcut'&&unknown.shortcutAppId==='2415919105'&&unknown.steamAccount==='17','unidentified shortcut reaches native queue intact');
 check(!calls.some(c=>c.command==='gameMedia.identify'),'renderer never owns slow automatic identification');
 await owner.supplement(false);check(reads===1,'inventory event does not rescan native manifests');owner.stop();
}
const ordered=[{sourceTier:0},{sourceTier:1},shortcut,generic,{sourceTier:3}].map(wallpaperSourcePriority);check(ordered.join(',')==='0,1,2,3,4','old persisted tiers support new shortcut priority');
check(downloadDescription({phase:'identifying',current:{name:'candidate'},total:5}).includes('正在识别'),'status distinguishes identification from completed downloads');
check(downloadInterruptionReason('media-identity-timeout').includes('超时'),'identity timeout has visible retry reason');
check(downloadInterruptionReason('media-identity-network-unavailable').includes('身份查询网络'),'transient identity network failure is not labeled as a rejected identity');
check(downloadDescription({enabled:true,total:2,completed:1,remaining:1,nextDueAt:Date.now()+20000,pendingReason:'media-identity-network-unavailable'}).includes('等待自动重试'),'identity network backoff exposes its recovery reason');
check(downloadInterruptionReason('media-identity-helper-unavailable').includes('组件'),'missing resolver has a distinct visible reason');
check(downloadInterruptionReason('media-identity-target-unavailable').includes('游戏路径'),'missing EXE has a distinct visible reason');
check(downloadInterruptionReason('media-identity-worker-unavailable').includes('或'),'legacy ambiguity is retained rather than blamed on only one file');
check(downloadInterruptionReason('media-shortcut-no-longer-current').includes('账号'),'account/shortcut change has visible retry reason');
const aliases=new Map<string,string>(),notices:Array<[string,string]>=[];const emit=(previous:string,key:string)=>notices.push([previous,key]);reconcileMediaAliases(aliases,{'exe-old':'steam-100'},emit);for(let n=0;n<100;n++)reconcileMediaAliases(aliases,{'exe-old':'steam-100'},emit);check(notices.length===1&&notices[0][0]==='exe-old','cached identity notifies its previous selected key once, without repeated progress work');
console.log(JSON.stringify({ok:true,checks,scope:'production inventory coordinator, source precedence, de-duplication and status; deterministic IPC, no live writes'}));
