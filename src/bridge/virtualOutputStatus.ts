export interface VirtualOutputRuntimeState {
  state?: string;
  reason?: string;
  requestedPersona?: string;
  boundPersona?: string;
  hostAlive?: boolean;
  prepared?: boolean;
  neutralized?: boolean;
  frameAccepted?: boolean;
  sourceLive?: boolean;
  /** Read-only Steam process observation; null/absent means unknown, not stopped. */
  steamRunning?: boolean | null;
  suppressionState?: 'unknown' | 'verified' | 'degraded';
}

export interface VirtualOutputPresentation {
  text: string;
  detail: string;
  tone: 'on' | 'off' | 'pending' | 'warning';
}

const REASONS: Record<string, string> = {
  'backend-fault-hold': '后端发生错误，虚拟输出已停止，请重新选择手柄模式重试。',
  'physical-fallback': '虚拟手柄未恢复，已请求退回物理手柄。',
  'transport-fault': '虚拟手柄后端通信异常。',
  'host-not-alive': '虚拟手柄后端尚未启动或已经退出。',
  'persona-mismatch': '当前实际运行的手柄模式与所选模式不一致。',
  'target-not-admitted': '虚拟设备尚未完成创建和输出准入。',
  'suppression-degraded': '已有读回显示物理手柄屏蔽配置异常，可能出现重复手柄。',
  'physical-source-unavailable': '当前未取得可用的物理手柄输入。',
};

// Saved configuration and driver-preparation warnings cannot establish runtime success.
export function virtualOutputPresentation(
  runtime: VirtualOutputRuntimeState | null, enabled: boolean, persona: string, busy = false,
): VirtualOutputPresentation {
  if (!enabled || persona === 'disabled') {
    return { text: '虚拟手柄-未开启', detail: '当前使用本机手柄。', tone: 'off' };
  }
  if (busy || (runtime?.requestedPersona && runtime.requestedPersona !== persona)) {
    return { text: '虚拟手柄-正在切换', detail: '等待所选手柄模式的实际运行结果。', tone: 'pending' };
  }
  if ((persona === 'steamdeck' || persona === 'dualsense-edge') && runtime?.steamRunning === false) {
    const name = persona === 'steamdeck' ? 'SteamDeck' : 'PS5';
    return {
      text: '虚拟手柄-Steam未开启实际无效', tone: 'warning',
      detail: `${name} 虚拟手柄需要 Steam 运行后才能实际生效。请先启动 Steam，再确认手柄已被识别。`,
    };
  }
  if (!runtime || !runtime.requestedPersona || runtime.state === 'unknown') {
    return { text: '虚拟手柄-状态待确认', detail: '等待后端运行状态，已保存设置不代表成功开启。', tone: 'pending' };
  }
  if (runtime.state === 'recovering') {
    return { text: '虚拟手柄-正在恢复', detail: '恢复完成后会重新确认设备和输入状态。', tone: 'pending' };
  }
  if (runtime.state === 'starting') {
    return { text: '虚拟手柄-正在开启', detail: '等待设备创建、输入准入和第一帧回执。', tone: 'pending' };
  }
  if (runtime.state === 'active') {
    if (runtime.boundPersona !== persona || !runtime.hostAlive || !runtime.prepared ||
        !runtime.neutralized || !runtime.frameAccepted || !runtime.sourceLive ||
        runtime.suppressionState === 'degraded') {
      return { text: '虚拟手柄-疑似失败', detail: '运行回执不完整或所选模式未生效，请导出日志。', tone: 'warning' };
    }
    return {
      text: '虚拟手柄-成功开启', tone: 'on',
      detail: '虚拟设备已创建，后端已接收当前手柄输入。Steam 中的设备数量和背键编辑仍以实际表现为准。',
    };
  }
  if (runtime.state === 'suspected-failed') {
    return { text: '虚拟手柄-疑似失败', detail: REASONS[runtime.reason || ''] || '尚未确认虚拟手柄正常输出，请导出日志。', tone: 'warning' };
  }
  return { text: '虚拟手柄-状态待确认', detail: '等待当前手柄模式的后端运行结果。', tone: 'pending' };
}
