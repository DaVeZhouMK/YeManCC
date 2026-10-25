import type { ContinuousPerformanceIdentity, ContinuousPerformanceSnapshot, ContinuousSidePatch, ControlSide } from './performanceControls';
/** An isolated editor can reuse the production controls without writing production settings. */
export interface PerformancePanelSource {
  readonly sideLocked: boolean;
  read(side?: ControlSide): Promise<ContinuousPerformanceSnapshot>;
  commit(identity: ContinuousPerformanceIdentity, patch: ContinuousSidePatch): Promise<ContinuousPerformanceSnapshot>;
  subscribe(refresh: () => void): () => void;
}
