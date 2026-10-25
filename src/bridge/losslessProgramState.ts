// Shared display model only; no process, UIAutomation, profile or settings IO.
export interface LosslessProgramState { known:boolean; running:boolean; }
export function losslessProgramLabel(state:LosslessProgramState|null|undefined):string {
  return !state?.known ? '小黄鸭状态待确认' : state.running ? '小黄鸭已运行' : '小黄鸭未运行';
}
