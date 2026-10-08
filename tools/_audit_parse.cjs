const fs = require('fs');
const p = process.argv[2];
const s = fs.readFileSync(p, 'utf8');
const arr = [];
let d = 0, cur = '';
for (const l of s.split(/\r?\n/)) {
  if (!l.trim()) continue;
  d += (l.match(/{/g) || []).length - (l.match(/}/g) || []).length;
  cur += l;
  if (d <= 0 && cur.trim()) { try { arr.push(JSON.parse(cur)); } catch {} cur = ''; }
}
const c = arr.filter(x => x.event === 'input-host-command-result');
const byCmd = {};
for (const x of c) { const k = x.command || '?'; byCmd[k] = (byCmd[k] || 0) + 1; }
console.log('command-result by command:', JSON.stringify(byCmd));
const byPhase = {};
for (const x of c) { const k = (x.phase || '?') + '/' + (x.ok === false ? 'fail' : 'ok'); byPhase[k] = (byPhase[k] || 0) + 1; }
console.log('command-result by phase/ok:', JSON.stringify(byPhase));
const br = arr.filter(x => x.event === 'input-capture-bus-rate');
if (br.length) {
  console.log('bus-rate sample:', JSON.stringify(br[0]));
  console.log('bus-rate last:', JSON.stringify(br[br.length - 1]));
}
