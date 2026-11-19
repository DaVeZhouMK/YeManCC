import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {readBatteryPolicy,validateBatteryRouteSemantics} from './lib/hc_battery_catalog.mjs';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const policy=readBatteryPolicy({repo});
const rules=JSON.parse(fs.readFileSync(path.join(repo,policy.runtimeRules),'utf8'));
const routeCount=validateBatteryRouteSemantics(rules,policy.admittedProviderRoutes);
console.log(`BATTERY_ROUTE_SEMANTICS_OK: ${rules.length} rules, ${routeCount} reviewed route contracts`);
