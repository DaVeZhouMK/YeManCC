import { createRouter, createWebHashHistory } from 'vue-router';
import TdpView from './views/TdpView.vue';
import CpuView from './views/CpuView.vue';
import RtssView from './views/RtssView.vue';
import PowerView from './views/PowerView.vue';
import SteamView from './views/SteamView.vue';
import SleepGuardView from './views/SleepGuardView.vue';
import SettingsView from './views/SettingsView.vue';

export const ROUTES = [
  { path: '/tdp', name: 'tdp', title: 'TDP功耗', icon: '⚡', component: TdpView },
  { path: '/cpu', name: 'cpu', title: 'CPU调度', icon: '🎛️', component: CpuView },
  { path: '/rtss', name: 'rtss', title: 'RTSS监控', icon: '📊', component: RtssView },
  { path: '/power', name: 'power', title: '开机启动', icon: '🚀', component: PowerView },
  { path: '/steam', name: 'steam', title: 'Steam大屏', icon: '🎮', component: SteamView },
  { path: '/sleep', name: 'sleep', title: '睡眠优化', icon: '🌙', component: SleepGuardView },
  { path: '/settings', name: 'settings', title: '设置', icon: '⚙', component: SettingsView },
];

export const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    { path: '/', redirect: '/tdp' },
    ...ROUTES,
  ],
});
