// All routes share the original shell scale. The monitor workbench can expand
// into available space, but cannot change the sidebar/control font scale.
export const YMCC_SHELL_BASE_WIDTH = 580;
export const YMCC_SHELL_BASE_HEIGHT = 780;
export const YMCC_SHELL_SCALE_BOOST = 1.3;
export const MONITOR_WORKBENCH_BASE_WIDTH = 900;
export const YMCC_NAV_WIDTH = 105;
export const YMCC_PAGE_HORIZONTAL_PADDING = 24;
export const YMCC_SCROLLBAR_ALLOWANCE = 16;
export function ymccDesignWidth(path: string): number {
  return path === '/rtss' ? MONITOR_WORKBENCH_BASE_WIDTH : YMCC_SHELL_BASE_WIDTH;
}
// Width is expressed in the shell's existing zoom coordinate space. Clamp the
// workbench instead of using its wider design width as the global zoom cap.
export function ymccStageWidth(designWidth: number, viewportWidth: number, scale: number): number {
  return Math.min(designWidth, Math.max(1, viewportWidth) / Math.max(0.01, scale));
}
export function monitorScreenLayout(width: number, height: number) {
  const w = Math.max(1, width), h = Math.max(1, height);
  const requestedScale = h / YMCC_SHELL_BASE_HEIGHT * YMCC_SHELL_SCALE_BOOST;
  const scale = Math.min(requestedScale, w / YMCC_SHELL_BASE_WIDTH);
  const stageWidth = ymccStageWidth(MONITOR_WORKBENCH_BASE_WIDTH, w, scale);
  const contentWidth = Math.max(0, stageWidth - YMCC_NAV_WIDTH - YMCC_PAGE_HORIZONTAL_PADDING - YMCC_SCROLLBAR_ALLOWANCE);
  return { designWidth: MONITOR_WORKBENCH_BASE_WIDTH, stageWidth, contentWidth, requestedScale, scale,
    widthCompressed: scale < requestedScale - 1e-6, windowPixelWidth: stageWidth * scale,
    contentPixelWidth: contentWidth * scale };
}
