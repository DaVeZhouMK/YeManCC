'use strict';
const path=require('node:path');
const nativeResolve=path.resolve.bind(path);
function makePaths(caseRoot,sourceRoot){
 if(!path.isAbsolute(caseRoot)||!path.isAbsolute(sourceRoot))throw Error('TDP_DISPLAY_ABSOLUTE_ROOTS_REQUIRED');
 caseRoot=nativeResolve(caseRoot);sourceRoot=nativeResolve(sourceRoot);
 const canonical=file=>nativeResolve(file).replace(/[\\/]+$/,'').toLowerCase();
 const inside=(base,target)=>{const b=canonical(base),t=canonical(target);return t===b||t.startsWith(b+path.sep);};
 const legacy=nativeResolve(sourceRoot,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/templates-tdp-content-20261010');
 const output=nativeResolve(caseRoot,'private-output');
 function redirect(file){const absolute=nativeResolve(file);if(!inside(legacy,absolute))return{path:absolute,redirected:false};return{path:nativeResolve(output,absolute.slice(legacy.length).replace(/^[\\/]+/,'')),redirected:true};}
 return{caseRoot,sourceRoot,legacy,output,nativeResolve,inside,redirect};
}
module.exports={makePaths};
