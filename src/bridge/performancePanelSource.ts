import type { ContinuousPerformanceIdentity, ContinuousPerformanceSnapshot, ContinuousSidePatch, ContinuousSideControls, ControlSide } from './performanceControls';
/** An isolated editor can reuse the production controls without writing production settings. */
export interface PerformancePanelSource {
  readonly sideLocked: boolean;
  /** Match the live page's density and row positions without using its writer. */
  readonly nativeLayout?: boolean;
  /** One workbench may navigate between the editor and the adjacent TS results. */
  readonly sharedNavigation?: boolean;
  preview?(bank: ContinuousSideControls, identity: ContinuousPerformanceIdentity): void;
  read(side?: ControlSide): Promise<ContinuousPerformanceSnapshot>;
  commit(identity: ContinuousPerformanceIdentity, patch: ContinuousSidePatch): Promise<ContinuousPerformanceSnapshot>;
  subscribe(refresh: () => void): () => void;
}
