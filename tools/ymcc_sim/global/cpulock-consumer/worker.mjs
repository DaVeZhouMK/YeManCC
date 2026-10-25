import fs from 'node:fs';
import { runScenarios } from './scenarios.mjs';
const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
try {
  const [bundleFile, specFile, outputFile] = process.argv.slice(2);
  const result = await runScenarios(fs.readFileSync(bundleFile, 'utf8'), read(specFile));
  fs.writeFileSync(outputFile, JSON.stringify(result, null, 2) + '\n');
  console.log(JSON.stringify({ status: result.status, scenarios: result.scenarioCount, failures: result.failedScenarios.map(row => ({ id: row.id, message: row.error.message })) }));
  process.exitCode = result.status === 'PASS' ? 0 : 1;
} catch (error) {
  console.error(error.stack);
  process.exitCode = 2;
}
