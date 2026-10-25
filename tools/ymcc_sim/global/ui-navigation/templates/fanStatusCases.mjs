import assert from 'node:assert/strict';
import path from 'node:path';
// Actual component display regressions; fixtures only set presentation refs in the test SFC.
const notices=[
 {name:'idle',kind:'idle',expected:'风扇控制未开启'},
 {name:'connecting',kind:'connecting'},
 {name:'recovering',kind:'recovering',expected:'正在重新接管'},
 {name:'attention',kind:'attention',expected:'风扇自动恢复未完成'},
 {name:'device-gate-long',expected:'机型暂未适配',kind:'device-gate-long',detail:'风扇控制失败：FAN_DEVICE_GATE_DENIED: HC 当前设备未声明 FanControl 能力，设备类型不在允许范围，请确认机型适配后重试'},
 {name:'environment',expected:'风扇环境不可用',kind:'message',detail:'风扇 Host 环境不可用：Windows Desktop Runtime 依赖校验失败，无法加载 HandheldCompanion 程序集，请检查安装文件'},
 {name:'timeout',expected:'风扇响应超时',kind:'message',detail:'风扇控制失败：IPC command timed out: http.request，当前风扇请求未响应，请等待现有操作结算后重试'},
 {name:'no-feedback',expected:'正在确认风扇状态',kind:'message',detail:'风扇状态暂不可读，控制意图已保留；正在继续检查…'},
 {name:'permission',expected:'风扇授权失败',kind:'message',detail:'风扇控制失败：AUTH_REJECTED：授权失败或凭据不可用，请检查当前运行实例并重新开启'},
 {name:'curve-save',expected:'曲线保存失败',kind:'save-failure',detail:'当前自定义风扇曲线保存失败，原配置保持，请检查配置目录写入权限后重试'},
 {name:'setting-save',expected:'自启设置保存失败',kind:'message',detail:'开机/休眠唤醒启动风扇设置保存失败，请检查配置存储目录和访问权限后重试'},
 {name:'exit-notice',expected:'交还仍在进行',kind:'message',detail:'已取消控制，风扇交还仍在进行；正在等待本次操作和所拥有的设备资源独立结算，完成后可继续'},
 {name:'mirror-generic',expected:'风扇状态待确认',kind:'message',detail:'来自侧边栏镜像的扩展提示：设备服务正在校对当前会话与曲线，请等待状态更新后继续操作'},
 {name:'mirror-wait-not-confirmed',expected:'正在确认风扇状态',kind:'message',detail:'已开启，等待 YMCC 风扇真实信号；不重复启动'},
 {name:'node-saved-uncontrolled',expected:'节点 1 已保存',kind:'message',detail:'节点 1 已保存（未写入硬件：风扇控制未开启）'},
 {name:'preset-saved-uncontrolled',expected:'挡位已保存',kind:'message',detail:'风扇挡位已保存，未启用风扇控制'},
 {name:'applying-retained-wording',expected:'正在应用风扇曲线',kind:'message',detail:'正在应用风扇曲线'},
 {name:'sleep-retained-wording',expected:'睡眠中',kind:'message',detail:'睡眠中'},
];
export async function fanStatusCases({height,open,actionCase,page,shot,task}) {
 for(const scenario of notices) {
  const f=await open('/fan',height);
  await actionCase('Fan compact status '+scenario.name,height,async()=>{
   const baseline=await f.evaluate(()=>{const h=document.querySelector('.console-heading h1');return {width:h.getBoundingClientRect().width,fontSize:getComputedStyle(h).fontSize};});
   const relevantCalls=()=>f.evaluate(()=>window.__previewCalls.filter(c=>/^(?:fan\.|http\.request|power\.fan|process\.)/.test(c.cmd)).length);
   const beforeCalls=await relevantCalls();
   const projected=await f.evaluate(scenario=>window.__fanStatusAudit.set(scenario),scenario);
   const observed=await f.evaluate(()=>{
    const heading=document.querySelector('.console-heading'),h1=heading.querySelector('h1'),badge=heading.querySelector('.fan-status-banner'),strong=badge.querySelector('strong');
    const rect=el=>el.getBoundingClientRect().toJSON(),range=document.createRange();range.selectNodeContents(h1);
    const titleRects=[...range.getClientRects()].map(r=>({top:r.top,bottom:r.bottom}));const button=document.querySelector('.fan-toggle'),style=getComputedStyle(button);
    return {h1Text:h1.textContent.trim(),heading:rect(heading),h1:rect(h1),badge:rect(badge),titleRects,h1FontSize:getComputedStyle(h1).fontSize,
      h1ClientWidth:h1.clientWidth,h1ScrollWidth:h1.scrollWidth,badgeText:strong.textContent.trim(),badgeTitle:badge.getAttribute('title'),strongTitle:strong.getAttribute('title'),
      badgeFocusable:window.__gamepadAudit.focusables().some(el=>badge.contains(el)),
      button:{text:button.textContent.trim(),pressed:button.getAttribute('aria-pressed'),busy:button.getAttribute('aria-busy'),disabled:button.disabled,
       pending:button.classList.contains('fan-toggle-pending'),background:style.backgroundColor,color:style.color,opacity:style.opacity}};
   });
   assert.deepEqual(projected.ownerAfter,projected.ownerBefore,'presentation fixture changed lifecycle owner');
   assert.equal(await relevantCalls(),beforeCalls,'status presentation performed Fan/process/HTTP IPC');
   assert.equal(observed.h1Text,'风扇控制');
   assert.equal(observed.h1FontSize,baseline.fontSize,'status shrank title font');
   assert.ok(Math.abs(observed.h1.width-baseline.width)<1,'status compressed title width: '+JSON.stringify(observed));
   assert.ok(observed.titleRects.length>0&&Math.max(...observed.titleRects.map(r=>r.top))-Math.min(...observed.titleRects.map(r=>r.top))<2,'h1 wrapped');
   assert.ok(observed.h1ScrollWidth<=observed.h1ClientWidth+1,'h1 text clipped by status');
   assert.ok(observed.h1.right<=observed.badge.left+1,'heading/status overlap');
   assert.ok(observed.badge.right<=observed.heading.right+1,'status spills out of header');
   assert.ok(Math.abs(observed.h1.y+observed.h1.height/2-observed.badge.y-observed.badge.height/2)<5,'status no longer shares title row');
   assert.ok(observed.badgeText.length>0&&[...observed.badgeText].length<=12,'status badge exceeds12: '+observed.badgeText);
   assert.equal(observed.badgeText,projected.label,'real component summary was replaced');
   if(scenario.expected)assert.equal(observed.badgeText,scenario.expected,'status category changed: '+scenario.name);
   assert.equal(observed.badgeTitle??observed.strongTitle,projected.detail,'full actual status detail lost from title');
   assert.equal(observed.button.text,projected.buttonText,'compact banner changed button projection');
   assert.equal(observed.badgeFocusable,false,'informational badge entered gamepad navigation');
   if(scenario.detail)assert.equal(projected.detail,scenario.detail,'requested message failed to reach actual status computed');
   if(scenario.name==='connecting')assert.equal(observed.button.busy,'true');
   if(scenario.name==='recovering'){assert.equal(observed.button.text,'正在恢复…');assert.equal(observed.button.pending,true,'recovery button lost lit pending style');assert.equal(observed.button.opacity,'1');assert.notEqual(observed.button.background,'rgba(0, 0, 0, 0)');}
   if(scenario.name==='attention'||scenario.name==='device-gate-long')assert.equal(observed.button.text,'风扇未操控');
   if(scenario.name==='attention')assert.equal(observed.badgeText,'风扇自动恢复未完成');
   if(scenario.name==='mirror-wait-not-confirmed')assert.notEqual(observed.badgeText,'控制已开启','pending mirror wording falsely claimed established control');
   if(scenario.name==='applying-retained-wording')assert.equal(observed.badgeText,'正在应用风扇曲线');
   if(scenario.name==='sleep-retained-wording')assert.equal(observed.badgeText,'睡眠中');
   if(scenario.name==='exit-notice')assert.equal(observed.badgeText,'交还仍在进行');
   if(['device-gate-long','recovering','connecting'].includes(scenario.name)){if(shot)await shot('fan-status-'+scenario.name+'-'+height);if(task)await f.locator('.console-heading').screenshot({path:path.join(task,'evidence','fan-header-'+scenario.name+'-'+height+'.png')});}
  });
 }
}
