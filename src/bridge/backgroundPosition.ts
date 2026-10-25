export interface BackgroundPosition { x: number; y: number }

export function normalizeBackgroundPosition(value?: Partial<BackgroundPosition> | null): BackgroundPosition {
  const axis = (input: unknown) => typeof input === 'number' && Number.isFinite(input)
    ? Math.round(Math.max(0, Math.min(100, input)) * 10) / 10 : 50;
  return { x: axis(value?.x), y: axis(value?.y) };
}

// object-position moves over the cropped part of a cover image, not its full width.
export function dragBackgroundPosition(origin: BackgroundPosition, dx: number, dy: number,
  viewport: { width: number; height: number }, media: { width: number; height: number }): BackgroundPosition {
  if (!(viewport.width > 0 && viewport.height > 0 && media.width > 0 && media.height > 0)) return { ...origin };
  const scale = Math.max(viewport.width / media.width, viewport.height / media.height);
  const overflowX = media.width * scale - viewport.width;
  const overflowY = media.height * scale - viewport.height;
  return normalizeBackgroundPosition({
    x: overflowX > 0.5 ? origin.x - dx * 100 / overflowX : origin.x,
    y: overflowY > 0.5 ? origin.y - dy * 100 / overflowY : origin.y,
  });
}
