import { spatialNavigationRect } from './spatial';

// Isolated H5 acceptance adapter. The production implementation above is unchanged.
const auditOptions = {router: {currentRoute: {value: previewAuditRoute}, push: (route: string) => window.__navigate(route)} } as any;
function previewAuditRoute() { return {path: window.location.pathname}; }
const auditRouter = { get value() { return {path: (window as any).__currentAuditRoute?.() || '/schedule'}; } };
auditOptions.router.currentRoute = auditRouter;
(window as any).__gamepadAudit = {
 focusables, moveFocus, rect: spatialNavigationRect,
 startNative:()=>startGamepad(auditOptions),
 nativeCommand:(action:NativeUiAction)=>window.dispatchEvent(new CustomEvent('ipc:gamepad.ui-input',{detail:{action}})),
 command: (action: NativeUiAction) => dispatchNativeUiAction(auditOptions, action),
 browserStart: (held: boolean, previouslyHeld: boolean, modified=false) => handleBrowserStartRelease(auditOptions,held,previouslyHeld,modified),
 editing: () => sliderEditMode,
 reset: () => { sliderEditMode = false; },
 focus: focusGamepadElement,
};
