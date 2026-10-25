/** Main-document wiring to YMCC's existing fan settings and lifecycle. */
import { powerLifecycle } from '@/bridge/api';
import { readSettingsSection } from '@/bridge/settingsRepository';
import { fanHostLifecycle } from '@/bridge/fanHost';
import { FAN_IMPORT_ENABLED, FAN_FORCE_PREVIEW, fanControlActive, getFanSelectedPreset, normalizeFanPresetCurves,
  saveFanPresetSelection, setFanControlActive, setFanNavigationDuty, recordFanHandshake, type FanPreset } from '@/bridge/fanFeature';
import { getFanMirrorDisplay, observeFanMirrorDisplay, isFanMirrorUiBusy } from '@/bridge/deckyFanDisplay';
import { createRadialFanActions } from './fanActions';
export function createLocalRadialFanActions(alive:()=>boolean) {
  return createRadialFanActions({
    owner:fanHostLifecycle,enabled:()=>fanControlActive.value,currentPreset:getFanSelectedPreset,
    read:()=>readSettingsSection('fan'),
    curves:(source,preset)=>normalizeFanPresetCurves(source.presetCurves,source.nodes,
      ['soft','balanced','aggressive'].includes(String(source.preset))?source.preset as FanPreset:'balanced')[preset],
    save:saveFanPresetSelection,updateActive:setFanControlActive,updateDuty:setFanNavigationDuty,
    power:()=>powerLifecycle.get(),featureAllowed:()=>alive()&&FAN_IMPORT_ENABLED&&!FAN_FORCE_PREVIEW,
    uiBusy:isFanMirrorUiBusy,currentDisplay:getFanMirrorDisplay,display:observeFanMirrorDisplay,
    observeCapability:allowed=>recordFanHandshake(allowed),nextRevision:()=>crypto.randomUUID(),
  });
}
