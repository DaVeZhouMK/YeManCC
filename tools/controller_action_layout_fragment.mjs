import fs from 'node:fs';
import path from 'node:path';

export function writeActionLayoutComparison({ fragment, css, bundle }) {
  const variants = [['dock', '底部操作栏'], ['inline', '卡片内展开'], ['popover', '卡片浮层']];
  // The preview has a real 1280px layout canvas. Only its outer presentation is
  // scaled uniformly to fit chat; viewport queries use that canvas container.
  const shadowCss = css.replace(/:root\b/g, ':host').replace(/\bbody\b/g, ':host').replace(/@media\s*(\((?:min|max)-width:[^)]+\))/g, '@container ymcc-preview $1');
  const surface = '<style>' + shadowCss + '\n:host{display:block;position:relative;contain:layout paint;container-type:size;container-name:ymcc-preview;width:1280px;height:720px;background:var(--bg-solid);color:var(--text)}</style><div class="comparison-app"></div><div class="comparison-portal"></div>';
  const markup = `<div id="ymcc-action-layouts">
  <div class="comparison-toolbar">
    <span>YMCC · 100% 布局</span>
    <label>画幅 <select class="comparison-resolution"><option value="720">1280 × 720</option><option value="800">1280 × 800</option></select></label>
    <label>手柄图示 <select class="comparison-persona"><option value="steamdeck">Steam Deck</option><option value="dualsense-edge">PS5</option><option value="elite">Xbox</option></select></label>
    <label><input class="comparison-test" type="checkbox">模拟按键</label>
    <button class="comparison-press" type="button" aria-pressed="false">按住背键／专用键示例</button>
    <small>示例输入 · 应用仅保存本页</small>
  </div>
  <div class="viz-carousel" aria-label="YMCC 动作编辑布局" data-previous-label="上一方案" data-next-label="下一方案">
    ${variants.map(([value, label], i) => `<section data-variant="${label}" aria-label="${label}"${i ? ' hidden' : ''}><div class="comparison-viewport"><div id="action-layout-${value}" class="comparison-source"></div></div></section>`).join('\n    ')}
  </div>
</div>
<style>
#ymcc-action-layouts{width:100%;font:14px system-ui;color:light-dark(#172433,#e9eef5)}
#ymcc-action-layouts .comparison-toolbar{display:flex;align-items:center;flex-wrap:wrap;gap:10px;padding:8px 4px 12px}
#ymcc-action-layouts label{display:flex;align-items:center;gap:6px}
#ymcc-action-layouts select,#ymcc-action-layouts .comparison-press{font:inherit;border:1px solid light-dark(#bec8d3,#3c4d61);border-radius:6px;padding:6px 10px;background:light-dark(#f3f5f8,#182333);color:inherit}
#ymcc-action-layouts small{font-size:11px;opacity:.75}
#ymcc-action-layouts .comparison-viewport{position:relative;width:100%;aspect-ratio:1280/720;overflow:hidden}
#ymcc-action-layouts .comparison-source{position:absolute;left:0;top:0;width:1280px;height:720px;transform-origin:0 0}
</style>
<script>
{
  const root=document.getElementById('ymcc-action-layouts');
  const variants=${JSON.stringify(variants)};
  const surface=${JSON.stringify(surface).replace(/<\/script/gi, '<\\/script')};
  window.__editorPreviewCatalogProfile='two';window.__editorPreviewWidthSession=true;
  window.__editorPreviewMounts=variants.map(([variant])=>{const host=root.querySelector('#action-layout-'+variant),shadow=host.attachShadow({mode:'open'});shadow.innerHTML=surface;return{variant,host,root:shadow.querySelector('.comparison-app'),portal:shadow.querySelector('.comparison-portal')}});
}
${bundle.replace(/<\/script/gi, '<\\/script')}
{
  const root=document.getElementById('ymcc-action-layouts'),mounts=window.__editorPreviewMounts;
  const model=root.querySelector('.comparison-persona'),test=root.querySelector('.comparison-test'),press=root.querySelector('.comparison-press'),resolution=root.querySelector('.comparison-resolution');
  const apply=state=>{const v=state?.privateContent;if(v&&['steamdeck','dualsense-edge','elite'].includes(v.persona)){model.value=v.persona;test.checked=!!v.test;if(['720','800'].includes(v.resolution))resolution.value=v.resolution}window.__editorPreview.setPersona(model.value);window.__editorPreview.test(test.checked)};
  const remember=()=>window.openai?.setWidgetState?.({modelContent:{controller:model.value,resolution:'1280×'+resolution.value,scale:'100%',purpose:'选择动作卡片管理布局'},privateContent:{persona:model.value,test:test.checked,resolution:resolution.value}})?.catch(()=>{});
  model.addEventListener('change',()=>{window.__editorPreview.setPersona(model.value);remember()});test.addEventListener('change',()=>{window.__editorPreview.test(test.checked);remember()});
  const release=()=>{window.__editorPreview.state({});press.setAttribute('aria-pressed','false')},hold=()=>{window.__editorPreview.state({oemPressedKeys:window.__editorPreview.catalog.keys.filter(k=>k.captureImplemented!==false).map(k=>k.keyId)});press.setAttribute('aria-pressed','true')};
  press.addEventListener('pointerdown',e=>{press.setPointerCapture(e.pointerId);hold()});for(const name of['pointerup','pointercancel','lostpointercapture','blur'])press.addEventListener(name,release);press.addEventListener('keydown',e=>{if([' ','Enter'].includes(e.key)){e.preventDefault();hold()}});press.addEventListener('keyup',release);window.addEventListener('blur',release);
  let scheduled=false;const fit=()=>{if(scheduled)return;scheduled=true;requestAnimationFrame(()=>{scheduled=false;const height=Number(resolution.value);for(const m of mounts){const viewport=m.host.parentElement;viewport.style.aspectRatio='1280/'+height;m.host.style.height=height+'px';const width=viewport.clientWidth;if(width>0)m.host.style.transform='scale('+(width/1280)+')'}})};
  const resize=new ResizeObserver(fit);for(const m of mounts)resize.observe(m.host.parentElement);window.addEventListener('resize',fit);new MutationObserver(fit).observe(root.querySelector('.viz-carousel'),{attributes:true,subtree:true,attributeFilter:['hidden']});resolution.addEventListener('change',()=>{fit();remember()});
  apply(window.openai?.widgetState);window.addEventListener('openai:set_globals',e=>{apply(e.detail?.globals?.widgetState);fit()});
  const ready=()=>{let done=true;for(const m of mounts){const row=m.portal.querySelector('.rule-row');if(!row){done=false;continue}if(!m.initialized){row.click();m.initialized=true}}if(!done)requestAnimationFrame(ready);fit()};requestAnimationFrame(()=>requestAnimationFrame(ready));
}
</script>`;
  if (Buffer.byteLength(markup) > 1_000_000) throw new Error('Comparison exceeds inline size limit');
  fs.mkdirSync(path.dirname(fragment), { recursive: true });
  fs.writeFileSync(fragment, markup);
}
