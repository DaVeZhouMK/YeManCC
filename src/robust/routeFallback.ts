const FALLBACK_ID = 'yemancc-route-fallback';

export function showRouteFallback(message: string, route = ''): void {
  if (typeof document === 'undefined') return;
  let node = document.getElementById(FALLBACK_ID);
  if (!node) {
    node = document.createElement('div');
    node.id = FALLBACK_ID;
    node.setAttribute('role', 'alert');
    Object.assign(node.style, {
      position: 'fixed',
      inset: '0',
      zIndex: '2147483000',
      display: 'grid',
      placeItems: 'center',
      padding: '32px',
      background: '#101218',
      color: '#f3f4f6',
      fontFamily: 'system-ui, sans-serif',
      textAlign: 'center',
    });
    document.body.appendChild(node);
  }
  node.textContent = `${message}${route ? `（${route}）` : ''}。请返回上一页或重启界面。`;
}

export function clearRouteFallback(): void {
  document.getElementById(FALLBACK_ID)?.remove();
}
