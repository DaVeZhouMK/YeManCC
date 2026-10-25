// Read-only GPU capability source shared by the simulator and TS chart.
(function(root){
'use strict';
const SOURCE_SERIES={
  "ts": {
    "headerWatts": [
      5,
      8,
      10,
      13,
      15,
      17,
      20,
      23,
      26,
      30,
      35
    ],
    "rogOptimized": [
      951,
      956,
      1249,
      1829,
      2280,
      2543,
      2779,
      2902,
      2892,
      3020,
      3058
    ],
    "a8Stock": [
      906,
      1163,
      1555,
      2255,
      2653,
      2871,
      2960,
      3111,
      3303,
      3411,
      3412
    ],
    "a8Optimized": [
      934,
      1292,
      1638,
      2323,
      2671,
      3037,
      3316,
      3433,
      3444,
      3576,
      3682
    ],
    "ambiguousColumns": [],
    "fitUsesColumns": [
      0,
      1,
      2,
      3,
      4,
      5,
      6,
      7,
      8,
      9,
      10
    ],
    "notes": "2026-10-17用户指定左图TS曲线为主，右图只补258V的游戏性能比例；原始波动保留，拟合取单调包络；跨CPU只迁移形状，不冒充实测。"
  },
  "release": {
    "imageWatts": [
      5,
      8,
      10,
      13,
      15,
      17,
      20,
      23,
      26,
      30,
      35,
      40,
      45,
      50,
      55,
      60,
      65,
      70,
      75,
      80,
      85
    ],
    "m395Optimized": [
      null,
      null,
      null,
      2803,
      2928,
      3240,
      3762,
      4386,
      5001,
      5817,
      6607,
      7491,
      8536,
      9478,
      10014,
      10326,
      10513,
      10691,
      10852,
      10970,
      11145
    ],
    "deckGame": [
      [
        5,
        19
      ],
      [
        8,
        29
      ],
      [
        10,
        34
      ],
      [
        13,
        38
      ],
      [
        15,
        40
      ],
      [
        16,
        40
      ]
    ],
    "intel258vGame": [
      [
        5,
        18
      ],
      [
        8,
        29
      ],
      [
        10,
        36
      ],
      [
        13,
        46
      ],
      [
        15,
        51
      ],
      [
        17,
        56
      ],
      [
        20,
        62
      ],
      [
        23,
        68
      ],
      [
        26,
        71
      ],
      [
        30,
        77
      ]
    ],
    "g3eGame1080": {
      "points": [
        [
          8,
          19
        ],
        [
          17,
          40
        ],
        [
          35,
          58
        ]
      ],
      "preset": "1080p-medium-native-no-upscaling",
      "source": "g3eChip",
      "powerKind": "configured-PL1-with-PL2-10-19-45W"
    },
    "g3eSteady": [
      {
        "watts": 15,
        "gpu": 4044,
        "kind": "ratio-fit",
        "ratioTo35W": 0.6,
        "source": "g3eSteady",
        "note": "约60%为原作者性能比例；非精确GPU读数"
      },
      {
        "watts": 20,
        "gpu": 4718,
        "kind": "published-graphics-score",
        "source": "g3e20",
        "note": "不同评测样机"
      },
      {
        "watts": 25,
        "gpu": 5729,
        "kind": "ratio-fit",
        "ratioTo35W": 0.85,
        "source": "g3eSteady",
        "note": "约85%为原作者性能比例；非精确GPU读数"
      },
      {
        "watts": 35,
        "gpu": 6740,
        "kind": "published-graphics-score",
        "source": "g3eSteady"
      }
    ],
    "scope": "image-shape-plus-device-specific-anchors;not-all-points-TS-measured"
  }
};
const clamp=(x,a,b)=>Math.max(a,Math.min(b,x));
function interpolate(points,x){if(x<=points[0][0])return points[0][1];for(let i=1;i<points.length;i++)if(x<=points[i][0]){const a=points[i-1],b=points[i];return a[1]+(b[1]-a[1])*(x-a[0])/(b[0]-a[0]);}return points.at(-1)[1];}
function monotone(points,ceiling=Infinity){let last=0;return points.map(([w,v])=>[w,last=Math.max(last,Math.min(ceiling,v))]);}
function create(profiles){
 // Simulation/chart horizon is separate from the measured sustained envelope.
 for(const [id,p]of Object.entries(profiles)){p.powerReferenceWatts??=p.max;p.calibratedMaxWatts??=p.max;if(id==='g3e'){p.max=80;p.limitEvidence={kind:'Intel-ARK-maximum-turbo-not-sustained-TDP',watts:80,sustainedConfigMaxWatts:35,source:'https://www.intel.com/content/www/us/en/products/sku/245625/intel-arc-g3-extreme-processor-12m-cache-up-to-4-70-ghz/specifications.html'};}else if(!['deck','m395'].includes(id))p.max=Math.max(p.max,45);}
 const raw=SOURCE_SERIES.ts,release=SOURCE_SERIES.release,rog=monotone(raw.headerWatts.map((w,i)=>[w,raw.rogOptimized[i]])),a8=raw.headerWatts.map((w,i)=>[w,raw.a8Optimized[i]]),max395=release.imageWatts.map((w,i)=>[w,release.m395Optimized[i]]).filter(p=>p[1]!==null),rawCache=new Map();
 const floors={reference:[1.8,2.4],deck:[.65,1.6],m780:[1.8,2.4],z2e:[1.9,2.2],c8:[.24,1.1],g3e:[.8,2.4],m395:[8,1.7],m6800:[1.8,2.4],i1195:[1.5,2.3],m5800:[1.7,2.3],c7:[2.4,2.6],gos:[1.8,2.3]};
 const origins={
  reference:['Z1 Extreme 图示曲线 / 17W参考锚点','shape-transfer','原17W样机仅保留内部校准，不提供机型选项。'],
  deck:['Z1 Extreme 图示形状 + 用户16W / 1400分','shape-transfer','曲线形状借用左图，16W封顶；不再借用右图的Steam Deck游戏帧率。'],
  m780:['Z1 Extreme 图示形状 + 7840U独立TS锚点','shape-transfer','同类780M借用功耗释放形状，按独立30W显卡分归一。'],
  z2e:['用户左图 · 微星A8 Z2E优化显卡分','user-ts-series','直接使用灰线TS显卡分；原版与优化共用能力库，差别由资源分配产生。'],
  m395:['用户左图 · Ryzen AI Max+ 395优化显卡分','user-ts-series','直接使用黄色TS显卡分。图中含电压偏移和关闭CCD设置，不视为所有395的原厂保证。13W以下无能力数据。'],
  c8:['用户右图 · 258V 720P中画质2077比例 + 独立TS锚点','game-ratio-transfer','18/29/36/46/51/56/62/68/71/77是游戏FPS，只迁移曲线比例，不当成实测TS分。'],
  g3e:['G3E独立曲线 · 官方设备45W持续/Intel最大睿频80W','device-specific-mixed','官方X2 Mini/APEX Air支持45W持续释放；Intel最大睿频80W不是整机持续保证。35W以上缺少同条件TS显卡分，数值仅为保守占位，不代表35W后无提升或确认进入平台。15/25W为比例推算。'],
  m6800:['7840U曲线 · 低瓦数同瓦性能×85%','user-requested-transfer','3–15W按7840U同瓦TS模拟分的85%；15–28W平滑衔接6800U独立28W锚点。'],
  i1195:['6800U参考形状 + i7-1195G7独立TS锚点','shape-transfer','缺少同设置功耗扫描，借用6800U形状；并非Intel实测释放曲线。'],
  m5800:['6800U参考形状 + 5800U独立TS锚点','shape-transfer','不同核显架构仅借用释放形状，不冒充同架构实测。'],
  gos:['6800U参考形状 + Ryzen Z2 Go独立TS锚点','shape-transfer','按独立TS显卡锚点归一。'],
  c7:['左图低瓦数形状 + 155H既有20/35/43W参考','mixed-anchor-transfer','20W以上沿用既有参考；跨测试的35W/43W微小倒挂拟合为平台，原始读数保留。']
 };
 const g3ePowerEvidence={verifiedAt:'2026-10-17',supportedDeviceSustainedWatts:45,maximumTurboWatts:80,comparableGpuDataThroughWatts:35,confirmedGpuPlateauWatts:null,highPowerGpuStatus:'uncalibrated-not-confirmed-plateau',sources:[{kind:'manufacturer-device-power',url:'https://onexplayerstore.com/collections/new-arrival/products/onexplayer-x2-mini-with-intel%C2%AE-arc%E2%84%A2-g3-extreme-8-8-oled-3-in-1-gaming-handheld'},{kind:'manufacturer-device-power',url:'https://onexplayerstore.com/blogs/weekly-blog/apex-vs-apex-air-comparison'},{kind:'processor-specification',url:'https://www.intel.com/content/www/us/en/products/sku/245625/intel-arc-g3-extreme-processor-12m-cache-up-to-4-70-ghz/specifications.html'}]};
 const minimumWatts=id=>id==='m395'?13:3;
 const lowParams=id=>floors[id]??floors.reference;
 function below(points,id,w){const [floor,exponent]=lowParams(id),first=points[0];return first[1]*Math.pow(clamp((w-floor)/(first[0]-floor),0,1),exponent);}
 function rawCurve(id){if(!rawCache.has(id))rawCache.set(id,buildCurve(id));return rawCache.get(id);}
 function buildCurve(id){
  const p=profiles[id];if(!p)throw Error('未知GPU机型');
  if(id==='m395')return max395;
  if(id==='z2e')return a8;
  if(id==='c8')return release.intel258vGame.map(([w,v])=>[w,v*p.anchor.gpu/77]);
  if(id==='g3e'){const points=release.g3eSteady.map(r=>[r.watts,r.gpu]);return [[8,points[0][1]*Math.pow(6/13,.6)],...points];}
  if(id==='c7'){const top=p.anchor.gpu,scale=2581/interpolate(rog,20);return [...rog.filter(([w])=>w<20).map(([w,v])=>[w,v*scale]),[20,2581],[35,Math.min(3517,top)],[43,top]];}
  if(id==='m6800')return [5,8,10,13,15,17,20,23,26,28].map(w=>[w,defaultScore('m780',w)*ratio6800(w)]);
  if(['i1195','m5800','gos'].includes(id)){const scale=p.anchor.gpu/defaultScore('m6800',Math.min(28,p.anchor.watts));return [5,8,10,13,15,17,20,23,26,28,p.max].filter((w,i,a)=>w<=p.max&&a.indexOf(w)===i).sort((a,b)=>a-b).map(w=>[w,defaultScore('m6800',Math.min(28,w))*scale]);}
  const scale=p.anchor.gpu/interpolate(rog,p.anchor.watts);return rog.map(([w,v])=>[w,v*scale]);
 }
 function ratio6800(w){const anchorRatio=profiles.m6800.anchor.gpu/defaultScore('m780',28);return .85+(anchorRatio-.85)*clamp((w-15)/13,0,1);}
 function defaultScore(id,w){if(w<minimumWatts(id))return 0;const points=rawCurve(id);if(id==='m6800'&&w<5)return defaultScore('m780',w)*.85;return w<points[0][0]?below(points,id,w):interpolate(points,w);}
 function score(id,watts){const p=profiles[id];if(!p||!Number.isFinite(watts)||watts<1||watts>p.max)throw Error('超出机型GPU曲线范围');return defaultScore(id,watts);}
 function defaultPoints(id){const p=profiles[id],watts=[...Array.from({length:Math.min(35,p.max)-minimumWatts(id)+1},(_,i)=>i+minimumWatts(id)),...Array.from({length:10},(_,i)=>40+i*5),...rawCurve(id).map(row=>row[0]),p.max].filter((w,i,a)=>w>=minimumWatts(id)&&w<=p.max&&a.indexOf(w)===i).sort((a,b)=>a-b);return watts.map(w=>[w,defaultScore(id,w)]);}
 function entry(id){const p=profiles[id];if(!p)throw Error('未知GPU机型');const [source,kind,note]=origins[id]??origins.reference;return {id,...(id==='g3e'?{powerEvidence:structuredClone(g3ePowerEvidence),unverifiedGpuAboveWatts:35}:{}),label:p.label,minWatts:minimumWatts(id),maxWatts:p.max,calibratedMaxWatts:p.calibratedMaxWatts,chartMaxWatts:p.max>55?85:p.max>35?Math.ceil(p.max/5)*5:p.max,grid:{low:{from:1,to:35,step:1},high:{from:35,to:85,step:5}},anchorWatts:p.anchor.watts,anchorGpu:defaultScore(id,p.anchor.watts),source,kind,note,points:defaultPoints(id).map(row=>[...row]),rawPoints:rawCurve(id).map(row=>[...row]),scope:'synthetic-TS-GPU-capability-not-game-FPS-or-measured-rail-power',noDataBelowWatts:id==='m395'?13:null,lowPowerExtrapolationBelowWatts:id==='m395'?null:rawCurve(id)[0][0]};}
 function tdpAdvisory(id,watts){const threshold=id==='deck'?16:['g3e','m395'].includes(id)?null:45;return {threshold,watts,show:threshold!==null&&watts>threshold,message:threshold!==null&&watts>threshold?(id==='deck'?'Steam Deck':'当前APU')+' 已超过'+threshold+'W参考范围；保留设定值，实际释放以设备为准':''};}
 const api={tdpAdvisory,minimumWatts,defaultScore,score,entry,defaultPoints};return Object.freeze(api);
}
const api={SOURCE_SERIES,create,interpolate};if(typeof module!=='undefined')module.exports=api;else root.YmccGpuCapabilityLibrary=api;
})(globalThis);
