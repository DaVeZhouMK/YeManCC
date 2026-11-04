import { reactive } from 'vue';
import './mock-shell';
import { mkdtempSync, rmSync, readFileSync } from 'fs';
import { tmpdir } from 'os';
import { join } from 'path';
import { automaticCpuControls } from '../src/bridge/performanceControls';
import { installFixtureClock } from './ymcc_sim/global/tdp-schedule-consumer/fixture-clock';

const YEMAN = '1cb8b882-a900-4b9f-9bac-99d151e64441';
const CPU_MAX_STATE = 'bc5038f7-23e0-4960-96da-33abaf5935ec';
const CPU_FREQ0 = '75b0ae3f-bce0-45a7-8c89-c9611c25e100';
const CPU_BOOST = 'be337238-0d82-4146-a960-4f3749d470c7';
const CPU_SCHED0 = '36687f9e-e3a5-4dbf-b1dc-15eb381c6863';
const CPU_MIN0 = '893dee8e-2bef-41e0-89c6-b55d0929964c';
const CPU_THROTTLE = '3b04d4fd-1cc7-4f23-ab1c-d1337819c4bb';
const unsupportedLog = [
  '2026-08-11 12:51:07 ensure_pawnio enter',
  '2026-08-11 12:51:07 pawnio_present=True,no action needed',
  '2026-08-11 12:51:07 [family] Desktop-17h(Zen~Zen2) power_feature=pbo_ppt_tdc_edc transport=unsupported verified=False',
  '2026-08-11 12:51:07 [family] Desktop-17h(Zen~Zen2) TDP capability 未验证 -> 拒绝写入',
  'rc=6',
].join('\n');

function assert(condition: unknown, message: string): asserts condition {
  if (!condition) throw new Error(message);
}

/** Standalone persistence cases stay in the original TDP consumer and use
 * only this process's mock IPC plus its already-owned temporary root. */
