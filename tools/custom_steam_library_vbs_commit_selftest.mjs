import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';

assert.equal(process.argv.length, 4, 'usage: worker.exe fresh-fixture-root');
const worker = fs.realpathSync(process.argv[2]);
const root = path.resolve(process.argv[3]);
assert(!fs.existsSync(root), 'fixture root must be new');
fs.mkdirSync(root);
const data = path.join(root, 'data');
const state = path.join(data, 'state');
fs.mkdirSync(state, {recursive: true});
const games = ['Legacy', 'New A', 'Bad wrapper', 'New B', 'Ordinary', 'Bad legacy mode', 'Bad metadata shape', 'Missing explicit source'].map(directoryName => {
  const gameDirectory = path.join(root, 'games', directoryName);
  const primaryExecutable = path.join(gameDirectory, 'bin', 'Game.exe');
  fs.mkdirSync(path.dirname(primaryExecutable), {recursive: true});
  fs.copyFileSync(worker, primaryExecutable);
  return {gameDirectory, directoryName, primaryExecutable, status: 'ready', selectionSource: 'manual-primary'};
});
const [legacy, first, blocked, second, ordinary, badMode, badShape, missingSource] = games;
const wrapper = game => path.join(game.gameDirectory, 'StartGame.vbs');
const sample = '\uFEFFOption Explicit\r\nWScript.Quit 0\r\n';
fs.writeFileSync(wrapper(second), sample, 'utf16le');
fs.writeFileSync(wrapper(ordinary), sample, 'utf16le');
fs.mkdirSync(wrapper(blocked));
fs.writeFileSync(path.join(state, 'library-scan.json'), JSON.stringify({status:'completed',roots:[],games}));
const overridesPath = path.join(data, 'config', 'manual-overrides.json');
fs.mkdirSync(path.dirname(overridesPath));
const overrides = {schemaVersion:1,items:{}};
function setMode(game, launchConfiguration) {
  overrides.items[game.primaryExecutable] = {launchConfiguration};
  fs.writeFileSync(overridesPath, JSON.stringify(overrides));
}
for (const game of [first, blocked, second]) setMode(game, {mode:'auto'});
setMode(badMode, {mode:'historical-unknown'});
setMode(badShape, ['legacy']);
setMode(missingSource, {mode:'auto',source:path.join(missingSource.gameDirectory,'missing.bat')});

const cstr = value => Buffer.from(value + '\0');
const str = (key, value) => Buffer.concat([Buffer.from([1]), cstr(key), cstr(value)]);
const obj = (key, ...body) => Buffer.concat([Buffer.from([0]), cstr(key), ...body, Buffer.from([8])]);
const appid = 0xf55cba88;
const id = Buffer.alloc(4); id.writeUInt32LE(appid);
const vdf = account => path.join(data, 'sandbox-steam', 'userdata', account, 'config', 'shortcuts.vdf');
const a = vdf('19627'), b = vdf('19628');
const original = Buffer.concat([obj('shortcuts', obj('7', Buffer.from([2]), cstr('appid'), id,
  str('AppName','Original title'),str('Exe',`"${legacy.primaryExecutable}"`),str('StartDir',`"${path.dirname(legacy.primaryExecutable)}"`),
  str('LaunchOptions','--original-args'),str('icon','original.ico'),str('OriginalMarker','opaque'))),Buffer.from([8])]);
for (const target of [a,b]) {
  fs.mkdirSync(path.dirname(target), {recursive:true});
  fs.writeFileSync(target, target===a ? original : Buffer.concat([obj('shortcuts'),Buffer.from([8])]));
}

function inspect(file) {
  const data = fs.readFileSync(file); let at = 0;
  const text = () => { const end=data.indexOf(0,at);assert(end>=at);const value=data.toString('utf8',at,end);at=end+1;return value; };
  const read = () => {
    const row = Object.create(null);
    while (at<data.length) {
      const type=data[at++]; if(type===8)return row;
      const name=text(); assert(!(name in row));
      if(type===0)row[name]=read();
      else if(type===1)row[name]=text();
      else if(type===2){row[name]=data.readUInt32LE(at);at+=4;}
      else throw Error('unexpected field type '+type);
    }
    throw Error('unterminated VDF');
  };
  const doc=read();assert.equal(at,data.length);return Object.values(doc.shortcuts);
}

const reports=[];
function invoke(name,target,selection,commit=false,update=false,expectedExit=0) {
  const output=path.join(state,name+'.json');
  const args=[commit?'--commit-add-library-to-steam':'--plan-add-library-to-steam','--shortcuts',target,'--output',output];
  for(const game of selection)args.push('--select-game',game.gameDirectory);
  if(commit)args.push('--confirm-steam-closed');
  if(update)args.push('--update-vbs-launch');
  const result=spawnSync(worker,args,{encoding:'utf8',timeout:30000,windowsHide:true,cwd:root,
    env:{...process.env,YEMAN_STEAM_BIG_PICTURE_DATA_ROOT:data}});
  fs.writeFileSync(path.join(root,name+'.log'),result.stdout+'\n'+result.stderr);
  assert.equal(result.status,expectedExit,result.error?.message||result.stderr);
  const report=JSON.parse(fs.readFileSync(output,'utf8'));
  assert.equal(report.realSteamTarget,false);
  assert.equal(path.resolve(report.shortcutsVdf),target);
  reports.push(name);
  return report;
}

