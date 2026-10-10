import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { createServer } from 'vite';
import { createSSRApp } from 'vue';
import { renderToString } from 'vue/server-renderer';
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname.replace(/^\/(?:([A-Z]):)/i, '$1:')), '..');
const output = path.resolve(root, '../../Build/Validation/ScreenTouchpads');
const server = await createServer({ root, server: { middlewareMode: true }, appType: 'custom' });
try {
  const { default: Component } = await server.ssrLoadModule('/src/components/ScreenTouchpadsSettings.vue');
  const originalSetup = Component.setup;
  for (const [name, config, expected] of [
    ['disabled', { enabled: false }, ['屏幕触摸板']],
    ['standalone-steam-error', { enabled:false, standaloneSpecialMode:'steamdeck', standaloneSpecialStatus:'steam-menu-unavailable' }, ['Steam 菜单不可用']],
    ['standalone-microphone-error', { enabled:false, standaloneSpecialMode:'ps5', standaloneSpecialStatus:'microphone-mute-failed' }, ['默认麦克风静音失败']],
    ['legacy-xbox-buttons', { enabled: false, specialMask: 3, specialEnabled: true }, ['都开启']],
    ['legacy-rear-buttons', { enabled: false, rearMask: 2, rearEnabled: true }, ['选择组合']],
    ['steamdeck', { enabled: true, leftMode: 'steamdeck', rightMode: 'steamdeck' }, ['左侧映射', '右侧映射', 'SteamDeck 左触摸板 (Steam内设置)', 'SteamDeck 右触摸板 (Steam内设置)', '触摸板缩放', '触摸板显示透明度']],
    ['dual-ps5', { enabled: true, layout: 'dual', leftMode: 'dualsense', rightMode: 'dualsense' }, ['PS5 左触摸板 (Steam内设置)', 'PS5 右触摸板 (Steam内设置)']],
    ['dual-ps4', { enabled: true, layout: 'dual', leftMode: 'dualsense', rightMode: 'dualsense' }, ['PS4 左触摸板 (Steam内设置)', 'PS4 右触摸板 (Steam内设置)']],
    ['single-ps5', { enabled: true, layout: 'single', singleMode: 'dualsense' }, ['触摸板映射', 'PS5触摸板 (Steam内设置)', '触摸板缩放']],
    ['single-keyboard', { enabled: true, layout: 'single', singleMode: 'arrows' }, ['触摸板映射', '键盘 ↑ / ← / ↓ / →']],
    ['wasd-mouse', { enabled: true, leftMode: 'wasd', rightMode: 'mouse' }, ['键盘 W / A / S / D', '模拟鼠标', '触摸板鼠标灵敏度']],
    ['arrows-mouse', { enabled: true, leftMode: 'arrows', rightMode: 'mouse', transparency: 100 }, ['键盘 ↑ / ← / ↓ / →', '100%']],
  ]) {
    // Render the actual compiled SFC and shared controls, changing only its
    // setup refs as an IPC receipt would. No forked mock template.
    const Fixture = { ...Component, setup(props, context) {
      const bindings = originalSetup(props, context);
      assert.ok(bindings.state && bindings.loading, 'Actual SFC setup state must be available');
      bindings.state.value = { ...bindings.state.value, layout: config.enabled ? 'dual' : 'off', ...config, available: true, steamDeckAvailable: true, ps5Available: true, visible: config.enabled };
      bindings.loading.value = false;
      if (props.persona === 'steamdeck' || props.persona === 'dualsense-edge') {
        assert.equal(bindings.rearOptions.value.length, 4, 'Rear menu exposes exactly four choices');
        assert.deepEqual(bindings.rearOptions.value.map(option => option.label), props.persona === 'steamdeck'
          ? ['L5+R5', 'L4+R4', '全开启', '全关闭'] : ['LFN+RFN', 'LB+RB', '全开启', '全关闭']);
      }
      return bindings;
    } };
    const html = await renderToString(createSSRApp(Fixture,{persona: name.startsWith('standalone-')?'disabled':['single-ps5','dual-ps5'].includes(name)?'dualsense-edge':name==='dual-ps4'?'dualshock4':name==='legacy-xbox-buttons'?'elite':'steamdeck',ps5Enabled:true,steamDeckEnabled:true}));
    for (const label of expected) assert.ok(html.includes(label), `${name}: missing ${label}`);
    assert.ok(html.includes('aria-label="屏幕触摸板布局"'));
    for(const label of ['YMCC呼出位置','专用按键组合','背部按键组合'])assert.ok(html.includes('aria-label="'+label+'"'));
    assert.equal((html.match(/class="screen-control-select"/g) || []).length, 3, 'Three controller-accessible dropdowns are always shown');
    assert.ok(!html.includes('role="switch"'));
    assert.ok(!html.includes('屏幕双触摸板'));
    if(name.startsWith('single')){assert.ok(!html.includes('左侧映射'));assert.ok(!html.includes('右侧映射'));}
    if (name === 'disabled') assert.ok(!html.includes('左侧映射'));
    else {
      assert.ok(html.includes('aria-label="触摸板缩放"'), 'Enabled profile must offer scaling');
      assert.ok(html.includes('min="50" max="200" step="5"'), 'Scale bounds and step');
      assert.ok(html.includes('data-gp-accelerate="false"'), 'Scale has exact 5% gamepad/keyboard steps');
    }
    if (name === 'steamdeck') {
      assert.ok(!html.includes('触摸板鼠标灵敏度'));
      assert.ok(html.includes('80%'), 'Default transparency must be 80%');
    }
    for (const removed of ['模拟触摸自测', '键盘模式按区域中心判断方向', '左右区域已显示', 'screen-touchpad-test']) {
      assert.ok(!html.includes(removed), `${name}: removed UI still present: ${removed}`);
    }
    await fs.mkdir(output, { recursive: true });
    await fs.writeFile(path.join(output, `ui-${name}.html`), html, 'utf8');
  }
  console.log('SCREEN_TOUCHPADS_UI_RENDER_OK: actual Vue SFC renders disabled/native/WASD+mouse/arrows+mouse states');
} finally { await server.close(); }