async function runGameCustomPersistenceStorm(
  schedule: typeof import('../src/bridge/performanceSchedule'),
  settings: typeof import('../src/bridge/settingsRepository'), dir: string,
): Promise<void> {
  const previousResponder = (globalThis as any).__mockIpcResponder;
  const files = new Map<string, string>();
  const calls: Array<{cmd:string;args:any}> = [];
  const canonical = (p: string) => p.replace(/\\/g, '/').toLowerCase();
  const expectedPath = canonical(dir + '/game-custom.json');
  const main = 'main', backup = 'backup';
  const fileKey = (p: string) => canonical(p).endsWith('game-custom.json') ? main
    : canonical(p).endsWith('game-custom.json.bak') ? backup : canonical(p);
  let readError = false, existsError = false, writeFailure = false, archiveFailure = false;
  let falseSuccess = false;
  let readHook: ((p:string)=>Promise<void>) | null = null;
  let writeHook: ((p:string,content:string)=>Promise<void>) | null = null;
  let backupReadError=false,mainReplaceFailure=false;
  const otherCalls:string[]=[];
  const isGameFile = (p: unknown) => typeof p === 'string' && /game-custom\.json/i.test(p);
  (globalThis as any).__mockIpcResponder = (cmd:string,args:any) => {
    if (!isGameFile(args?.path)){otherCalls.push(cmd);return previousResponder(cmd,args);}
    return (async()=>{
    calls.push({cmd,args}); const key=fileKey(args.path);
    if (cmd==='fs.exists') { if(existsError)throw Error('game custom metadata denied');return files.has(key); }
    if (cmd==='fs.readTextFile' || cmd==='fs.readTextRange') {
      if(readHook)await readHook(args.path);
      if(readError || (backupReadError&&key===backup))throw Error('game custom read denied');
      if(!files.has(key))throw Error('missing game custom fixture');
      const raw=files.get(key)!;if(cmd==='fs.readTextRange'){const size=new TextEncoder().encode(raw).byteLength;return {content:size>args.maxBytes?'':raw,offset:0,size};}return raw;
    }
    if(cmd==='fs.writeTextFileAtomic') {
      if(writeHook)await writeHook(args.path,args.content);
      if(writeFailure || (mainReplaceFailure&&key===main) || (archiveFailure && key!==main && key!==backup))return false;
      if(!falseSuccess)files.set(key,args.content);return true;
    }
    throw Error('Forbidden standalone operation '+cmd);
    })();
  };
  const empty=()=>({version:1,entries:{},rtss:{}});
  let passed=0;
  async function check(name:string,run:()=>Promise<void>):Promise<void>{await run();passed++;console.log('GameCustom PASS '+name);}
  const reset=(raw?:string,bak?:string)=>{files.clear();if(raw!==undefined)files.set(main,raw);if(bak!==undefined)files.set(backup,bak);calls.length=0;readError=existsError=writeFailure=archiveFailure=false;falseSuccess=false;readHook=null;writeHook=null;backupReadError=mainReplaceFailure=false;otherCalls.length=0;};
  const noEntries=(value:any)=>Object.keys(value.entries).length===0&&Object.keys(value.rtss).length===0;
  // Compare structure including undefined, independent of JSON property insertion order.
  const signature=(value:unknown)=>JSON.stringify(value,(_field,v)=>v===undefined?'__fixture_optional_undefined__':v&&typeof v==='object'&&!Array.isArray(v)?Object.fromEntries(Object.entries(v).sort(([a],[b])=>a<b?-1:a>b?1:0)):v);
  const key='c:\\games\\fixture.exe';
  const profile=schedule.defaultPerformanceScheduleConfig().profiles.ac.performance;
  const legal:any={version:1,entries:{[key]:{displayName:'kept',enabled:true,ac:{...profile,futureAc:{keep:true}},dc:profile,padPersona:'follow',gyroOverride:'follow',futureEntry:{keep:true}}},rtss:{[key]:{enabled:true,acFps:120,dcFps:60,futureRtss:{keep:true}}},futureDocument:{keep:['opaque']}};
  const controls=schedule.continuousControlsFromSchedule(schedule.defaultPerformanceScheduleConfig());
  legal.entries[key].controls={...controls,futureControls:{keep:true},ac:{...controls.ac,futureBank:{keep:true}}};
  legal.entries[key].inputOverrides={outputTarget:{persona:'disabled',futureTarget:{keep:true}},futureOverride:{keep:true}};
  let notifications=0;
  const off=schedule.onGameCustomConfigChanged(()=>notifications++);
  try {
    await check('read refusal returns empty runtime and preserves both originals',async()=>{
      reset(JSON.stringify(legal),JSON.stringify(legal));readError=true;
      const loaded=await schedule.loadGameCustomConfig();assert(noEntries(loaded),'read error must return product empty defaults instead of throwing/stale active data');
      assert(files.get(main)===JSON.stringify(legal)&&files.get(backup)===JSON.stringify(legal),'read refusal changed originals');
      let failed=false;try{await schedule.saveGameCustomConfig(empty() as any)}catch{failed=true}assert(failed,'unreadable save falsely succeeded');
      assert(calls.every(c=>c.cmd!=='fs.writeTextFileAtomic'),'read failure attempted write');
      readError=false;assert((await schedule.loadGameCustomConfig()).entries[key]?.displayName==='kept','explicit read after refusal did not heal');
    });

    await check('healthy BOM config consumes legal values and retains root/entry/side/RTSS unknown fields',async()=>{
      const raw='\uFEFF'+JSON.stringify(legal);reset(raw);
      const loaded=await schedule.loadGameCustomConfig();
      assert(schedule.getCustomProfileFor(loaded,key,'ac')?.tdpMax===profile.tdpMax,'legal dedicated profile lost');
      assert((loaded as any).futureDocument.keep[0]==='opaque'&&(loaded.entries[key].ac as any).futureAc.keep,'unknown fields lost on read');
      assert(schedule.getCustomRtssFor(loaded,key)?.acFps===120,'RTSS consumer lost legal FPS');
      assert(files.get(main)===raw&&calls.every(c=>c.cmd!=='fs.writeTextFileAtomic'),'healthy read rewrote file');
      assert(calls.every(c=>canonical(c.args.path).startsWith(expectedPath)),'standalone ignored native settings directory');
      const edit=structuredClone(loaded);edit.entries[key].displayName='edited';
      await schedule.saveGameCustomConfig(edit);
      const reread=await schedule.loadGameCustomConfig();
      assert(reread.entries[key].displayName==='edited'&&(reread as any).futureDocument.keep[0]==='opaque','edit/read lost root unknown');
      assert((reread.entries[key] as any).futureEntry.keep&&(reread.rtss[key] as any).futureRtss.keep,'edit/read lost nested unknown');
      assert((reread.entries[key].controls as any).futureControls.keep&&(reread.entries[key].controls!.ac as any).futureBank.keep&&(reread.entries[key].inputOverrides as any).futureOverride.keep,'edit/read lost control/input unknown fields');
      assert(files.get(backup)===raw,'previous legal document backup not exact bytes');
    });
    // The normalizer emits optional undefined fields. Pass that in-memory result
    // back through the real save boundary, which normalizes it a second time.
    await check('normalizing an already normalized optional profile and RTSS record is idempotent',async()=>{
      const optional=structuredClone(legal);delete optional.entries[key].controls;delete optional.entries[key].inputOverrides;
      reset(JSON.stringify(optional));const once=await schedule.loadGameCustomConfig();
      assert(Object.prototype.hasOwnProperty.call(once.entries[key],'controls')&&once.entries[key].controls===undefined,'fixture did not consume normalizer-produced undefined controls');
      assert(Object.prototype.hasOwnProperty.call(once.entries[key],'inputOverrides')&&once.entries[key].inputOverrides===undefined,'fixture did not consume normalizer-produced undefined input overrides');
      const initial=signature(once);
      await schedule.saveGameCustomConfig(once);const twice=schedule.getGameCustomConfig();
      assert(twice.entries[key].enabled===true&&twice.rtss[key].enabled===true,'second normalization disabled valid optional profile/RTSS data');
      assert(signature(twice)===initial,'normalize(normalize(x)) changed optional profile, RTSS, or unknown fields');
      assert(signature(await schedule.loadGameCustomConfig())===initial,'save/read broke normalized optional field idempotence');
      const optionalRtss=schedule.getGameCustomConfig();
      for(const field of ['acCeiling','dcCeiling','acLastFps','dcLastFps'] as const)optionalRtss.rtss[key][field]=undefined;
      await schedule.saveGameCustomConfig(optionalRtss);
      assert(signature(schedule.getGameCustomConfig())===initial,'undefined optional RTSS numbers disabled or changed valid defaults');
      assert(signature(await schedule.loadGameCustomConfig())===initial,'undefined optional RTSS numbers changed after save/read');
    });
    await check('editing another entry preserves enabled other.exe through save and reread consumers',async()=>{
      const other='c:\\games\\other.exe',document=structuredClone(legal);
      document.entries[other]={...structuredClone(legal.entries[key]),displayName:'other'};
      delete document.entries[other].controls;delete document.entries[other].inputOverrides;
      document.rtss[other]=structuredClone(legal.rtss[key]);reset(JSON.stringify(document));
      const edit=await schedule.loadGameCustomConfig();const before=signature(edit.entries[other]);
      edit.entries[key].displayName='edited different entry';edit.entries[key].ac.tdpMax=profile.tdpMax+1;
      await schedule.saveGameCustomConfig(edit);const saved=JSON.parse(files.get(main)!);
      assert(saved.entries[other].enabled===true&&saved.rtss[other].enabled===true,'saving another entry disabled other.exe in actual persisted JSON');
      const reread=await schedule.loadGameCustomConfig();
      assert(reread.entries[key].displayName==='edited different entry','edited entry did not persist');
      assert(reread.entries[other].enabled===true&&signature(reread.entries[other])===before,'other.exe changed during sibling save/read');
      assert(schedule.getCustomProfileFor(reread,other,'ac')?.tdpMax===profile.tdpMax,'other.exe disappeared from performance consumer');
      assert(schedule.getCustomRtssFor(reread,other)?.acFps===120,'other.exe disappeared from RTSS consumer');
      assert(!otherCalls.some(cmd=>/^(registry\.write|tdpDaemon\.|shell\.|process\.|input\.)/.test(cmd)),'configuration save activated hardware/process IPC');
    });
    await check('eight explicit malformed optional controls/input values remain disabled after save and reread',async()=>{
      for(const field of ['controls','inputOverrides'])for(const value of [null,'invalid',7,[]]){
        const document=structuredClone(legal);document.entries.bad=structuredClone(legal.entries[key]);document.entries.bad[field]=value;
        reset(JSON.stringify(document));const loaded=await schedule.loadGameCustomConfig();
        assert(loaded.entries.bad.enabled===false&&schedule.getCustomProfileFor(loaded,'bad','ac')===null,field+' explicit '+JSON.stringify(value)+' activated malformed entry');
        assert(loaded.entries[key].enabled===true,'malformed optional sibling disabled legal fixture');
        await schedule.saveGameCustomConfig(loaded);const reread=await schedule.loadGameCustomConfig();
        assert(reread.entries.bad.enabled===false&&schedule.getCustomProfileFor(reread,'bad','ac')===null,'save/read re-enabled explicit malformed '+field);
        assert(reread.entries[key].enabled===true&&(reread.entries[key] as any).futureEntry.keep,'malformed optional save lost legal sibling/unknown fields');
      }
    });
    await check('root corruption and wrong map shapes return empty product defaults; valid edit archives both exact originals',async()=>{
      for(const raw of ['', '[]','null','0','false','"text"','{"version":1,"entries":', '{"version":1,"entries":[]}', '{"version":1,"entries":null}', '{"version":1,"entries":{},"rtss":[]}', '{"entries":{"bad":NaN}}']){
        reset(raw,'');const before=notifications;
        const loaded=await schedule.loadGameCustomConfig();assert(noEntries(loaded),'corrupt document created dedicated performance data');
        assert(schedule.getCustomProfileFor(loaded,key,'ac')===null,'corrupt runtime consumer produced a profile');
        assert(files.get(main)===raw&&files.get(backup)==='','load changed corrupt originals');assert(notifications===before,'corrupt read notified activation');
        const edit=structuredClone(legal);await schedule.saveGameCustomConfig(edit);
        const archived=[...files].filter(([k])=>k!==main&&k!==backup).map(([,v])=>v);
        assert(archived.length===2&&archived.includes(raw)&&archived.includes(''),'exact main/empty backup archives missing');
        assert((await schedule.loadGameCustomConfig()).entries[key]?.displayName==='kept','corrupt edit did not persist');
      }
    });
    await check('compatible backup recovers without legacy resurrection and survives broken main archive',async()=>{
      reset('{main-bad',JSON.stringify(legal));const loaded=await schedule.loadGameCustomConfig();
      assert(loaded.entries[key]?.displayName==='kept','healthy backup not consumed');
      await schedule.saveGameCustomConfig(loaded);assert(files.get(backup)===JSON.stringify(legal),'corrupt main replaced healthy backup');
      assert([...files].some(([k,v])=>k!==main&&k!==backup&&v==='{main-bad'),'broken main not archived');
      reset(undefined,JSON.stringify(legal));assert((await schedule.loadGameCustomConfig()).entries[key]?.displayName==='kept','missing main ignored existing backup');
    });
    await check('future main/backup/schema objects retain originals and reject ordinary save',async()=>{
      for(const [a,b] of [[{...legal,version:2},legal],[legal,{...legal,version:2}],[{...legal,schemaVersion:2},legal],['{bad',{...legal,schemaVersion:99}]]){
        const raw=typeof a==='string'?a:JSON.stringify(a),bak=JSON.stringify(b);reset(raw,bak);
        assert(noEntries(await schedule.loadGameCustomConfig()),'future document leaked active data');
        assert(schedule.getGameCustomRecoveryState().reason==='unsupported','future document not marked protected');
        let failed=false;try{await schedule.saveGameCustomConfig(legal)}catch{failed=true}assert(failed,'future ordinary write accepted');
        assert(files.get(main)===raw&&files.get(backup)===bak&&calls.every(c=>c.cmd!=='fs.writeTextFileAtomic'),'future originals changed');
      }
    });
    await check('wrong entry scalars cannot become enabled data; numeric strings/NaN use owned defaults',async()=>{
      const bad=structuredClone(legal);bad.entries.null=null;bad.entries.array=[];bad.entries.number=3;
      bad.entries[key].enabled='false';bad.entries[key].displayName=12;bad.entries[key].ac.tdpMax='22';bad.entries[key].ac.fpsTarget=null;
      bad.rtss[key].enabled='true';bad.rtss[key].acFps='120';bad.rtss[key].dcFps=null;
      reset(JSON.stringify(bad));const loaded=await schedule.loadGameCustomConfig();
      assert(Object.keys(loaded.entries).length===1&&!loaded.entries[key].enabled,'invalid entry shapes created enabled profiles');
      assert(loaded.entries[key].ac.tdpMax===profile.tdpMax&&loaded.entries[key].ac.fpsTarget===profile.fpsTarget,'wrong profile numbers coerced');
      assert(schedule.getCustomProfileFor(loaded,key,'ac')===null&&!loaded.rtss[key].enabled&&loaded.rtss[key].acFps===0,'wrong scalar activated dedicated consumer');
      await schedule.saveGameCustomConfig(loaded);assert(!(await schedule.loadGameCustomConfig()).entries[key].enabled,'wrong scalar repair not durable');
      const nonfinite=structuredClone(legal);nonfinite.entries[key].ac.tdpMax=NaN;nonfinite.entries[key].dc.fpsTarget=Infinity;
      await schedule.saveGameCustomConfig(nonfinite);const fixed=await schedule.loadGameCustomConfig();
      assert(fixed.entries[key].ac.tdpMax===profile.tdpMax&&fixed.entries[key].dc.fpsTarget===profile.fpsTarget,'nonfinite saved instead of defaults');
    });
    await check('wrong owned fields cannot activate repaired fallback while a separate legal entry remains intact',async()=>{
      const bad=structuredClone(legal);bad.entries.bad=structuredClone(bad.entries[key]);bad.entries.bad.ac.tdpMax='37';
      bad.rtss.bad={enabled:true,acFps:'120',dcFps:60};reset(JSON.stringify(bad));const loaded=await schedule.loadGameCustomConfig();
      assert(loaded.entries[key].enabled&&schedule.getCustomProfileFor(loaded,key,'ac')?.tdpMax===profile.tdpMax,'legal sibling entry lost');
      assert(loaded.entries.bad.enabled&&loaded.entries.bad.ac.tdpValid===false&&!loaded.rtss.bad.enabled,'TDP-only faults must retain ownership while carrying invalid data; RTSS remains independently disabled');
      assert(schedule.continuousControlsForEntry(loaded.entries.bad,schedule.defaultPerformanceScheduleConfig()).ac.tdp===legal.entries[key].controls.ac.tdp,'invalid legacy field replaced the intact continuous target');
      const legacy=structuredClone(loaded.entries.bad);delete legacy.controls;
      assert(schedule.continuousControlsForEntry(legacy,schedule.defaultPerformanceScheduleConfig()).ac.tdpValid===false,'legacy numeric fallback was incorrectly admitted as a hardware target');
    });
    await check('oversized main/backup is a read fault and cannot archive or replace unobserved bytes',async()=>{
      for(const [raw,bak] of [[' '.repeat(4*1024*1024+1),JSON.stringify(legal)],[JSON.stringify(legal),' '.repeat(4*1024*1024+1)]]){
        reset(raw,bak);assert(noEntries(await schedule.loadGameCustomConfig()),'oversize leaked stale active data');
        let failed=false;try{await schedule.saveGameCustomConfig(legal)}catch{failed=true}assert(failed,'oversize save accepted');
        assert(files.get(main)===raw&&files.get(backup)===bak&&calls.every(c=>c.cmd!=='fs.writeTextFileAtomic'),'oversize bytes overwritten');
      }
    });
    await check('metadata/backup read rejection stays fault, never missing initialization, and explicit retry heals',async()=>{
      reset(JSON.stringify(legal),JSON.stringify(legal));existsError=true;
      assert(noEntries(await schedule.loadGameCustomConfig()),'metadata rejection propagated or reused active data');
      existsError=false;backupReadError=true;
      assert(noEntries(await schedule.loadGameCustomConfig()),'backup rejection was ignored');
      let failed=false;try{await schedule.saveGameCustomConfig(legal)}catch{failed=true}assert(failed,'unreadable backup saved');
      assert(calls.every(c=>c.cmd!=='fs.writeTextFileAtomic'),'metadata/read failure wrote files');
      backupReadError=false;await schedule.saveGameCustomConfig(legal);assert((await schedule.loadGameCustomConfig()).entries[key]?.displayName==='kept','retry after IO refusal failed');
    });
    await check('deletion while read rejects cannot become absent migration',async()=>{
      reset(JSON.stringify(legal));readHook=async()=>{files.delete(main);throw Error('deleted while read denied')};
      assert(noEntries(await schedule.loadGameCustomConfig()),'read/delete race threw');
      assert(calls.every(c=>c.cmd!=='fs.writeTextFileAtomic'),'read/delete race initialized as missing');
    });
    await check('archive failure preserves originals and suppresses success notification; later valid edit heals',async()=>{
      reset('{main-bad','{backup-bad');archiveFailure=true;const before=notifications;
      let failed=false;try{await schedule.saveGameCustomConfig(legal)}catch{failed=true}assert(failed,'archive failure reported save success');
      assert(files.get(main)==='{main-bad'&&files.get(backup)==='{backup-bad'&&notifications===before,'failed archive changed source or notified');
      assert(noEntries(await schedule.loadGameCustomConfig()),'archive failure prevents default loading');
      archiveFailure=false;await schedule.saveGameCustomConfig(legal);assert(notifications===before+1,'successful recovery save not confirmed exactly once');
      assert((await schedule.loadGameCustomConfig()).entries[key]?.displayName==='kept','archive failure poisoned queue');
    });
    await check('atomic replacement/false success failures leave cache unchanged and queue accepts retry',async()=>{
      reset(JSON.stringify(legal));await schedule.loadGameCustomConfig();const edit=structuredClone(legal);edit.entries[key].displayName='new';
      const before=notifications;mainReplaceFailure=true;
      let failed=false;try{await schedule.saveGameCustomConfig(edit)}catch{failed=true}assert(failed,'replace failure reported saved');
      assert(schedule.getGameCustomConfig().entries[key].displayName==='kept'&&notifications===before,'failed replace advanced cache');
      mainReplaceFailure=false;falseSuccess=true;failed=false;
      try{await schedule.saveGameCustomConfig(edit)}catch{failed=true}assert(failed,'unchanged readback falsely acknowledged save');
      falseSuccess=false;await schedule.saveGameCustomConfig(edit);assert((await schedule.loadGameCustomConfig()).entries[key]?.displayName==='new','replace retry failed');
    });
    await check('new dedicated save without RTSS keeps the existing global AC/DC default selection',async()=>{
      reset(JSON.stringify(empty()));const edit=structuredClone(legal);delete edit.rtss;
      await schedule.saveGameCustomConfig(edit);const loaded=await schedule.loadGameCustomConfig();
      assert(loaded.rtss[key].acFps===120&&loaded.rtss[key].dcFps===60,'standalone persistence changed dedicated frame default policy');
    });
    await check('oversized submitted edit rejects without overwriting legal main or backup',async()=>{
      reset(JSON.stringify(legal),JSON.stringify(legal));const edit=structuredClone(legal);edit.futureOversize='中'.repeat(2*1024*1024);
      let failed=false;try{await schedule.saveGameCustomConfig(edit)}catch{failed=true}assert(failed,'oversized submitted edit saved');
      assert(files.get(main)===JSON.stringify(legal)&&files.get(backup)===JSON.stringify(legal),'oversized edit changed originals');
      assert(calls.every(c=>c.cmd!=='fs.writeTextFileAtomic'),'oversized submitted edit began preservation');
    });
    await check('retained unknown fields survive while entry/RTSS/inputRestore deletions remain durable',async()=>{
      reset(JSON.stringify(legal));const edit=await schedule.loadGameCustomConfig();delete edit.entries[key];delete edit.rtss[key];
      await schedule.saveGameCustomConfig(edit);const loaded=await schedule.loadGameCustomConfig();
      assert(noEntries(loaded)&&(loaded as any).futureDocument.keep[0]==='opaque','deletion resurrected key or erased root unknown');
    });
    await check('legacy restore clear uses shared preservation and does not restore deleted input state',async()=>{
      const restore={persona:'elite' as const,buttonMappingEnabled:true,gyroEnabled:false,motionEnabled:false};
      reset(JSON.stringify({...legal,inputRestore:{...restore,unknownRestore:true}}));const loaded=await schedule.loadGameCustomConfig();
      await schedule.clearGameInputRestoreSnapshot(loaded.inputRestore!);
      assert(!(await schedule.loadGameCustomConfig()).inputRestore,'cleared legacy restore resurrected');
      assert(JSON.parse(files.get(backup)!).inputRestore.unknownRestore,'restore clear did not preserve original');
    });
    await check('root and map prototype-shaped unknown keys stay data without polluting records',async()=>{
      const raw=JSON.stringify(legal).replace('"version":1','"__proto__":{"mustRemainData":true},"version":1');reset(raw);
      const loaded=await schedule.loadGameCustomConfig();await schedule.saveGameCustomConfig(loaded);const reread=await schedule.loadGameCustomConfig();
      assert(Object.prototype.hasOwnProperty.call(reread,'__proto__')&&!(Object.prototype as any).mustRemainData,'opaque prototype key was dropped or applied as prototype');
    });
    await check('64 parallel reads share one transaction and detached readers cannot poison cache',async()=>{
      reset(JSON.stringify(legal));const all=await Promise.all(Array.from({length:64},()=>schedule.loadGameCustomConfig()));
      assert(calls.filter(c=>c.cmd==='fs.readTextRange'&&fileKey(c.args.path)===main).length===1,'parallel reads did not coalesce');
      all[0].entries[key].displayName='poison';assert(all[1].entries[key].displayName==='kept'&&schedule.getGameCustomConfig().entries[key].displayName==='kept','reader aliases cache');
    });
    await check('queued edit snapshots caller data and waits for in-flight old read before committing',async()=>{
      reset(JSON.stringify(legal));let release:()=>void=()=>{};let started:()=>void=()=>{};const entered=new Promise<void>(r=>started=r);
      readHook=async p=>{if(fileKey(p)===main){readHook=null;started();await new Promise<void>(r=>release=r)}};
      const old=schedule.loadGameCustomConfig();await entered;const edit=structuredClone(legal);edit.entries[key].displayName='queued';
      const pending=schedule.saveGameCustomConfig(reactive(edit));edit.entries[key].displayName='caller-late';edit.futureDocument.keep[0]='caller-root-late';edit.entries[key].futureEntry.keep=false;
      assert(calls.every(c=>c.cmd!=='fs.writeTextFileAtomic'),'save raced outstanding read');release();await old;await pending;
      const saved=await schedule.loadGameCustomConfig();assert(saved.entries[key].displayName==='queued'&&(saved as any).futureDocument.keep[0]==='opaque'&&(saved.entries[key] as any).futureEntry.keep,'old read or nested caller mutation overwrote queued edit');
    });
    await check('external main replacement during backup preservation cancels stale save',async()=>{
      reset(JSON.stringify(legal));const newer=structuredClone(legal);newer.entries[key].displayName='external-newer';
      writeHook=async p=>{if(fileKey(p)===backup){files.set(main,JSON.stringify(newer));writeHook=null}};
      let failed=false;try{await schedule.saveGameCustomConfig(legal)}catch{failed=true}assert(failed,'stale save overwrote external replacement');
      assert(JSON.parse(files.get(main)!).entries[key].displayName==='external-newer','external newer document lost');
    });
    await check('read timeout returns bounded default with no automatic retry and explicit read heals',async()=>{
      reset(JSON.stringify(legal));let release:()=>void=()=>{};let count=0;
      readHook=async()=>{count++;await new Promise<void>(r=>release=r)};const started=Date.now();
      assert(noEntries(await schedule.loadGameCustomConfig()),'read timeout did not use defaults');assert(Date.now()-started<3500&&count===1,'read timed out unbounded/retried');
      readHook=null;release();assert((await schedule.loadGameCustomConfig()).entries[key].displayName==='kept','explicit read after timeout did not heal');
    });
    await check('timed-out native write blocks newer writes until late operation settles, then explicit save heals',async()=>{
      reset(JSON.stringify(legal));let release:()=>void=()=>{};writeHook=async p=>{if(fileKey(p)===main){writeHook=null;await new Promise<void>(r=>release=r)}};
      const edit=structuredClone(legal);edit.entries[key].displayName='late-write';const before=notifications;
      let failed=false;try{await schedule.saveGameCustomConfig(edit)}catch{failed=true}assert(failed,'write timeout falsely succeeded');
      let blocked=false;try{await schedule.saveGameCustomConfig(legal)}catch{blocked=true}assert(blocked,'new save overlapped timed-out native write');
      assert(notifications===before,'unconfirmed/late write notified success');release();await new Promise<void>(r=>setTimeout(r,10));
      await schedule.saveGameCustomConfig(legal);assert((await schedule.loadGameCustomConfig()).entries[key].displayName==='kept','explicit post-settlement retry failed');
    });
    await check('retired settings generation cancels late read and queued save without resurrecting old cache',async()=>{
      reset(JSON.stringify(legal));let release:()=>void=()=>{};let started:()=>void=()=>{};const entered=new Promise<void>(r=>started=r);
      readHook=async()=>{readHook=null;started();await new Promise<void>(r=>release=r)};
      const old=schedule.loadGameCustomConfig().then(()=>false,()=>true);await entered;
      const queued=schedule.saveGameCustomConfig(legal).then(()=>false,()=>true);settings.clearSettingsCache();release();
      assert(await old,'retired load returned stale config');assert(await queued,'retired queued write accepted');assert(calls.every(c=>c.cmd!=='fs.writeTextFileAtomic'),'old generation wrote files');
      assert(noEntries(schedule.getGameCustomConfig()),'retired generation cache visible');assert((await schedule.loadGameCustomConfig()).entries[key].displayName==='kept','new generation cannot recover');
    });
    await check('generation change while native write is pending cancels cache/listener publication',async()=>{
      reset(JSON.stringify(legal));let release:()=>void=()=>{};let started:()=>void=()=>{};const entered=new Promise<void>(r=>started=r);
      writeHook=async p=>{if(fileKey(p)===main){writeHook=null;started();await new Promise<void>(r=>release=r)}};
      const edit=structuredClone(legal);edit.entries[key].displayName='retired-write';const before=notifications;
      const saving=schedule.saveGameCustomConfig(edit).then(()=>false,()=>true);await entered;settings.clearSettingsCache();release();
      assert(await saving,'retired native write claimed success');assert(notifications===before&&noEntries(schedule.getGameCustomConfig()),'retired write published cache/listener');
      await schedule.saveGameCustomConfig(legal);assert((await schedule.loadGameCustomConfig()).entries[key].displayName==='kept','new generation explicit retry failed');
    });
    await check('standalone storage chain never calls performance/input/firmware activation',async()=>{
      reset(JSON.stringify(legal));await schedule.loadGameCustomConfig();await schedule.saveGameCustomConfig(legal);await schedule.loadGameCustomConfig();
      assert(otherCalls.every(cmd=>!(/^(tdp|power\.(set|write|rebuild)|registry\.write|gamepad\.|inputHost\.|shell\.run|rtss\.set)/i.test(cmd))),'storage chain activated hardware/scheduler');
    });
  } finally {
    off();(globalThis as any).__mockIpcResponder=previousResponder;
  }
  console.log(`GameCustom persistence storm: PASS (${passed} cases; mock IPC only)`);
}