const advanced=[legacy,first,blocked,second];
const plan=invoke('plan',a,advanced);
assert.deepEqual(fs.readFileSync(a),original);
assert(!fs.existsSync(wrapper(first)),'planning is read-only for game folders');
assert.equal(plan.summary.readyToAdd,3);
const adds=invoke('mixed-add',a,advanced,true);
assert.equal(adds.summary.readyToAdd,2);
assert.equal(adds.summary.launchFailures,1);
assert.equal(adds.items.find(x=>x.gameDirectory===blocked.gameDirectory).status,'launch-file-error');
assert.equal(fs.readFileSync(wrapper(second),'utf16le'),sample,'existing script unchanged');
let rows=inspect(a);
assert.equal(rows.length,3);
assert.equal(rows[0].Exe,`"${legacy.primaryExecutable}"`,'bulk add leaves old launch untouched');
function verify(row,game,target) {
  assert.match(row.Exe,/\\System32\\wscript\.exe"$/i);
  assert.equal(row.LaunchOptions,`//B //NoLogo "${wrapper(game)}"`);
  const dir=row.StartDir.slice(1,-1);
  assert(!fs.existsSync(path.join(dir,'steam_appid.txt')));
  const binding=JSON.parse(fs.readFileSync(path.join(dir,'binding.json'),'utf8'));
  assert.equal(binding.primaryExecutable,game.primaryExecutable);
  assert.equal(binding.script,wrapper(game));
  assert.equal(binding.shortcutAppId,row.appid);
  assert.equal(binding.shortcutsVdf,target);
  return dir;
}
verify(rows.find(row=>row.AppName===first.directoryName),first,a);
verify(rows.find(row=>row.AppName===second.directoryName),second,a);
setMode(legacy, {mode:'auto'});
assert.equal(invoke('update',a,[legacy],true,true).summary.launchUpdates,1);
rows=inspect(a);
assert.equal(rows.length,3);
const old=rows.find(row=>row.appid===appid);
assert.equal(old.AppName,'Original title');assert.equal(old.icon,'original.ico');assert.equal(old.OriginalMarker,'opaque');
const start=verify(old,legacy,a);
const settled=fs.readFileSync(a);
const repeat=invoke('repeat',a,[legacy],true,true);
assert.equal(repeat.committed,false);assert(repeat.items.find(x=>x.gameDirectory===legacy.gameDirectory).launchAlreadyCurrent);
assert.deepEqual(fs.readFileSync(a),settled);
const accountB=invoke('other-account',b,[first],true);
assert.equal(accountB.summary.readyToAdd,1);
verify(inspect(b)[0],first,b);
assert.deepEqual(fs.readFileSync(a),settled,'second account does not touch first');
fs.writeFileSync(path.join(start,'steam_appid.txt'),'1091500');
const dirty=invoke('contaminated-start-in',a,[legacy],true,true,1);
assert.equal(dirty.summary.shortcutIdConflicts,1);
assert.notEqual(dirty.committed,true);
assert.deepEqual(fs.readFileSync(a),settled,'bad start directory retains the original card bytes');
fs.unlinkSync(path.join(start,'steam_appid.txt'));
setMode(legacy, {mode:null});
const retained=invoke('malformed-config-existing-vbs',a,[legacy],true,true);
assert.equal(retained.summary.launchFailures,1);
assert.deepEqual(fs.readFileSync(a),settled,'unknown mode must not convert an existing VBS card');
setMode(legacy, {mode:'direct'});
const quoted = value => '"' + value + '"';
assert.equal(invoke('explicitly-return-to-ordinary',a,[legacy],true,true).summary.launchUpdates,1);
assert.equal(inspect(a).find(row=>row.appid===appid).Exe,quoted(legacy.primaryExecutable));
// Recreate the exact ordinary legacy fields, including arguments YMCC must not reinterpret.
fs.writeFileSync(a,original);
setMode(legacy, {mode:20});
const malformedLegacy=invoke('malformed-config-existing-ordinary',a,[legacy],true,true);
const goodConfigBytes=fs.readFileSync(overridesPath);
const defaults=invoke('ordinary-with-and-without-legacy-metadata',b,[ordinary,badMode,badShape,missingSource],true);
const match=(report,game)=>report.items.find(row=>row.gameDirectory===game.gameDirectory);
const results={
  legacyNoOp:malformedLegacy.summary.launchFailures===0&&match(malformedLegacy,legacy).launchAlreadyCurrent===true,
  legacyBytesRetained:fs.readFileSync(a).equals(original),
  ordinaryAdded:match(defaults,ordinary).status==='added-to-steam',
  changedModeRecovered:match(defaults,badMode).status==='added-to-steam',
  invalidShapeRecovered:match(defaults,badShape).status==='added-to-steam',
  explicitlySelectedMissingSourceRejected:match(defaults,missingSource).status==='launch-file-error'&&!fs.existsSync(wrapper(missingSource)),
  explicitSourceNotRewritten:fs.readFileSync(overridesPath).equals(goodConfigBytes)
};
for(const game of [ordinary,badMode,badShape]) {
  const row=inspect(b).find(entry=>entry.AppName===game.directoryName);
  results[game.directoryName]=row?.Exe===quoted(game.primaryExecutable) &&
    row.StartDir===quoted(path.dirname(game.primaryExecutable)) && row.LaunchOptions==='';
}
assert.equal(fs.readFileSync(wrapper(ordinary),'utf16le'),sample,'ordinary import must leave existing local VBS untouched');
assert(!fs.existsSync(wrapper(badMode))&&!fs.existsSync(wrapper(badShape)));
fs.writeFileSync(path.join(root,'mode-observations.json'),JSON.stringify(results,null,2));
assert.deepEqual(Object.entries(results).filter(([,passed])=>!passed),[]);
fs.writeFileSync(path.join(root,'result.json'),JSON.stringify({ok:true,reports,realSteamWrites:0},null,2));
console.log('VBS_COMMIT_SELFTEST_OK',reports.length);
