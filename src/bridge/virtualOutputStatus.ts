import { STEAM_DECK_SELF_OUTPUT_REASON, STEAM_DECK_SELF_OUTPUT_MESSAGE } from './virtualTargetPolicy';

/** Native policy observations; saved intent alone cannot establish passthrough. */
export interface ExternalPriorityRuntimeState {
  enabled: boolean;
  state: string;
  reason: string;
  externalPresent: boolean;
  builtinHidden: boolean;
  virtualSuspended: boolean;
}

/** Native owns admission and target state; UI may bound cached waiting without declaring success. */
export interface VirtualStartProtectionState {
  locked: boolean;
  requestId: number;
  phase: 'idle' | 'pending' | 'verifying' | 'success' | 'failure' | 'cancelled' | 'expired';
  persona: string;
  remainingMs: number;
  elapsedMs: number;
  /** Producer wall-clock deadline bounds display waiting only; never grants native admission or proves target success. */
  expiresAtUnixMs?: number;
  maxMs: 30000;
  settleMs: 10000;
  verificationPending?: boolean;
  verificationRemainingMs?: number;
  candidateOutcome?: string;
}

export interface VirtualOutputRuntimeState {
  startProtection?: VirtualStartProtectionState;
  state?: string;
  reason?: string;
  requestedPersona?: string;
  boundPersona?: string;
  /** Native host generation; zero means no current observation. */
  hostEpoch?: number;
  hostAlive?: boolean;
  prepared?: boolean;
  neutralized?: boolean;
  frameAccepted?: boolean;
  sourceLive?: boolean;
  /** Fresh canonical producer read in the current power/topology context. */
  sourceCurrent?: boolean;
  /** Read-only Steam process observation; null/absent means unknown, not stopped. */
  steamRunning?: boolean | null;
  suppressionState?: 'unknown' | 'verified' | 'degraded';
  /** Backend transaction work remains; never a UI timer or a saved-mode guess. */
  switchPending?: boolean;
  /** MSI virtual-OFF return debt with active backend recovery/observation work. */
  physicalReturnPending?: boolean;
  /** Bounded return observation ended without a match; clear only on real confirmation. */
  physicalReturnFailed?: boolean;
}

export interface VirtualOutputPresentation {
  text: string;
  detail: string;
  tone: 'on' | 'off' | 'pending' | 'warning';
}

const REASONS: Record<string, string> = {
  [STEAM_DECK_SELF_OUTPUT_REASON]: STEAM_DECK_SELF_OUTPUT_MESSAGE,
  'backend-fault-hold': '后端发生错误，虚拟输出已停止，请重新选择手柄模式重试。',
  'physical-fallback': '虚拟手柄未恢复，已请求退回物理手柄。',
  'physical-return-unconfirmed': '本次恢复观察已结束，尚未确认本机手柄的预期设备信号；等待后续设备事件确认。',
  'transport-fault': '虚拟手柄后端通信异常。',
  'host-not-alive': '虚拟手柄后端尚未启动或已经退出。',
  'persona-mismatch': '当前实际运行的手柄模式与所选模式不一致。',
  'target-not-admitted': '虚拟设备尚未完成创建和输出准入。',
  'suppression-degraded': '已有读回显示物理手柄屏蔽配置异常，可能出现重复手柄。',
  'physical-source-unavailable': '当前未取得可用的物理手柄输入。',
};

const EXTERNAL_PRIORITY_POLICY = '经核验的外接手柄可用时暂停本机虚拟输出、隐藏本机物理手柄，让外接手柄直通；断开后恢复已请求模式，保留所选模式。';

