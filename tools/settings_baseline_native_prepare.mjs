import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
const root=process.cwd(),evidence=process.env.YMCC_BASELINE_EVIDENCE;
if(!evidence)throw Error('YMCC_BASELINE_EVIDENCE is required');fs.mkdirSync(evidence,{recursive:true});
const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
const slice=(start,end)=>{const a=main.indexOf(start),b=main.indexOf(end,a+start.length);if(a<0||b<0||main.indexOf(start,a+1)!==-1)throw Error('production source anchor mismatch: '+start);return main.slice(a,b);};
const bodies=[
 slice('static void ymSettingsApplyDelta(', 'static json ymSettingsReadUnlocked()'),
 slice('static json ymSettingsReadUnlocked()', 'class SettingsFileGuard'),
 slice('static bool ymSettingsHasCompatibleWriteBase(', 'static bool ymSettingsWriteDocument('),
 slice('static json ymSettingsResetApplication(', 'static json ymSettingsRead()'),
 slice('static bool ymSettingsWriteSection(', '// Native MSI shield writes'),
 slice('static bool ymSettingsPatchSection(', '// ================================================================\n// Native monitor daemon'),
];
const source=fs.readFileSync(path.join(root,'tools/fixtures/settings_baseline_native.cpp'),'utf8').replace('// SETTINGS_BASELINE_PRODUCTION_BODIES',bodies.join('\n'));
fs.writeFileSync(path.join(evidence,'settings-baseline-native.cpp'),'#include <fstream>\n'+source);
const inputs=['native/main.cpp','native/settings_baseline.h','native/controller_persona_settings.h','native/controller_persona.h','deps/json/json.hpp','tools/settings_baseline_native_prepare.mjs','tools/settings_baseline_native_selftest.mjs','tools/fixtures/settings_baseline_native.cpp','PowerControl/settings-baseline.json','PowerControl/settings-defaults-v1.json'];
fs.writeFileSync(path.join(evidence,'native-source-inputs.json'),JSON.stringify(inputs.map(file=>({path:file,sha256:crypto.createHash('sha256').update(fs.readFileSync(path.join(root,file))).digest('hex')})),null,2)+'\n');console.log('NATIVE_CONSUMERS_EXTRACTED '+bodies.length);
