// The startup page now hosts BootModesPanel. Replay its actual components and original
// gamepad engine through the focused memory-only suite; legacy policy constraints stay
// in input_startup_policy_selftest.mjs rather than obsolete PowerView controls.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
const args=process.argv.slice(2);
const flag=args.indexOf('--out-dir');
const out=path.resolve((flag>=0?args[flag+1]:args[0])||process.env.YMCC_UI_BROWSER_OUT||fs.mkdtempSync(path.join(os.tmpdir(),'ymcc-boot-startup-ui-')));
process.argv=[process.argv[0],process.argv[1],out];
await import('./boot_modes_browser_selftest.mjs');
const report=JSON.parse(fs.readFileSync(path.join(out,'boot-modes-browser.json'),'utf8'));
fs.writeFileSync(path.join(out,'browser-results.json'),JSON.stringify({...report,suite:'Input startup browser UI',replacedSurface:'BootModesPanel/BootModeWorkbench',passed:report.counts.passed,cases:report.assertions},null,2));
