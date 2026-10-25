/** Pure shell policy: an entry is not proof of hardware readiness.
 * No bridge, filesystem, process, handshake, or device probe belongs here.
 */
export interface NavigationEntry { path: string; hidden?: boolean; testing?: 'radial'; }
export function residentNavigationRoutes<T extends NavigationEntry>(routes: readonly T[], _quickMode: boolean, radialTestingEnabled = false): T[] {
  return routes.filter((route) => !route.hidden && route.path !== '/tdp' && route.path !== '/cpu' && (route.testing !== 'radial' || radialTestingEnabled));
}