async function main(): Promise<void> {
  const dir = mkdtempSync(join(tmpdir(), 'yeman-schedule-tdp-'));
  let powerLifecycleStates: Array<'ready' | 'resuming'> = [];
  let blockedProfileAttempts = 0;
  let blockNextProfileWrite = false;
  let failNextRequiredProfileWrite = false;
  let templateScriptCalls = 0;
  const powerValues = new Map<string, number>();
  (globalThis as any).__mockShellDryRun = true;
  (globalThis as any).__mockShellResponder = (program: string, args: string[]) => {
    const exe = program.toLowerCase();
    if (exe === 'cscript.exe') templateScriptCalls++;
    if (exe.endsWith('yemantdpctl.exe')) {
      return { exitCode: 6, stdout: unsupportedLog, stderr: '' };
    }
    if (exe === 'powercfg' && args[0]?.toLowerCase() === '/getactivescheme') {
      return { exitCode: 0, stdout: `Power Scheme GUID: ${YEMAN} (YeMan)`, stderr: '' };
    }
    return undefined;
  };
  (globalThis as any).__mockIpcResponder = (cmd: string, args: any) => {
    if (cmd === 'registry.writePowerBatch') {
      const entries = Array.isArray(args?.entries) ? args.entries : [];
      const isFixedProfile = entries.some((entry: any) => entry.setting === CPU_MAX_STATE);
      if (isFixedProfile) {
        blockedProfileAttempts++;
        if (blockNextProfileWrite) {
          blockNextProfileWrite = false;
          throw new Error('hardware writes are blocked during power transition');
        }
      }
      const writes = (globalThis as any).__mockRegistryWrites as Array<{
        root: string; path: string; name: string; value: any;
      }>;
      for (const entry of entries) {
        const value = Number(entry.value);
        writes.push({
          root: 'HKLM',
          path: `SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes\\${args.scheme}\\${args.subGroup}\\${entry.setting}`,
          name: args.valueName,
          value,
        });
        powerValues.set(`${args.valueName}|${entry.setting}`, value);
      }
      if (isFixedProfile && failNextRequiredProfileWrite) {
        failNextRequiredProfileWrite = false;
        return { ok: false, written: entries.length - 1, failed: [{ setting: CPU_FREQ0, code: 5 }] };
      }
      return { ok: true, written: entries.length, failed: [] };
    }
    // The offline fixture must not consult the execution host's real registry.
    if (cmd === 'registry.read' && args?.name === 'VendorIdentifier') return 'AuthenticAMD';
    if (cmd === 'registry.read' && String(args?.path || '').includes('\\User\\PowerSchemes\\')) {
      const setting = String(args?.path || '').split('\\').pop() || '';
      return powerValues.get(`${args?.name}|${setting}`) ?? 0;
    }
    if (cmd === 'power.schemeExists') return {ok:true,exists:true,code:0};
    if (cmd === 'power.sourceSnapshot') return {known:true,acLine:0};
    if (cmd === 'fs.readTextRange' && /game-custom\.json/i.test(String(args?.path))) {
      const content=readFileSync(args.path,'utf8'),size=new TextEncoder().encode(content).byteLength;
      return {content:size>args.maxBytes?'':content,offset:0,size};
    }
    if (cmd === 'power.activeScheme') return YEMAN;
    if (cmd === 'power.setActiveScheme') return {ok:true,code:0};
    if (cmd === 'power.lifecycle') {
      const phase = powerLifecycleStates.shift() ?? 'ready';
      return { phase, generation: 1, hardwareWritesAllowed: phase === 'ready', inputReady: phase === 'ready', hibernateAvailable: true };
    }
    if (cmd === 'sys.info') {
      return { cpuName: 'AMD Ryzen Threadripper 3970X', physicalCores: 32, logicalProcs: 64, totalMemoryBytes: 1, acLine: 0, powerMode: 'dc', hasBattery: true };
    }
    if (cmd === 'cpu.architecture') {
      return { detected: true, heterogeneous: false, efficiencyClasses: [0], source: 'cpu-set', logical: 64, physical: 32 };
    }
    if (cmd === 'game.detect') return null;
    if (cmd === 'tdpDaemon.request') {
      if (args?.op === 'ping') return { version: 1, requestId: 'test', ok: true, rc: 0 };
      if (args?.op === 'set') return { version: 1, requestId: 'test', ok: false, rc: 6, error: unsupportedLog };
      return { version: 1, requestId: 'test', ok: true, rc: 0 };
    }
    if (cmd === 'tdpDaemon.start' || cmd === 'monitor.setFpsEnabled') return { ok: true };
    return undefined;
  };

  try {
    const settings = await import('../src/bridge/settingsRepository');
    const yeman = await import('../src/bridge/yeman');
    const autofloat = await import('../src/bridge/autofloat');
    const cpuProfiles = await import('../src/bridge/cpuProfiles');
    const schedule = await import('../src/bridge/performanceSchedule');
    const frameRates = await import('../src/bridge/frameRateLimits');
    settings.setSettingsDirectory(dir);
    yeman.setPowerControlDir(dir);
    autofloat.setAutofloatPowerControlDir(dir);

    const savedCpuProfiles = cpuProfiles.defaultCpuProfilesConfig();
    savedCpuProfiles.profiles.balanced = {
      acFreq: 6200,
      dcFreq: 3600,
      acTurbo: true,
      dcTurbo: true,
      acAggr: 91,
      dcAggr: 77,
    };
    await cpuProfiles.saveCpuProfiles(savedCpuProfiles);

    const warnings: string[] = [];
    const offWarning = schedule.onPerformanceScheduleWarning((warning) => warnings.push(warning.message));
    const config = schedule.defaultPerformanceScheduleConfig();
    config.configured = true;
    config.enabled = true;
    // Simulate the affected class of machine: a battery device currently on DC.
    config.active.dc = 'balanced';
    config.profiles.dc.balanced.cpuPreset = 'balanced';

    // Reactive saves in unrelated pages must not poison the shared settings
    // cache used by real CPU/TDP mode application. Hardware remains mocked.
    await settings.saveSettingsSection('fan', { deviceIdentity: reactive({ manufacturer: 'fixture', model: 'test' }) });
    await settings.saveSettingsSection('quickApps', { apps: reactive([{ name: 'fixture', path: 'fixture.exe' }]) });

    // Establish the fixture's real no-game observation before public frame edits.
    await (await import('../src/bridge/gamedetect')).refreshGameStatusStrict();
    await frameRates.saveGlobalFrameRate('ac',120);
    await frameRates.saveGlobalFrameRate('dc',60);
    await runGameCustomPersistenceStorm(schedule, settings, dir);
    if(process.env.YMCC_GAME_CUSTOM_ONLY==='1'){console.log('performance schedule GameCustom scope: PASS (original CPU/OEM cases NOT_RUN)');return;}
    const autoApplied = await schedule.applyPerformanceSchedule('dc', 'balanced', config);
    assert(autoApplied, 'TDP rc=6 不应让手动→自动切换返回失败');
    assert(autofloat.getFloatInfo().enabled, 'TDP rc=6 后 CPU 浮动仍应启动');
    assert((await schedule.getPerformanceScheduleOwnership()) === 'auto', '自动模式必须独立持久化');
    const warning = schedule.getPerformanceScheduleWarning();
    assert(warning?.code === 'tdp-unsupported', 'Desktop-17h 应给出明确的不支持提示');
    assert(warning.message.includes('不会阻断 CPU 调度'), '提示必须明确 CPU 调度仍继续');
    const writes = (globalThis as any).__mockRegistryWrites as Array<{
      root: string; path: string; name: string; value: any;
    }>;
    assert(!writes.some((item) => item.name === 'ACSettingIndex'), '当前是DC；TDP失败也不能改写AC记忆参数');
    assert(writes.some((item) => item.name === 'DCSettingIndex'), 'TDP 失败后 DC CPU 参数仍应写入');
    assert(!writes.some((item) => item.path.endsWith(`\\${CPU_MAX_STATE}`)), 'CPU 浮动接管时不得重复应用固定 CPU 挡位');

    // Deprecated per-combination FPS (including zero) cannot stop floating or rebind independent FPS.
    config.profiles.dc.balanced.fpsTarget = 0;
    config.profiles.dc.balanced.cpuTarget = 'aggressive';
    config.profiles.dc.balanced.tdpStrategy = 'aggressive';
    writes.length = 0;
    const noLockApplied = await schedule.applyPerformanceSchedule('dc', 'balanced', config);
    assert(noLockApplied, '不锁帧档位仍应完成自动模式应用');
    assert(autofloat.getFloatInfo().enabled, '组合旧帧率为0也不能关闭CPU/TDP浮动');
    assert(autofloat.getFloatInfo().target === 60, '优化参考目标必须读取独立DC帧率，不读取组合旧帧率');
    config.profiles.dc.balanced.cpuTarget='none';writes.length=0;
    await schedule.applyPerformanceSchedule('dc','balanced',config);
    assert(writes.length >= 12, `连续 CPU 参数至少应写入当前 DC 侧的必需字段，实际=${writes.length}`);
    assert(writes.every((item) => item.name === 'DCSettingIndex'), 'DC 自动参数不得覆盖 AC 保存值');
    const valueFor = (setting: string) => writes.find((item) => item.path.endsWith(`\\${setting}`))?.value;
    const canonicalCpu = automaticCpuControls(schedule.continuousControlsFromSchedule(await schedule.loadPerformanceSchedule()).dc);
    assert(valueFor(CPU_FREQ0) === canonicalCpu.maxMHz, '自动模式必须应用连续 bank 计算出的 DC 主频');
    assert(valueFor(CPU_BOOST) === (canonicalCpu.turbo ? 2 : 0), '自动模式必须同步连续 bank 计算出的 DC 睿频');
    assert(typeof valueFor(CPU_SCHED0) === 'number', '自动模式必须写入 DC 积极性');
    assert(valueFor(CPU_MIN0) === canonicalCpu.minState, '自动模式必须同步连续 bank 计算出的最小处理器状态');
    assert(valueFor(CPU_MAX_STATE) === undefined, '连续自动模式不应回写旧固定最大处理器状态');
    assert(typeof valueFor(CPU_THROTTLE) === 'number', '自动模式必须同步频率对应的节流状态');
    assert(templateScriptCalls === 0, '自动模式不得运行任何 CPU 模板脚本');

    config.profiles.dc.balanced.fpsTarget = 45;
    const relockApplied = await schedule.applyPerformanceSchedule('dc', 'balanced', config);
    assert(relockApplied, '从不锁帧切回有效目标后应能重新应用自动浮动');
    assert(autofloat.getFloatInfo().enabled, '性能组合应用不受旧帧率字段控制');
    assert(autofloat.getFloatInfo().target===60, '切换旧组合帧率不能改变独立DC值');
    assert((await frameRates.loadGlobalFrameRates()).ac.fps===120, 'DC模式不得改写独立AC帧率');

    await schedule.disablePerformanceSchedule(config);
    assert(!autofloat.getFloatInfo().enabled, '自动→手动必须停止 CPU 浮动控制');
    assert((await schedule.getPerformanceScheduleOwnership()) === 'manual', '自动→手动必须独立持久化');

    // Retry only after native reports that the transition has returned to Ready.
    config.profiles.dc.balanced.fpsTarget = 0;
    config.profiles.dc.balanced.cpuPreset = 'balanced';
    config.profiles.dc.balanced.cpuTarget = 'none';
    config.profiles.dc.balanced.tdpStrategy = 'none';
    powerLifecycleStates = ['ready', 'resuming', 'ready'];
    blockedProfileAttempts = 0;
    blockNextProfileWrite = true;
    const recoveredApply = await schedule.applyPerformanceSchedule('dc', 'balanced', config);
    assert(recoveredApply, '短暂电源写入闸门应在 native Ready 后自动重试一次');
    assert(blockedProfileAttempts === 0, `连续性能路径不应回写旧固定 CPU 挡位，实际=${blockedProfileAttempts}`);

    failNextRequiredProfileWrite = true;
    const noLegacyProfileFailure = await schedule.applyPerformanceSchedule('dc', 'balanced', config);
    assert(noLegacyProfileFailure, '连续性能路径不应被旧固定 CPU 挡位失败分支阻断');
    assert(templateScriptCalls === 0, '连续性能路径不得回退运行模板脚本');

    const autoAppliedAgain = await schedule.applyPerformanceSchedule('dc', 'balanced', config);
    assert(autoAppliedAgain, '相同 TDP 不支持错误再次出现时仍不得阻断自动模式');
    assert(warnings.length === 1, '同一平台兼容性提示每次运行只应发布一次');
    await schedule.disablePerformanceSchedule(config);
    offWarning();
    // MSI G3E OEM isolation: real bridge with a fake native endpoint only.
    // No WMI/driver/module/registry/hardware call is made by these cases.
    powerLifecycleStates = []; // A separate OEM fixture starts in a known Ready epoch.
    const circuit = await import('../src/bridge/tdpCircuitBreaker');
    const previousResponder = (globalThis as any).__mockIpcResponder;
    const oemCalls: any[] = [];
    const nonOemCalls: string[] = [];
    let oemResult: any = null; // null = typed OEM-owned acceptance fixture
    let firmwareAcceptedCeiling = 35;
    const runtime = await import('../src/bridge/tdpRuntimeState');
    let capabilityFailure = false;
    const validOemCapability = { cpuFamily: 'intel-arc-g3-extreme', provider: 'msi-g3e-wmi', selected: true, ready: true, minPL1: 8, maxPL1: 200, machineMaximumKnown: false, rangeSource: 'application-request-domain-NOT-machine-maximum', generation: 1, deadlineTick: 8000 };
    let oemCapability: any = validOemCapability;
    let genericG3eFixture = false;
    let oemPowerSide: 'ac' | 'dc' = 'dc';
    let nativeGeneration = 1;
    let monitorCommands: string[] = [];
    let fixtureMonitor = false, monitorSeq = 0, fixtureFps = 90, fixtureGame: string | null = 'g3e-fixture.exe';
    const observations: any[] = [];
    const timerOemWrites: Array<{ at: number; watts: number }> = [];
    const genericG3eCalls: any[] = [];
    (globalThis as any).__mockIpcResponder = (cmd: string, args: any) => {
      if (fixtureMonitor && cmd.startsWith('monitor.') && cmd !== 'monitor.snapshot') monitorCommands.push(cmd);
      if (cmd === 'tdp.capability') {
        if (capabilityFailure) throw new Error('fixture capability transport failure');
        return oemCapability;
      }
      if (cmd === 'tdp.observe') { observations.push(...(args.events ?? [args]).map((row: any) => ({ event: row.event, ...row.detail }))); return true; }
      if (fixtureMonitor && cmd === 'monitor.snapshot') {
        const now = Date.now();
        return { available: true, schema: 1, session: 900001, generation: 1, seq: ++monitorSeq, collectedAt: now,
          heartbeatAt: now, hwinfoOkAt: now, topActive: true, fpsActive: true, hwDown: false,
          // Current control samples carry Displayed producer identity and expiry.
          // Keep HUD FPS separately; it is not authority for the float loop.
          top: JSON.stringify({ ts: now, ac: oemPowerSide === 'ac' ? 1 : 0,
            cpuUsage: 25, cpuUsageValid: true, cpuUsageTs: now, freqMhz: 2900, cpuClockValid: true, tdpW: 20, cpuPowerValid: true,
            gpuSelectionPresent: true, gpuName: 'fixture-selected-adapter', gpuUsagePct: 20,
            displayedFramerate: fixtureGame ? { source: 'hwinfo-displayed', fps: fixtureFps, fps1: fixtureFps,
              pid: 9876, processCreated: '12345', game: fixtureGame, observedAt: now, validUntil: now + 5000, pollPeriodMs: 1000 } : null }),
          fps: JSON.stringify({ ts: now, fps: fixtureFps, fps1: fixtureFps, gpu: 0, game: fixtureGame, pid: 9876 }) };
      }
      if (fixtureMonitor && cmd === 'monitor.takeover') return {ok:true,cleaned:true};
      if (fixtureMonitor && (cmd === 'monitor.start' || cmd === 'monitor.stop')) return { ok: true };
      if (cmd === 'game.detect') return fixtureGame ? {pid:9876,name:fixtureGame,path:'C:\\Fixture\\'+fixtureGame,title:'Fixture',processCreated:'12345'} : null;
      if (cmd === 'sys.info') return { cpuName: 'Intel(R) Arc(TM) G3 Extreme', physicalCores: 16, logicalProcs: 16, totalMemoryBytes: 1, acLine: oemPowerSide === 'ac' ? 1 : 0, powerMode: oemPowerSide, hasBattery: true };
      if (cmd === 'power.lifecycle') return { ...previousResponder(cmd, args), generation: nativeGeneration };
      if (cmd === 'tdpMsi.set') {
        assert(Number.isInteger(args.watts) && args.watts >= 8 && args.watts <= 200, 'every OEM request is an application packet value, not a CPU/model ceiling');
        assert(args.generation === nativeGeneration, 'every OEM write must use the current native generation');
        oemCalls.push(args);
        if (fixtureMonitor) timerOemWrites.push({ at: Date.now(), watts: args.watts });
        if (oemResult instanceof Error) throw oemResult;
        if (oemResult !== null) return oemResult;
        const acceptedPL1W = Math.min(args.watts, firmwareAcceptedCeiling);
        return { ok: true, rc: 0, readbackMatched: acceptedPL1W === args.watts,
          acceptedVerified: true, acceptedPL1W, outcome: acceptedPL1W === args.watts ? 'applied' : 'oem-adjusted' };
      }
      if (cmd === 'tdpDaemon.start' || cmd === 'tdpDaemon.request' || (cmd === 'shell.run' && String(args?.program).toLowerCase().endsWith('yemantdpctl.exe'))) nonOemCalls.push(cmd);
      // These original generic range/vendor cases exercise the pre-send CLI fallback.
      // Refuse daemon admission before any set; ordinary persistent transport is tested separately.
      if (genericG3eFixture && cmd === 'tdpDaemon.request' && args?.op === 'ping') return { ok: false, rc: 1 };
      if (genericG3eFixture && cmd === 'tdpDaemon.start') return { ok: false };
      if (genericG3eFixture && cmd === 'fs.stat' && String(args?.path).endsWith('float-active')) throw new Error('fixture float owner absent');
      if (genericG3eFixture && cmd === 'shell.run' && String(args?.program).toLowerCase().endsWith('yemantdpctl.exe')) {
        genericG3eCalls.push(args);
        return { exitCode: 0, stdout: 'fixture generic Intel write receipt', stderr: '' };
      }
      return previousResponder(cmd, args);
    };
    await (await import('../src/bridge/gamedetect')).refreshGameStatusStrict();
    yeman.setPowerControlDir(dir);
    circuit.resetTdpCapabilityState();
    runtime.tdpRuntimeStore.powerMode('dc');
    assert(await yeman.setTdp('ac', 20, { apply: true, save: false, vendor: 'amd', source: 'ui' }), 'G3E uses native actual vendor, not stale saved AMD');
    assert(oemCalls.length === 1 && oemCalls[0].watts === 20 && oemCalls[0].generation === 1, 'same native OEM request contains watts and live epoch');
    assert(oemCalls[0].explicitRetry === false, 'diagnostic source ui is not a retry authorization');
    assert(await yeman.ensureTdpDaemon(), 'MSI requires no PawnIO daemon');
    assert(await yeman.resumeTdpDaemonAfterWake(true), 'MSI wake does not write ceiling/start helper');
    await yeman.stopTdpDaemon();
    assert(nonOemCalls.length === 0, 'MSI set/lifecycle never call daemon/MSR CLI');
    assert(autofloat.getTdpTarget(8, 'medium') === 8, 'MSI float respects provider minimum without changing strategy');
    const savedBefore = await yeman.readTdp('ac');
    for (const invalid of [-2, 0, 1, 8.5, 35.1, 201, NaN, Infinity]) {
      let rejected = false;
      try { await yeman.setTdp('ac', invalid, { apply: true, save: true, source: 'startup' }); }
      catch { rejected = true; }
      assert(rejected && oemCalls.length === 1, `MSI target ${invalid} rejected before save/write`);
    }
    assert(await yeman.readTdp('ac') === savedBefore, 'invalid inputs never save or write');
    for (const intent of [2, 7, 55, 120, 200]) {
      await yeman.setTdp('ac', intent, { apply: true, save: true, source: 'schedule' });
      const effective = Math.max(8, Math.min(35, intent));
      assert(oemCalls[oemCalls.length - 1].watts === Math.max(8, intent), 'portable intent is offered once without a software 35W cap');
      assert(await yeman.readTdp('ac') === intent, 'effective target must not silently replace saved portable intent');
      const receipt = observations.filter(r => r.event === 'tdp.target-resolved').pop();
      assert(receipt.requestedW === intent && receipt.effectiveW === Math.max(8, intent) && runtime.tdpRuntimeStore.get().watts === effective, 'offered intent and OEM-accepted setting stay separate');
    }
    assert(yeman.formatTdpTarget(120) === '120W', 'unknown machine maximum does not introduce a waiting-status label into the existing frontend');
    for (const strategy of ['none', 'small', 'medium', 'large', 'aggressive'] as const) {
      const target = autofloat.getTdpTarget(120, strategy);
      assert(target >= 8 && target <= 120, 'preview without a receipt is a request estimate, not a hardware maximum');
      assert(autofloat.getTdpTarget(35, strategy) <= 35, 'runtime float uses the OEM-accepted baseline for ratios');
    }
    // Replay both AC/DC banks through the canonical continuous schedule.
    const modelConfig = schedule.defaultPerformanceScheduleConfig();
    modelConfig.configured = true;
    modelConfig.enabled = true;
    const modelWarnings: string[] = [];
    const offModelWarning = schedule.onPerformanceScheduleWarning(w => { if (w.code === 'tdp-apply-failed') modelWarnings.push(w.message); });
    const independentBefore = JSON.stringify(await frameRates.loadGlobalFrameRates());
    for (const side of ['ac', 'dc'] as const) {
      oemPowerSide = side;
      // Deliver the same source receipt and revocation as App's native event binding.
      // Changing sys.info alone no longer changes the event-owned execution side.
      runtime.tdpRuntimeStore.powerMode(side);
      autofloat.invalidateFloatPowerSource(side);
      assert(await schedule.applyPerformanceSchedule(side, 'balanced', modelConfig), 'each canonical AC/DC bank can cold/hot switch');
      const saved = await schedule.loadPerformanceSchedule();
      const expectedBank = schedule.continuousControlsFromSchedule({ ...modelConfig, active: { ...modelConfig.active, [side]: 'balanced' } })[side];
      assert(saved.controls?.[side].tdp === expectedBank.tdp, 'continuous bank TDP intent is preserved');
      const state = autofloat.getFloatInfo();
      assert(state.tdpMax >= 8 && state.tdpMax <= 35 && state.tdpApplied >= 8 && state.tdpApplied <= 35, 'float stays inside the admitted OEM range');
    }
    assert(modelWarnings.length === 0, 'canonical bank application does not create false TDP-apply-failed warnings');
    assert(JSON.stringify(await frameRates.loadGlobalFrameRates()) === independentBefore, 'canonical schedule application does not change independent FPS');
    // Execute the registered production timer callbacks with a fixture clock.
    // The TDP-first bank is explicit; the default bias may correctly probe CPU first.
    // Never reset the owner to bypass its rollback cooldown or write on a missing identity.
    await autofloat.disableFloat();
    const clock = installFixtureClock();
    const timerStartedAt = clock.now();
    const settle = async <T>(label: string, work: Promise<T>): Promise<T> => {
      let done = false, value: T, failed = false, error: unknown;
      void work.then(result => { value = result; done = true; }, reason => { error = reason; failed = true; done = true; });
      await clock.advanceBy(0);
      for (let attempt = 0; !done && attempt < 100; ++attempt) await clock.advanceBy(100);
      assert(done, `timer stage ${label} did not settle: ${JSON.stringify(autofloat.getFloatInfo())}`);
      if (failed) throw error;
      return value!;
    };
    const waitForApplied = async (label: string, test: (watts: number) => boolean): Promise<void> => {
      await clock.advanceBy(0);
      for (let attempt = 0; attempt < 100; ++attempt) {
        if (test(autofloat.getFloatInfo().tdpApplied)) return;
        await clock.advanceBy(100);
      }
      throw new Error(`timer stage ${label} did not converge: ${JSON.stringify(autofloat.getFloatInfo())}`);
    };
    try {
      fixtureMonitor = true;
      autofloat.setAutofloatPowerControlDir(dir);
      const floatBank = schedule.continuousControlsFromSchedule(await schedule.loadPerformanceSchedule()).dc;
      floatBank.tdp = 35; floatBank.auto.saving = 'aggressive'; floatBank.auto.bias = 25;
      await settle('enable', autofloat.enableContinuousFloat(floatBank, 'dc', 60, async () => true, async () => {}, { requested: 120, accepted: 35 }));
      await settle('save-portable-intent', yeman.saveTdp('dc', 120));
      await waitForApplied('first-admitted-TDP-trial', w => w >= 8 && w < 35);
      fixtureFps = 10;
      await waitForApplied('low-FPS-rollback', w => w === 35);
      const rollbackAt = clock.now(), afterRollback = timerOemWrites.length;
      fixtureFps = 90;
      await clock.advanceBy(29000);
      assert(autofloat.getFloatInfo().tdpApplied === 35 && timerOemWrites.slice(afterRollback).every(write => write.watts === 35),
        'production30s rollback cooldown must forbid another TDP trial before expiry');
      await waitForApplied('TDP-trial-after-full-cooldown', w => w >= 8 && w < 35);
      const secondTrial = timerOemWrites.slice(afterRollback).find(write => write.watts < 35);
      assert(secondTrial !== undefined && secondTrial.at - rollbackAt >= 30000,
        'a renewed TDP trial must consume the same owner through its complete30s cooldown');
      const admittedBeforeUnknown = autofloat.getFloatInfo().tdpApplied;
      fixtureGame = null;
      await settle('confirm-game-identity-missing', (await import('../src/bridge/gamedetect')).refreshGameStatusStrict());
      const beforeUnknown = oemCalls.length;
      await clock.advanceBy(5000);
      assert(oemCalls.length === beforeUnknown && autofloat.getFloatInfo().tdpApplied === admittedBeforeUnknown,
        'missing game identity must not fabricate idle recovery or perform an OEM write');
      const beforeDisable = oemCalls.length;
      await settle('disable', autofloat.disableFloat());
      assert(oemCalls.length === beforeDisable, 'disableFloat never restores an OEM limit');
      assert(clock.pendingIntervals() === 0, 'disabled float must clear its registered interval before fixture clock cleanup');
      await clock.advanceBy(5000);
      assert(oemCalls.length === beforeDisable && !autofloat.getFloatInfo().enabled,
        'disabled owner must not resume writes from a late registered timer callback');
      console.log('TDP timer evidence: '+JSON.stringify({ elapsedPolicyMs: clock.now() - timerStartedAt,
        rollbackAt, renewedTrialAt: secondTrial.at, cooldownMs: secondTrial.at - rollbackAt,
        callbacks: clock.callbacks, pendingIntervalsBeforeClockRestore: clock.pendingIntervals(),
        pendingTimeoutsBeforeClockRestore: clock.pendingTimeouts(), writes: timerOemWrites,
        clock: 'fixture-controlled registered callbacks; not physical elapsed-time acceptance' }));
    } finally {
      try { await settle('final-disable', autofloat.disableFloat()); }
      finally { fixtureMonitor = false; clock.restore(); }
    }
    // Actual schedule + actual float + actual yeman with the same inert native
    // responders. Only repeated TDP changes: retain CPU and monitoring ownership.
    const editClock = installFixtureClock();
    const settleEdit = async <T>(label: string, work: Promise<T>): Promise<T> => {
      let done = false, value: T, error: unknown, failed = false;
      void work.then(result => { value = result; done = true; }, reason => { error = reason; failed = true; done = true; });
      for (let n = 0; !done && n < 100; ++n) await editClock.advanceBy(100);
      assert(done, `incremental stage ${label} did not settle`);
      if (failed) throw error;
      return value!;
    };
    try {
      fixtureMonitor = true; fixtureGame = 'g3e-fixture.exe'; fixtureFps = 90;
      await settleEdit('game', (await import('../src/bridge/gamedetect')).refreshGameStatusStrict());
      const initial = await settleEdit('read', schedule.getContinuousPerformanceSnapshot('effective'));
      await settleEdit('admit', schedule.updateContinuousPerformanceControls(initial.identity, { mode: 'auto', tdp: 20, auto: { saving: 'aggressive' } }));
      await editClock.advanceBy(1500); // finish original shared monitor admission
      await settleEdit('monitor-cold-takeover', import('../src/bridge/monitorData').then(module => module.readMonitorSnapshot()));
      const frequency = autofloat.getFloatInfo().state.freq;
      const registry = (globalThis as any).__mockRegistryWrites as Array<{path:string}>;
      const cpuSetting = (row: {path:string}) => [CPU_FREQ0,CPU_BOOST,CPU_SCHED0,CPU_MIN0].some(id => row.path.endsWith('\\'+id));
      const cpuBefore = registry.filter(cpuSetting).length, commandsBefore = monitorCommands.length, tdpBefore = oemCalls.length;
      for (const watts of [21,22,23]) {
        const current = await settleEdit('current', schedule.getContinuousPerformanceSnapshot('effective'));
        const result = await settleEdit('tdp-'+watts, schedule.updateContinuousPerformanceControls(current.identity, {tdp:watts}));
        assert(result.tdpWriteReceipt?.accepted === watts && result.tdpWriteReceipt.ok, 'actual incremental edit must return its own accepted TDP receipt');
        assert(autofloat.getFloatInfo().enabled && autofloat.getFloatInfo().state.freq === frequency, 'TDP-only edit must preserve the live CPU state');
      }
      assert(registry.filter(cpuSetting).length === cpuBefore, 'actual TDP-only transaction must not rewrite CPU parameters');
      assert(monitorCommands.length === commandsBefore, 'actual TDP-only transaction must not restart shared monitor resources: '+JSON.stringify(monitorCommands.slice(commandsBefore)));
      assert(oemCalls.length === tdpBefore + 3, 'actual incremental edits send one original OEM command each, without replay');
      console.log('TDP incremental evidence: '+JSON.stringify({edits:3,tdpWrites:oemCalls.length-tdpBefore,cpuSettingWrites:registry.filter(cpuSetting).length-cpuBefore,monitorLifecycleCommands:monitorCommands.length-commandsBefore,frequencyMHz:frequency,scope:'actual schedule/float/yeman; memory native; not hardware acceptance'}));
      await settleEdit('disable', autofloat.disableFloat());
      await settleEdit('restore-case-config', yeman.saveTdp('dc',120));
      assert(editClock.pendingIntervals() === 0, 'incremental scenario must release its original float interval');
    } finally {
      try { await settleEdit('final-disable', autofloat.disableFloat()); }
      finally { fixtureMonitor = false; editClock.restore(); }
    }
    offModelWarning();
    const acceptedBeforeHotkey = Math.min(firmwareAcceptedCeiling, oemCalls.at(-1).watts);
    assert(acceptedBeforeHotkey > 8 && acceptedBeforeHotkey < 35 && runtime.tdpRuntimeStore.get().watts === acceptedBeforeHotkey,
      'disabled float preserves its last accepted limit for the next explicit hotkey');
    assert(await yeman.adjustTdp(-1) === acceptedBeforeHotkey - 1 && oemCalls.at(-1).watts === acceptedBeforeHotkey - 1 && await yeman.readTdp('dc') === 120,
      'manual hotkey decreases from current effective limit, not from old 120W intent');
    assert(await yeman.setTdpCurrentMode(120) === 35, 'manual explicit target returns actual admitted watts');
    assert(nonOemCalls.length === 0, 'full model flow never starts a helper/daemon or replays MSR');
    nativeGeneration = 1;
    oemCapability = validOemCapability;
    yeman.setPowerControlDir(dir);
    for (const bad of [
      { ok: false, rc: 6, readbackMatched: false, error: 'fixture partial unknown' },
      { ok: true, rc: 0, readbackMatched: false },
      new Error('fixture native timeout'),
    ]) {
      circuit.resetTdpCapabilityState();
      oemResult = bad;
      let rejected = false;
      try { await yeman.setTdp('dc', 25, { apply: true, save: false, source: 'autofloat' }); }
      catch { rejected = true; }
      assert(rejected && nonOemCalls.length === 0, 'OEM partial/mismatch/timeout never replays CLI/MSR');
      assert(oemCalls[oemCalls.length - 1].explicitRetry === false, 'autofloat cannot clear native partial latch');
    }
    oemResult = null;
    for (const outcome of ['unverified', 'oem-rejected']) {
      circuit.resetTdpCapabilityState();
      oemResult = { ok: false, rc: 0, readbackMatched: false, outcome, note: 'fixture OEM refusal or missing readback' };
      assert(!(await yeman.setTdp('dc', 120, { apply: true, save: false })), 'OEM refusal/unknown readback does not throw a user-visible failure');
      assert(runtime.tdpRuntimeStore.get().status === 'unknown' && runtime.tdpRuntimeStore.get().watts === null, 'no readback is unknown, never requested/stale watts');
    }
    oemResult = null;
    firmwareAcceptedCeiling = 65;
    circuit.resetTdpCapabilityState();
    await yeman.setTdp('dc', 120, { apply: true, save: false });
    assert(oemCalls.at(-1).watts === 120 && runtime.tdpRuntimeStore.get().watts === 65, 'same OEM channel may accept above the former 35W software policy');
    firmwareAcceptedCeiling = 35;
    for (const invalidCapability of [
      { ...validOemCapability, generation: 2 }, // current native lifecycle is generation 1
      { ...validOemCapability, deadlineTick: -1 },
      { ...validOemCapability, minPL1: 0 },
      { ...validOemCapability, selected: false },
      { ...validOemCapability, ready: false, error: 'fixture vendor not verified' },
      { provider: 'default', selected: false, ready: false, minPL1: 2, maxPL1: 200 },
    ]) {
      oemCapability = invalidCapability;
      yeman.setPowerControlDir(dir);
      circuit.resetTdpCapabilityState();
      const before = oemCalls.length;
      let rejected = false;
      try { await yeman.setTdp('ac', 20, { apply: true, save: false }); } catch { rejected = true; }
      assert(rejected && oemCalls.length === before && nonOemCalls.length === 0, 'stale epoch/invalid capability rejected before every hardware route');
    }
    oemCapability = validOemCapability;
    yeman.setPowerControlDir(dir);
    const staleSettingsProbe = yeman.getTdpExecutionCapability().then(() => false, () => true);
    yeman.setPowerControlDir(dir);
    assert(await staleSettingsProbe, 'in-flight descriptor cannot cross a settings directory generation');
    const supersededProbe = yeman.getTdpExecutionCapability(true).then(() => false, () => true);
    const currentProbe = yeman.getTdpExecutionCapability(true);
    assert(await supersededProbe, 'superseded descriptor cannot overwrite the current capability cache');
    assert((await currentProbe).selected, 'newest valid descriptor owns the cache');
    circuit.resetTdpCapabilityState();
    await yeman.setTdp('ac', 25, { apply: true, save: false, source: 'unspecified', retryMsi: true });
    assert(oemCalls[oemCalls.length - 1].explicitRetry === true, 'retry permission is a separate explicit option');
    capabilityFailure = true;
    yeman.setPowerControlDir(dir);
    let rejectedCapability = false;
    try { await yeman.setTdp('ac', 20, { apply: true, save: false }); } catch { rejectedCapability = true; }
    assert(rejectedCapability && nonOemCalls.length === 0, 'failed capability lookup never defaults to MSR');
    // Same CPU in ONEXPLAYER/another brand: shared limits/vendor, never MSI ACPI.
    capabilityFailure = false;
    genericG3eFixture = true;
    // Different machine, same CPU: consume its channel descriptor, never the MSI range.
    oemCapability = { cpuFamily: 'intel-arc-g3-extreme', provider: 'default', selected: false, ready: true, minPL1: 5, maxPL1: 45, machineMaximumKnown: true, rangeSource: 'FAKE-only-channel-range-not-real-ONEXPLAYER-proof' };
    yeman.setPowerControlDir(dir);
    circuit.resetTdpCapabilityState();
    const beforeGeneric = oemCalls.length;
    assert(await yeman.setTdp('ac', 20, { apply: true, save: false, vendor: 'amd' }), 'non-MSI G3E retains generic Intel transport');
    assert(oemCalls.length === beforeGeneric && genericG3eCalls.length === 1, 'non-MSI G3E never uses MSI WMI blocks or shift');
    assert(genericG3eCalls[0].args.join(' ') === 'set 20 --vendor intel', 'G3E CPU identity overrides stale AMD settings on generic transport too');
    assert(yeman.getTdpHardwareMinimum() === 5 && yeman.getTdpHardwareMaximum() === 45, 'same CPU never forces the other machine to MSI 8..35W');
    assert(autofloat.getTdpTarget(120, 'none') === 45, 'float consumes the selected channel ceiling, not a CPU catalog ceiling');
    assert(await yeman.setTdp('ac', 55, { apply: true, save: true }), 'same CPU policy resolves legacy intent on the generic transport');
    assert(genericG3eCalls.length === 2 && genericG3eCalls[1].args.join(' ') === 'set 45 --vendor intel' && oemCalls.length === beforeGeneric, 'non-MSI G3E never receives an MSI OEM recipe');
    assert(await yeman.readTdp('ac') === 55, 'non-MSI G3E also preserves portable intent');
    oemCapability = { ...oemCapability, maxPL1: 65 };
    yeman.setPowerControlDir(dir);
    circuit.resetTdpCapabilityState();
    await yeman.setTdp('ac', 120, { apply: true, save: true });
    assert(genericG3eCalls.at(-1).args.join(' ') === 'set 65 --vendor intel', 'a larger admitted channel range must not be truncated by an old CPU/35W cap');
    assert(await yeman.readTdp('ac') === 120 && oemCalls.length === beforeGeneric, 'different machine limits do not inherit MSI writes or migrate intent');
    (globalThis as any).__mockIpcResponder = previousResponder;
    yeman.setPowerControlDir(dir);
    circuit.resetTdpCapabilityState();
    assert(!(await yeman.getTdpExecutionCapability()).selected, 'ordinary CPU retains the existing route');
    assert(yeman.getTdpHardwareMinimum() === 2, 'ordinary CPU minimum is not narrowed by the G3E fix');
    assert(yeman.resolveTdpTarget(120).effectiveW === 120, 'ordinary CPU legacy mode is not narrowed by the MSI fix');
    assert(autofloat.getTdpTarget(120, 'none') === 120, 'ordinary CPU float maximum remains unchanged');
    console.log('G3E complete model flow: PASS (AC/DC continuous banks, timer-driven float and low-level TDP transport; fake native only)');
    console.log('G3E CPU/OEM isolation: PASS (fake native only, not hardware acceptance)');
    console.log('performance schedule TDP isolation: PASS');
  } finally {
    await (await import('../src/bridge/autofloat')).disableFloat().catch(()=>{});
    const scheduler = await import('../src/scheduler');
    for (const name of scheduler.getScheduledTaskNames()) scheduler.unregisterScheduledTask(name);
    rmSync(dir, { recursive: true, force: true });
    delete (globalThis as any).__mockShellResponder;
    delete (globalThis as any).__mockIpcResponder;
    delete (globalThis as any).__mockShellDryRun;
  }
}

// The resident game-target singleton intentionally stays alive; this isolated process ends after cleanup.
void main().then(() => process.exit(0)).catch((error) => {
  console.error((error as Error)?.stack || error);
  process.exitCode = 1;
  process.exit(1); // A failed isolated suite must not hang on resident mock polling.
});
