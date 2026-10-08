import fs from 'node:fs';
const js = fs.readFileSync('native/steam_live.js', 'utf8').trimEnd();
const output = '// Generated from steam_live.js; do not edit.\n#pragma once\nnamespace ymcc::steamlive {\ninline constexpr const char kScript[] = R"YMSTEAM(' + js + ')YMSTEAM";\n}\n';
const file = 'native/steam_live_script.h';
if (process.argv.includes('--check')) {
  if (!fs.existsSync(file) || fs.readFileSync(file, 'utf8') !== output) throw Error('Steam live script embed is stale');
  console.log('Steam live script embed OK');
} else fs.writeFileSync(file, output);
