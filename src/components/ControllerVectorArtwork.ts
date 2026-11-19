// Render declarative controller paths; input still comes from GamepadVisualizer's presenter.
import { computed, defineComponent, Fragment, h, inject, provide, toRef, withMemo, type InjectionKey, type PropType, type Ref, type VNode } from 'vue';
import { diagramAxis, type DiagramModel, type DiagramVectorNode } from '@/bridge/controllerDeviceDiagram';

const stickAxes: InjectionKey<Ref<readonly number[]>> = Symbol('controller-artwork-axes');

// Axis samples update only the two moving groups and their mapped-fill copies.
// Inject the exact original axes so the fixed artwork never depends on them.
const ControllerVectorStick = defineComponent({
  name: 'ControllerVectorStick',
  props: {
    node: { type: Object as PropType<DiagramVectorNode>, required: true },
    vectorAttrs: { type: Object as PropType<Record<string, unknown>>, required: true },
    nodes: { type: Array as PropType<Array<VNode | string>>, required: true },
    fillLayer: Boolean,
  },
  setup(props) {
    const axes = inject(stickAxes)!;
    return () => {
      const node = props.node;
      const x = diagramAxis(axes.value[node.stick! * 2]);
      const y = diagramAxis(axes.value[node.stick! * 2 + 1]);
      const attrs = { ...props.vectorAttrs, transform: `translate(${x * node.travel![0]} ${-y * node.travel![1]})` };
      if (!props.fillLayer) {
        attrs['class'] = Math.hypot(x, y) > .002 ? 'stick-cap' : 'stick-neutral';
        attrs['data-stick'] = node.stick;
      }
      return h(node.tag, attrs, props.nodes);
    };
  },
});

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
    provide(stickAxes, toRef(props, 'axes'));
    // Reuse unchanged SVG nodes: an axis sample only rebuilds its moving caps.
    // Keep child VNodes in the memo so nested Deck stick fills still update.
    const nodeCaches = new WeakMap<DiagramVectorNode, VNode[]>();
    const fillCaches = new WeakMap<DiagramVectorNode, VNode[]>();
    // Only the mapped fill is composited. The original strokes and glyphs remain
    // in the foreground artwork and never participate in the opacity animation.
    const fillTree = computed(() => {
      const project = (nodes: DiagramVectorNode[]): DiagramVectorNode[] => nodes.flatMap(node => {
        const children = project(node.children);
        return node.fillFor || children.length ? [{ ...node, children }] : [];
      });
      return project(props.model.nodes!);
    });
    const renderFill = (node: DiagramVectorNode): VNode | string => {
      const active = node.fillFor?.some(code => props.active.has(code)) ?? false;
      const live = node.fillFor?.some(code => props.live.has(code)) ?? false;
      const visible = !node.visibleFor || node.visibleFor.some(code => props.active.has(code));
      const children = node.children.map(renderFill);
      let cache = fillCaches.get(node);
      if (!cache) { cache = []; fillCaches.set(node, cache); }
      return withMemo([active, live, visible, props.model.fillColor, ...children], () => {
        if (node.tag === 'fragment') return h(Fragment, {}, children);
        const attrs: Record<string, unknown> = { ...node.attrs, key: node.id, stroke: 'none' };
        if (node.fillFor) { attrs.fill = active && !live && visible ? props.model.fillColor : 'none'; attrs['data-blink-for'] = node.fillFor.join(' '); attrs['data-blink-active'] = String(active && !live && visible); }
        if (node.stick != null && node.travel) return h(ControllerVectorStick, { key: node.id, node, vectorAttrs: attrs, nodes: children, fillLayer: true });
        return h(node.tag, attrs, children);
      }, cache, 0);
    };
    const renderNode = (node: DiagramVectorNode): VNode | string => {
      if (node.tag === 'text') return node.text || '';
      if (node.visibleFor && !node.visibleFor.some(code => props.active.has(code))) return '';
      const children = node.children.map(renderNode);
      const active = node.fillFor?.some(code => props.active.has(code)) ?? false;
      const live = node.fillFor?.some(code => props.live.has(code)) ?? false;
      const available = !node.availabilityFor || props.available.has(node.availabilityFor);
      let cache = nodeCaches.get(node);
      if (!cache) { cache = []; nodeCaches.set(node, cache); }
      return withMemo([active, live, available, props.model.fillColor, ...children], () => {
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
            attrs.fill = live ? props.model.fillColor : 'transparent';
            attrs['data-fill-live']=String(live);
            attrs['data-fill-active'] = 'true';
          }
        }
        if (node.stick != null && node.travel) {
          return h(ControllerVectorStick, { key: node.id, node, vectorAttrs: attrs, nodes: children });
        }
        return h(node.tag, attrs, children);
      }, cache, 0);
    };
    const fills = computed(() => fillTree.value.map(renderFill));
    const artwork = computed(() => props.model.nodes!.map(renderNode));
    const blinking = computed(() => [...props.active].some(code => !props.live.has(code)));
    return () => {
      return h(Fragment, {}, [h('svg', {
      class: ['device-blink-layer', { blinking: blinking.value }],
      xmlns: 'http://www.w3.org/2000/svg',
      viewBox: `0 0 ${props.model.width} ${props.model.height}`,
      fill: 'none',
      'aria-hidden': 'true',
    }, fills.value), h('svg', {
      class: 'device-base device-vector',
      xmlns: 'http://www.w3.org/2000/svg',
      width: props.model.width,
      height: props.model.height,
      viewBox: `0 0 ${props.model.width} ${props.model.height}`,
      fill: 'none',
      role: 'img',
      'aria-label': '完整控制器矢量轮廓',
      'data-original-svg': 'steam-client',
    }, artwork.value)]);
    };
  },
});
