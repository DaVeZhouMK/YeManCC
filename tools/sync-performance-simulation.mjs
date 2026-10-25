/** Import the reviewed standalone simulator without a localhost dependency. */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const source = process.argv[2];
if (!source) throw Error('Provide the reviewed performance preview directory');
const target = path.resolve('public/performance-simulation');
fs.mkdirSync(target, { recursive: true });
const outputs = new Map();
const read = name => fs.readFileSync(path.join(source, name), 'utf8');
let simulation = read('floating-optimization-20261015/floating-lab/index.html');
simulation = simulation.replaceAll("origin='http://127.0.0.1:13132'", 'origin=location.origin')
  .replaceAll("hostOrigin='http://127.0.0.1:13132'", 'hostOrigin=location.origin')
  .replaceAll("frame.src=origin+'/?'+query", "frame.src=origin+'/performance-simulation/prototype.html?'+query");
simulation = simulation.replace('function gamepad(now){', "function gamepad(now){if(new URLSearchParams(location.search).get('nativeHost')==='1')return;");
simulation = simulation.replace("else if(action==='command'&&['a','b','left','right','up','down'].includes(command))", "else if(action==='command'&&['slider-left','slider-right'].includes(command)){if(armed)press(command==='slider-left'?'left':'right');}else if(action==='command'&&['a','b','left','right','up','down'].includes(command))");
outputs.set('index.html', simulation);
let engine = read('app.js').replaceAll("clientOrigin='http://127.0.0.1:13133'", 'clientOrigin=location.origin')
  .replaceAll("simulationOrigin='http://127.0.0.1:13133'", 'simulationOrigin=location.origin');
// The hidden draft engine accepts the same 1W lower bound as continuous controls.
engine = engine.replace('unknown:{min:5,', 'unknown:{min:1,');
outputs.set('app.js', engine);
outputs.set('prototype.html', read('index.html'));
outputs.set('style.css', read('style.css'));
outputs.set('schedule-controls.css', read('schedule-controls.css'));
const manifest = [];
for (const [name, text] of outputs) {
  fs.writeFileSync(path.join(target, name), text);
  manifest.push({ name, bytes: Buffer.byteLength(text), sha256: crypto.createHash('sha256').update(text).digest('hex') });
}
fs.writeFileSync(path.join(target, 'manifest.json'), JSON.stringify({ source: 'reviewed-continuous-performance-preview', draftOnly: true, files: manifest }, null, 2) + '\n');
console.log(JSON.stringify({ target, files: manifest.length, localServerRequired: false }));
