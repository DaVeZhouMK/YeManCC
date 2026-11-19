import { formatControllerShortcutInput, type ControllerShortcutInput, type OemKeysSnapshot } from './controllerShortcutRules';

/** Steam Deck output names/slots; physical key IDs remain the recording contract. */
const STEAM_DECK_REAR_NAMES = ['L4', 'R4', 'L5', 'R5'] as const;

export function controllerOemDisplayCatalog(persona: string | undefined, catalog: OemKeysSnapshot): OemKeysSnapshot {
  if (persona !== 'steamdeck' || !catalog.supported) return catalog;
  return {
    ...catalog,
    keys: catalog.keys.map(key => {
      // Some models reuse m1 for a front auxiliary key. Only rear keys participate.
      if (key.backIndex == null || !Number.isInteger(key.backIndex) || key.backIndex < 1 || key.backIndex > 4) return key;
      // The model catalog owns front-view left/right; M1/M2 numbers do not.
      // Lift a numbered rear key into the lower Deck row on the SAME side.
      // MSI G3E: M1 is right -> R5, M2 is left -> L5. Already lifted slots stay stable.
      const numberedRear = (key.keyId === 'm1' || key.keyId === 'm2') &&
        catalog.deviceClass !== 'HandheldCompanion.Devices.AYANEONEXT2';
      const backIndex = numberedRear && key.backIndex <= 2 ? key.backIndex + 2 : key.backIndex;
      return { ...key, backIndex, label: STEAM_DECK_REAR_NAMES[backIndex - 1] };
    }),
  };
}

/** Describe a raw key's possible profile contribution without changing its saved identity. */
export function controllerShortcutInputLabel(input: ControllerShortcutInput, catalog: OemKeysSnapshot): string {
  const raw = formatControllerShortcutInput(input);
  if (input.source !== 'keyboard' || !catalog.supported) return raw;
  const candidates = catalog.keys.filter(key => key.captureImplemented !== false && key.keyboardContributions?.some(rule =>
    rule.earlierGroupRules.length === 0 && rule.keyGroups.length === 1 && rule.mouseCodes.length === 0 && rule.keyGroups[0].includes(input.code)));
  const gated = candidates.length === 1 && candidates[0].keyboardContributions?.some(rule => rule.keyGroups.length === 1 && rule.keyGroups[0].includes(input.code) && rule.requiresBoundInterface);
  return candidates.length === 1 ? `${raw}（${candidates[0].label} ${gated ? '条件映射' : '映射'}）` : raw;
}

export function controllerShortcutInputDefinition(input: ControllerShortcutInput, catalog: OemKeysSnapshot): string {
  if (input.source !== 'oem') {
    const gated = input.source === 'keyboard' && catalog.supported && catalog.keys.some(key => key.captureImplemented !== false && key.keyboardContributions?.some(rule =>
      rule.keyGroups.length === 1 && rule.mouseCodes.length === 0 && rule.keyGroups[0].includes(input.code) && rule.requiresBoundInterface));
    return `${input.source === 'keyboard' ? '键盘' : input.source === 'mouse' ? '鼠标' : '手柄'}输入：${input.code}。保存时保留此来源和键码。${gated ? '专用键映射仅为条件候选，需当前背键接口及发布／接口代次有效。' : ''}`;
  }
  const key = catalog.keys.find(key => key.keyId === input.code);
  if (!key) return `专用输入：${input.code}。当前机型清单未提供定义。`;
  const trigger = key.triggerCapability === 'click-only' ? '一次点击事件，仅支持按下／双击。' : '支持的触发方式按当前能力定义；持续按住仍取决于设备实际信号。';
  const expressions = key.keyboardContributions?.map(rule => {
    const groups = rule.keyGroups.map(group => [...new Set(group.map(code => formatControllerShortcutInput({ source: 'keyboard', code })))].join('／'));
    const chord = [...groups, ...rule.mouseCodes.map(code => formatControllerShortcutInput({ source: 'mouse', code }))].join(' + ');
    return `${chord}${rule.requiresBoundInterface ? '（需当前背键接口及代次有效）' : ''}${rule.earlierGroupRules.length ? '（同组更早规则匹配时不成立）' : ''}`;
  }).join(' 或 ');
  const contributionKind = key.keyboardContributions?.some(rule => rule.mouseCodes.length) ? key.keyboardContributions.some(rule => rule.keyGroups.length) ? '键盘／鼠标' : '鼠标' : '键盘';
  return `专用输入：${key.keyId}。${trigger}${expressions ? `${contributionKind}贡献：${expressions}。允许附带键，左右修饰键合并识别；保存的专用规则仍核对完整专用状态。` : ''}不自动合并键盘与专用输入。`;
}

export function formatControllerShortcutDisplayInputs(
  inputs: readonly ControllerShortcutInput[], persona: string | undefined, catalog: OemKeysSnapshot,
): string {
  const displayed = controllerOemDisplayCatalog(persona, catalog);
  return inputs.map(input => input.source === 'oem'
    ? displayed.keys.find(key => key.keyId === input.code)?.label || input.code
    : controllerShortcutInputLabel(input, catalog)).join(' + ');
}

/** Hardware preview only. Gesture IDs remain in the full recording/action catalog. */
export function controllerOemPhysicalKeys(catalog: OemKeysSnapshot) {
  return catalog.keys.filter(key => !key.physicalKeyIds?.length ||
    (key.physicalKeyIds.length === 1 && key.physicalKeyIds[0] === key.keyId));
}

export function controllerOemPhysicalPressedKeys(pressed: readonly string[], catalog: OemKeysSnapshot): string[] {
  const byId = new Map(catalog.keys.map(key => [key.keyId, key]));
  return [...new Set(pressed.flatMap(id => {
    const key = byId.get(id);
    return key?.physicalKeyIds?.length ? key.physicalKeyIds : [id];
  }))];
}
