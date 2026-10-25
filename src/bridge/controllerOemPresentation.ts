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
      const numberedRear = key.keyId === 'm1' || key.keyId === 'm2';
      const backIndex = numberedRear && key.backIndex <= 2 ? key.backIndex + 2 : key.backIndex;
      return { ...key, backIndex, label: STEAM_DECK_REAR_NAMES[backIndex - 1] };
    }),
  };
}

export function formatControllerShortcutDisplayInputs(
  inputs: readonly ControllerShortcutInput[], persona: string | undefined, catalog: OemKeysSnapshot,
): string {
  const displayed = controllerOemDisplayCatalog(persona, catalog);
  return inputs.map(input => input.source === 'oem'
    ? displayed.keys.find(key => key.keyId === input.code)?.label || input.code
    : formatControllerShortcutInput(input)).join(' + ');
}
