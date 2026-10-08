// Exercise the production singleton with mocked Audio/native I/O. Never plays
// sound, opens dialogs, or writes the user's music settings.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(path.join(root, 'package.json'));
const vue = require('vue');
const { transformSync } = require('esbuild');
const source = fs.readFileSync(path.join(root, 'src/bridge/music.ts'), 'utf8');
const code = transformSync(source, { loader: 'ts', format: 'cjs' }).code;
const cases = [];
async function check(name, test) {
  await test();
  cases.push(name);
}

function fixture({ mode = 'random', names = ['Song 3.mp3', 'Song 1.mp3', 'Song 2.mp3'], enabled = true } = {}) {
  const calls = { random: 0, get: 0, folder: 0, reload: 0, modes: [] };
  let randomValue = 0.8;
  let audio;
  let rejectPlay = false;
  class FakeAudio {
    preload = '';
    volume = 1;
    muted = false;
    paused = true;
    currentTime = 0;
    playCalls = 0;
    source = null;
    listeners = new Map();
    constructor() { audio = this; }
    get src() { return this.source ?? ''; }
    set src(value) { this.source = value; this.currentTime = 0; }
    getAttribute(name) { return name === 'src' ? this.source : null; }
    removeAttribute(name) { if (name === 'src') this.source = null; }
    addEventListener(event, listener) { this.listeners.set(event, listener); }
    emit(event) { this.listeners.get(event)?.(); }
    play() {
      this.playCalls++;
      if (rejectPlay) return Promise.reject(new Error('simulated media failure'));
      this.paused = false;
      this.emit('play');
      return Promise.resolve();
    }
    pause() {
      const changed = !this.paused;
      this.paused = true;
      if (changed) this.emit('pause');
    }
    load() { this.currentTime = 0; }
  }
  const state = { enabled, folder: 'C:/fixture/music', baseUrl: 'https://music-assets.invalid/', mode, volume: 0.35 };
  const api = {
    music: {
      get: async () => { calls.get++; return state; },
      setFolder: async () => state,
      setMode: async value => { calls.modes.push(value); },
      setVolume: async () => {},
    },
    fs: { readDir: async () => names.map(name => ({ isFile: true, name })) },
    dialog: { openFolder: async () => { calls.folder++; return enabled ? state.folder : null; } },
  };
  const math = Object.create(Math);
  math.random = () => { calls.random++; return randomValue; };
  const module = { exports: {} };
  vm.runInNewContext(code, {
    module, exports: module.exports, Audio: FakeAudio, Math: math,
    window: { location: { reload() { calls.reload++; } } },
    require(name) {
      if (name === 'vue') return vue;
      if (name === './api') return api;
      throw new Error('Unexpected import ' + name);
    },
  });
  return {
    music: module.exports, audio, calls,
    random(value) { randomValue = value; },
    rejectPlay(value) { rejectPlay = value; },
  };
}

await check('Restore saved random mode without auto-playing; first start chooses a random song', async () => {
  const f = fixture();
  await f.music.initMusic();
  assert.equal(f.music.mode.value, 'random');
  assert.equal(f.music.index.value, 0);
  assert.equal(f.music.playing.value, false);
  assert.equal(f.audio.src, '');
  assert.equal(f.audio.playCalls, 0);
  f.music.togglePlay();
  assert.equal(f.music.index.value, 2);
  assert.equal(f.music.currentName.value, 'Song 3.mp3');
  assert.equal(f.audio.src, 'https://music-assets.invalid/Song%203.mp3');
  assert.equal(f.music.playing.value, true);
  assert.equal(f.calls.random, 1);
});

await check('Pause/resume retains both the random song and its playback position', async () => {
  const f = fixture();
  await f.music.initMusic();
  f.music.togglePlay();
  f.audio.currentTime = 42;
  const name = f.music.currentName.value;
  f.music.togglePlay();
  assert.equal(f.music.playing.value, false);
  f.random(0.1);
  f.music.togglePlay();
  assert.equal(f.music.playing.value, true);
  assert.equal(f.music.currentName.value, name);
  assert.equal(f.audio.currentTime, 42);
  assert.equal(f.calls.random, 1);
});

await check('Random start includes every track, including the first and last', async () => {
  const f = fixture();
  await f.music.initMusic();
  for (const [draw, expected] of [[0, 0], [0.5, 1], [0.999999, 2]]) {
    f.music.stop();
    f.random(draw);
    f.music.togglePlay();
    assert.equal(f.music.index.value, expected);
  }
});

await check('Stop, rescan and folder changes all allow a fresh random start', async () => {
  const f = fixture();
  await f.music.initMusic();
  f.music.togglePlay();
  f.music.stop();
  f.random(0.1);
  f.music.togglePlay();
  assert.equal(f.music.index.value, 0);
  await f.music.scanFolder();
  assert.equal(f.music.playing.value, false);
  f.random(0.5);
  f.music.togglePlay();
  assert.equal(f.music.index.value, 1);
  await f.music.chooseFolder();
  assert.equal(f.audio.src, '');
  f.random(0.8);
  f.music.togglePlay();
  assert.equal(f.music.index.value, 2);
  assert.equal(f.calls.folder, 1);
});

