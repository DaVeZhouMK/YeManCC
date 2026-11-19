import { invoke } from './ipc';

export interface GameRulesSnapshot {
  blacklist: string[];
  whitelist: string[];
}

export type GameRuleList = 'blacklist' | 'whitelist';

const LITERAL_RULE_PREFIX = '@exe:';
function validLiteralRuleName(value: string): boolean {
  return value.endsWith('.exe.exe') && value.length >= 8 &&
    !value.startsWith(' ') && !value.endsWith(' ') &&
    !/[\u0000-\u001f\\/:*?\[\]<>|#"]/.test(value);
}

export function gameRuleNameFromPath(path: string): string {
  const input = String(path || '').trim().toLowerCase();
  if (input.startsWith('@exe') && input.includes(':')) {
    if (!input.startsWith(LITERAL_RULE_PREFIX) || !validLiteralRuleName(input.slice(LITERAL_RULE_PREFIX.length)))
      throw new Error('进程规则格式未知或无效，已保留原名单');
    return input;
  }
  const file = input.split(/[\\/]/).pop() || '';
  if (validLiteralRuleName(file)) return LITERAL_RULE_PREFIX + file;
  return file.endsWith('.exe') ? file.slice(0, -4) : file;
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
