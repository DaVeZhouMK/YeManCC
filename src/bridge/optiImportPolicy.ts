// Informational only: none of these actions change anti-cheat, services or Steam options.
export const OPTI_OFFLINE_GUIDE = [
  'YMCC 不执行反作弊绕过，也不判断当前运行方式是否安全。',
  '优先查找游戏官方支持的不启用反作弊的单人/离线启动方式；不同游戏没有保证通用有效的方法。',
  '仅断网或使用 Steam 离线模式，不代表反作弊已关闭。没有确认可用方式时，不建议带着 OPT 启动受保护的游戏。',
  '导入只部署 OPT 文件与配置，不保证运行库能够加载或插帧生效。原 OPT 客户端仍可能拒绝为反作弊游戏配置插帧。',
  '返回联机前，请先卸载并还原 YMCC 部署的 OPT 文件，并自行恢复手动改过的设置。YMCC 的还原不包含这些手动设置。',
].join('\n\n');

export function optiAntiCheatWarning(gameName: string): string {
  return `「${gameName}」检测到反作弊。\n\nYMCC 不会关闭或绕过反作弊。继续仅部署 OPT 文件及配置，可能被阻止加载，不能保证插帧生效或联机安全。\n\n仅供你自行确认的离线单人环境使用；联机使用可能导致封禁。导入后不会自动启动游戏。\n\n返回联机前必须先还原 OPT 文件；手动离线设置需要你自行恢复。\n\n是否了解风险并仍然导入？`;
}

export function optiMayOfferLaunch(acknowledgedAntiCheat: boolean, resultAntiCheat: boolean): boolean {
  return !acknowledgedAntiCheat && !resultAntiCheat;
}
