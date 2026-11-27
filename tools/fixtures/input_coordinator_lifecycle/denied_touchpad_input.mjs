import {createHash} from 'node:crypto';

export const deniedTouchpadInputSource='tools/fixtures/input_coordinator_lifecycle/denied_touchpad_input.mjs';

// This fixture never opens a real touchpad input channel.
export function bindDeniedTouchpadInput(source){
 const anchor='#include "screen_touchpads.h"';
 if(source.split(anchor).length!==2)throw Error('CURRENT_TOUCHPAD_INPUT_INCLUDE_REQUIRED');
 const prefix=[
  '#ifndef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE',
  'namespace ymcc::screenpads {',
  'struct FixtureDeniedTouchpadInputRuntime {',
  ' HRESULT error=E_UNEXPECTED;',
  ' bool useFreshInputChannel(){++::forbiddenIo;throw std::logic_error("unexpected-offline-touchpad-input-channel-reset");}',
  '};',
  'inline FixtureDeniedTouchpadInputRuntime nativePadInputRuntime;',
  '}',
  '#endif'
 ].join('\n')+'\n';
 const replacement=prefix+anchor,adapted=source.replace(anchor,()=>replacement);
 if(adapted.replace(replacement,()=>anchor)!==source)throw Error('CURRENT_TOUCHPAD_INPUT_PORT_INVERSE_REQUIRED');
 const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
 return {source:adapted,substitutions:[{symbol:'ymcc::screenpads::nativePadInputRuntime',beforeSha256:hash(anchor),afterSha256:hash(replacement),
  rejectOnly:true,sourceCaptureModified:false,scope:'Native touchpad input surface is excluded in this controller fixture; any channel creation increments forbiddenIo and throws before hardware or thread work.'}]};
}
