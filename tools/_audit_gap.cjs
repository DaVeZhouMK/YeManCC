const fs = require('fs');
const p = process.argv[2];
const ev = process.argv[3] || 'gamepad-xinput-input';
const s = fs.readFileSync(p, 'utf8');
const arr = [];
let d = 0, cur = '';
for (const l of s.split(/\r?\n/)) {
  if (!l.trim()) continue;
  d += (l.match(/{/g) || []).length - (l.match(/}/g) || []).length;
  cur += l;
  if (d <= 0 && cur.trim()) { try { arr.push(JSON.parse(cur)); } catch {} cur = ''; }
}
const q = arr.filter(x => x.event === ev);
console.log(ev, 'count', q.length);
if (q.length) {
  const first = new Date(q[0].time).getTime();
  const last = new Date(q[q.length - 1].time).getTime();
  console.log('spanMs', last - first, 'first', q[0].time, 'last', q[q.length - 1].time);
  if (q.length > 1) {
    const gaps = [];
    for (let i = 1; i < q.length; i++) gaps.push(new Date(q[i].time).getTime() - new Date(q[i - 1].time).getTime());
    gaps.sort((a, b) => a - b);
    console.log('medianGapMs', gaps[Math.floor(gaps.length / 2)], 'minGapMs', gaps[0], 'p90GapMs', gaps[Math.floor(gaps.length * 0.9)]);
  }
}
