import { createRouter, createWebHashHistory } from 'vue-router';
import TdpView from './views/TdpView.vue';
import CpuView from './views/CpuView.vue';
import RtssView from './views/RtssView.vue';
import PowerView from './views/PowerView.vue';
import SteamView from './views/SteamView.vue';
import SleepGuardView from './views/SleepGuardView.vue';
import SettingsView from './views/SettingsView.vue';
import QuickAppView from './views/QuickAppView.vue';
import PerformanceScheduleView from './views/PerformanceScheduleView.vue';

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
