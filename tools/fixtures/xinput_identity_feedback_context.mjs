// Shared declaration of the pure support required by the current physical consumer.
import fs from 'node:fs';
import path from 'node:path';
export const CURRENT_GAME_FUNCTIONS=Object.freeze(['nativeGameFeedbackReadMatchesToken','nativeGameFeedbackFrameCurrent','nativeGameFeedbackCommandCurrent','gamepadSerialGameManualStarted','gamepadSerialGameOwnerFinished']);
export function currentFeedbackHeaders(repo){
 const seen=new Set(),queue=['game_feedback_dispatch.h','game_feedback_diagnostics.h'];
 while(queue.length){const name=queue.shift();if(seen.has(name))continue;
  if(!/^[A-Za-z0-9_.-]+\.h$/.test(name))throw Error('XINPUT_CONTEXT_HEADER_ESCAPE');seen.add(name);
  const file=path.join(repo,'native',name),text=fs.readFileSync(file,'utf8');
  for(const match of text.matchAll(/^\s*#include\s+"([^"]+)"/gm))queue.push(match[1]);
 }
 return [...seen].map(name=>'native/'+name);
}
export function currentFeedbackSupport(source,fn,record){
 function declaration(name){const pattern=new RegExp('^static[^\\r\\n]*\\b'+name+'(?:[;{=])[^\\r\\n]*','m'),match=pattern.exec(source);
  if(!match)throw Error('XINPUT_CONTEXT_DECLARATION_MISSING:'+name);
  return record(name,match.index,match.index+match[0].length,'declaration');
 }
 return {
  GAME_COMMAND:declaration('g_gameMotorExecutionStats')+'\n'+CURRENT_GAME_FUNCTIONS.slice(0,3).map(fn).join('\n'),
  GAME_DISPATCH:[declaration('g_gamepadSerialGameFeedback'),declaration('g_gamepadSerialGameInterested'),...CURRENT_GAME_FUNCTIONS.slice(3).map(fn)].join('\n'),
 };
}
