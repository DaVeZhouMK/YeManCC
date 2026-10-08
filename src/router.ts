import { createRouter, createWebHashHistory } from 'vue-router';
import {
  CUSTOM_STEAM_LIBRARY_INTEGRATION_ENABLED,
  CUSTOM_STEAM_LIBRARY_ROUTE,
} from './bridge/customSteamLibrary';
const TdpView = () => import('./views/TdpView.vue');
const CpuView = () => import('./views/CpuView.vue');
const RtssView = () => import('./views/RtssView.vue');
const PowerView = () => import('./views/PowerView.vue');
const SteamView = () => import('./views/SteamView.vue');
const SleepGuardView = () => import('./views/SleepGuardView.vue');
const SettingsView = () => import('./views/SettingsView.vue');
const QuickAppView = () => import('./views/QuickAppView.vue');
const PerformanceScheduleView = () => import('./views/PerformanceScheduleView.vue');
// Fan navigation is resident, not handshake-gated. Keep the existing bundled view;
// rendering an entry is not hardware admission.
import FanView from './views/FanView.vue';
const CustomSteamLibraryView = () => import('./views/CustomSteamLibraryView.vue');
const ButtonMappingView = () => import('./views/ButtonMappingView.vue');
const GyroMotionView = () => import('./views/GyroMotionView.vue');
const ControllerShortcutEditorStandaloneView = () => import('./views/ControllerShortcutEditorStandaloneView.vue');

// 侧边栏 = LR 切页共用这一份顺序（2026-09-27 用户要求重排）：
//   手动模式：性能调度 → TDP功耗 → CPU调度 → 风扇 → 控制器 → 陀螺仪 →
//             Steam大屏 → 开机启动 → 睡眠优化 → 监控/锁帧 → 设置 → 快捷应用
//   自动模式（quickMode）：隐藏 TDP/CPU，其余顺序不变。
export const ROUTES = [
  { path: '/schedule', name: 'schedule', title: '性能调度', icon: 'gauge', component: PerformanceScheduleView },
  { path: '/tdp', name: 'tdp', title: 'TDP功耗', icon: 'tdp', component: TdpView },
  { path: '/cpu', name: 'cpu', title: 'CPU调度', icon: 'cpu', component: CpuView },
  { path: '/fan', name: 'fan', title: '风扇控制', icon: 'fan', component: FanView, feature: 'fan' as const },
  { path: '/button-mapping', name: 'button-mapping', title: '控制器', icon: 'gamepad', component: ButtonMappingView, feature: 'virtual-gamepad' as const },
  { path: '/gyro-motion', name: 'gyro-motion', title: '陀螺仪', icon: 'rotate', component: GyroMotionView, feature: 'gyro-motion' as const },
  { path: '/steam', name: 'steam', title: 'Steam大屏', icon: 'steam', component: SteamView },
  { path: '/power', name: 'power', title: '开机启动', icon: 'startup', component: PowerView },
  { path: '/sleep', name: 'sleep', title: '睡眠优化', icon: 'sleep', component: SleepGuardView },
  { path: '/rtss', name: 'rtss', title: '监控/锁帧', icon: 'rtss', component: RtssView },
  { path: '/settings', name: 'settings', title: '设置', icon: 'settings', component: SettingsView },
  // 快捷应用置于「设置」下方（用户要求的导航排序）
  { path: '/quick', name: 'quick', title: '快捷应用', icon: 'quick', component: QuickAppView },
  {
    path: CUSTOM_STEAM_LIBRARY_ROUTE,
    name: 'custom-steam-library',
    title: 'Steam自定义游戏库',
    icon: 'steam',
    component: CustomSteamLibraryView,
    feature: 'custom-steam-library' as const,
    // 接入准备阶段保留路由契约，但一级菜单不显示。
    hidden: true,
  },
];

// Resolve the sidebar chunks while the native spinner is up. Importing a
// component does not mount its page or invoke its hardware/lifecycle hooks.
let startupRoutes: Promise<void> | undefined;
export function preloadStartupRoutes(): Promise<void> {
  return startupRoutes ??= Promise.all(ROUTES.filter((route) => !route.hidden).map((route) => {
    const component = route.component;
    return typeof component === 'function' ? (component as () => Promise<unknown>)() : Promise.resolve(component);
  })).then(() => undefined);
}

export const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    { path: '/', redirect: '/schedule' },
    ...ROUTES,
    // Legacy direct route retained for browser preview/compatibility. The
    // normal controller entry opens the editor as a main-window bubble.
    { path: '/button-mapping/shortcuts', name: 'button-mapping-shortcuts', component: ControllerShortcutEditorStandaloneView },
  ],
});

router.beforeEach((to) => {
  // Hardware pages and legacy editor deep links are always reachable.
  // Inner-page enable actions own readiness and failure reporting.
  if (to.path === CUSTOM_STEAM_LIBRARY_ROUTE && !CUSTOM_STEAM_LIBRARY_INTEGRATION_ENABLED) return '/schedule';
  return true;
});
