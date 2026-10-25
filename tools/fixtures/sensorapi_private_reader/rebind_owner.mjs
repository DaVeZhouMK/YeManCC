import {extract} from '../legion_motion_consumer/extract_main.mjs';
export function rebindPrivateOwner(base,before,current,bind){
 const once=(text,from,to)=>{if(text.split(from).length!==2)throw Error('Owner rebind seam: '+from.slice(0,110));return text.replace(from,()=>to);};
 const start=base.indexOf('namespace candidate {');if(start<0)throw Error('Current owner namespace missing');let prefix=base.slice(0,start),tail=base.slice(start);
 const section=(text,which)=>{const a=text.indexOf('// Private typed capture for an explicit-source transaction.'),b=text.indexOf('// A choice guard is present before the first selection.',a),e=text.indexOf('static DWORD WINAPI inputCaptureSensorThreadProc',b);if(a<0||b<a||e<b)throw Error('Actual source sections');return which===0?text.slice(a,b):text.slice(b,e);};
 for(let i=0;i<2;++i){const old=section(before,i),now=section(current,i);tail=once(tail,old,now);bind({symbol:i?'native-source-view-section':'native-private-transport-section',code:now});}
 for(const name of ['inputCapturePrivateOemCalibrationRecipe','inputCapturePrivateCalibrationRequestedKey','inputCaptureWindowsProcessingAdopt']){
  const old=extract(tail,name),prior=extract(before,name),now=extract(current,name);if(old.code.replace(/\r\n/g,'\n')!==prior.code.replace(/\r\n/g,'\n'))throw Error('Retained function does not equal approved preimage: '+name);tail=once(tail,old.code,now.code);bind(now);
 }
 const added=extract(current,'inputCapturePrivateStandardApiBound'),marker=extract(tail,'inputCapturePrivateOemCalibrationRecipe').code;tail=once(tail,marker,added.code+'\n'+marker);bind(added);
 const metadata=source=>{const body=extract(source,'inputCaptureOnGamepad').code.replace(/\r\n/g,'\n'),a=body.indexOf('    InputCaptureSensorMeta sensorMeta;'),b=body.indexOf('    const bool sameProvider',a);if(a<0||b<a)throw Error('Actual BUS metadata');return body.slice(a,b);};
 tail=once(tail,metadata(before),metadata(current));bind({symbol:'actual-current-bus-metadata',code:metadata(current)});
 prefix=once(prefix,'#include <windows.h>','#include <windows.h>\n#include <initguid.h>\n#include "sensorapi_private_reader.h"');
 return prefix+tail;
}