/** Re-enter an inner page at its title instead of inheriting the previous page's scroll. */
export function resetInnerPageViewport(root: ParentNode = document): void {
  const content = root.querySelector<HTMLElement>('.app-content');
  if (!content) return;
  content.scrollTo({ top: 0, left: 0, behavior: 'instant' });
}
