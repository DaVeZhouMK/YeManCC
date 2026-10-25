import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),source=path.join(root,'src/components/MonitorHudTemplate.vue'),target=path.join(root,'public/performance-simulation/monitor-hud-base.css');
const text=fs.readFileSync(source,'utf8'),style=text.match(/<style scoped>([\s\S]*?)<\/style>/)?.[1];if(!style)throw Error('Production monitor styles missing');
const start=style.indexOf('.ymcc-hud {'),end=style.indexOf('.hud-chart {',start);if(start<0||end<0)throw Error('Production monitor core boundary missing');
const core=style.slice(start,end).trim()+'\n',sha=crypto.createHash('sha256').update(core).digest('hex');
const generated='/* Generated from src/components/MonitorHudTemplate.vue; use tools/sync_simulation_monitor_style.mjs.\n   Shared core typography only; no sensor connections. Core SHA256: '+sha+' */\n'+core;
if(process.argv.includes('--check')){if(!fs.existsSync(target)||fs.readFileSync(target,'utf8')!==generated)throw Error('Simulation monitor styles differ from production; run the sync tool');}else fs.writeFileSync(target,generated);
console.log(JSON.stringify({file:path.relative(root,target).replaceAll('\\','/'),coreSha256:sha,check:process.argv.includes('--check')},null,2));
