/** Pure shell policy: an entry is not proof of hardware readiness.
 * No bridge, filesystem, process, handshake, or device probe belongs here.
 */
export interface NavigationEntry { path: string; hidden?: boolean; }
export function residentNavigationRoutes<T extends NavigationEntry>(routes: readonly T[], quickMode: boolean): T[] {
  return routes.filter((route) => !route.hidden && (!quickMode || (route.path !== '/tdp' && route.path !== '/cpu')));
}
