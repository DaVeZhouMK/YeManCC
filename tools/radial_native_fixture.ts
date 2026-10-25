import {UTILITY_OPTIONS,emptyRadialUtilities} from '../src/radial/utilities';
import {RADIAL_INPUT_OPTIONS} from '../src/radial/input';
import {writeFileSync} from 'node:fs';
import {defaultRadialSettings} from '../src/radial/model';
import {defaultContinuousBank} from '../src/bridge/performanceControls';
import {radialPerformanceFrame} from '../src/radial/performance';
const bank=defaultContinuousBank('ac');bank.tdp=15;bank.tdpCeiling=25;bank.manual.unlimited=false;
const snapshot:any={identity:{scope:'global',followEffective:true,side:'ac',actualSide:'ac',exe:'',pid:0,processCreated:'',generation:1,revision:'native-fixture'},side:'ac',actualSide:'ac',bank,controls:{version:1,ac:structuredClone(bank),dc:structuredClone(bank)},frameRates:{ac:{mode:'off'},dc:{mode:'off'}},configured:true,hardwareMinimum:3,hardwareMaximum:40,hybrid:true,smtAvailable:true,hyperThreadMode:'default'};
const capability={ready:true,minPL1:5,maxPL1:30},performance=radialPerformanceFrame(snapshot,capability,'native-performance-auto');
const manual=structuredClone(snapshot);manual.bank.mode='manual';manual.controls.ac.mode='manual';manual.identity.revision='native-manual';
const fixturePerformanceManual=radialPerformanceFrame(manual,capability,'native-performance-manual');
const fixtureCurrentSettings=defaultRadialSettings();
const fixtureHud={type:'desktop-hud',protocol:1,show:true,
 preferences:{enabled:true,output:'ymcc',ymcc:{visibility:'always',position:1,scale:1,dragLocked:false}},
 data:{source:'ymcc-uniform',stale:false,values:{tdp:15,cpuTemp:62},coreUsage:[],coreClocks:[],frameTimes:[]}};
const settings=defaultRadialSettings();settings.pages[0].items[2]={id:'monitor',label:'监控',action:'page.monitor',enabled:true};settings.pages[0].items[6]={id:'tdp',label:'TDP',action:'tdp',enabled:true};
settings.pages[0].items[7]={id:'browserDirect',label:'浏览器',action:'windows.browser',enabled:true};
settings.pages[0].items[4]={id:'settings',label:'设置',action:'page.settings',enabled:true};
settings.pages[0].items[3]={id:'fanControls',label:'风扇',action:'fan',enabled:true};
const fan={sourceRevision:'fixture-fan-1',available:true,busy:false,choice:'off',active:false,pending:false,confirmed:false,notice:'原生夹具 · 风扇未启用'};
const input={sourceRevision:'fixture-input-1',scope:'global',label:'原生夹具 · 全局输入',available:true,busy:false,pending:false,notice:'原生夹具输入状态',controls:(['padPersona','gyroEnabled','gyroPreset'] as const).map(key=>({key,value:key==='padPersona'?'elite':key==='gyroEnabled'?'on':'fps',enabled:true,choices:RADIAL_INPUT_OPTIONS[key].map(option=>({value:option.data,label:option.label,enabled:true}))}))};
const utilities=emptyRadialUtilities();utilities['monitor.templates']={sourceRevision:'fixture-monitor-A',available:true,busy:false,pending:false,selected:'A',notice:'Owned monitor template A',choices:UTILITY_OPTIONS['monitor.templates'].map(choice=>({...choice,enabled:true}))};utilities['steam.controls']={sourceRevision:'fixture-steam-desktop',available:true,busy:false,pending:false,selected:'start',notice:'Owned Steam desktop',choices:UTILITY_OPTIONS['steam.controls'].map(choice=>({...choice,enabled:choice.value!=='start'}))};
writeFileSync(process.argv[2],JSON.stringify({settings,fixtureCurrentSettings,fixtureHud,fan,input,utilities,tdp:{available:true,min:5,max:25,value:15,label:'Owned native fixture'},performance,fixturePerformanceManual,feedback:false}));
