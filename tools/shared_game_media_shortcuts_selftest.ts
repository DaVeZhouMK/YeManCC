import {strict as assert} from 'node:assert';
import {SharedGameMedia,wallpaperSourcePriority,reconcileMediaAliases} from '../src/bridge/sharedGameMedia';
import {downloadDescription,downloadInterruptionReason} from '../src/bridge/gameMediaDownloads';
let checks=0;const check=(x:unknown,label:string)=>{checks++;assert.ok(x,label);};
const shortcut={appId:100,name:'added game',executable:'G:/Games/Added.exe',verified:true,catalogSource:'steam-shortcut',steamAccount:'17',shortcutAppId:'2415919104',sourceTier:2};
const generic={appId:100,name:'generic alias',executable:'G:/Games/Other.exe',verified:true,catalogSource:'ymcc-confirmed',sourceTier:2};
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
check(downloadInterruptionReason('media-shortcut-no-longer-current').includes('账号'),'account/shortcut change has visible retry reason');
const aliases=new Map<string,string>(),notices:Array<[string,string]>=[];const emit=(previous:string,key:string)=>notices.push([previous,key]);reconcileMediaAliases(aliases,{'exe-old':'steam-100'},emit);for(let n=0;n<100;n++)reconcileMediaAliases(aliases,{'exe-old':'steam-100'},emit);check(notices.length===1&&notices[0][0]==='exe-old','cached identity notifies its previous selected key once, without repeated progress work');
console.log(JSON.stringify({ok:true,checks,scope:'production inventory coordinator, source precedence, de-duplication and status; deterministic IPC, no live writes'}));
