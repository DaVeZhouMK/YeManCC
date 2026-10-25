/** Native machine policy only. Missing/old-host state is unknown, never a model guess. */
export interface VirtualTargetPolicy {
  steamDeckSelfOutputBlocked: boolean;
  reason: string;
}
export const STEAM_DECK_SELF_OUTPUT_REASON = 'physical-steamdeck-self-output-blocked';
export const STEAM_DECK_SELF_OUTPUT_MESSAGE = '真实 Steam Deck 不可选择 SteamDeck 虚拟输出，请改用 PS5 或 Xbox。已保存的 SteamDeck 配置会保留，但在本机不可用。';
export function readVirtualTargetPolicy(state: unknown): VirtualTargetPolicy | null {
  if (!state || typeof state !== 'object' || Array.isArray(state)) return null;
  const policy = (state as Record<string, unknown>).virtualTargetPolicy;
  if (!policy || typeof policy !== 'object' || Array.isArray(policy)) return null;
  const value = policy as Record<string, unknown>;
  if (typeof value.steamDeckSelfOutputBlocked !== 'boolean') return null;
  return { steamDeckSelfOutputBlocked: value.steamDeckSelfOutputBlocked,
    reason: typeof value.reason === 'string' ? value.reason : '' };
}
export function virtualTargetSelectionError(persona: unknown, policy: VirtualTargetPolicy | null, enabled = true): string {
  return enabled && persona === 'steamdeck' && policy?.steamDeckSelfOutputBlocked === true
    ? STEAM_DECK_SELF_OUTPUT_MESSAGE : '';
}
export function requireVirtualTargetAllowed(persona: unknown, policy: VirtualTargetPolicy | null, enabled = true): void {
  const error = virtualTargetSelectionError(persona, policy, enabled);
  if (error) throw new Error(error);
}
export function virtualTargetOptions<T extends { value?: string | number; data?: string; label: string; disabled?: boolean }>(
  options: readonly T[], policy: VirtualTargetPolicy | null,
): Array<T & { sub?: string }> {
  return options.map(option => virtualTargetSelectionError(option.value ?? option.data, policy)
    ? { ...option, label: option.label + '（本机不可用）', disabled: true, sub: STEAM_DECK_SELF_OUTPUT_MESSAGE } : { ...option });
}

interface VirtualToggleRule {
  id: string;
  actionId: string;
  enabled: boolean;
  params: Record<string, unknown>;
}
/** Preserve old saved rules; reject newly selected/enabled blocked virtual presets. */
export function virtualTargetShortcutError(previous: readonly VirtualToggleRule[], next: readonly VirtualToggleRule[], policy: VirtualTargetPolicy | null): string {
  for (const rule of next) {
    if (!rule.enabled || rule.actionId !== 'input.virtualGamepadToggle' ||
        !virtualTargetSelectionError(rule.params.preset ?? 'steamdeck', policy)) continue;
    const old = previous.find(candidate => candidate.id === rule.id);
    if (!old?.enabled || old.actionId !== rule.actionId || (old.params.preset ?? 'steamdeck') !== (rule.params.preset ?? 'steamdeck'))
      return STEAM_DECK_SELF_OUTPUT_MESSAGE;
  }
  return '';
}
