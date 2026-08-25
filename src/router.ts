import { createRouter, createWebHashHistory } from 'vue-router';
import { fanFeatureEnabled } from './bridge/fanFeature';
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
// Fan is a capability-gated entry that appears only after the native HC
// handshake. Keep its view in the main shell bundle instead of a second lazy
// chunk: an incomplete/partially cached update must never turn a visible fan
// navigation item into an empty page.
import FanView from './views/FanView.vue';
const CustomSteamLibraryView = () => import('./views/CustomSteamLibraryView.vue');

export const ROUTES = [
  { path: '/schedule', name: 'schedule', title: '性能调度', icon: 'gamepad', component: PerformanceScheduleView },
  { path: '/fan', name: 'fan', title: '风扇控制', icon: 'fan', component: FanView, feature: 'fan' as const },
  { path: '/tdp', name: 'tdp', title: 'TDP功耗', icon: 'tdp', component: TdpView },
  { path: '/cpu', name: 'cpu', title: 'CPU调度', icon: 'cpu', component: CpuView },
  { path: '/rtss', name: 'rtss', title: '监控/锁帧', icon: 'rtss', component: RtssView },
  { path: '/power', name: 'power', title: '开机启动', icon: 'startup', component: PowerView },
  { path: '/steam', name: 'steam', title: 'Steam大屏', icon: 'steam', component: SteamView },
  { path: '/sleep', name: 'sleep', title: '睡眠优化', icon: 'sleep', component: SleepGuardView },
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

export const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    { path: '/', redirect: '/schedule' },
    ...ROUTES,
  ],
});

router.beforeEach((to) => {
  // Do not read Fan settings from the route guard: the native-resolved
  // PowerControl directory is installed by App.vue after mount. Until that
  // initialization completes the feature remains false, so direct deep links
  // cannot trigger an early read from the compatibility path.
  if (to.path === '/fan' && !fanFeatureEnabled.value) return '/schedule';
  if (to.path === CUSTOM_STEAM_LIBRARY_ROUTE && !CUSTOM_STEAM_LIBRARY_INTEGRATION_ENABLED) return '/schedule';
  return true;
});
