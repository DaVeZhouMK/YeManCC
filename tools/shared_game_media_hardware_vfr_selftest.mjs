import assert from 'node:assert/strict';
import {existsSync, mkdirSync, writeFileSync} from 'node:fs';
import {join, resolve} from 'node:path';
import {spawnSync} from 'node:child_process';

const [tool, ffmpeg, ffprobe, directory, ...adapters] = process.argv.slice(2);
if (!directory || !adapters.length || !adapters.every(x => /^[01]$/.test(x))) {
  throw new Error('Usage: node shared_game_media_hardware_vfr_selftest.mjs <hardware-selftest.exe> <ffmpeg.exe> <ffprobe.exe> <fresh-output-dir> <adapter [adapter]>');
}
const out = resolve(directory);
mkdirSync(out);
const run = (exe, args) => {
  const result = spawnSync(resolve(exe), args, {encoding: 'utf8', timeout: 70000, windowsHide: true});
  if (result.error) throw result.error;
  return {status: result.status, output: result.stdout + result.stderr, stdout: result.stdout};
};
const ok = (exe, args) => {
  const result = run(exe, args);
  assert.equal(result.status, 0, result.output);
  return result;
};
const probe = path => JSON.parse(ok(ffprobe, ['-v', 'error', '-select_streams', 'v:0',
  '-show_entries', 'stream=width,height,avg_frame_rate,duration,nb_frames', '-of', 'json', path]).stdout).streams[0];
const short = "select='lt(t,2)+gte(t,3)+not(mod(n,6))'";
const cases = [
  {name: 'interior-gap', size: '1280x720', filter: short, count: 250},
  {name: 'scaled-gap', size: '1920x1080', filter: short, count: 250},
  {name: 'fallback-480p-gap', size: '854x480', filter: short, count: 250, outputSize: [854, 480]},
  {name: 'long-gap', size: '1280x720', filter: "select='lt(t,2)+gte(t,3.5)'", count: 210,
    fail: /media-hardware-source-(timestamp-gap|discontinuity)/},
  {name: 'cancel', source: 'interior-gap', extra: ['cancel'], fail: /fixture-cancelled/},
];
const results = [];
for (const test of cases) {
  const source = join(out, `${test.source ?? test.name}.mp4`);
  if (!test.source) {
    ok(ffmpeg, ['-hide_banner', '-loglevel', 'error', '-nostdin', '-n', '-f', 'lavfi',
      '-i', `testsrc2=size=${test.size}:rate=60:duration=5`, '-an', '-vf', test.filter,
      '-vsync', 'vfr', '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-preset', 'veryfast', source]);
    const input = probe(source);
    assert.equal(Number(input.nb_frames), test.count);
    assert.equal(Number(input.duration), 5);
  }
  for (const adapter of new Set(adapters)) {
    const target = join(out, `${test.name}-${adapter}-output.mp4`);
    const result = run(tool, [source, target, adapter, ...(test.extra ?? [])]);
    if (test.fail) {
      assert.equal(result.status, 1, result.output);
      assert.match(result.output, test.fail);
      assert(!existsSync(target), 'rejected conversion left an output');
      results.push({case: test.name, adapter, rejected: result.output.trim()});
      continue;
    }
    assert.equal(result.status, 0, result.output);
    assert.match(result.output, /hardware decoder and encoder verified/);
    const output = probe(target);
    const [width, height] = test.outputSize ?? [1280, 720];
    assert.deepEqual([output.width, output.height, output.avg_frame_rate, Number(output.duration), Number(output.nb_frames)],
      [width, height, '30/1', 5, 150]);
    const comparison = ok(ffmpeg, ['-hide_banner', '-nostdin', '-i', source, '-i', target, '-an',
      '-filter_complex', `[0:v]fps=30:round=up,scale=${width}:${height},setsar=1[ref];[ref][1:v]ssim`, '-f', 'null', '-']);
    const similarity = Number(comparison.output.match(/SSIM .* All:([\d.]+)/)?.[1]);
    assert(similarity >= 0.97, `frame contents diverged from time-aligned hold reference: ${similarity}`);
    results.push({case: test.name, adapter, output, similarity});
  }
}
writeFileSync(join(out, 'results.json'), JSON.stringify(results, null, 2) + '\n');
console.log(`PASS ${results.length} hardware VFR checks`);
