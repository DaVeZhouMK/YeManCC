// Release gate entry; same scenario is discoverable in the existing global simulator.
import path from 'node:path';
const evidence=path.resolve(process.argv[2]||process.env.YMCC_DECKY_ARTIFACT_ROOT||'work/decky-update-recovery','decky-update-recovery');
process.argv=[process.execPath,process.argv[1],'--evidence-dir',evidence];
await import('./ymcc_sim/global/decky-update-recovery/run.mjs');
