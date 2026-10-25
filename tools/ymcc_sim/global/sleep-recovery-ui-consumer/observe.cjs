'use strict';
// Add passive evidence to the existing controller; do not rewrite the fixture or its program.
require('../fan-monitor-browser-consumer/observe.cjs');
const fs=require('node:fs'),path=require('node:path'),Module=require('node:module'),crypto=require('node:crypto'),http=require('node:http');
const {native,root}=require('../fan-monitor-browser-consumer/io.cjs');
const trace={serverClosed:0,pageErrors:[],snapshotErrors:[],snapshots:[],served:[]};
const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const createServer=http.createServer;
http.createServer=function(listener){
 const server=createServer.call(this,function(request,response){
  const chunks=[],end=response.end,write=response.write;
  const capture=(chunk,encoding)=>{if(chunk!==undefined&&chunk!==null)chunks.push(Buffer.isBuffer(chunk)?Buffer.from(chunk):Buffer.from(chunk,typeof encoding==='string'?encoding:undefined));};
  response.write=function(chunk,encoding,...rest){capture(chunk,encoding);return write.call(this,chunk,encoding,...rest);};
  response.end=function(chunk,encoding,...rest){
   capture(chunk,encoding);const bytes=Buffer.concat(chunks),file=path.join(root,'served-'+trace.served.length+(request.url==='/fixture.js'?'.js':'.html'));
   native.write(file,bytes);const row={url:request.url,file,sha256:digest(bytes),bytes:bytes.length,finished:false};trace.served.push(row);response.once('finish',()=>{row.finished=true;});
   return end.call(this,chunk,encoding,...rest);
  };
  return listener(request,response);
 });
 server.once('close',()=>{trace.serverClosed++;});return server;
};
require('node:module').syncBuiltinESMExports();
const load=Module._load,wrapped=new WeakMap();
Module._load=function(name,parent,isMain){
 const actual=load.apply(this,arguments);if(name!=='playwright')return actual;if(wrapped.has(actual))return wrapped.get(actual);
 const launch=actual.chromium.launch.bind(actual.chromium);
 const chromium=new Proxy(actual.chromium,{get(target,key){if(key!=='launch')return Reflect.get(target,key);return async options=>{
  const browser=await launch(options),newPage=browser.newPage.bind(browser),close=browser.close.bind(browser),pages=[];
  async function snapshot(page,reason){
   if(page.isClosed())return;
   try{const memory=await page.evaluate(()=>{const f=window.__fixture;return f?{calls:structuredClone(f.calls),opened:structuredClone(f.opened),sessionStorageKeys:Object.keys(sessionStorage),localStorageKeys:Object.keys(localStorage)}:null;});if(!memory)throw Error('FIXTURE_MEMORY_MISSING');trace.snapshots.push({page:pages.indexOf(page),reason,...memory});}
   catch(error){trace.snapshotErrors.push({page:pages.indexOf(page),reason,message:error.message});}
  }
  browser.newPage=async options=>{
   const page=await newPage(options);pages.push(page);page.on('pageerror',error=>trace.pageErrors.push({page:pages.indexOf(page),message:String(error)}));
   const reload=page.reload.bind(page),pageClose=page.close.bind(page);
   page.reload=async(...args)=>{await snapshot(page,'before-original-reload');return reload(...args);};
   page.close=async(...args)=>{await snapshot(page,'before-original-page-close');return pageClose(...args);};
   return page;
  };
  browser.close=async(...args)=>{for(const page of pages)await snapshot(page,'before-original-browser-close');return close(...args);};
  return browser;
 };}});
 const api={...actual,chromium};wrapped.set(actual,api);return api;
};
process.prependOnceListener('exit',()=>native.write(path.join(root,'ui-observation.json'),JSON.stringify(trace,null,2)));
