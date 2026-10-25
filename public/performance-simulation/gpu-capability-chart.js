// Four CPU/GPU score curves over watts, refreshed by the selected machine and tuning parameters.
(function(){
'use strict';
const library=window.YmccBenchmarkData?.GPU_CAPABILITIES,curves=window.YmccTsPowerCurve,charts=document.querySelector('.charts');
if(!library||!curves||!charts||!window.ymccEnergyDemo)return;
const card=document.createElement('section');card.className='chart panel gpu-ts-chart';card.setAttribute('aria-label','CPU与GPU的TS功耗曲线');
card.innerHTML='<div class="chart-title"><strong>TS 功耗曲线</strong><span id="gpu-ts-readout"></span></div><div class="ts-live-legend"></div><svg id="gpu-ts-chart" role="img" aria-label="按瓦数对比原版与当前优化方案的CPU和GPU分数"></svg>';
charts.append(card);charts.classList.add('with-gpu-ts');
const svg=card.querySelector('svg'),readout=card.querySelector('#gpu-ts-readout'),legend=card.querySelector('.ts-live-legend'),NS='http://www.w3.org/2000/svg';
let snapshot=null,optimizationMode='auto',cachedKey='',curve=null,lastDrawKey='';
function node(tag,attrs,text){const el=document.createElementNS(NS,tag);for(const[key,value]of Object.entries(attrs))el.setAttribute(key,String(value));if(text!==undefined)el.textContent=text;svg.append(el);return el;}
function draw(){
 if(!snapshot)return;const selected=window.ymccEnergyDemo.viewState().previewMode;if(selected==='auto'||selected==='manual')optimizationMode=selected;
 const key=curves.signature(snapshot,optimizationMode);if(key!==cachedKey){curve=curves.sample(snapshot,optimizationMode);cachedKey=key;}
 const box=svg.getBoundingClientRect(),width=svg.clientWidth,height=svg.clientHeight;if(width<=0||height<=0)return;
 const at=Math.max(curve.minWatts,Math.min(curve.maxWatts,snapshot.worlds[optimizationMode].control.tdp)),drawKey=[cachedKey,width,height,at,box.width].join(':');if(drawKey===lastDrawKey)return;lastDrawKey=drawKey;
 const modeName=optimizationMode==='manual'?'手动':'自动',left=30,right=7,top=5,bottom=21,units=w=>w<=35?w-3:32+(w-35)/5,low=units(3),high=units(curve.maxWatts),ceiling=Math.max(100,...curve.series.flatMap(line=>line.points.map(point=>point.score)))*1.08,x=w=>left+(units(w)-low)/(high-low)*(width-left-right),y=score=>height-bottom-score/ceiling*(height-top-bottom);
 svg.setAttribute('viewBox',`0 0 ${width} ${height}`);svg.replaceChildren();
 for(const fraction of [0,.5,1]){const value=ceiling*fraction,atY=y(value);node('line',{x1:left,y1:atY,x2:width-right,y2:atY,class:'ts-grid'});node('text',{x:left-3,y:atY+3,'text-anchor':'end'},value>=1000?(value/1000).toFixed(1)+'k':Math.round(value));}
 const ticks=[...Array.from({length:Math.min(35,curve.maxWatts)-2},(_,i)=>i+3),...Array.from({length:10},(_,i)=>40+i*5)].filter(w=>w<=curve.maxWatts);
 for(const w of ticks)node('line',{x1:x(w),x2:x(w),y1:top,y2:height-bottom,class:'ts-grid-x'});
 const label=w=>{const el=node('text',{y:height-4,class:'ts-watt-label','data-watts':w},w+'W'),textWidth=el.getComputedTextLength(),start=Math.max(2,Math.min(width-textWidth-2,x(w)-textWidth/2));el.setAttribute('x',String(start));const bounds=el.getBoundingClientRect(),pixelsPerUnit=box.width/width,shift=bounds.right>box.right-2?box.right-2-bounds.right:bounds.left<box.left+2?box.left+2-bounds.left:0;if(shift)el.setAttribute('x',String(start+shift/pixelsPerUnit));return el;};
 const first=label(3),last=label(curve.maxWatts),middle=label(curve.maxWatts>35?20:Math.round((3+curve.maxWatts)/2)),a=first.getBoundingClientRect(),b=middle.getBoundingClientRect(),c=last.getBoundingClientRect();if(b.left-a.right<12||c.left-b.right<12)middle.remove();
 const unverified=library.entry(curve.profile).unverifiedGpuAboveWatts;if(unverified){node('rect',{x:x(unverified),y:top,width:x(curve.maxWatts)-x(unverified),height:height-top-bottom,fill:'#f5c77a',opacity:.07,class:'ts-unverified-region'});}
 if(curve.minWatts>3)node('rect',{x:x(3),y:top,width:x(curve.minWatts)-x(3),height:height-top-bottom,class:'ts-no-data'});
 for(const line of curve.series){const kind=line.part+' '+(line.world==='stock'?'baseline':'optimized');node('path',{d:line.points.map((point,i)=>(i?'L':'M')+x(point.watts)+','+y(point.score)).join(' '),class:'ts-power-line '+kind,'data-series':line.key,'data-watts':JSON.stringify(line.points.map(point=>point.watts)),'data-scores':JSON.stringify(line.points.map(point=>point.score))});}
 node('line',{x1:x(at),x2:x(at),y1:top,y2:height-bottom,class:'ts-current'});
 legend.replaceChildren(...curve.series.map(line=>{const el=document.createElement('span');el.className=line.part+' '+(line.world==='stock'?'baseline':'optimized');el.textContent=line.part.toUpperCase()+' '+(line.world==='stock'?'原版':modeName);return el;}));
 readout.textContent=unverified&&at>unverified?'>35W 待校准':at+'W · '+modeName;svg.setAttribute('aria-label',`TS功耗曲线：3至${curve.maxWatts}W，原版CPU/GPU两条虚线，${modeName}CPU/GPU两条实线。`);
 card.title=unverified?'G3E官方设备支持45W持续释放，80W是芯片最大睿频；35W以上TS分数待校准，数值是占位，不能视为确定无提升。':'原版与优化线共用实际CPU/GPU供给计算；提高瓦数后优化差距可以收敛，GPU绝对性能不因优化幅度退坡而下降。';card.dataset.unverifiedAboveWatts=unverified?String(unverified):'';card.dataset.profile=curve.profile;card.dataset.comparisonMode=optimizationMode;card.dataset.axis='watts';card.dataset.axisStart='3';card.dataset.minimum=String(curve.minWatts);card.dataset.maximum=String(curve.maxWatts);card.dataset.seriesCount='4';card.dataset.wattLabelMinimumGap='12';
}
function render(value){snapshot=value;draw();const advisory=library.tdpAdvisory(value.config.profile,value.config.budget);if(window.parent!==window)window.parent.postMessage({type:'ymcc-simulation-tdp-advisory',...advisory},location.origin);}
window.addEventListener('ymcc-simulation-render',event=>render(event.detail));new ResizeObserver(draw).observe(svg);render(window.ymccEnergyDemo.snapshot());
})();
