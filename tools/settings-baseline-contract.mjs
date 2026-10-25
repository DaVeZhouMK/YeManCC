import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { compileSettingsConsumer, createSettingsFixture } from './fixtures/settings_baseline.mjs';
const root = process.cwd();
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const contract = JSON.parse(read('PowerControl/settings-baseline.json'));
const defaults = JSON.parse(read('PowerControl/' + contract.defaultsFile));
assert.equal(contract.formatVersion, 1);
assert.equal(contract.document.path,'yeman-settings.json');assert.equal(contract.document.backup,'yeman-settings.json.bak');
assert.equal(contract.document.maxBytes,4194304);assert.equal(contract.stampKey,'configurationBaseline');assert.equal(contract.documentSchemaVersion,1);
assert.deepEqual(contract.managedResources.map(row=>row.path).sort(),['settings-baseline.json',contract.defaultsFile].sort()); assert.equal(contract.id, 'ymcc-settings');
assert(Number.isSafeInteger(contract.revision) && contract.revision > 0);
assert.equal(contract.defaultsFile, `settings-defaults-v${contract.revision}.json`);
const f = createSettingsFixture(await compileSettingsConsumer(root));
assert.deepEqual(JSON.parse(JSON.stringify(f.repo.normalizeSettings({}))), defaults,
  'Frozen defaults changed: review the storage revision and migrate deliberately before updating the snapshot');
const names = contract.sections.map(s => s.name).sort();
assert.equal(new Set(names).size, names.length);
assert.deepEqual(names, Object.keys(defaults).filter(k => !['schemaVersion','baselineId','configurationBaseline','appVersionWritten'].includes(k)).sort());
const repo = read('src/bridge/settingsRepository.ts');
const imported = [...repo.matchAll(/readLegacy(?:Json|Number)\('([^']+)'/g)].map(m => m[1].replaceAll('\\\\','/'));
imported.push('launch_apps.json');
assert.deepEqual(contract.legacyImports.map(i => i.path).sort(), imported.sort(), 'Actual import paths must match the ownership contract');
for (const item of [...contract.legacyImports, ...contract.independentUserFiles]) {
  assert(!item.path.includes('..') && !/[:\\*?\x00-\x1f]/.test(item.path));
}
const header = read('native/settings_baseline.h');
assert(header.includes(`kId = "${contract.id}"`));
assert(header.includes(`kRevision = ${contract.revision};`));
const knownSections = new Set([...names, ...contract.nativeOwnedSections, ...contract.moduleOwnedSections.map(s => s.name)]);
const observedSections = new Map();
function inventory(directory) {
  for (const entry of fs.readdirSync(directory, {withFileTypes:true})) {
    const file = path.join(directory,entry.name);
    if (entry.isDirectory()) { if (!['bin','obj','node_modules'].includes(entry.name)) inventory(file); continue; }
    if (!/\.(ts|vue|cpp|h)$/.test(entry.name)) continue;
    const text = fs.readFileSync(file,'utf8');
    for (const pattern of [/ymSettings(?:Section|WriteSection|PatchSection)\("([^"\n]+)"/g, /(?:readSettingsSection|saveSettingsSection|replaceSettingsSection)(?:<[^>\n]*>)?\(['"]([^'"\n]+)['"]/g]) {
      for (const match of text.matchAll(pattern)) {
        const sources=observedSections.get(match[1])||new Set();sources.add(path.relative(root,file).replaceAll('\\','/'));observedSections.set(match[1],sources);
      }
    }
  }
}
inventory(path.join(root,'src'));inventory(path.join(root,'native'));
for (const [section] of observedSections) assert(knownSections.has(section), 'Unclassified live settings section: '+section);
if(process.env.YMCC_BASELINE_INVENTORY) fs.writeFileSync(process.env.YMCC_BASELINE_INVENTORY,JSON.stringify([...observedSections].sort().map(([name,sources])=>({name,sources:[...sources]})),null,2)+'\n');
const main = read('native/main.cpp');
assert(main.includes('#include "settings_baseline.h"'));
assert.equal(contract.cleanupPolicy.unknownPaths, 'preserve');
assert.equal(contract.cleanupPolicy.userSettings, 'never-package-or-delete-on-upgrade');
assert.equal(f.state.calls.length, 0);
console.log(JSON.stringify({status:'PASS',revision:contract.revision,sections:names.length,legacyPaths:imported.length,actualDefaults:true,configurationIO:false}));
