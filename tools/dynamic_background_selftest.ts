import { cleanGameTitle, normalizeGameTitle, scoreSteamTitle, stripLaunchModeSuffix } from '../src/bridge/dynamicBackground';
import { nextLowBatteryStatic, shouldSkipVideoReconcile } from '../src/bridge/backgroundPolicy';

const title = 'Wargame: Red Dragon - DirectX 11';
if (stripLaunchModeSuffix(title) !== 'Wargame: Red Dragon') throw new Error('DirectX suffix was not removed');
if (normalizeGameTitle(title) !== 'wargamereddragon') throw new Error('Normalized title is incorrect');
if (stripLaunchModeSuffix('WARNO-DirectX 11') !== 'WARNO') throw new Error('WARNO DirectX suffix was not removed');
if (normalizeGameTitle('WARNO - DirectX 11') !== 'warno') throw new Error('WARNO normalized title is incorrect');
const copyrightTitle = 'Cyberpunk 2077(C)2020 by CD Projekt RED';
if (cleanGameTitle(copyrightTitle) !== 'Cyberpunk 2077') throw new Error(`Copyright suffix was not removed: ${cleanGameTitle(copyrightTitle)}`);
if (normalizeGameTitle(copyrightTitle) !== 'cyberpunk2077') throw new Error('Copyright title normalization is incorrect');
if (scoreSteamTitle(copyrightTitle, 'Cyberpunk 2077') !== 1) throw new Error('Copyright title did not fuzzy-match the Steam title');

const base = scoreSteamTitle(title, 'Wargame: Red Dragon');
const dlc = scoreSteamTitle(title, 'Wargame: Red Dragon - Nation Pack: Israel');
if (base !== 1 || dlc >= base) throw new Error(`Search scoring is incorrect: base=${base}, dlc=${dlc}`);

const dc18 = { ac: 0, hasBattery: true, batteryPercent: 18, chargeW: -8 };
const dc23 = { ac: 0, hasBattery: true, batteryPercent: 23, chargeW: -8 };
const dc26 = { ac: 0, hasBattery: true, batteryPercent: 26, chargeW: -8 };
const charging18 = { ac: 1, hasBattery: true, batteryPercent: 18, chargeW: 20 };
if (!nextLowBatteryStatic(false, dc18, true)) throw new Error('Low battery did not enter static mode');
if (!nextLowBatteryStatic(true, dc23, true)) throw new Error('Low battery hysteresis released too early');
if (nextLowBatteryStatic(true, dc26, true)) throw new Error('Low battery hysteresis did not release at 25%+');
if (nextLowBatteryStatic(true, charging18, true)) throw new Error('Charging did not release low battery static mode');
if (nextLowBatteryStatic(true, dc18, false)) throw new Error('Disabled battery policy did not release static mode');

// Regression: autoplay may be playing while the remembered intent is false.
if (shouldSkipVideoReconcile(false, false, true)) throw new Error('Playing video was incorrectly treated as reconciled');
if (!shouldSkipVideoReconcile(false, false, false)) throw new Error('Paused video was not treated as reconciled');
if (!shouldSkipVideoReconcile(true, true, true)) throw new Error('Playing video was not treated as reconciled');
if (shouldSkipVideoReconcile(true, true, false)) throw new Error('Paused video was incorrectly treated as playing');

console.log(JSON.stringify({ title, cleaned: stripLaunchModeSuffix(title), normalized: normalizeGameTitle(title), base, dlc }));
