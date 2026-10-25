import { loadSettings, updateSettings, getSettingsGeneration, assertSettingsGeneration } from '@/bridge/settingsRepository';
import { normalizeRadialSettings, type RadialSettings } from './model';
export async function loadRadialSettings():Promise<RadialSettings>{return normalizeRadialSettings((await loadSettings()).radialMenu);}
export async function saveRadialSettings(expectedRevision:number,value:RadialSettings):Promise<RadialSettings>{
  // Normalization rebuilds every persisted field, including nested menu items.
  // It accepts Vue proxies and produces a detached snapshot without cloning one.
  const generation=getSettingsGeneration(),draft=normalizeRadialSettings(value);
  await updateSettings(settings=>{assertSettingsGeneration(generation);const current=normalizeRadialSettings(settings.radialMenu);
    if(current.revision!==expectedRevision)throw new Error('轮盘设置已在其他窗口修改，请重新载入。');
    draft.revision=current.revision+1;settings.radialMenu={...settings.radialMenu,...draft};
  });window.dispatchEvent(new CustomEvent('ymcc:radial-settings-changed'));return draft;
}
