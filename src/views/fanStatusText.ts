/** Presentation only: detailed Host errors and operation state remain unchanged. */
export const FAN_STATUS_MAX_LENGTH = 12;

const LONG_STATUS_LABELS: Readonly<Record<string, string>> = {
  '风扇服务已启动，正在连接并握手…': '正在连接风扇服务',
  '握手成功，正在初始化并接管风扇…': '握手成功，正在接管',
  '首次启用正在初始化风扇，请稍候…': '首次开启，正在准备',
  '风扇服务已就绪，正在重新接管…': '正在重新接管',
  '正在恢复风扇曲线并确认控制…': '正在恢复曲线',
  '正在确认恢复后的风扇控制': '正在确认恢复控制',
  '控制意图已开启，正在恢复…': '正在恢复风扇',
  '控制意图已登记，正在恢复…': '正在恢复风扇',
  '控制意图已保留，正在恢复…': '正在恢复风扇',
  '风扇状态暂不可读，控制意图已保留；正在继续检查…': '正在确认风扇状态',
};

const CONFIRMED_CONTROL_LABELS: ReadonlySet<string> = new Set([
  '控制已开启', '风扇已开启', '风扇控制已开启',
  '风扇控制已开启，无重复启用调用', '风扇控制已由 YMCC 确认开启',
]);

/** A header summary, never a control/admission decision or a replacement error. */
export function compactFanStatus(detail: string): string {
  const message = detail.trim();
  if (Object.prototype.hasOwnProperty.call(LONG_STATUS_LABELS, message)) return LONG_STATUS_LABELS[message];
  if (/FAN_DEVICE_GATE_DENIED|设备门拒绝|设备类型.*不在/i.test(message)) return '机型暂未适配';
  if (/FAN_UNSUPPORTED|风扇不支持/i.test(message)) return '机型暂不支持';
  if (/REAL_WRITE_AUTH_REQUIRED|写入.*未.*验证|能力待确认|写入.*恢复验证尚未完成/i.test(message)) return '控制能力待确认';
  if (/依赖校验失败|环境不可用|Fan Host 不存在|runtimeconfig|Windows Desktop Runtime|程序集|FileNotFoundException|DllNotFoundException/i.test(message)) return '风扇环境不可用';
  if (/AUTH_REQUIRED|AUTH_REJECTED|UNAUTHORIZED|FORBIDDEN|凭据|授权失败/i.test(message)) return '风扇授权失败';
  if (/FAN_ROUTE_CONFLICT|HC_OPENLIB_CONFLICT|EXTERNAL_FAN_OWNER|其他风扇控制程序/i.test(message)) return '其他程序正在控制';
  if (/LEASE_INVALID|LEASE_REQUIRED|租约.*失效|lease/i.test(message)) return '控制已失效，请重试';
  if (/POWER_SUSPENDING|睡眠中/i.test(message)) return '睡眠中';
  if (/POWER_RESUMING|FAN_RESUME_|系统正在恢复/i.test(message)) return '正在准备恢复';
  if (/HC_SESSION_ROUTE_LOST|连接.*中断/i.test(message)) return '风扇连接已中断';
  if (/FAULT_LOCKED|fault-locked|故障锁定/i.test(message)) return '风扇需重新开启';
  if (/OEM_RESTORE_FAILED|OEM.*恢复未确认|交还.*未确认|FAN_ROUTE_RESTORE_UNAVAILABLE/i.test(message)) return '风扇交还未确认';
  if (/交还仍在进行/.test(message)) return '交还仍在进行';
  if (/风扇自动恢复未完成/.test(message)) return '风扇自动恢复未完成';
  if (/恢复未完成/.test(message)) return '恢复未完成，请重试';
  if (/IPC command timed out|timeout|timed out|超时|未响应/i.test(message)) return '风扇响应超时';
  if (/启动风扇设置保存失败/.test(message)) return '自启设置保存失败';
  if (/图标设置保存失败/.test(message)) return '图标设置保存失败';
  if (/曲线.*保存失败|预设.*保存失败/.test(message)) return '曲线保存失败';
  if (/保存失败/.test(message)) return '设置保存失败';
  if (/风扇服务已停止/.test(message)) return '风扇服务已停止';
  if (/交还|交回/.test(message) && /等待|正在确认/.test(message)) return '交还仍在进行';
  if (/等待.*(?:真实信号|确认|生命周期)|尚未确认.*风扇控制/.test(message)
      || (/控制意图/.test(message) && !/正在恢复|正在重建|重新接管/.test(message))) return '正在确认风扇状态';
  const savedNode = /^节点 ([1-4]) 已保存/.exec(message);
  if (savedNode) return '节点 ' + savedNode[1] + ' 已保存';
  if (/风扇挡位已保存/.test(message)) return '挡位已保存';
  const editingNode = /^节点 ([1-4]) 编辑中/.exec(message);
  if (editingNode) return '节点 ' + editingNode[1] + ' 编辑中';
  if (/模拟控制已开启/.test(message)) return '模拟控制已开启';
  if (/关闭中/.test(message)) return '正在关闭控制';
  if (/正在应用风扇曲线/.test(message)) return '正在应用风扇曲线';
  if (/正在恢复|正在重建|重新接管/.test(message)) return '正在恢复风扇';
  if (/启用中|正在启动风扇服务/.test(message)) return '正在启动风扇服务';
  // Only explicit confirmed source phrases may summarize as established control.
  // Mirror '已开启' may only mean retained intent while the Host signal is pending.
  if (CONFIRMED_CONTROL_LABELS.has(message)) return '控制已开启';
  if (message === '风扇控制已关闭（原生命周期已确认）') return '控制已关闭';
  if (message === '风扇设置已由 YMCC 原生命周期确认') return '风扇设置已确认';
  // Keep short user-facing messages. Unknown long notices never expose a clipped
  // path/code as their summary; the component keeps the full detail in its title.
  if (message && message.length <= FAN_STATUS_MAX_LENGTH && !/[A-Z_]{4,}/.test(message)) return message;
  return /失败|错误|拒绝|error|fail|denied/i.test(message) ? '风扇操作失败' : '风扇状态待确认';
}
