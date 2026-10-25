import assert from 'node:assert/strict';
import { test } from 'node:test';
import { loadCatalog, validateCatalog, inspectIdentity, assessFirmware, supportsResearchValue } from './battery-charge-audit.mjs';
const catalog = loadCatalog();
const route = id => catalog.routes.find(item => item.id === id);

test('catalog has immutable evidence and cannot advertise runtime support', () => {
  assert.deepEqual(validateCatalog(catalog, true), []);
  const corrupt = structuredClone(catalog);
  corrupt.routes[0].writeEnabled = true;
  assert.ok(validateCatalog(corrupt).some(message => message.includes('writeEnabled')));
  corrupt.routes[0].sourceRefs.push('invented-proof');
  assert.ok(validateCatalog(corrupt).some(message => message.includes('invented-proof')));
});
test('all source entries stay research-only even with matching firmware', () => {
  for (const item of catalog.inventory) {
    const result = inspectIdentity(catalog, { deviceClass: item.deviceClass, firmware: { ecMajor: 2, ecMinor: 80 } });
    assert.equal(result.writeEnabled, false);
    assert.ok(result.candidates.every(value => !value.writeEnabled && !value.hardwareValidated && !value.runtimeImplemented));
    if (item.evidence === 'explicit-exclusion') assert.ok(result.exclusions.length);
  }
});
test('exact ASUS models match; laptops, suffix lookalikes and brand alone do not', () => {
  for (const model of ['RC71L', 'RC72LA', 'RC73YA', 'RC73XA']) {
    const result = inspectIdentity(catalog, { manufacturer: 'ASUSTeK COMPUTER INC.', boardProduct: model });
    assert.equal(result.candidates[0]?.routeId, 'asus-ally-acpi');
  }
  for (const model of ['RC71L-unknown', 'ROG STRIX', '']) {
    assert.equal(inspectIdentity(catalog, { manufacturer: 'ASUSTeK COMPUTER INC.', boardProduct: model }).candidates.length, 0);
  }
});
test('Legion fixed conservation must reject unsupported slider values', () => {
  const item = route('lenovo-go-conservation');
  for (const value of [80, 100]) assert.equal(supportsResearchValue(item, value), true);
  for (const value of [0, 60, 79, 90, 101, 80.5, NaN, '80']) assert.equal(supportsResearchValue(item, value), false);
  for (const model of ['83E1', '83N0', '83N1', '83L3', '83N6', '83Q2', '83Q3']) {
    const result = inspectIdentity(catalog, { manufacturer: 'LENOVO', systemProduct: model });
    assert.equal(result.candidates[0]?.routeId, item.id);
  }
});
test('MSI exact boards cover all four classes without accepting MSI laptops', () => {
  for (const model of ['MS-1T41','MS-1T42','MS-1T52','MS-1T8K','MS-1T91']) {
    assert.equal(inspectIdentity(catalog, { manufacturer:'MICRO-STAR INTERNATIONAL CO., LTD.', boardProduct:model }).candidates[0]?.routeId, 'msi-claw-wmi');
  }
  assert.equal(inspectIdentity(catalog, { manufacturer:'MICRO-STAR INTERNATIONAL CO., LTD.', boardProduct:'MS-7D75' }).candidates.length, 0);
});
test('CEc and CEii stay separate; old AYANEO and unknown brands do not inherit', () => {
  const inspect = deviceClass => inspectIdentity(catalog, { deviceClass }).candidates[0];
  assert.equal(inspect('AYANEO2S').routeId, 'ayaneo-cec-bypass');
  assert.equal(inspect('AYANEOAIRPlusAMD').routeId, 'ayaneo-ceii-bypass');
  for (const name of ['AYANEO2021', 'AYANEONEXT', 'AYANEONEXTLite', 'AYANEOFuture']) assert.equal(inspect(name), undefined);
  assert.equal(route('ayaneo-cec-bypass').kind, 'software-bypass-threshold');
});
test('OXP EC gates preserve component predicate and reject unknown SMBIOS versions', () => {
  for (const id of ['oxp-x1-amd-ec','oxp-x1-intel-ec']) {
    for (const ec of [{}, {ecMajor:255,ecMinor:255}, {ecMajor:1,ecMinor:null}, {ecMajor:-1,ecMinor:3}, {ecMajor:1.5,ecMinor:3}, {ecMajor:'1',ecMinor:3}])
      assert.equal(assessFirmware(route(id), ec), 'unknown');
  }
  assert.equal(assessFirmware(route('oxp-x1-amd-ec'), {ecMajor:1,ecMinor:3}), 'source-predicate-matched');
  assert.equal(assessFirmware(route('oxp-x1-amd-ec'), {ecMajor:2,ecMinor:0}), 'source-predicate-rejected');
  assert.equal(assessFirmware(route('oxp-x1-intel-ec'), {ecMajor:0,ecMinor:67}), 'source-predicate-matched');
  assert.equal(assessFirmware(route('oxp-x1-intel-ec'), {ecMajor:1,ecMinor:1}), 'source-predicate-rejected');
});
test('unknown ONE-NETBOOK fallback class is explicitly untrusted', () => {
  const result = inspectIdentity(catalog, {manufacturer:'ONE-NETBOOK', boardProduct:'UNKNOWN MODEL', deviceClass:'OneXPlayerX1AMD'});
  assert.equal(result.candidates.length, 1);
  assert.equal(result.candidates[0].identityEvidence, 'class-only-untrusted');
  assert.equal(result.writeEnabled, false);
  assert.equal(inspectIdentity(catalog, {manufacturer:'ONE-NETBOOK', boardProduct:'UNKNOWN MODEL'}).candidates.length, 0);
});
test('X2/3 must not inherit an X1 Windows writer, even with new firmware', () => {
  for (const name of ['OneXPlayerX2','OneXPlayerX2MiniPro','OneXPlayer3']) {
    const result=inspectIdentity(catalog,{deviceClass:name,firmware:{ecMajor:10,ecMinor:100}});
    assert.equal(result.candidates.length,0);
    assert.equal(result.exclusions.length,1);
  }
  const linuxOnly=inspectIdentity(catalog,{manufacturer:'ONE-NETBOOK',boardProduct:'ONEXPLAYER X2Mini PRO',deviceClass:'OneXPlayerX2MiniPro'});
  assert.equal(linuxOnly.candidates[0]?.evidencePlatform,'linux-reference');
  assert.equal(linuxOnly.exclusions.length,1);
});
test('Linux per-board switch separates A1X charge support from A1/A2 and OrangePi', () => {
  for (const [manufacturer,boardProduct] of [['AOKZOE','AOKZOE A1 Pro'],['AOKZOE','AOKZOE A2 Pro'],['OrangePi','NEO-01']])
    assert.equal(inspectIdentity(catalog,{manufacturer,boardProduct}).candidates.length,0);
});
test('AOKZOE A1X has Linux-only charge evidence via oxp_fly', () => {
  const result=inspectIdentity(catalog,{manufacturer:'AOKZOE',boardProduct:'AOKZOE A1X'});
  assert.equal(result.candidates[0]?.evidencePlatform,'linux-reference');
  assert.equal(result.writeEnabled,false);
});
test('Steam Deck requires exact firmware AND board; unsupported pairs stay rejected', () => {
  const item=route('valve-deck-firmware');
  for (const pair of item.firmwareGate.pairs) assert.equal(assessFirmware(item,pair),'source-predicate-matched');
  for (const pair of [{boardId:6,firmware:0xB030},{boardId:5,firmware:0x1030},{boardId:5,firmware:0x9999},{boardId:10,firmware:0x1010}])
    assert.equal(assessFirmware(item,pair),'source-predicate-rejected');
  assert.equal(assessFirmware(item,{}),'unknown');
});
test('GPD driver scope cannot extend from HX370 to 8840U or an unreviewed BIOS', () => {
  const common={manufacturer:'GPD',boardProduct:'G1617-02'};
  assert.equal(inspectIdentity(catalog,{...common,processor:'AMD Ryzen AI 9 HX 370'}).candidates[0]?.routeId,'gpd-win-mini-2025');
  assert.equal(inspectIdentity(catalog,{...common,processor:'AMD Ryzen 7 8840U'}).candidates.length,0);
  assert.equal(inspectIdentity(catalog,{...common,processor:'AMD Ryzen AI 9 365'}).candidates.length,0);
  assert.equal(assessFirmware(route('gpd-win-mini-2025'),{bios:'2.11',ecMajor:2,ecMinor:7}),'unreviewed-firmware');
  for (const value of [0,40,59,101]) assert.equal(supportsResearchValue(route('gpd-win-mini-2025'),value),false);
});
test('source class conflicts are visible and never become permissions', () => {
  const result=inspectIdentity(catalog,{manufacturer:'ASUSTeK COMPUTER INC.',boardProduct:'RC73YA',deviceClass:'ROGAlly'});
  assert.equal(result.candidates[0].classContradiction,true);
  assert.equal(result.writeEnabled,false);
});
