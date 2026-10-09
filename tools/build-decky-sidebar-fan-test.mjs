import { task } from './fixtures/decky_task_paths.mjs';
import path from 'node:path';
import {createRequire} from 'node:module';
const root=path.resolve(import.meta.dirname,'..');const require=createRequire(path.join(root,'package.json'));const {build}=require('esbuild');
await build({entryPoints:[path.join(root,'tools/decky_sidebar_fan_lifecycle_selftest.ts')],bundle:true,platform:'node',format:'cjs',target:'node22',alias:{'@':path.join(root,'src')},outfile:path.resolve(task,'Build/decky-sidebar-fan-lifecycle-test.cjs')});

await build({entryPoints:[path.join(root,'tools/decky_sidebar_fan_fixture.ts')],bundle:true,platform:'node',format:'cjs',target:'node22',alias:{'@':path.join(root,'src')},outfile:path.resolve(task,'Build/decky-sidebar-fan-fixture.cjs')});

await build({entryPoints:[path.join(root,'tools/decky_sidebar_fan_rebase_selftest.ts')],bundle:true,platform:'node',format:'cjs',target:'node22',alias:{'@':path.join(root,'src')},outfile:path.resolve(task,'Build/decky-sidebar-fan-rebase-test.cjs')});
