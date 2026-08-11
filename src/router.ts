import { createRouter, createWebHashHistory } from 'vue-router';
const TdpView = () => import('./views/TdpView.vue');
const CpuView = () => import('./views/CpuView.vue');
const RtssView = () => import('./views/RtssView.vue');
const PowerView = () => import('./views/PowerView.vue');
const SteamView = () => import('./views/SteamView.vue');
const SleepGuardView = () => import('./views/SleepGuardView.vue');
const SettingsView = () => import('./views/SettingsView.vue');
const QuickAppView = () => import('./views/QuickAppView.vue');
const PerformanceScheduleView = () => import('./views/PerformanceScheduleView.vue');

export const ROUTES = [
  { path: '/schedule', name: 'schedule', title: '性能调度', icon: 'gamepad', component: PerformanceScheduleView },
  { path: '/tdp', name: 'tdp', title: 'TDP功耗', icon: 'tdp', component: TdpView },
  { path: '/cpu', name: 'cpu', title: 'CPU调度', icon: 'cpu', component: CpuView },
  { path: '/rtss', name: 'rtss', title: '监控/锁帧', icon: 'rtss', component: RtssView },
  { path: '/power', name: 'power', title: '开机启动', icon: 'startup', component: PowerView },
  { path: '/steam', name: 'steam', title: 'Steam大屏', icon: 'steam', component: SteamView },
  { path: '/sleep', name: 'sleep', title: '睡眠优化', icon: 'sleep', component: SleepGuardView },
  { path: '/settings', name: 'settings', title: '设置', icon: 'settings', component: SettingsView },
  // 快捷应用置于「设置」下方（用户要求的导航排序）
  { path: '/quick', name: 'quick', title: '快捷应用', icon: 'quick', component: QuickAppView },
];

export const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    { path: '/', redirect: '/schedule' },
    ...ROUTES,
  ],
});
