import { dialog } from '../bridge/api';
import { resetApplicationSettings } from '../bridge/settingsRepository';

const FALLBACK_ID = 'yemancc-route-fallback';
let inertRoot: HTMLElement | null = null;
let previousInert = false;
// Consume semantic actions only; never read or take ownership of controllers.
function onFallbackAction(event: Event): void {
  const node = document.getElementById(FALLBACK_ID);
  if (!node) return;
  event.stopImmediatePropagation();
  if ((event as CustomEvent<{ action?: string }>).detail?.action === 'confirm') {
    const active = document.activeElement;
    const focused = active instanceof HTMLButtonElement && node.contains(active) ? active : null;
    (focused ?? node.querySelector<HTMLButtonElement>('button'))?.click();
  }
}

export function showRouteFallback(message: string, route = '', startupFailure = false, detail = ''): void {
  if (typeof document === 'undefined') return;
  let node = document.getElementById(FALLBACK_ID);
  if (node?.dataset.recoveryBusy === 'true') return; // Never replace an in-flight confirmed recovery.
  if (!node) {
    node = document.createElement('div');
    node.id = FALLBACK_ID;
    node.setAttribute('role', 'alert');
    node.setAttribute('data-gp-modal', 'startup-failure');
    Object.assign(node.style, {
      position: 'fixed', inset: '0', zIndex: '2147483000', display: 'grid',
      placeItems: 'center', padding: '24px', background: '#101218', color: '#f3f4f6',
      fontFamily: 'system-ui, sans-serif', textAlign: 'center', overflow: 'auto',
    });
    // Outside #app: its inert state cannot disable recovery controls.
    document.body.appendChild(node);
  }
  if (!inertRoot) {
    inertRoot = document.getElementById('app');
    if (inertRoot) { previousInert = inertRoot.inert; inertRoot.inert = true; }
  }
  window.addEventListener('ipc:gamepad.ui-input', onFallbackAction, true);
  if (startupFailure) node.dataset.startupFailure = 'true';
  node.replaceChildren();
  const content = document.createElement('div');
  Object.assign(content.style, { maxWidth: '720px', lineHeight: '1.65' });
  const text = document.createElement('p');
  text.textContent = `${message}${route ? `（${route}）` : ''}。原配置不会自动清空；可以重试，或手动备份后重置应用共享配置。`;
  const retry = document.createElement('button');
  retry.type = 'button'; retry.textContent = '重新加载';
  retry.onclick = () => { retry.disabled = true; window.location.reload(); };
  retry.setAttribute('data-gp', 'startup-retry');
  Object.assign(retry.style, { padding: '10px 20px', margin: '8px', cursor: 'pointer', font: 'inherit' });
  const recover = document.createElement('button');
  recover.type = 'button'; recover.textContent = '备份并重置应用配置';
  recover.setAttribute('data-gp', 'startup-reset-settings');
  Object.assign(recover.style, { padding: '10px 20px', margin: '8px', cursor: 'pointer', font: 'inherit' });
  const status = document.createElement('p');
  status.setAttribute('role', 'status'); status.setAttribute('aria-live', 'polite');
  const error = document.createElement('pre');
  error.textContent = detail.slice(0, 1800);
  Object.assign(error.style, { textAlign: 'left', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere', fontSize: '13px' });
  const root = node;
  recover.onclick = async () => {
    if (root.dataset.recoveryBusy === 'true') return;
    root.dataset.recoveryBusy = 'true'; retry.disabled = recover.disabled = true;
    try {
      const confirmed = await dialog.confirm('备份并重置应用配置',
        '共享应用设置（档位、界面、音乐等）会还原。可解析的风扇、手柄及原生专属设置保留；无法解析的原始文件也先完整备份。重置后关闭自动 TDP/CPU 档位应用。不会删除程序、媒体或模块文件。是否继续？');
      if (!confirmed) { status.textContent = '已取消，配置未修改。'; return; }
      status.textContent = '正在备份原配置；备份失败会取消重置……';
      const result = await resetApplicationSettings();
      status.textContent = `配置恢复成功，备份：${result.backups.join('；') || '原配置不存在'}。正在重新加载……`;
      window.location.reload();
    } catch (err) {
      status.textContent = `恢复失败：${err instanceof Error ? err.message : String(err)}。请重试；不要手动删除原配置。`;
    } finally {
      root.dataset.recoveryBusy = 'false'; retry.disabled = recover.disabled = false;
    }
  };
  content.append(text, retry, recover, status, error);
  node.appendChild(content);
  retry.focus?.();
}

export function clearRouteFallback(): void {
  const node = document.getElementById(FALLBACK_ID);
  if (node?.dataset.startupFailure === 'true' || node?.dataset.recoveryBusy === 'true') return;
  node?.remove();
  window.removeEventListener('ipc:gamepad.ui-input', onFallbackAction, true);
  if (inertRoot) { inertRoot.inert = previousInert; inertRoot = null; }
}
