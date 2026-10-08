import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { residentNavigationRoutes } from '../src/bridge/residentNavigation';
const routes = [
  { path: '/schedule' }, { path: '/fan', feature: 'fan' },
  { path: '/tdp' }, { path: '/cpu' },
  { path: '/gyro-motion', feature: 'gyro-motion' },
  { path: '/button-mapping', feature: 'virtual-gamepad' },
  { path: '/hidden', hidden: true },
];
const original = JSON.stringify(routes);
const all = ['/schedule', '/fan', '/tdp', '/cpu', '/gyro-motion', '/button-mapping'];
let checks = 0;
function check(name: string, run: () => void) { run(); checks++; console.log(`PASS ${name}`); }
// Readiness is deliberately NOT an input to route visibility.
for (const readiness of ['not-initialized', 'files-missing', 'handshake-failed', 'unsupported-device', 'timeout', 'ready']) {
  check(`resident routes: ${readiness}`, () => assert.deepEqual(residentNavigationRoutes(routes, false).map(r => r.path), all));
}
check('quick mode only removes CPU/TDP entries', () => assert.deepEqual(residentNavigationRoutes(routes, true).map(r => r.path), ['/schedule', '/fan', '/gyro-motion', '/button-mapping']));
check('policy does not mutate routes', () => assert.equal(JSON.stringify(routes), original));
const read = (path: string) => readFileSync(path, 'utf8');
const nav = read('src/components/NavRail.vue');
const engine = read('src/gamepad/engine.ts');
const router = read('src/router.ts');
const app = read('src/App.vue');
check('pointer and LB/RB share policy', () => {
  assert.match(nav, /residentNavigationRoutes\(ROUTES, quickModeEnabled.value\)/);
  assert.match(engine, /residentNavigationRoutes\(ROUTES, enabled\)/);
});
for (const [name, text] of [['sidebar', nav], ['controller navigation', engine], ['router', router]]) {
  check(`${name}: no capability visibility dependency`, () => assert.doesNotMatch(text, /fanFeatureEnabled|virtualGamepadFeatureEnabled|gyroMotionFeatureEnabled|refreshGyroVirtualFeatureAvailability/));
}
check('shell does not run input asset discovery', () => assert.doesNotMatch(app, /refreshGyroVirtualFeatureAvailability/));
// 2026-09-27 user requirement: this is the exact sidebar / LB-RB order, and quick
// mode removes exactly TDP+CPU from it.
check('sidebar order matches the declared manual/auto sequences', () => {
  const blockStart = router.indexOf('export const ROUTES = [');
  const block = router.slice(blockStart, router.indexOf('];', blockStart));
  const declared = [...block.matchAll(/path:\s*'(\/[^']+)'/g)].map((m) => m[1]);
  assert.deepEqual(declared, [
    '/schedule', '/tdp', '/cpu', '/fan', '/button-mapping', '/gyro-motion',
    '/steam', '/power', '/sleep', '/rtss', '/settings', '/quick',
  ]);
  assert.deepEqual(declared.filter((path) => path !== '/tdp' && path !== '/cpu'), [
    '/schedule', '/fan', '/button-mapping', '/gyro-motion',
    '/steam', '/power', '/sleep', '/rtss', '/settings', '/quick',
  ]);
});
check('deep links are not redirected by hardware readiness', () => {
  const guard = router.slice(router.indexOf('router.beforeEach'));
  assert.doesNotMatch(guard, /to.path.*(?:\/fan|\/gyro-motion|\/button-mapping)/);
  assert.match(guard, /CUSTOM_STEAM_LIBRARY_ROUTE/);
});
check('inner pages retain native admission and failure receipts', () => {
  const pad = read('src/views/ButtonMappingView.vue');
  const gyro = read('src/views/GyroMotionView.vue');
  assert.match(pad, /compareAndSwapInputSettings/);
  assert.match(pad, /backendFaultHold/);
  assert.match(pad, /backendInstallPrompt/);
  assert.match(gyro, /compareAndSwapInputSettings/);
  assert.match(gyro, /backendInstallPrompt/);
});
check('negative control catches a missing Fan route', () => {
  const mutant = routes.filter(r => !r.hidden && r.path !== '/fan').map(r => r.path);
  assert.throws(() => assert.deepEqual(mutant, all));
});
console.log(`RESIDENT_NAVIGATION_PASS checks=${checks}; hardware execution not tested`);
