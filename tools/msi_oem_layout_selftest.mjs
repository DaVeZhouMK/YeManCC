// Pure production-catalog regression. All device APIs are absent.
// Run this generator from the repository; compile the output C++ with the workspace compiler.
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const out = path.resolve(process.argv[2] || 'G:/YeManCC-Work/_scratch/msi-oem-key-layout-20261014/native');
fs.mkdirSync(out, { recursive: true });
const source = fs.readFileSync(process.argv[3] ? path.resolve(process.argv[3]) : path.join(root, 'native/main.cpp'), 'utf8');
function block(marker) {
  const start = source.indexOf(marker);
  if (start < 0) throw Error('Missing production definition: ' + marker);
  const end = source.indexOf('\n}', start);
  if (end < 0) throw Error('Unclosed production definition: ' + marker);
  return source.slice(start, end + (source[end + 2] === ';' ? 3 : 2));
}
const definitions = [block('enum class YmccFamily : int {'), block('enum class OemKeyForm : int {'),
  block('struct OemKeyDef {'), 'struct OemCatalogRef { const OemKeyDef* keys; int count; };',
  ...[...source.matchAll(/static const OemKeyDef \w+\[\] = \{[\s\S]*?\n\};/g)].map(m => m[0]),
  'template <size_t N>\n' + block('static OemCatalogRef oemCatalogOf('), block('static OemCatalogRef oemCatalogForFamily(')];
