import fs from 'node:fs';
const [input,output]=process.argv.slice(2);if(!input||!output)throw Error('usage: analysis.json output.json');
const sessions=JSON.parse(fs.readFileSync(input,'utf8').replace(/^\uFEFF/,''));const pairs=[];
for(const session of sessions){
 const events=session.events.filter(e=>e.pid===session.pid&&e.file==='native-lifecycle.log').sort((a,b)=>a.time.localeCompare(b.time)||a.line-b.line);
 for(const t of events.filter(e=>e.event==='input-host-persona-transition')){
  const within=events.filter(e=>e.time>=t.time&&Date.parse(e.time)-Date.parse(t.time)<=2200);
  const m1=within.find(e=>e.event==='msi-claw-gamepad-mode'&&e.action==='restore-xinput'&&e.mode===1&&e.reason==='input-host-stop'&&e.sent===true);
  const m2=m1&&within.find(e=>e.event==='msi-claw-gamepad-mode'&&e.action==='switch-dinput'&&e.mode===2&&e.time>m1.time&&e.sent===true);
  if(!m1||!m2)throw Error('persona transition without measured self mode pair');
  const source=events.filter(e=>e.event==='msi-main-source-record'&&e.phase==='published'&&e.time<t.time).at(-1);
  const stop=within.find(e=>e.event==='input-host-stop-result'&&e.stopReason==='persona-changed');
  const lost=within.find(e=>e.event==='msi-dinput-transition'&&e.phase==='input-lost'&&e.time>m2.time);
  pairs.push({pack:session.pack,pid:session.pid,nativeSha256:session.manifest.nativeSha256,from:t.from,to:t.to,transitionTime:t.time,oldHostEpoch:m1.hostEpoch,powerGeneration:m1.powerGeneration,sourceGeneration:source?.generation,sourceRoot:source?.usbInstance,mode1Time:m1.time,mode2Time:m2.time,pairGapMs:Date.parse(m2.time)-Date.parse(m1.time),localReleaseAck:stop?.localReleaseAck,visibilityHeld:stop?.visibilityHeld,lostTime:lost?.time||null,lines:{transition:t.line,mode1:m1.line,mode2:m2.line}});
 }
}
if(pairs.length!==4||pairs.some(p=>p.pairGapMs>30||!p.sourceRoot||!p.oldHostEpoch))throw Error('unexpected real pair fixture');
fs.writeFileSync(output,JSON.stringify({fixtureVersion:1,scope:'measured YMCC mode writes and persona types; offline replacement tests provide synthetic local release/source health, not proof of hardware causation',pairs},null,2));console.log('REAL_PERSONA_PAIRS '+pairs.length+' '+pairs.map(p=>p.pairGapMs).join(','));
