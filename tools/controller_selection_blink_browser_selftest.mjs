import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
import { buildEditorPreview } from './controller_shortcut_editor_preview.mjs';

const out = process.env.YMCC_BLINK_OUT || 'G:/YeManCC-Work/Build/Tasks/ControllerShortcutBlinkFix-20261017';
const { chromium } = createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_task.cjs')('playwright');
await buildEditorPreview({ out: path.join(out, 'actual-editor') });
const checks = [], errors = [];
const check = (value, name) => { assert(value, name); checks.push(name); };
const browser = await chromium.launch({ headless: true, executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe' });
let page;
const selectedFill = code => `.device-vector [data-fill-for~="${code}"][data-fill-active=true]`;
try {
  page = await browser.newPage({ viewport: { width: 1280, height: 720 }, deviceScaleFactor: 1, reducedMotion: 'no-preference' });
  page.on('pageerror', error => errors.push(error.message));
  await page.goto(pathToFileURL(path.join(out, 'actual-editor/editor.html')).href);
  await page.locator('.rule-row').first().waitFor();
  await page.evaluate(async () => {
    const preview = window.__editorPreview;
    const spec = [
      ['mouse.toggle', {}, [['controller', 'back'], ['controller', 'y']]],
      ['fan.switchPreset', { preset: 'next' }, [['controller', 'leftThumb']]],
      ['power.tdpAdjust', { direction: 'up', amount: 1 }, [['oem', 'm2']]],
      ['input.frontendButton', { button: 'steam' }, [['oem', 'claw']]],
      ['monitor.toggle', { operation: 'toggle' }, [['keyboard', 'ControlLeft'], ['keyboard', 'KeyK']]],
      ['display.frameRateControl', { operation: 'set', fps: '60' }, [['controller', 'a'], ['keyboard', 'F14']]],
      ['keyboard.letter', { key: 'KeyJ' }, [['keyboard', 'KeyK']]],
    ];
    for (const key of Object.keys(preview.settings.gamepad)) if (typeof preview.settings.gamepad[key] === 'boolean') preview.settings.gamepad[key] = false;
    preview.settings.input.buttonMapping.rules.shortcutRules = spec.map(([actionId, params, inputs], index) => ({
      id: 'blink-' + index, origin: 'custom', enabled: true, actionId, params,
      inputs: inputs.map(([source, code]) => ({ source, code })), trigger: 'press', holdMs: 500, doubleWindowMs: 500, intervalMs: 150,
    }));
    await preview.setCatalog('two');
  });
  await page.locator('.rule-row').first().waitFor();
  check(await page.locator('.rule-row').count() === 7, 'only fixture rules are displayed; all operations stay in memory');
  const initialWrites = await page.evaluate(() => [window.__editorPreview.writes, window.__editorPreview.nativeWrites]);
  async function select(index) { await page.locator('.rule-row').nth(index).click(); await page.locator('.action-manage-cancel').click(); await page.mouse.move(1, 1); await page.evaluate(() => document.activeElement.blur()); }
  async function animationChanges(locator, property, name) {
    const result = await locator.first().evaluate((node, property) => {
      const animation = node.getAnimations()[0];
      if (!animation) return { animated: false };
      const timing = animation.effect.getTiming();
      animation.pause(); animation.currentTime = 0;
      const first = getComputedStyle(node).getPropertyValue(property);
      animation.currentTime = Number(timing.duration) / 2;
      const second = getComputedStyle(node).getPropertyValue(property);
      const rect = node.getBoundingClientRect();
      const stable = { x: rect.x, y: rect.y, w: rect.width, h: rect.height };
      animation.currentTime = 0;
      const again = node.getBoundingClientRect();
      animation.play();
      return { animated: true, first, second, infinite: timing.iterations === Infinity, duration: timing.duration,
        stable: stable.x === again.x && stable.y === again.y && stable.w === again.width && stable.h === again.height,
        filter: getComputedStyle(node).filter };
    }, property);
    check(result.animated && result.infinite && result.duration === 850, name + ' uses the original continuous 850ms blink');
    check(result.first !== result.second, name + ' changes its fill across the actual animation');
    check(result.stable && result.filter === 'none', name + ' preserves geometry without adding a shadow');
  }

  // Preview must react to hovering or controller focus without clicking or editing a rule.
  await select(0);
  const editingBefore = await page.locator('.output-summary b').innerText();
  const savedBefore = await page.evaluate(() => JSON.stringify(window.__editorPreview.settings));
  await page.locator('.rule-row').nth(1).hover();
  check(await page.locator(selectedFill('leftThumb')).first().evaluate(n => n.getAnimations().length > 0), 'editor mouse hover blinks the pointed rule without clicking');
  check(await page.locator('.rule-row.active').innerText().then(t => t.includes('模拟鼠标')) && await page.locator('.output-summary b').innerText() === editingBefore, 'hover leaves the actual editing target unchanged');
  check(await page.locator('.rule-context-popover').count() === 0, 'hover never opens rule management');
  await page.locator('.rule-row').nth(2).focus();
  check(await page.locator(selectedFill('L5')).first().evaluate(n => n.getAnimations().length > 0), 'controller focus takes over the preview while the mouse remains on another rule');
  await page.locator('.rule-row').nth(3).focus();
  check(await page.locator('.oem-capsule[data-oem-key="claw"]').evaluate(n => n.classList.contains('mapped') && n.getAnimations().length > 0), 'controller focus previews the dedicated handheld input bubble');
  await page.mouse.move(1, 1); await page.locator('.rule-row').nth(0).hover();
  check(await page.locator(selectedFill('y')).first().evaluate(n => n.getAnimations().length > 0), 'mouse hover can take the preview back after controller navigation');
  await page.locator('.rule-row').nth(4).focus();
  check(await page.locator('.mirror-stage .keyboard-preview path[data-key-code="KeyK"]').evaluate(n => n.getAttribute('data-selected') === 'true' && n.getAnimations().length > 0), 'controller focus previews the keyboard rule with blinking keys');
  await page.mouse.move(1, 1);
  await page.evaluate(() => document.activeElement.blur());
  check(await page.locator(selectedFill('y')).first().evaluate(n => n.getAnimations().length > 0), 'leaving hover and focus returns to the rule being edited');
  await page.locator('.rule-row').nth(5).hover();
  check(await page.locator(selectedFill('a')).first().evaluate(n => n.getAnimations().length > 0) && await page.locator('.handheld-key-bubbles span').evaluate(n => n.getAnimations().length > 0), 'hover previews controller and F14 together through the existing mixed-input chain');
  check(await page.evaluate(() => JSON.stringify(window.__editorPreview.settings)) === savedBefore, 'mouse and controller preview changes preserve all draft/settings data');
  await select(1); await page.locator('.rule-row').nth(1).click();
  await page.locator('.rule-row').nth(0).hover();
  check(await page.locator(selectedFill('leftThumb')).first().evaluate(n => n.getAnimations().length > 0), 'open management stays attached to its actual edit rule during hover');
  await page.locator('.action-manage-cancel').click();
  await page.locator('.new-action').click(); await page.locator('.record-action').click();
  await page.evaluate(() => window.__editorPreview.emit('shortcut.recording.gamepad', { inputs: [{ source: 'controller', code: 'a' }], released: false }));
  await page.locator('.rule-row').nth(0).dispatchEvent('mouseenter');
  check(await page.locator(selectedFill('a')).first().evaluate(n => n.getAnimations().length > 0), 'recording stays on its live input instead of a hovered rule');
  await page.locator('.record-action').click(); await page.locator('.creation-cancel').click();
  await select(0);

  for (const [label, model] of [['Steam Deck', 'steamdeck'], ['PS5', 'ps5'], ['Xbox', 'xbox']]) {
    await page.locator('.preview-selector [aria-haspopup=listbox]').click();
    await page.getByRole('option', { name: label, exact: true }).click();
    await select(0);
    check(await page.locator('.device-diagram').getAttribute('data-diagram-persona') === model, label + ' uses its shared production artwork');
    const diagramBefore = await page.locator('.device-diagram').boundingBox();
    await animationChanges(page.locator(selectedFill('y')), 'fill', label + ' associated Y button');
    await animationChanges(page.locator(selectedFill('back')), 'fill', label + ' associated Back button');
    const liveShape = page.locator(selectedFill('y')).first();
    const geometry = await liveShape.getAttribute('d');
    const glyphs = await page.locator('.device-vector [data-fill-for]').evaluateAll(nodes => nodes.filter(n => n.getAttribute('data-fill-active') !== 'true').every(n => !n.getAnimations().length));
    check(glyphs, label + ' unassociated buttons remain still');
    const held = Array(18).fill(false); held[3] = true;
    await page.evaluate(buttons => window.__editorPreview.state({ buttons }), held);
    await page.waitForFunction(() => document.querySelector('[data-fill-for~="y"][data-fill-live=true]'));
    check(await liveShape.evaluate(n => !n.getAnimations().length && getComputedStyle(n).fill === 'rgb(72, 151, 210)'), label + ' actual press overrides blinking with solid fill');
    check(await liveShape.getAttribute('d') === geometry, label + ' actual press preserves the original shape');
    await page.evaluate(() => window.__editorPreview.state({}));
    check(await liveShape.getAttribute('data-fill-live') === 'true', label + ' minimum short press visibility remains intact');
    await page.waitForFunction(() => document.querySelector('[data-fill-for~="y"][data-fill-live=false]'));
    check(await liveShape.evaluate(n => n.getAnimations().length > 0), label + ' selected blink resumes after real press release');
    await select(1);
    check(await page.locator('.device-vector [data-fill-for~="y"]').first().evaluate(n => !n.getAnimations().length), label + ' previous rule stops blinking when selection changes');
    await animationChanges(page.locator(selectedFill('leftThumb')), 'fill', label + ' selected stick click');
    const cap = page.locator('.device-vector [data-stick="0"]').first(), neutral = await cap.getAttribute('transform');
    await page.evaluate(() => window.__editorPreview.state({ axes: [.75, -.5, 0, 0] }));
    check(await cap.getAttribute('transform') !== neutral, label + ' real stick displacement remains functional during blinking');
    await page.evaluate(() => window.__editorPreview.state({}));
    await select(2);
    await animationChanges(page.locator(selectedFill(model === 'steamdeck' ? 'L5' : 'L4')), 'fill', label + ' recognized rear button');
    check(await page.locator('.device-vector [data-fill-for="' + (model === 'steamdeck' ? 'L4' : 'L5') + '"]').first().evaluate(n => !n.getAnimations().length), label + ' absent extra rear button never blinks');
    const diagramAfter = await page.locator('.device-diagram').boundingBox();
    check(['x', 'y', 'width', 'height'].every(key => Math.abs(diagramBefore[key] - diagramAfter[key]) < .1), label + ' rule selection keeps central controller fixed');
  }
  await select(3);
  const oem = page.locator('.oem-capsule[data-oem-key="claw"]');
  await animationChanges(oem, 'background-color', 'dedicated handheld button bubble');
  await page.evaluate(() => window.__editorPreview.state({ oemPressedKeys: ['claw'] }));
  check(await oem.evaluate(n => n.classList.contains('live') && !n.getAnimations().length), 'actual dedicated button press keeps solid fill');
  await page.evaluate(() => window.__editorPreview.state({}));
  await select(4);
  const keyboard = page.locator('.mirror-stage .keyboard-preview');
  await animationChanges(keyboard.locator('path[data-key-code="KeyK"]'), 'fill', 'associated keyboard key');
  check(await keyboard.locator('path[data-key-code="KeyL"]').evaluate(n => !n.getAnimations().length), 'unassociated keyboard key remains still');
  const textBefore = await keyboard.locator('.keyboard-asset').evaluate(n => [...n.querySelectorAll('path:not([data-key-code])')].map(n => n.outerHTML).join(''));
  await page.keyboard.down('k');
  check(await keyboard.locator('path[data-key-code="KeyK"]').evaluate(n => n.getAttribute('data-live') === 'true' && !n.getAnimations().length), 'actual keyboard press keeps solid fill over selected animation');
  check(await keyboard.locator('.keyboard-asset').evaluate(n => [...n.querySelectorAll('path:not([data-key-code])')].map(n => n.outerHTML).join('')) === textBefore, 'keyboard glyph outlines are unchanged');
  await page.keyboard.up('k');
  await page.waitForFunction(() => document.querySelector('.mirror-stage path[data-key-code="KeyK"][data-live=false]'));
  await select(5);
  await animationChanges(page.locator('.handheld-key-bubbles span'), 'background-color', 'mixed handheld F14 bubble');
  await page.evaluate(() => window.dispatchEvent(new KeyboardEvent('keydown', { code: 'F14', key: 'F14', bubbles: true })));
  check(await page.locator('.handheld-key-bubbles span').evaluate(n => n.classList.contains('live') && !n.getAnimations().length), 'actual F14 press uses solid bubble fill');
  await page.evaluate(() => window.dispatchEvent(new KeyboardEvent('keyup', { code: 'F14', key: 'F14', bubbles: true })));
  await select(6);
  await page.locator('.output-summary').click();
  await animationChanges(page.locator('.keyboard-output-dialog path[data-key-code="KeyJ"]'), 'fill', 'shared output keyboard selected key');
  await page.keyboard.press('Escape');
  for (const height of [720, 800]) {
    await page.setViewportSize({ width: 1280, height });
    await select(0);
    check(await page.locator('.controller-preview-frame.match-deck-height').count() === 0, height + 'p editor keeps its original preview sizing');
    const box = await page.locator('.device-diagram').boundingBox();
    check(box.x >= 0 && box.y >= 0 && box.x + box.width <= 1280 && box.y + box.height <= height, height + 'p keeps whole controller visible');
    check(await page.locator('.editor-global-actions button').evaluateAll(ns => ns.every(n => n.offsetWidth === 112 && n.offsetHeight === 40)), height + 'p keeps original button proportions');
    await page.screenshot({ path: path.join(out, 'blink-' + height + '.png') });
  }
  await page.emulateMedia({ reducedMotion: 'reduce' });
  await page.waitForFunction(() => getComputedStyle(document.querySelector('[data-fill-for~="y"][data-fill-active=true]')).animationName === 'none');
  check(await page.locator(selectedFill('y')).first().evaluate(n => !n.getAnimations().length && n.getAttribute('data-fill-active') === 'true'), 'reduced motion keeps static selected controller fill');
  await select(3);
  check(await oem.evaluate(n => !n.getAnimations().length && n.classList.contains('mapped')), 'reduced motion keeps static dedicated button selection');
  await select(4);
  check(await keyboard.locator('path[data-key-code="KeyK"]').evaluate(n => !n.getAnimations().length && n.getAttribute('data-selected') === 'true'), 'reduced motion keeps static keyboard selection');
  await select(5);
  check(await page.locator('.handheld-key-bubbles span').evaluate(n => !n.getAnimations().length), 'reduced motion disables mixed-key bubble motion');
  check(JSON.stringify(await page.evaluate(() => [window.__editorPreview.writes, window.__editorPreview.nativeWrites])) === JSON.stringify(initialWrites), 'selection animation performs no settings or native writes');
  check(errors.length === 0, 'no browser exceptions');
  fs.writeFileSync(path.join(out, 'BROWSER-ACCEPTANCE.json'), JSON.stringify({ checks, errors, actualSettingsWritten: false, nativeInputInjected: false }, null, 2));
  console.log('PASS ' + checks.length + ' controller/keyboard selection blink regression checks');
} catch (error) {
  if (page) { await page.screenshot({ path: path.join(out, 'failure.png') }); fs.writeFileSync(path.join(out, 'failure.html'), await page.content()); }
  throw error;
} finally { await browser.close(); }