export function externalControllerPriorityPresentation(
  runtime: ExternalPriorityRuntimeState | null, enabled: boolean, busy = false,
): VirtualOutputPresentation {
  if (busy) {
    return { text: '正在保存', detail: '等待外接手柄优先设置的保存结果。' + EXTERNAL_PRIORITY_POLICY, tone: 'pending' };
  }
  if (!runtime || runtime.enabled !== enabled || !runtime.state || runtime.state === 'unknown') {
    return { text: enabled ? '已开启，状态待确认' : '已关闭，状态待确认', detail: '等待后端确认外接手柄优先状态。' + EXTERNAL_PRIORITY_POLICY, tone: 'pending' };
  }
  if (runtime.state === 'degraded') {
    return { text: '外接优先恢复待确认', detail: '尚未确认本机屏蔽或归还完成，请导出日志。保留已请求的手柄模式。', tone: 'warning' };
  }
  if (runtime.state === 'taking-over' || runtime.state === 'transitioning') {
    return { text: '正在接管外接手柄', detail: '等待本机虚拟输出释放和物理手柄屏蔽的实际结果。', tone: 'pending' };
  }
  if (runtime.state === 'holding') {
    return { text: '等待外接手柄恢复', detail: '短暂断连期间保留本机屏蔽，确认持续断开后再恢复本机输出。', tone: 'pending' };
  }
  if (runtime.state === 'suspended') {
    return { text: '唤醒后等待手柄确认', detail: '保留外接优先策略，等待本次电源状态和设备身份重新确认。', tone: 'pending' };
  }
  if ((!enabled || runtime.externalPresent !== true) && (runtime.builtinHidden === true || runtime.virtualSuspended === true)) {
    return { text: '正在恢复本机输出', detail: '等待本机手柄恢复的后端确认，保留已请求的手柄模式。', tone: 'pending' };
  }
  if (!enabled) {
    return { text: '已关闭', detail: '外接手柄优先已关闭。' + EXTERNAL_PRIORITY_POLICY, tone: 'off' };
  }
  if (runtime.externalPresent !== true) {
    return { text: '等待外接手柄', detail: '尚未确认有可用的外接手柄，继续按已请求模式运行。' + EXTERNAL_PRIORITY_POLICY, tone: 'pending' };
  }
  if (runtime.builtinHidden !== true || runtime.virtualSuspended !== true || runtime.state !== 'active') {
    return { text: '外接优先待确认', detail: '已检测到外接手柄，本机物理手柄隐藏或虚拟输出暂停尚未确认。' + EXTERNAL_PRIORITY_POLICY, tone: 'warning' };
  }
  return { text: '外接手柄直通', detail: '本机物理手柄已隐藏，本机虚拟输出已暂停；外接断开后恢复已请求模式，保留所选模式。', tone: 'on' };
}

/** Reject an older request even when it selects the same persona/source again. */
export function acceptVirtualOutputRuntime(
  previous: VirtualOutputRuntimeState | null, incoming: VirtualOutputRuntimeState | null,
  minimumRequestId = -1,
): VirtualOutputRuntimeState | null {
  const id = (value: VirtualOutputRuntimeState | null) => {
    const n = value?.startProtection?.requestId;
    return Number.isSafeInteger(n) && n! >= 0 ? n! : -1;
  };
  const floor = Math.max(minimumRequestId, id(previous));
  if (incoming && id(incoming) < floor) return id(previous) >= floor ? previous : null;
  return incoming;
}

