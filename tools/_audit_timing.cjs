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
const q = arr.filter(x => x.event === 'input-host-command-result' && x.command === 'HIDHIDE_QUERY');
const ts = q.map(x => new Date(x.time).getTime());
if (!ts.length) { console.log('no HIDHIDE_QUERY'); process.exit(0); }
const t0 = ts[0];
// bucket by 5s
const buckets = {};
for (const t of ts) { const b = Math.floor((t - t0) / 5000) * 5; buckets[b] = (buckets[b] || 0) + 1; }
console.log('total', ts.length, 'spanMs', ts[ts.length - 1] - t0);
for (const k of Object.keys(buckets).map(Number).sort((a, b) => a - b)) {
  console.log('+'+k+'s .. +'+(k+5)+'s : ' + buckets[k]);
}