await check('Sequential start and pause/resume behavior remain unchanged', async () => {
  const f = fixture({ mode: 'sequential' });
  await f.music.initMusic();
  f.music.togglePlay();
  assert.equal(f.music.index.value, 0);
  f.music.playNext();
  assert.equal(f.music.index.value, 1);
  f.audio.currentTime = 17;
  f.music.togglePlay();
  f.music.togglePlay();
  assert.equal(f.music.index.value, 1);
  assert.equal(f.audio.currentTime, 17);
  assert.equal(f.calls.random, 0);
});

await check('Switching to random before start takes effect; switching while paused does not skip', async () => {
  const f = fixture({ mode: 'sequential' });
  await f.music.initMusic();
  await f.music.setMode('random');
  f.music.togglePlay();
  assert.equal(f.music.index.value, 2);
  f.music.pause();
  await f.music.setMode('sequential');
  await f.music.setMode('random');
  f.random(0.1);
  f.music.togglePlay();
  assert.equal(f.music.index.value, 2);
  assert.equal(f.calls.random, 1);
  assert.deepEqual(f.calls.modes, ['random', 'sequential', 'random']);
});

await check('Random previous/next and automatic song end still choose a different song', async () => {
  const f = fixture();
  await f.music.initMusic();
  f.music.togglePlay();
  f.random(0.5);
  f.music.playNext();
  assert.equal(f.music.index.value, 1);
  f.random(0.1);
  f.music.playPrev();
  assert.equal(f.music.index.value, 0);
  f.random(0.8);
  f.audio.emit('ended');
  assert.equal(f.music.index.value, 2);
});

await check('A single track starts safely; an empty folder never assigns a bogus random index', async () => {
  const one = fixture({ names: ['Only.mp3'] });
  await one.music.initMusic();
  one.music.togglePlay();
  assert.equal(one.music.index.value, 0);
  one.music.playNext();
  one.music.playPrev();
  assert.equal(one.music.index.value, 0);
  const empty = fixture({ names: ['unsupported.txt'] });
  await empty.music.initMusic();
  empty.music.togglePlay();
  assert.equal(empty.music.index.value, -1);
  assert.equal(empty.audio.playCalls, 0);
  assert.equal(empty.calls.random, 0);
  assert.equal(empty.music.error.value, '没有可播放的曲目');
});

await check('Without a configured folder, start opens folder selection rather than playing', async () => {
  const f = fixture({ enabled: false });
  await f.music.initMusic();
  f.music.togglePlay();
  await Promise.resolve();
  assert.equal(f.calls.folder, 1);
  assert.equal(f.audio.playCalls, 0);
  assert.equal(f.calls.random, 0);
});

await check('A rejected play can be retried without unexpectedly selecting another random song', async () => {
  const f = fixture();
  await f.music.initMusic();
  f.rejectPlay(true);
  f.music.togglePlay();
  await Promise.resolve();
  assert.equal(f.music.playing.value, false);
  assert.equal(f.music.index.value, 2);
  f.rejectPlay(false);
  f.random(0.1);
  f.music.togglePlay();
  assert.equal(f.music.index.value, 2);
  assert.equal(f.music.playing.value, true);
  assert.equal(f.calls.random, 1);
});

await check('Every player icon has SVG markup and agrees with the canonical generator and SVG exports', () => {
  const icons = JSON.parse(fs.readFileSync(path.join(root, 'src/icons.json'), 'utf8'));
  const generator = fs.readFileSync(path.join(root, 'tools/gen-icons.mjs'), 'utf8');
  const view = fs.readFileSync(path.join(root, 'src/views/QuickAppView.vue'), 'utf8');
  const begin = view.indexOf('<h3 class="card-title"><InlineIcon name="music"');
  assert(begin >= 0, 'Music card not found');
  const card = view.slice(begin, view.indexOf('</section>', begin));
  const names = new Set();
  for (const [, tag] of card.matchAll(/<InlineIcon\b([^>]+)>/g)) {
    const attribute = tag.match(/:?name="([^"]+)"/);
    assert(attribute, 'Player icon must declare a name');
    if (tag.includes(':name=')) {
      for (const [, name] of attribute[1].matchAll(/'([^']+)'/g)) names.add(name);
    } else {
      names.add(attribute[1]);
    }
  }
  assert.equal(names.size, 9, 'Check both dynamic play/pause and volume/mute icons');
  for (const name of names) {
    assert(icons[name]?.startsWith('<'), `Blank player icon: ${name}`);
    const definition = generator.match(new RegExp('\\b' + name + ': `([^`]+)`'));
    assert(definition, `Icon missing in generator: ${name}`);
    assert.equal(definition[1], icons[name], `Generator differs from registry: ${name}`);
    const svg = fs.readFileSync(path.join(root, 'public/icons', name + '.svg'), 'utf8');
    assert(svg.includes('viewBox="0 0 24 24"') && svg.includes(icons[name]), `SVG export differs: ${name}`);
  }
});

console.log(JSON.stringify({ ok: true, count: cases.length, cases }, null, 2));
