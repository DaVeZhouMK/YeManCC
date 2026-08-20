// Cross-view mutex for destructive or process-affecting quick actions.
// The top Y menu and the legacy QuickApp/Performance views can coexist, so
// per-component busy flags alone are not enough to serialize file/process
// operations.
let activeOwner = '';

export function tryAcquireQuickAction(owner: string): (() => void) | null {
  if (activeOwner) return null;
  activeOwner = owner;
  let released = false;
  return () => {
    if (released) return;
    released = true;
    if (activeOwner === owner) activeOwner = '';
  };
}

export function isQuickActionBusy(): boolean {
  return activeOwner !== '';
}
