// Shared display/action model only; no process, UIAutomation, profile or settings IO.
export interface LosslessProgramState { known:boolean; running:boolean; }
export function losslessProgramLabel(state:LosslessProgramState|null|undefined):string {
  return !state?.known ? '小黄鸭状态待确认' : state.running ? '小黄鸭已运行' : '小黄鸭未运行';
}

export type LosslessToggleValue = 'on' | 'off';
export interface LosslessToggleState { running:boolean; enabled:boolean|null; configuredEnabled:boolean; }
// An unreadable running session with this game's enabled profile must retain a
// close route. This is the next user intent, never proof of active frame generation.
export function losslessToggleValue(state:LosslessToggleState|null):LosslessToggleValue {
  return state?.running===true&&(state.enabled===true||(state.enabled===null&&state.configuredEnabled===true)) ? 'off' : 'on';
}
export function losslessToggleLabel(next:LosslessToggleValue):string {
  return next==='off' ? '一键关闭小黄鸭' : '一键开启小黄鸭';
}
