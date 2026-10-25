import type { RadialItem } from './model';
/** Names come from the same AppIcon registry used by YMCC's local pages. */
export function radialItemIcon(item:RadialItem):string {
  if(item.action==='submenu') {
    if(item.children?.some(child=>child.action==='utility.control'))return item.children.some(child=>child.id.endsWith('/A')||child.id.endsWith('/B'))?'rtss':'steam';
    if(item.children?.some(child=>child.action==='input.control'))return item.children.some(child=>child.id.includes('/padPersona/'))?'gamepad':'rotate';
    if(item.children?.some(child=>child.action==='fan.control'))return 'fan';
    if(item.children?.some(child=>child.action==='page.schedule'||child.action==='tdp'||child.action==='performance.control'))return 'gauge';
    if(item.id==='windows')return 'monitor';
    return 'quick';
  }
  if(item.action==='input.control')return item.id.includes('/padPersona/')?'gamepad':'rotate';
  if(item.action==='utility.control')return item.id.endsWith('/A')||item.id.endsWith('/B')?'rtss':'steam';
  const icons:Record<string,string>={
    'monitor.templates':'rtss','steam.controls':'steam',
    'input.pad':'gamepad','input.gyro':'rotate','input.control':'gamepad','fan':'fan','fan.control':'fan','performance':'gauge','performance.control':'gauge','ymcc.show':'yeman','tdp':'gauge','page.schedule':'gauge','page.fan':'fan',
    'page.monitor':'rtss','page.settings':'settings','page.radial':'quick',
    'windows.thisPc':'monitor','windows.browser':'globe','windows.taskView':'list','none':'close',
  };
  return icons[item.action]??'quick';
}
