import { invoke } from './ipc';

export interface GameRulesSnapshot {
  blacklist: string[];
  whitelist: string[];
}

export type GameRuleList = 'blacklist' | 'whitelist';

export function gameRuleNameFromPath(path: string): string {
  const file = String(path || '').trim().split(/[\\/]/).pop() || '';
  const lower = file.toLowerCase();
  return lower.endsWith('.exe') ? lower.slice(0, -4) : lower;
}

function normalizeSnapshot(raw: Partial<GameRulesSnapshot> | null | undefined): GameRulesSnapshot {
  const list = (value: unknown): string[] => {
    if (!Array.isArray(value)) return [];
    const out: string[] = [];
    for (const item of value) {
      const normalized = gameRuleNameFromPath(String(item));
      if (normalized && !out.includes(normalized)) out.push(normalized);
    }
    return out;
  };
  return {
    blacklist: list(raw?.blacklist),
    whitelist: list(raw?.whitelist),
  };
}

export async function getGameRules(): Promise<GameRulesSnapshot> {
  return normalizeSnapshot(await invoke<Partial<GameRulesSnapshot>>('game.rules.get'));
}

export async function setGameRuleList(
  kind: GameRuleList,
  values: string[],
): Promise<GameRulesSnapshot> {
  const result = await invoke<Partial<GameRulesSnapshot> & { ok?: boolean; error?: string }>(
    'game.rules.set',
    { [kind]: values.map(gameRuleNameFromPath).filter(Boolean) },
  );
  if (result?.ok === false) throw new Error(String(result.error || '名单保存失败'));
  return normalizeSnapshot(result);
}
