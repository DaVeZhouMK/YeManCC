// Actual SettingsView + wallpaper component + settings repository; only native IO is memory-backed.
import assert from 'node:assert/strict';
const persisted = f => f.evaluate(() => {
  const call = window.__previewCalls.filter(c => c.cmd === 'settings.write').at(-1);
  const ui = call ? JSON.parse(call.args.content).ui : {};
  return { x: ui?.backgroundPositionX ?? 50, y: ui?.backgroundPositionY ?? 50 };
});
export async function backgroundPreviewCases({ f, height, press, actionCase, command }) {
  const frame = f.locator('.wallpaper-preview-frame');
  await press(f, f.locator('[data-settings-section="appearance"]'));
  await actionCase('Wallpaper preview uses lightweight motion without recursive UI or default video', height, async () => {
    await frame.scrollIntoViewIfNeeded();
    assert.equal(await f.locator('.background-effect-preview .mini-ymcc-shell,.background-effect-preview .navrail,.background-effect-preview .continuous-performance').count(), 0);
    assert.equal(await frame.locator('video').count(), 0, 'no implicit sample-video playback');
    assert.equal(await frame.locator('.wallpaper-preview-ambient').count(), 1);
    assert.equal(await frame.locator('.wallpaper-preview-label').innerText(), '动态预览');
    await f.waitForFunction(() => getComputedStyle(document.querySelector('.wallpaper-preview-ambient span')).animationPlayState === 'running');
    await f.evaluate(() => window.dispatchEvent(new CustomEvent('ipc:window.hidden')));
    await f.waitForFunction(() => getComputedStyle(document.querySelector('.wallpaper-preview-ambient span')).animationPlayState === 'paused');
    await f.evaluate(() => window.dispatchEvent(new CustomEvent('ipc:window.shown')));
  });
  await actionCase('Wallpaper preview plays selected media and commits drag position only on release', height, async () => {
    await f.evaluate(() => {
      window.__fixture.selectedBackground = '/memory/wallpaper.mp4';
      window.__fixture.selectedBackgroundState = { enabled: true, kind: 'video', file: '/memory/wallpaper.mp4', url: new URL('/settings-preview/SteamDeck.mp4', location.href).href };
    });
    await press(f, f.getByRole('button', { name: '选择背景', exact: true }));
    await frame.scrollIntoViewIfNeeded();
    await f.waitForFunction(() => { const v = document.querySelector('.wallpaper-preview-frame video'); return v && v.videoWidth === 1920 && !v.paused && v.currentTime > 0; });
    const box = await frame.boundingBox(), mouse = f.page().mouse, before = await persisted(f);
    await mouse.move(box.x + box.width * .65, box.y + box.height * .45); await mouse.down();
    await mouse.move(box.x + box.width * .35, box.y + box.height * .45, { steps: 6 });
    assert.deepEqual(await persisted(f), before, 'drag preview must not write every frame');
    await mouse.up();
    await f.waitForFunction(() => { const call = window.__previewCalls.filter(c => c.cmd === 'settings.write').at(-1); return call && JSON.parse(call.args.content).ui.backgroundPositionX > 50; });
    const after = await persisted(f); assert(after.x > 50); assert.equal(after.y, 50);
    assert.equal(await frame.locator('video').evaluate(v => v.style.objectPosition), `${after.x}% ${after.y}%`);
  });
  await actionCase('Wallpaper preview cancels pointer drafts and supports keyboard and gamepad recenter', height, async () => {
    const before = await persisted(f), box = await frame.boundingBox(), mouse = f.page().mouse;
    await mouse.move(box.x + box.width * .5, box.y + box.height * .5); await mouse.down();
    await mouse.move(box.x + box.width * .75, box.y + box.height * .5, { steps: 4 });
    await frame.dispatchEvent('pointercancel', { pointerId: 1 }); await mouse.up();
    assert.deepEqual(await persisted(f), before, 'cancel must not commit draft');
    assert.equal(await frame.locator('video').evaluate(v => v.style.objectPosition), `${before.x}% ${before.y}%`);
    const trigger = frame.getByRole('button', { name: '动态预览，按 A 调整壁纸位置', exact: true });
    await press(f, trigger); await trigger.press('ArrowLeft'); await command(f, 'confirm');
    await f.waitForFunction(expected => { const c = window.__previewCalls.filter(c => c.cmd === 'settings.write').at(-1); return c && Math.abs(JSON.parse(c.args.content).ui.backgroundPositionX - expected) < .05; }, before.x - 2);
    await press(f, f.locator('.wallpaper-preview-position').getByRole('button', { name: '居中', exact: true }));
    await f.waitForFunction(() => { const c = window.__previewCalls.filter(c => c.cmd === 'settings.write').at(-1); return c && JSON.parse(c.args.content).ui.backgroundPositionX === 50; });
    assert.deepEqual(await persisted(f), { x: 50, y: 50 });
  });
  await actionCase('Wallpaper preview gamepad owns both axes, A saves and B cancels', height, async () => {
    const trigger = frame.getByRole('button', { name: '动态预览，按 A 调整壁纸位置', exact: true });
    const before = await persisted(f);
    await press(f, trigger); await command(f, 'nav-right'); await command(f, 'nav-down');
    assert.equal(await trigger.getAttribute('aria-pressed'), 'true');
    assert.deepEqual(await persisted(f), before, 'gamepad draft must not persist before A');
    await command(f, 'page-next'); assert.equal(await f.evaluate(() => window.__currentAuditRoute()), '/settings');
    await command(f, 'back'); assert.equal(await trigger.getAttribute('aria-pressed'), 'false');
    assert.deepEqual(await persisted(f), before);
    assert.equal(await frame.locator('video').evaluate(v => v.style.objectPosition), '50% 50%');
    await press(f, trigger); await command(f, 'nav-right'); await command(f, 'nav-down'); await command(f, 'confirm');
    await f.waitForFunction(() => { const c = window.__previewCalls.filter(c => c.cmd === 'settings.write').at(-1); const ui = c && JSON.parse(c.args.content).ui; return ui?.backgroundPositionX === 52 && ui?.backgroundPositionY === 52; });
    await press(f, f.locator('.wallpaper-preview-position').getByRole('button', { name: '居中', exact: true }));
    await f.waitForFunction(() => { const c = window.__previewCalls.filter(c => c.cmd === 'settings.write').at(-1); return c && JSON.parse(c.args.content).ui.backgroundPositionX === 50; });
  });
  await actionCase('Wallpaper preview pauses hidden media and releases it when settings tab closes', height, async () => {
    await f.evaluate(() => { window.__ownedWallpaperVideo = document.querySelector('.wallpaper-preview-frame video'); window.dispatchEvent(new CustomEvent('ipc:window.hidden')); });
    await f.waitForFunction(() => window.__ownedWallpaperVideo.paused);
    await f.evaluate(() => window.dispatchEvent(new CustomEvent('ipc:window.shown')));
    await f.waitForFunction(() => !window.__ownedWallpaperVideo.paused);
    await press(f, f.locator('[data-settings-section="diagnostics"]'));
    await f.waitForFunction(() => window.__ownedWallpaperVideo.paused && !window.__ownedWallpaperVideo.isConnected);
    await press(f, f.locator('[data-settings-section="appearance"]'));
    await frame.waitFor(); await frame.scrollIntoViewIfNeeded();
    await f.waitForFunction(() => { const v = document.querySelector('.wallpaper-preview-frame video'); return v && !v.paused; });
    await press(f, f.getByRole('button', { name: '清除背景', exact: true }));
    await frame.locator('.wallpaper-preview-ambient').waitFor();
    assert.equal(await frame.locator('video').count(), 0);
  });
}
