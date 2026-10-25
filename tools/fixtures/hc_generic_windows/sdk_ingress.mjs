import {extract} from '../legion_motion_consumer/extract_main.mjs';

// Only the retained current candidate is rebound. Historical comparisons remain intact.
export function rebindGenericSdkIngress(base, source) {
  const start = base.indexOf('namespace candidate {');
  if (start < 0) throw Error('Current candidate missing');
  const prefix = base.slice(0, start);
  let tail = base.slice(start);
  const bindings = [];
  for (const symbol of ['inputCaptureResolveHcMotionSelector', 'inputCaptureMotionSelectorBound',
    'inputCaptureCandidateMatrixReceipt', 'inputCaptureMotionCapabilityReceipt']) {
    const prior = extract(tail, symbol), current = extract(source, symbol);
    if (tail.split(prior.code).length !== 2) throw Error('Nonunique candidate ' + symbol);
    tail = tail.replace(prior.code, () => current.code);
    const {code, ...binding} = current;
    bindings.push(binding);
  }
  const helpers = ['inputCaptureGenericHcMotionRecipe', 'inputCaptureResolveGenericHcMotionSelector',
    'inputCaptureGenericHcMotionSelectorBound', 'inputCaptureMotionDeviceClass'].map(symbol => {
    const {code, ...binding} = extract(source, symbol);
    bindings.push(binding);
    return code;
  });
  const before = extract(tail, 'inputCaptureDefaultHcMotionIdentity').code;
  tail = tail.replace(before, () => helpers.join('\n\n') + '\n\n' + before);
  return {code: prefix + '\n#include "hc_generic_motion_catalog.h"\n' + tail, bindings};
}

function replaceOnce(text, before, after) {
  if (text.split(before).length !== 2) throw Error('SDK ingress harness anchor: ' + before.slice(0, 90));
  return text.replace(before, () => after);
}

export function extendGenericSdkHarness(text) {
  text = replaceOnce(text,
    'for(const auto& r:oracle["legacy"]["cases"])rows.push_back(r);',
    'for(const auto& r:oracle["legacy"]["cases"])rows.push_back(r);for(const auto& r:oracle["genericIngress"])rows.push_back(r);');
  text = replaceOnce(text,
    'const auto classFamily=oracle["sourceFamilies"].value(id.deviceClass,"");const bool expected=(allowed.contains(classFamily)||defaults.contains(id.deviceClass))&&id.resolved&&candidate::inputCaptureMotionSelectorBound();',
    `const auto classFamily=oracle["sourceFamilies"].value(id.deviceClass,"");
    const auto oracleClass=row.value("expectedGenericClass",row.value("hcClass",std::string{}));
    const bool genericExpected=(oracleClass=="HandheldCompanion.Devices.DefaultDevice"||oracleClass=="HandheldCompanion.Devices.ASUS.ASUS"||oracleClass=="HandheldCompanion.Devices.ASUS")&&!id.resolved&&id.family==candidate::YmccFamily::Unknown;
    const bool expected=genericExpected||((allowed.contains(classFamily)||defaults.contains(id.deviceClass))&&id.resolved&&candidate::inputCaptureMotionSelectorBound());
    if(row.contains("expectedGenericClass")){
      check(!id.resolved&&id.deviceClass.empty()&&id.inputHidKey.empty()&&id.gyroMatrixId.empty()&&id.accelMatrixId.empty(),"windows-fallback-scope","generic source preserves unresolved machine identity");
      check(declared.value("deviceClass",std::string{})==oracleClass,"windows-fallback-scope","generic effective class agrees with independent HC class");
    }`);
  text = replaceOnce(text, 'models.emplace(id.deviceClass,row);',
    'const auto effective=declared.value("deviceClass",id.deviceClass);if(row.contains("expectedGenericClass"))models.insert_or_assign(effective,row);else models.emplace(effective,row);');
  text = replaceOnce(text,
    'if(models.empty())throw std::runtime_error("No eligible source model");const auto row=models.begin()->second;',
    `if(models.empty())throw std::runtime_error("No eligible source model");
    check(models.contains("HandheldCompanion.Devices.DefaultDevice")&&models.contains("HandheldCompanion.Devices.ASUS.ASUS"),"windows-fallback-scope","both generic source classes reach actual SDK ingress");
    std::vector<json> lifecycleRows={models.begin()->second};
    for(const auto& generic:oracle["genericIngress"])lifecycleRows.push_back(generic);
    for(const auto& row:lifecycleRows){`);
  text = replaceOnce(text, ' destroy(candidate::g_gm);candidate::g_gm=nullptr;FreeLibrary(module);',
    ' }\n destroy(candidate::g_gm);candidate::g_gm=nullptr;FreeLibrary(module);');
  text = replaceOnce(text, '{"actualMainFunctions",true}',
    '{"genericModelClasses",2},{"lifecycleIdentities",lifecycleRows.size()},{"actualMainFunctions",true},{"actualFullProcessingOwner",false},{"wholeBusExecuted",false}');
  text = replaceOnce(text, 'id.deviceClass+": actual classifier', 'row.dump()+": actual classifier');
  return text;
}