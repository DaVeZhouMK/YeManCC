import fs from 'node:fs';
import {balancedEnd} from '../input-signals/source-extract.mjs';
import {digest} from './native-toolchain.mjs';
export function generateTouchExcerpts(repoRoot){
 const paths=['native/screen_touchpads.h','native/screen_touchpad_owner.h'];
 const source=fs.readFileSync(repoRoot+'/'+paths[0],'utf8'),owner=fs.readFileSync(repoRoot+'/'+paths[1],'utf8'),buttons=fs.readFileSync(repoRoot+'/native/screen_button_overlay.h','utf8'),spans=[];
 function take(text,file,name,start,end){const part=text.slice(start,end);spans.push({path:file,name,startLine:text.slice(0,start).split('\n').length,sha256:digest(part),text:part});return part;}
 const fn=(text,file,anchor)=>{const at=text.indexOf(anchor);if(at<0||text.indexOf(anchor,at+anchor.length)>=0)throw Error('TOUCH_SOURCE_ANCHOR_CHANGED:'+anchor);let from=anchor.includes('(')?balancedEnd(text,text.indexOf('(',at),'(',')'):at;const open=text.indexOf('{',from),end=balancedEnd(text,open);return take(text,file,anchor,at,end);};
 const first=source.indexOf('namespace ymcc::screenpads {'),end=source.indexOf('// A no-redirection input host');
 const generated={
  'touch-core.inc':take(source,paths[0],'Core-config-contact-transport',first,end)+'\n}\n',
  'touch-policy.inc':fn(source,paths[0],'inline Config getConfig()')+'\n'+fn(source,paths[0],'inline bool permitted(Mode mode)'),
  'touch-button-policy.inc':[fn(buttons,'native/screen_button_overlay.h','inline bool buttonPermitted(const Config& cfg,int index)'),fn(buttons,'native/screen_button_overlay.h','inline unsigned buttonFeedbackMask()'),fn(buttons,'native/screen_button_overlay.h','inline bool buttonsVisible()')].join('\n'),
  'touch-autohide-state.inc':fn(source,paths[0],'struct AutoHideState {')+';',
  'touch-state.inc':take(source,paths[0],'output-ownership-state',source.indexOf('inline unsigned sentKeys=0;'),source.indexOf('#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE',source.indexOf('inline unsigned sentKeys=0;'))),
 };
 const qualify=(parts)=>'namespace ymcc::screenpads {\n'+parts.join('\n\n')+'\n}\n';
 const hapticStart='inline std::atomic<bool> radialHapticContact{false};',hapticEnd='inline void outputHaptic(const Action& a)';
 const hapticAt=source.indexOf(hapticStart),hapticUntil=source.indexOf(hapticEnd);
 if(hapticAt<0||source.indexOf(hapticStart,hapticAt+1)>=0||hapticUntil<hapticAt||source.indexOf(hapticEnd,hapticUntil+1)>=0)throw Error('TOUCH_HAPTIC_STATE_ANCHOR_CHANGED');
 const hapticState=source.slice(hapticAt,hapticUntil);
 if(!/^inline std::atomic<bool> radialHapticContact\{false\};\r?\ninline std::atomic<bool> monitorHapticContact\{false\};(?:[ \t]*\/\/[^\r\n]*)?\r?\n$/.test(hapticState))throw Error('TOUCH_HAPTIC_STATE_SHAPE_CHANGED');
 generated['touch-output.inc']=qualify([take(source,paths[0],'actual-haptic-contact-state',hapticAt,hapticUntil),fn(source,paths[0],'inline void outputHaptic(const Action& a)'),fn(source,paths[0],'inline void output(const Action& a)'),fn(source,paths[0],'inline void releaseMappedState()'),fn(source,paths[0],'inline void release()')]);
 generated['touch-configure.inc']=qualify([fn(source,paths[0],'inline bool configure(const Config& cfg,int profile=-1)'),fn(source,paths[0],'inline void shutdown()')]);
 generated['touch-owner.inc']=qualify([take(owner,paths[1],'owner-epoch-state',owner.indexOf('inline constexpr UINT kOwnerRebindMessage'),owner.indexOf('struct NativeStartupPolicy')),fn(owner,paths[1],'inline bool onOwnerThread(HWND window)'),fn(owner,paths[1],'inline void ownerOnSuspend()'),fn(owner,paths[1],'inline bool queueOwnerResumeRebind()'),fn(owner,paths[1],'inline bool consumeOwnerRebind(UINT_PTR epoch)'),fn(owner,paths[1],'inline void shutdownForOwner()')]);
 // Core-only old assertion body remains unchanged; GDI destructors are separate counted IO.
 const old=source;
 const line=(anchor)=>{const a=old.indexOf(anchor);if(a<0)throw Error('PARTIAL_SOURCE_ANCHOR_MISSING:'+anchor);return old.slice(a,old.indexOf('\n',a))+'\n';};
 const chunk=(a,b)=>{const at=old.indexOf(a),until=old.indexOf(b,at+a.length);if(at<0||until<0)throw Error('PARTIAL_SOURCE_ANCHOR_MISSING');return old.slice(at,until);};
 let partial='namespace ymcc::screenpads {\n';
 for(const name of ['enum class Mode','enum class SummonPosition','enum class StandaloneSpecialMode','enum class Layout','enum class TouchFeedbackMode','struct Config','inline bool nativePad','inline Mode surfaceMode','struct Action'])partial+=line(name);
 partial+='inline int touchFeedbackStrength(TouchFeedbackMode){return 0;}\n'+chunk('struct Pad {','// Fixed-size pure state machine')+chunk('class Core {','    struct Projection')+'};\n'+chunk('inline unsigned sentKeys=0;','inline bool initialized=false;');
 partial+='inline Core core;inline std::mutex mutex;inline constexpr ULONG_PTR kInjectedTag=0x594d5450;inline HWND windows[2]{};\nstruct FeedbackOnly {void cancel(){}};inline FeedbackOnly feedbackOnlyContacts;struct AutoHide {std::array<UINT32,2> specialContacts{};};inline AutoHide autoHide;\ninline void outputHaptic(const Action&){} inline void buttonsReleaseMapped(){} inline void cursorStop(){} inline void syncOutlineFeedback(){} inline void syncContactVisuals(){} inline void nativeInputCancel(){} inline void buttonsRelease(){} inline void autoHideSync(){}\n';
 partial+=chunk('inline void output(const Action& a) {','// Dual PS surfaces share');
 partial+='inline UINT_PTR ownerLifecycleEpoch=1;inline bool ownerRebindQueued=false;\n'+fn(owner,paths[1],'inline void ownerOnSuspend()')+'\n}\n';
 generated['screen_touchpad_partial_send_extracted.inc']=partial;
 return {generated,spans};
}


