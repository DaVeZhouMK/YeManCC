import fs from 'node:fs';
const [sourceFile,outputFile]=process.argv.slice(2);
if(!sourceFile||!outputFile)throw Error('Usage: node tools/updater_package_validation_extract.mjs <git035-native.cpp> <fixture.cpp>');
const source=fs.readFileSync(sourceFile,'utf8');
const start=source.lastIndexOf('static std::string sha256File(const std::wstring& path) {');
const end=source.indexOf('// ── 更新加速器',start);
if(start<0||end<=start)throw Error('Public 035 validation boundary missing');
const fixture=fs.readFileSync(new URL('./updater_package_validation_fixture.cpp',import.meta.url),'utf8');
fs.writeFileSync(outputFile,fixture.replace('//__PRODUCTION_VALIDATORS__','#define APP_VER_STR "0.0.35"\n'+source.slice(start,end)));
