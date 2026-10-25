// Render declarative controller paths; input still comes from GamepadVisualizer's presenter.
import { defineComponent, Fragment, h, withMemo, type PropType, type VNode } from 'vue';
import { diagramAxis, type DiagramModel, type DiagramVectorNode } from '@/bridge/controllerDeviceDiagram';

export default defineComponent({
  name: 'ControllerVectorArtwork',
  props: {
    model: { type: Object as PropType<DiagramModel>, required: true },
    live: { type: Object as PropType<ReadonlySet<string>>, default: () => new Set<string>() },
    active: { type: Object as PropType<ReadonlySet<string>>, required: true },
    available: { type: Object as PropType<ReadonlySet<string>>, required: true },
    axes: { type: Array as PropType<readonly number[]>, required: true },
  },
  setup(props) {
    // Reuse unchanged SVG nodes: an axis sample only rebuilds its moving caps.
    // Keep child VNodes in the memo so nested Deck stick fills still update.
    const nodeCaches = new WeakMap<DiagramVectorNode, VNode[]>();
    const renderNode = (node: DiagramVectorNode): VNode | string => {
      if (node.tag === 'text') return node.text || '';
      if (node.visibleFor && !node.visibleFor.some(code => props.active.has(code))) return '';
      const children = node.children.map(renderNode);
      const active = node.fillFor?.some(code => props.active.has(code)) ?? false;
      const live = node.fillFor?.some(code => props.live.has(code)) ?? false;
      const available = !node.availabilityFor || props.available.has(node.availabilityFor);
      const x = node.stick != null ? diagramAxis(props.axes[node.stick * 2]) : 0;
      const y = node.stick != null ? diagramAxis(props.axes[node.stick * 2 + 1]) : 0;
      let cache = nodeCaches.get(node);
      if (!cache) { cache = []; nodeCaches.set(node, cache); }
      return withMemo([active, live, available, x, y, props.model.fillColor, ...children], () => {
        if (node.tag === 'fragment') return h(Fragment, {}, children);
        const attrs = { ...node.attrs, key: node.id, 'data-vector-id': node.id } as Record<string, unknown>;
        if (node.availabilityFor) {
          attrs['data-rear-control'] = node.availabilityFor;
          attrs['data-available'] = String(available);
          if (!available) attrs.opacity = .22;
        }
        if (node.fillFor) {
          attrs['data-fill-for'] = node.fillFor.join(' ');
          if (active) {
            attrs.fill = live ? props.model.fillColor : `color-mix(in srgb, ${props.model.fillColor} 25%, transparent)`;
            attrs['data-fill-live']=String(live);
            attrs['data-fill-active'] = 'true';
          }
        }
        if (node.stick != null && node.travel) {
          attrs.transform = `translate(${x * node.travel[0]} ${-y * node.travel[1]})`;
          attrs.class = Math.hypot(x, y) > .002 ? 'stick-cap' : 'stick-neutral';
          attrs['data-stick'] = node.stick;
        }
        return h(node.tag, attrs, children);
      }, cache, 0);
    };
    return () => h('svg', {
      class: 'device-base device-vector',
      xmlns: 'http://www.w3.org/2000/svg',
      width: props.model.width,
      height: props.model.height,
      viewBox: `0 0 ${props.model.width} ${props.model.height}`,
      fill: 'none',
      role: 'img',
      'aria-label': '完整控制器矢量轮廓',
      'data-original-svg': 'steam-client',
    }, props.model.nodes!.map(renderNode));
  },
});
