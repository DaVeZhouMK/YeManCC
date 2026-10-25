import contract from '../../PowerControl/settings-baseline.json';

/** Version the storage contract independently of the product's release number. */
export const SETTINGS_BASELINE = contract;
export interface ConfigurationBaseline extends Record<string, unknown> {
  id: string;
  revision: number;
  legacyImported: boolean;
}
const own = (value: object, key: string) => Object.prototype.hasOwnProperty.call(value, key);
const object = (value: unknown): value is Record<string, any> => !!value && typeof value === 'object' && !Array.isArray(value);

export function assertSupportedSettingsBaseline(document: Record<string, any>): void {
  if (!own(document, contract.stampKey)) return; // Existing releases have no receipt.
  const stamp = document[contract.stampKey];
  if (!object(stamp) || stamp.id !== contract.id || !Number.isSafeInteger(stamp.revision) ||
      stamp.revision < 1 || stamp.revision > contract.revision || stamp.legacyImported !== true) {
    throw new Error('配置基准无法由本程序兼容读取。原文件已保留，请使用匹配版本或备份后重置应用配置。');
  }
}

export function hasCompletedSettingsBaseline(document: Record<string, any>): boolean {
  assertSupportedSettingsBaseline(document);
  return own(document, contract.stampKey);
}

export function stampSettingsBaseline(document: Record<string, any>): void {
  assertSupportedSettingsBaseline(document);
  document[contract.stampKey] = {
    ...(object(document[contract.stampKey]) ? document[contract.stampKey] : {}),
    id: contract.id, revision: contract.revision, legacyImported: true,
  };
}

/** JSON object order is not a change; arrays and explicit values are significant. */
export function equalSettingsDocuments(left: unknown, right: unknown): boolean {
  if (left === right) return true;
  if (Array.isArray(left) && Array.isArray(right)) {
    return left.length === right.length && left.every((value, index) => equalSettingsDocuments(value, right[index]));
  }
  if (!object(left) || !object(right)) return false;
  const keys = Object.keys(left);
  return keys.length === Object.keys(right).length && keys.every(key => own(right, key) && equalSettingsDocuments(left[key], right[key]));
}
