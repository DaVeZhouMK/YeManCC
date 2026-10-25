// Boundaries specific to display repair; no product/menu/device startup.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {fileURLToPath} from 'node:url';
const [beforeRoot,output]=process.argv.slice(2);if(!beforeRoot||!output||!path.isAbsolute(beforeRoot)||!path.isAbsolute(output))throw Error('absolute before-source directory and report required');
const project=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');const read=p=>fs.readFileSync(p,'utf8').replace(/\r\n/g,'\n').replace(/^\uFEFF/,'');
const pads=read(path.join(project,'native','screen_touchpads.h')),before=read(path.join(beforeRoot,'screen_touchpads.h'));
const surface=read(path.join(project,'native','screen_touchpad_input_surface.h')),oldSurface=read(path.join(beforeRoot,'screen_touchpad_input_surface.h'));
const display=read(path.join(project,'native','screen_touchpad_display_policy.h')),visibility=read(path.join(project,'native','screen_touchpad_visibility.h'));
const region=(s,begin,end)=>{const a=s.indexOf(begin),b=s.indexOf(end,a);assert(a>=0&&b>a);return s.slice(a,b)};
const cases=[];function check(name,fn){fn();cases.push({name,ok:true})}
check('production mapping Core unchanged',()=>assert.equal(region(pads,'class Core {','inline std::mutex mutex;'),region(before,'class Core {','inline std::mutex mutex;')));
check('controlled pointer consumption unchanged',()=>assert.equal(region(surface,'void consume(','bool uploadZero('),region(oldSurface,'void consume(','bool uploadZero(')));
check('touch and pen, not mouse, remain controlled',()=>assert(surface.includes('controller_.SetControlledInput(winrt::Windows::UI::Core::CoreInputDeviceTypes::Touch|winrt::Windows::UI::Core::CoreInputDeviceTypes::Pen);')));
const repair=region(surface,'bool ensureMousePass(','auto source()const');
check('input hosts remain enabled',()=>{assert(repair.includes('EnableWindow(root_,TRUE)'));assert(!repair.includes('EnableWindow(root_,FALSE)'))});
check('input repair never manipulates focus or injects input',()=>{for(const term of ['SetForegroundWindow','SetFocus(','AttachThreadInput','SendInput','InjectTouchInput','SetCapture(','ReleaseCapture(','Sleep(','PeekMessage','GetMessage('])assert(!repair.includes(term),term)});
check('input repair keeps non-activation',()=>assert(repair.includes('SWP_NOACTIVATE')));
check('input repair remains bounded and retains upload cache',()=>{assert(repair.includes('size.cx>16384'));assert(repair.includes('else ++cacheHits'))});
check('visual-only layers are natively disabled',()=>assert(display.includes('EnableWindow(window,FALSE)')));
check('display repair does not change per-pixel alpha API',()=>assert(!/\bSetLayeredWindowAttributes\s*\(/.test(display)));
check('failed native initialization gates pad display',()=>{assert(pads.includes('const bool inputReady=nativeInputSync'));assert(pads.includes('show && permitted(mode) && inputReady?SW_SHOWNOACTIVATE:SW_HIDE'))});
check('pair repair happens before game re-raise',()=>{const r=region(visibility,'inline void restoreScreenLayerPair(','inline bool consumeOwnerVisibility(');assert(r.indexOf('screenPairMouseSafe')<r.indexOf('SetWindowPos'))});
check('failed pair repair hides instead of opaque fallback',()=>assert(visibility.includes('if(!safe){if(input)ShowWindow(input,SW_HIDE);if(outline)ShowWindow(outline,SW_HIDE);release();}')));
const result={ok:true,passed:cases.length,cases,scope:'repair code boundaries; not actual game or physical input acceptance'};fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output,JSON.stringify(result,null,2));console.log(JSON.stringify({ok:true,passed:cases.length,output}));
