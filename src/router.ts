/* [AI:MAP] 界面/能力搜索地图；用 [AI:USER:*] 找用户行为，[AI:UI:*] 找布局与手柄提醒。
 * 必留范围：动作效果；切页/隐藏；切换/取消；睡眠/唤醒/退出；失败/自愈；配置范围与能力限制。
 * 用户行为是须保持的产品含义；内部执行读代码，以下是定位入口，不是调用顺序或验收结论。
 * 性能调度/TDP/CPU/AC/DC：src/views/PerformanceScheduleView.vue；src/components/ContinuousPerformancePanel.vue；src/bridge/performanceSchedule.ts。
 * 风扇控制/曲线/交还：src/views/FanView.vue；src/bridge/fanHost.ts；FanLab/real-host/Program.cs。
 * 控制器/虚拟手柄/外置优先：src/views/ButtonMappingView.vue；src/bridge/virtualOutputStatus.ts；native/main.cpp；InputHost/Program.cs。
 * 控制器内页/快捷键/触摸板/灯光震动：src/components/ControllerShortcutsCard.vue、ScreenTouchpadsSettings.vue、ControllerFeedbackCapabilitiesCard.vue。
 * 陀螺仪/预设/校准：src/views/GyroMotionView.vue；src/bridge/gyroConfigActivation.ts、gyroCalibrationSession.ts；native/controller_motion_session.h。
 * Steam大屏/Decky/游戏库：src/views/SteamView.vue；src/bridge/steamUi.ts、deckySidebar.ts、customSteamLibrary.ts；专项入口 STEAM-UPGRADE.md。
 * 开机启动：src/views/PowerView.vue；src/bridge/inputStartupPolicy.ts；当前运行状态分别看控制器、陀螺仪、风扇页。
 * 睡眠优化/游戏暂停与恢复：src/views/SleepGuardView.vue；native/sleep_recovery_api.h、sleep_recovery_auto.h；实际电源与暂停逻辑继续追 native/main.cpp。
 * 监控/自定义监控/RTSS：src/views/MonitorOutputView.vue；src/components/MonitorOutputSettings.vue；src/bridge/monitorOutputCoordinator.ts、monitorHudModel.ts。
 * 设置/外观/日志/更新：src/views/SettingsView.vue；src/bridge/settingsRepository.ts；原生入口按该页 IPC 名在 native/main.cpp 查找。
 * 快捷应用/显示/音乐/游戏工具：src/views/QuickAppView.vue；src/bridge/quickapp.ts、optiImportPolicy.ts。
 * 游戏专属/公用配置：src/components/GameCustomProfilePanel.vue、GameProfileRemoveDialog.vue；src/bridge/gameInputEditor.ts、performanceSchedule.ts。
 * 窗口显示/页面刷新：src/bridge/uiLifecycle.ts；页面隐藏不等于功能关闭，窗口退出见 src/components/NavRail.vue。
 * UI布局/手柄：src/components/IntegratedPage.vue；src/styles/integratedConsole.css；src/gamepad/engine.ts、spatial.ts。
 * 机型能力：先查询 capabilities/hc-device-catalog.json、ACTIVE-INTEGRATION-SCOPE.json、USER-SCOPE-DECISIONS.json、EXECUTION-CHAINS.json；目录均在 capabilities/。
 * 新增PC/掌机机型能力须统一登记并接入总库消费者，不在UI或各模块另建型号能力表；声明、接线与本机可用分别核实。
 * 能力分配见 capabilities/HcDeviceCapabilityCatalog.cs；各域执行职责保留，现有CPU/GPU/TDP专用执行不因统一登记而改写。
 * 模拟/夹具：tools/ymcc_sim/ymcc_sim.ps1；先查 registry.json 与 global/adapters.mjs，再按影响选择场景；不足时记录或补对应覆盖。
 * 可见入口以当前 ROUTES/功能开关为准；旧路由、测试轮盘、隐藏游戏库不能据此写成已公开或全部支持。
 * 修改时原位更新对应短注释；共性只留本入口，模块写用户动作/效果，不复制内部执行过程。
 */