// OemKeyForm is an inline enum; its first newline-closing brace belongs to the
// next struct. Keep that enum exact without extracting adjacent declarations.
definitions[1] = source.match(/enum class OemKeyForm : int \{[^\n]+\};/)[0];
const cpp = '#include <cstdint>\n#include <cstddef>\n#include <string>\n#include <utility>\n#include <set>\n#include <iostream>\n#include <stdexcept>\n' + definitions.join('\n\n') + String.raw`
int main() {
  unsigned checks = 0;
  const auto check = [&](bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); };
  const auto find = [&](OemCatalogRef ref, const char* id) -> const OemKeyDef& {
    for (int i = 0; i < ref.count; ++i) if (std::string(ref.keys[i].keyId) == id) return ref.keys[i];
    throw std::runtime_error(std::string("missing key ") + id);
  };
  try {
    for (const char* model : {"ClawA1M", "ClawA2VM", "ClawBZ2EM", "ClawCG3EM"}) {
      const auto catalog = oemCatalogForFamily(YmccFamily::MsiClaw, std::string("HandheldCompanion.Devices.") + model);
      check(catalog.count == 4, "MSI catalog retains four independent keys");
      check(find(catalog, "m1").backIndex == 2, "MSI M1 must be on the right in the front-view preview");
      check(find(catalog, "m2").backIndex == 1, "MSI M2 must be on the left in the front-view preview");
      const bool g3e = std::string(model) == "ClawCG3EM";
      check(std::string(catalog.keys[0].keyId) == (g3e ? "qs" : "claw"), "model-specific front left key");
      check(std::string(catalog.keys[1].keyId) == (g3e ? "claw" : "qs"), "model-specific front right key");
      check(find(catalog,"claw").clickOnly && find(catalog,"qs").clickOnly, "WMI front trigger semantics retained");
      check(find(catalog,"claw").form == OemKeyForm::WmiEvent && find(catalog,"qs").form == OemKeyForm::WmiEvent,
        "front source identity retained");
      check(find(catalog,"claw").backIndex == 0 && find(catalog,"qs").backIndex == 0, "front keys stay out of rear slots");
      check(!find(catalog,"m1").clickOnly && !find(catalog,"m2").clickOnly, "rear press/release semantics retained");
      check(find(catalog,"m1").form == OemKeyForm::HidButton && find(catalog,"m2").form == OemKeyForm::HidButton,
        "rear sources retained");
      check(std::string(find(catalog,"m1").label) == "M1" && std::string(find(catalog,"m2").label) == "M2",
        "physical names are not renamed to fix positions");
      std::set<std::string> ids; for (int i=0;i<catalog.count;++i) ids.insert(catalog.keys[i].keyId);
      check(ids == std::set<std::string>{"claw","qs","m1","m2"}, "canonical IDs and persisted rule names unchanged");
    }
    for (auto family : {YmccFamily::AsusRogAllyX,YmccFamily::AsusRogAlly,YmccFamily::AsusRogAllyClassic,YmccFamily::AsusRogAllyXClassic}) {
      const auto catalog = oemCatalogForFamily(family,"HandheldCompanion.Devices.ROGAllyX");
      check(catalog.count == 5, "ROG count unchanged");
      check(std::string(catalog.keys[0].keyId)=="cc" && std::string(catalog.keys[1].keyId)=="ac", "ROG front order unchanged");
      check(find(catalog,"m1").backIndex==2 && find(catalog,"m2").backIndex==1, "classic ROG front-view positions match E25271/E25272 p14-15");
    }

    // 2026-10-16 RC73XA report: front-view preview must match the ASUS
    // RC73XA/YA manual rear diagram mirrored into a front view. Classic ROG
    // assertions above now use E25271/E25272 p14-15 for classic front views.
    for (const auto& model : {
      std::pair{YmccFamily::AsusRogAllyX, "HandheldCompanion.Devices.XboxROGAllyX"},
      std::pair{YmccFamily::AsusRogAlly, "HandheldCompanion.Devices.XboxROGAlly"}}) {
      const auto catalog=oemCatalogForFamily(model.first,model.second);
      check(catalog.count==5,"Xbox ROG keeps five canonical keys");
      check(find(catalog,"m1").backIndex==2,"Xbox ROG M1 is front-view right");
      check(find(catalog,"m2").backIndex==1,"Xbox ROG M2 is front-view left");
      for (const auto& side : {std::pair{"m1","M1"},std::pair{"m2","M2"}}) {
        const auto& key=find(catalog,side.first);
        check(std::string(key.label)==side.second,"Xbox ROG keeps canonical rear names");
        check(key.form==OemKeyForm::KeyboardVk && !key.clickOnly,"Xbox ROG keeps raw keyboard press/release semantics");
      }
      check(std::string(catalog.keys[0].keyId)=="cc" && std::string(catalog.keys[1].keyId)=="ac" &&
        std::string(catalog.keys[4].keyId)=="lib","Xbox ROG front order unchanged");
      for(const char* id:{"cc","ac","lib"})check(find(catalog,id).backIndex==0 && find(catalog,id).clickOnly,
        "Xbox ROG front keys stay out of rear slots");
    }
    const auto other=oemCatalogForFamily(YmccFamily::AsusRogAllyXClassic,"HandheldCompanion.Devices.XboxROGAllyX");
    check(other.keys==kRogKeys,"Xbox model name alone cannot select the Xbox catalog for a classic family");
    check(oemCatalogForFamily(YmccFamily::Unknown,"HandheldCompanion.Devices.XboxROGAllyX").count==0,
      "Xbox model name does not authorize unknown family");

    const auto gpd=oemCatalogForFamily(YmccFamily::Gpd,"HandheldCompanion.Devices.GPDWin4");
    check(gpd.count==2 && gpd.keys[0].backIndex==1 && gpd.keys[1].backIndex==2, "GPD legacy positions unchanged");
    check(oemCatalogForFamily(YmccFamily::Unknown,"HandheldCompanion.Devices.ClawCG3EM").count==0,"MSI class string cannot authorize another family");
    std::cout << "MSI_OEM_PHYSICAL_LAYOUT_OK checks=" << checks << " hardware_io=none production_catalog=true\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "MSI_OEM_PHYSICAL_LAYOUT_FAIL " << e.what() << '\n'; return 1; }
}
`;
fs.writeFileSync(path.join(out, 'actual-msi-oem-layout-selftest.cpp'), cpp);
fs.writeFileSync(path.join(out, 'source-binding.json'), JSON.stringify({schema:1,
  mainSha256:createHash('sha256').update(source).digest('hex'),
  definitionSha256:createHash('sha256').update(definitions.join('\n\n')).digest('hex'),
  testSha256:createHash('sha256').update(cpp).digest('hex'), hardwareIo:false, productRun:false},null,2));
console.log('MSI layout fixture generated from actual production catalogs and selector; hardware IO absent.');