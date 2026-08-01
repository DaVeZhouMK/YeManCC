// YeManCC 图标库 — 由 tools/gen-icons.mjs 生成 src/icons.json 后导入
// Steam 大屏风：实心白标 / 24×24 / 使用 currentColor 继承父级颜色
import icons from './icons.json';

export const ICONS: Record<string, string> = icons;
export type IconName = keyof typeof icons;