// Saved intent, a candidate outcome and an expired UI wait never establish success.
export function virtualOutputPresentation(
  runtime: VirtualOutputRuntimeState | null, enabled: boolean, persona: string, busy = false,
  externalPriority: ExternalPriorityRuntimeState | null = null,
): VirtualOutputPresentation {
  const pending = (text: string, detail: string): VirtualOutputPresentation => ({ text: '虚拟手柄-' + text, detail, tone: 'pending' });
  const failure = (reason: string): VirtualOutputPresentation => ({ text: '虚拟手柄-失败',
    detail: REASONS[reason] || '后端已报告失败，尚未确认所选虚拟手柄正常输出，请导出日志。', tone: 'warning' });
  if (!enabled || persona === 'disabled') {
    if (runtime?.physicalReturnPending === true) {
      return { text: '本机手柄-正在恢复', detail: '已请求关闭虚拟输出，等待后端完成本机手柄恢复并确认设备信号。', tone: 'pending' };
    }
    if (runtime?.physicalReturnFailed === true) {
      return { text: '本机手柄-恢复未确认', detail: REASONS['physical-return-unconfirmed'], tone: 'warning' };
    }
    const detail = externalPriority?.enabled === true && externalPriority.externalPresent === true && externalPriority.builtinHidden === true
      ? '外接手柄直通，本机物理手柄已隐藏；虚拟手柄保持未开启。'
      : '当前未请求虚拟手柄。';
    return { text: '虚拟手柄-未开启', detail, tone: 'off' };
  }
  const protection = runtime?.startProtection;
  const identityMismatch = (!!runtime?.requestedPersona && runtime.requestedPersona !== persona) ||
    (protection?.requestId > 0 && !!protection.persona && protection.persona !== persona);
  // A local save has not acquired its native request identity yet.
  if (busy || identityMismatch) return pending('正在切换', '等待所选手柄模式的本次运行回执。');
  if (persona === 'steamdeck' && runtime?.reason === STEAM_DECK_SELF_OUTPUT_REASON) {
    return { text: '虚拟手柄-本机不可用', detail: REASONS[STEAM_DECK_SELF_OUTPUT_REASON], tone: 'warning' };
  }
  if (runtime?.state === 'external-priority') {
    return { text: '虚拟手柄-外接优先暂停', tone: 'off',
      detail: '外接手柄优先策略已暂停本机虚拟输出；外接断开后恢复已请求模式，保留所选模式。' };
  }
  const terminalFault = runtime?.state === 'suspected-failed' &&
    (runtime.reason === 'backend-fault-hold' || runtime.reason === 'physical-fallback');
  if (terminalFault) return failure(runtime!.reason!);
  if (protection?.phase === 'failure') return failure(runtime?.reason || '');
  // Backend work can temporarily carry the previous host's incomplete observations.
  if (runtime?.switchPending === true) {
    if (runtime.state === 'recovering') return pending('正在恢复', '恢复完成后会重新确认设备和输入状态。');
    if (!runtime.hostAlive || runtime.boundPersona !== persona || !runtime.prepared || !runtime.neutralized)
      return pending('设备创建中', '等待当前模式的设备创建、绑定和输入准入回执。');
    if (!runtime.sourceLive || runtime.sourceCurrent === false || !runtime.frameAccepted)
      return pending('等待输入', '设备准备中，等待当前物理输入和后端接收帧回执。');
    return pending('检查中', '设备和输入已有回执，等待本次后端切换完成。');
  }
  if ((persona === 'steamdeck' || persona === 'dualsense-edge') && runtime?.steamRunning === false) {
    const name = persona === 'steamdeck' ? 'SteamDeck' : 'PS5';
    return { text: '虚拟手柄-Steam未开启实际无效', tone: 'warning',
      detail: name + ' 虚拟手柄需要 Steam 运行后才能实际生效。请先启动 Steam，再确认手柄已被识别。' };
  }
  if (!runtime || !runtime.requestedPersona) return pending('状态待确认', '等待后端运行状态，已保存设置不代表成功开启。');
  if (runtime.state === 'recovering') return pending('正在恢复', '恢复完成后会重新确认设备和输入状态。');
  const outputReady = runtime.boundPersona === persona && runtime.hostAlive === true && runtime.prepared === true &&
    runtime.neutralized === true && runtime.frameAccepted === true && runtime.sourceLive === true &&
    runtime.sourceCurrent !== false && runtime.hostEpoch !== 0;
  if (runtime.suppressionState === 'degraded' || runtime.reason === 'suppression-degraded') {
    const inspecting = protection?.locked === true && (protection.phase === 'pending' || protection.phase === 'verifying' || protection.verificationPending === true);
    return { text: '虚拟手柄-' + (outputReady ? inspecting ? '检查中，隔离异常' : '已开启，隔离异常' : '隔离异常'), tone: 'warning',
      detail: (outputReady ? '设备已创建，后端已接收当前物理输入。' : '当前输出链路仍需确认。') + REASONS['suppression-degraded'] +
        (inspecting ? '本次切换仍在检查，等待最终结果。' : '') };
  }
  // These are explicit native fault observations, even during inspection.
  if (runtime.state === 'suspected-failed' && runtime.reason !== 'physical-source-unavailable' &&
      runtime.reason !== 'physical-source-unconfirmed') return failure(runtime.reason || '');
  const inspecting = protection?.locked === true &&
    (protection.phase === 'pending' || protection.phase === 'verifying' || protection.verificationPending === true);
  if (runtime.state === 'starting' || inspecting) {
    if (!runtime.hostAlive || runtime.boundPersona !== persona || !runtime.prepared || !runtime.neutralized)
      return pending('设备创建中', '等待当前模式的设备创建、绑定和输入准入回执。');
  } else if (runtime.state === 'active') {
    if (!runtime.hostAlive) return failure('host-not-alive');
    if (runtime.boundPersona !== persona) return failure('persona-mismatch');
    if (!runtime.prepared || !runtime.neutralized) return failure('target-not-admitted');
  }
  if (runtime.sourceCurrent === false || runtime.sourceLive === false ||
      runtime.reason === 'physical-source-unconfirmed' || runtime.reason === 'physical-source-unavailable' ||
      (runtime.hostAlive && runtime.prepared && runtime.frameAccepted === false))
    return pending('等待输入', '尚未取得当前物理输入或后端接收帧回执；已有启动成功记录不能代替当前输入证据。');
  if (inspecting) return pending('检查中', '本次切换检查尚未结束；等待当前设备与输入链路的最终核验结果。');
  if (protection?.phase === 'expired' || protection?.phase === 'cancelled')
    return pending('状态待确认', '本次检查已超时或取消，尚未取得新的最终运行结果。');
  if (runtime.state === 'active' && runtime.boundPersona === persona && runtime.hostAlive && runtime.prepared &&
      runtime.neutralized && runtime.frameAccepted && runtime.sourceLive && runtime.hostEpoch !== 0) {
    return { text: '虚拟手柄-成功开启', tone: 'on',
      detail: '虚拟设备已创建，后端已接收当前手柄输入。Steam 中的设备数量和背键编辑仍以实际表现为准。' };
  }
  return pending('状态待确认', '等待当前手柄模式的后端运行结果。');
}