import { createRouter, createWebHashHistory } from 'vue-router';
import { RADIAL_USER_AVAILABLE } from './radial/availability';
import { isRadialTestingEnabled } from './radial/testingState';
import {
  CUSTOM_STEAM_LIBRARY_INTEGRATION_ENABLED,
  CUSTOM_STEAM_LIBRARY_ROUTE,
} from './bridge/customSteamLibrary';
// Unified three-way display page; legacy RtssView source remains untouched.
const RtssView = () => import('./views/MonitorOutputView.vue');
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
const RadialMenuView = () => import('./views/RadialMenuView.vue');
const MonitorHudPreviewView = () => import('./views/MonitorHudPreviewView.vue');
const ControllerShortcutEditorStandaloneView = () => import('./views/ControllerShortcutEditorStandaloneView.vue');

// Sidebar and controller page cycling share the active-page order.
// Legacy CPU/TDP screens are retired; performance/float UI belongs to /schedule.
export const ROUTES = [
  { path: '/schedule', name: 'schedule', title: '性能调度', icon: 'gauge', component: PerformanceScheduleView },
  { path: '/fan', name: 'fan', title: '风扇控制', icon: 'fan', component: FanView, feature: 'fan' as const },
  { path: '/button-mapping', name: 'button-mapping', title: '控制器', icon: 'gamepad', component: ButtonMappingView, feature: 'virtual-gamepad' as const },
  { path: '/gyro-motion', name: 'gyro-motion', title: '陀螺仪', icon: 'rotate', component: GyroMotionView, feature: 'gyro-motion' as const },
  { path: '/steam', name: 'steam', title: 'Steam大屏', icon: 'steam', component: SteamView },
  { path: '/power', name: 'power', title: '开机模式', icon: 'startup', component: PowerView },
  { path: '/sleep', name: 'sleep', title: '睡眠优化', icon: 'sleep', component: SleepGuardView },
  { path: '/rtss', name: 'rtss', title: '监控', icon: 'rtss', component: RtssView },
  ...(RADIAL_USER_AVAILABLE ? [{ path: '/radial', name: 'radial', title: '触摸轮盘 · 测试', icon: 'quick', component: RadialMenuView, testing: 'radial' as const }] : []),
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
  return startupRoutes ??= Promise.all(ROUTES.filter((route) => !route.hidden && !('testing' in route)).map((route) => {
    const component = route.component;
    return typeof component === 'function' ? (component as () => Promise<unknown>)() : Promise.resolve(component);
  })).then(() => undefined);
}

export const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    { path: '/', redirect: '/schedule' },
    // Retain old bookmarks and named links without loading retired screens.
    { path: '/tdp', name: 'tdp', redirect: '/schedule' },
    { path: '/cpu', name: 'cpu', redirect: '/schedule' },
    ...ROUTES,
    // Legacy direct route retained for browser preview/compatibility. The
    // normal controller entry opens the editor as a main-window bubble.
    { path: '/monitor/hud-preview', name: 'monitor-hud-preview', component: MonitorHudPreviewView },
    { path: '/button-mapping/shortcuts', name: 'button-mapping-shortcuts', component: ControllerShortcutEditorStandaloneView },
  ],
});

router.beforeEach((to) => {
  if (!isRadialTestingEnabled() && (to.path === '/radial' || to.path.startsWith('/radial/'))) return '/schedule';
  // Hardware pages and legacy editor deep links are always reachable.
  // Inner-page enable actions own readiness and failure reporting.
  if (to.path === CUSTOM_STEAM_LIBRARY_ROUTE && !CUSTOM_STEAM_LIBRARY_INTEGRATION_ENABLED) return '/schedule';
  return true;
});
