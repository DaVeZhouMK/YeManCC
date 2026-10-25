// Static decoration geometry only. Monitoring always consumes the sole YMCC snapshot.
export function honeycombOutline(width: number, height: number): string {
  const w=Math.max(96,width),h=Math.max(96,height);
  return `M 24 2 H ${w-47} L ${w-2} 46 V ${h-23} L ${w-23} ${h-2} H 45 L 2 ${h-44} V 24 Z`;
}
export function honeycombInnerLines(width: number, height: number): string {
  const w=Math.max(96,width),h=Math.max(96,height);
  return `M 37 8 H ${Math.min(167,w-60)} M ${w-37} 25 L ${w-13} 49 V 85 M 12 ${h-90} V ${h-50} L 48 ${h-14} H ${Math.min(113,w-30)} M ${w-85} ${h-10} H ${w-27} L ${w-11} ${h-26}`;
}
