// Source stays in the mainline; only test/build artifacts get a separate output directory.
import fs from 'node:fs';import path from 'node:path';
export const root=path.resolve(import.meta.dirname,'../..');
export const task=path.resolve(process.env.YMCC_DECKY_ARTIFACT_ROOT||path.join(process.env.YEMAN_WORKSPACE_ROOT||root,'Build/Tasks/YMCC-Decky-Sidebar'));
export const validation=path.join(task,'validation');
fs.mkdirSync(validation,{recursive:true});fs.mkdirSync(path.join(task,'Build'),{recursive:true});
