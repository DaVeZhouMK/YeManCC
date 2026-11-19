// Regenerate the Windows runtime allowlist from reviewed sources only.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {BATTERY_INPUT_PATHS,buildReviewedBatteryRoutes} from './lib/hc_battery_catalog.mjs';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const {rules,routeCount,bytes}=buildReviewedBatteryRoutes({repo:root});
const target=path.join(root,BATTERY_INPUT_PATHS.runtimeRules);
if(process.argv.includes('--check'))assert.ok(fs.readFileSync(target).equals(bytes),'BATTERY_RUNTIME_RULE_BYTES_DIFFER: regenerate the runtime allowlist from the reviewed catalogue');
else fs.writeFileSync(target,bytes);
console.log(`${rules.length} reviewed identity groups, ${routeCount} Windows routes; Linux/SuiPlay candidates remain disabled.`);
