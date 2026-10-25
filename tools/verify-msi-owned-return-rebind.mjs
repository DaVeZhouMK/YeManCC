// Offline source-exact rebind/owned-return integration. No product or device I/O.
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const here=path.dirname(fileURLToPath(import.meta.url));
const result=spawnSync(process.execPath,[path.join(here,'verify-msi-startup-owned-return-runtime.mjs'),
  '--fixture-directory',path.join(here,'fixtures/msi_owned_return_rebind'),'--include-rebind-source-fragments',...process.argv.slice(2)],
  {stdio:'inherit',windowsHide:true});
if(result.error)throw result.error;
process.exitCode=result.status??1;
