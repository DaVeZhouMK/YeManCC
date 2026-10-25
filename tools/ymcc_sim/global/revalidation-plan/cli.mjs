import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRevalidationPlan } from './index.mjs';

export function parseArgs(args) {
  const values = { receipts: [], tests: [], domains: [], profile: 'full', maxBatchSize: 6 };
  const seen = new Set();
  for (let index = 0; index < args.length; index++) {
    const name = args[index];
    if (name === '--help') { values.help = true; continue; }
    const value = args[++index];
    if (!['--receipt', '--test', '--domain', '--profile', '--out-dir', '--assignments', '--max-batch-size'].includes(name)
      || !value || value.startsWith('--') || /[\0\r\n]/.test(value)) throw Error(`INVALID_ARGUMENT:${name}`);
    if (name === '--receipt') values.receipts.push(value);
    else if (name === '--test' || name === '--domain') {
      const parts = value.split(',');
      if (parts.some(part => !part)) throw Error(`INVALID_ARGUMENT:${name}`);
      values[name === '--test' ? 'tests' : 'domains'].push(...parts);
    } else {
      if (seen.has(name)) throw Error(`DUPLICATE_ARGUMENT:${name}`);
      seen.add(name);
      if (name === '--max-batch-size') {
        if (!/^[1-9][0-9]*$/.test(value)) throw Error('INVALID_BATCH_SIZE');
        values.maxBatchSize = Number(value);
      } else values[name.slice(2).replace(/-([a-z])/g, (_, character) => character.toUpperCase())] = value;
    }
  }
  if (!values.help && (!values.receipts.length || !values.outDir || !['desktop', 'full', 'sleep'].includes(values.profile)
    || !Number.isSafeInteger(values.maxBatchSize) || values.maxBatchSize > 32)) throw Error('PLAN_ARGUMENTS_REQUIRED');
  return values;
}
export async function main(args = process.argv.slice(2), {
  repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..'),
} = {}) {
  const options = parseArgs(args);
  if (options.help) {
    console.log('Read-only revalidation plan: --receipt RUN_RESULT_JSON (repeatable, existing audit order) --out-dir NEW_DIRECTORY [--profile full|desktop|sleep] [--test IDS] [--domain DOMAINS] [--assignments FILE] [--max-batch-size 1..32]. Exit 12 = PLAN_ONLY, 3 = BLOCKED. No tests execute.');
    return 12;
  }
  const outDir = path.resolve(options.outDir);
  if (fs.existsSync(outDir)) throw Error('EVIDENCE_DIRECTORY_ALREADY_EXISTS');
  const [{ getAdapters }, { getSleepAdapters }, { getExtraAdapters }] = await Promise.all([
    import('../adapters.mjs'), import('../sleep.mjs'), import('../extra-adapters.mjs'),
  ]);
  const assignments = options.assignments ? JSON.parse(fs.readFileSync(options.assignments, 'utf8').replace(/^\uFEFF/, '')) : [];
  const { plan, audit } = createRevalidationPlan(options.receipts, {
    repoRoot, adapters: [...getAdapters(repoRoot), ...getSleepAdapters(repoRoot), ...getExtraAdapters(repoRoot)],
    profile: options.profile, tests: options.tests, domains: options.domains, maxBatchSize: options.maxBatchSize, assignments,
  });
  fs.mkdirSync(outDir);
  fs.writeFileSync(path.join(outDir, 'revalidation-plan.json'), JSON.stringify(plan, null, 2) + '\n');
  fs.writeFileSync(path.join(outDir, 'receipt-audit.json'), JSON.stringify(audit, null, 2) + '\n');
  console.log(JSON.stringify({ status: plan.status, ...plan.summary, evidence: outDir, executedTests: 0 }));
  return plan.status === 'BLOCKED' ? 3 : 12;
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().then(code => { process.exitCode = code; }).catch(error => {
    console.error(`REVALIDATION_PLAN_BLOCKED:${error.message}`); process.exitCode = 3;
  });
}
