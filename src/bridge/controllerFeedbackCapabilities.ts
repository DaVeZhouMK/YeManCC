/**
 * Controller feedback presentation gate.
 *
 * HC has a complete physical-controller feedback lifecycle, but the current
 * YMCC InputHost only submits virtual input reports. In particular, it does
 * not yet subscribe to HIDMaestro HMController.OutputReceived/OutputDecoded
 * or forward decoded game feedback to a physical-controller transport. This
 * model keeps the controller page honest: persisted settings are not runtime
 * receipts. Until InputHost/Coordinator exposes an independent, tuple-bound
 * feedback receipt, every feedback capability remains UNENCLOSED.
 */

export type ControllerPersona = 'disabled' | 'xbox360' | 'dualshock4' | 'steamdeck' | 'dualsense' | 'elite' | 'dualsense-edge';
export type FeedbackCapabilityId = 'rumble' | 'haptic' | 'light';
export type FeedbackAvailability = 'available' | 'unavailable' | 'not-applicable';

export interface FeedbackCapabilityView {
  id: FeedbackCapabilityId;
  title: string;
  subtitle: string;
  availability: FeedbackAvailability;
  closure: 'CLOSED' | 'UNENCLOSED' | 'NOT-APPLICABLE';
  detail: string;
}

export interface ControllerFeedbackView {
  personaLabel: string;
  targetDetail: string;
  routeDetail: string;
  capabilities: FeedbackCapabilityView[];
  allClosed: boolean;
}

function personaLabel(persona: ControllerPersona): string {
  if (persona === 'dualshock4') return 'DualShock 4';
  if (persona === 'xbox360') return 'Xbox 360';
  if (persona === 'steamdeck') return 'Steam Deck';
  if (persona === 'dualsense') return 'DualSense (PS5)';
  if (persona === 'elite') return 'Xbox';                 // 2026-09-27 与控制器页显示名同步
  if (persona === 'dualsense-edge') return 'PS5';         // 2026-09-27 与控制器页显示名同步
  return '未选择';
}

interface FeedbackProof {
  targetRequested: boolean;
  targetClosed: boolean;
  descriptorKnown: boolean;
  manifestClosed: boolean;
  feedbackClosed: boolean;
  outputCallbackBound: boolean;
  physicalRouteClosed: boolean;
  releaseClosed: boolean;
}

function proofFrom(targetEnabled: boolean, persona: ControllerPersona): FeedbackProof {
  return {
    targetRequested: targetEnabled && persona !== 'disabled',
    targetClosed: false,
    descriptorKnown: false,
    manifestClosed: false,
    feedbackClosed: false,
    outputCallbackBound: false,
    physicalRouteClosed: false,
    releaseClosed: false,
  };
}

function pendingDetail(proof: FeedbackProof, noun: string): string {
  if (!proof.targetRequested) return `先启用虚拟手柄，${noun}才可接收游戏反馈。`;
  if (!proof.descriptorKnown) return `当前 ${noun} 的反馈格式尚未确认。`;
  if (!proof.outputCallbackBound) return '当前版本尚未接收游戏发出的反馈。';
  if (!proof.physicalRouteClosed) return '当前版本尚未建立反馈到实体手柄的安全通路。';
  if (!proof.releaseClosed) return '睡眠、断开和退出时的自动停止保护尚未确认。';
  return `${noun}暂不可用，等待运行时支持完成。`;
}

function capabilityView(
  proof: FeedbackProof,
  id: FeedbackCapabilityId,
  title: string,
  subtitle: string,
): FeedbackCapabilityView {
  const closed = false;
  return {
    id,
    title,
    subtitle,
    availability: closed ? 'available' : 'unavailable',
    closure: closed ? 'CLOSED' : 'UNENCLOSED',
    detail: closed
      ? '已准备好，可由游戏控制。'
      : pendingDetail(proof, title),
  };
}

/**
 * Resolve the card from the shared input snapshot. No caller can make a
 * feedback switch appear active merely by changing a page-local preference.
 */
export function evaluateControllerFeedback(
  input: unknown,
  targetEnabled: boolean,
  persona: ControllerPersona,
): ControllerFeedbackView {
  // The current caller provides a persisted settings snapshot. Its unknown
  // fields survive CAS writes, so it is never a valid feedback readiness
  // authority. Keep the argument for UI compatibility until a separate,
  // runtime-owned receipt is introduced.
  void input;
  const proof = proofFrom(targetEnabled, persona);
  const capabilities = [
    capabilityView(proof, 'rumble', '游戏震动', '双马达：游戏的左、右马达反馈将被安全转发到实体手柄。'),
    capabilityView(proof, 'haptic', '按键与触摸触觉', '按键或触摸操作时提供短促的触觉反馈。'),
  ];

  if (persona === 'xbox360') {
    capabilities.push({
      id: 'light',
      title: '灯光、亮度与效果',
      subtitle: '此模式不支持通用灯条或颜色控制。',
      availability: 'not-applicable',
      closure: 'NOT-APPLICABLE',
      detail: '玩家编号提示不等同于可设置的灯光颜色。',
    });
  } else {
    capabilities.push(capabilityView(
      proof,
      'light',
      '灯光、亮度与效果',
      persona === 'dualshock4'
        ? '灯条颜色和效果会在兼容的实体手柄上显示。'
        : '颜色、亮度和效果只会在兼容的手柄上提供。',
    ));
  }

  const allClosed = capabilities.length > 0 && capabilities.every((entry) => entry.closure === 'CLOSED' || entry.closure === 'NOT-APPLICABLE');
  return {
    personaLabel: personaLabel(persona),
    targetDetail: proof.targetRequested
      ? `当前目标：${personaLabel(persona)}。`
      : '未启用虚拟手柄。',
    routeDetail: allClosed
      ? '游戏反馈会传给实体手柄；停止时会自动归零。'
      : '当前版本尚未确认完整反馈路径，相关选项暂不可用。',
    capabilities,
    allClosed,
  };
}
