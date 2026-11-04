// @ts-nocheck
// TS calibration data and CPU topology only, retained from the reviewed prototype.
import {gpuCapabilityLibrary} from './gpuCapabilityLibrary';
const tsScope:any={YmccGpuCapabilityLibrary:gpuCapabilityLibrary};
// Frozen TS calibration modules only. No game renderer, frame model, or hardware bridge.
(function(root){
'use strict';
// Review observations are whole-device measurements. Per-domain splits and sub-5W
// throughput are conservative scenario assumptions, never measured benchmark points.
const sources={
 intel258:{title:'Intel258V官方规格：8W Minimum Assured Power并非轻载瞬时封装消耗',url:'https://www.intel.com/content/www/us/en/products/sku/240957/intel-core-ultra-7-processor-258v-12m-cache-up-to-4-80-ghz/specifications.html'},
 claw258:{title:'Notebookcheck原测Claw8AI+：整机待机3.2/6.3/6.6W',url:'https://www.notebookcheck-cn.com/Claw-8-AI-Lunar-Lake.974588.0.html'},
 clawG3:{title:'Notebookcheck原测G3E：整机待机6.4/9.1/9.6W，非同屏幕亮度的封装对照',url:'https://www.notebookcheck.net/Intel-s-Arc-G3-Extreme-changes-the-handheld-game-MSI-Claw-8-EX-AI-Review.1325574.0.html'},
 amd395:{title:'AMD395官方规格：默认55W，cTDP45–120W',url:'https://www.amd.com/zh-tw/products/processors/laptop/ryzen/ai-300-series/amd-ryzen-ai-max-plus-395.html'},
 haloIdle:{title:'Phoronix原测395：空闲处理器约4W、整机墙上约12W，口径分开',url:'https://www.phoronix.com/review/framework-desktop-power/5'}
};
const envelopes={
 reference:{scoreFloor:1.8,gpuExponent:2.4,cpuExponent:1.9,activeIdle:2.8,lightIdle:1.7},
 deck:{scoreFloor:.65,gpuExponent:1.6,cpuExponent:1.25,activeIdle:1.75,lightIdle:.75},
 m780:{scoreFloor:1.8,gpuExponent:2.4,cpuExponent:1.9,activeIdle:2.8,lightIdle:1.7},
 c8:{scoreFloor:.24,gpuExponent:1.1,cpuExponent:.85,activeIdle:.75,lightIdle:.45},
 g3e:{scoreFloor:.8,gpuExponent:2.4,cpuExponent:1.6,activeIdle:1.8,lightIdle:1.4},
 m395:{scoreFloor:8,gpuExponent:1.7,cpuExponent:1.35,activeIdle:8,lightIdle:4},
 z2e:{scoreFloor:1.9,gpuExponent:2.2,cpuExponent:1.8,activeIdle:3,lightIdle:1.8},
 m6800:{scoreFloor:1.8,gpuExponent:2.3,cpuExponent:1.9,activeIdle:2.8,lightIdle:1.8},
 m5800:{scoreFloor:1.7,gpuExponent:2.3,cpuExponent:1.9,activeIdle:2.5,lightIdle:1.6},
 i1195:{scoreFloor:1.5,gpuExponent:2.3,cpuExponent:1.8,activeIdle:2.2,lightIdle:1.35},
 c7:{scoreFloor:2.4,gpuExponent:2.6,cpuExponent:2,activeIdle:3.3,lightIdle:2.2},
 gos:{scoreFloor:1.8,gpuExponent:2.3,cpuExponent:1.9,activeIdle:2.8,lightIdle:1.8}
};
const notes=['没有可信同平台1–5W CPU/GPU完整实测曲线；该域性能保持为保守假设，不能标为实测。','258V低瓦优势和G3E较高整机待机有原测支持，但不能由待机瓦数反推精确TS分。','395≤8W不填虚构游戏跑分：8W为本模型保守工作边界，不是已证实物理极限；低于边界保留待机开销、不给可用游戏吞吐。','系统基础开销为机型固定APU封装基底；没有屏幕、外设或其他周边功耗项。','请求TDP低于不可压缩开销时，实际模拟功耗可超过请求值；1W滑块不承诺硬件能写入。'];
const api={checkedAt:'2026-10-16',sources,envelopes,notes};root.YmccLowPowerData=api;
})(tsScope);
(function(root){
'use strict';
const SOURCES={
  "ulFormula": {
    "title": "UL：Time Spy分数计算",
    "url": "https://support.benchmarks.ul.com/support/solutions/articles/44002136143-how-is-the-3dmark-time-spy-score-calculated-",
    "kind": "official-method"
  },
  "ulGraphics": {
    "title": "UL：图形测试与CPU测试独立",
    "url": "https://support.benchmarks.ul.com/support/solutions/articles/44002136095-time-spy-graphics-tests",
    "kind": "official-method"
  },
  "deckSpec": {
    "title": "Valve：Steam Deck 4–15W",
    "url": "https://www.steamdeck.com/en/tech",
    "kind": "official-spec"
  },
  "deckRun": {
    "title": "UL实跑37419283：GPU1544 / CPU3547 / TS1686，功耗未提供",
    "url": "https://www.3dmark.com/spy/37419283",
    "kind": "published-result",
    "gpu": 1544,
    "cpu": 3547,
    "total": 1686,
    "watts": null,
    "driverApproved": false
  },
  "m780": {
    "title": "Notebookcheck：OneXPlayer2 Pro / 7840U，30W测试",
    "url": "https://www.notebookcheck-cn.com/OneXPlayer-2-Pro-Ryzen-7.798850.0.html",
    "kind": "firsthand-review",
    "watts": 30,
    "gpu": 2866,
    "total": 3183,
    "cpu": null
  },
  "c8Spec": {
    "title": "MSI：Claw8 / 258V / Arc140V",
    "url": "https://storage-asset.msi.com/excelSku/us/hh/Claw%208%20AI%2B%20A2VM-001US.pdf",
    "kind": "official-spec"
  },
  "c8": {
    "title": "Notebookcheck：Claw8，30W手动模式",
    "url": "https://www.notebookcheck-cn.com/Claw-8-AI-Lunar-Lake.974588.0.html",
    "kind": "firsthand-review",
    "watts": 30,
    "shortWatts": 37,
    "gpu": 4118,
    "total": 4448,
    "cpu": null
  },
  "g3e": {
    "title": "Notebookcheck：G3E 25/37W与手动35/45W图形分均已核",
    "url": "https://www.notebookcheck-cn.com/Arc-G3-Extreme-MSI-Claw-8-EX-AI.1327744.0.html",
    "kind": "firsthand-review",
    "watts": 25,
    "shortWatts": 37,
    "gpu": 6178,
    "total": 6619,
    "cpu": null,
    "manual": {
      "watts": 35,
      "shortWatts": 45,
      "gpu": 6755,
      "verification": "primary-review-table-verified-20261016",
      "source": "g3eManual"
    }
  },
  "user1": {
    "title": "用户图1：GPU分与功耗曲线",
    "kind": "user-image",
    "file": "reference-images/user-gpu-curves.jpg",
    "actualPowerKnown": false
  },
  "user2": {
    "title": "用户图2：17W[24W]设置下CPU主频对照",
    "kind": "user-image",
    "file": "reference-images/user-cpu-tradeoff.jpg",
    "nominalWatts": 17,
    "boostWatts": 24,
    "actualPowerKnown": false,
    "hardwareConfirmed": false
  },
  "m395": {
    "title": "Notebookcheck：MS-S1 Max / 395，80W Quiet GPU10258 / CPU10837 / TS10340",
    "url": "https://www.notebookcheck-cn.com/2025-PC-Minisforum-MS-S1-Max-AMD-Strix-Halo-Power-128-GB-Radeon-8060S.1125339.0.html",
    "kind": "firsthand-review",
    "quiet": {
      "watts": 80,
      "shortWatts": 110,
      "gpu": 10258,
      "cpu": 10837,
      "total": 10340
    },
    "performance": {
      "watts": 130,
      "shortWatts": 160,
      "gpu": 11340,
      "cpu": 10980,
      "total": 11284
    },
    "notes": "80W Quiet（短110W）；GPU/CPU/总分保留公开原始记录，公式重算与记录差1分时保留记录。"
  },
  "z2e": {
    "title": "Notebookcheck：Claw A8 / Z2 Extreme，AI档GPU3331 / TS3640",
    "url": "https://www.notebookcheck-cn.com/8-AMD-MSI-Claw-A8.1186781.0.html",
    "kind": "firsthand-review",
    "watts": 20,
    "shortWatts": 50,
    "gpu": 3331,
    "total": 3640,
    "cpu": null,
    "notes": "评测使用AI引擎模式；CPU持续约20W，短时50W；20W为名义标定，GPU测试实耗未知。"
  },
  "m6800": {
    "title": "Notebookcheck：OneXPlayer2 / 6800U，28W GPU2419 / TS2673",
    "url": "https://www.notebookcheck-cn.com/OneXPlayer-2-AMD-Zen3.732692.0.html",
    "kind": "firsthand-review",
    "watts": 28,
    "gpu": 2419,
    "total": 2673,
    "cpu": null
  },
  "m5800": {
    "title": "Notebookcheck：HP Aero13 / 5800U，GPU1128 / TS1283",
    "url": "https://www.notebookcheck-cn.com/Pavilion-Aero-13-Pavilion.555385.0.html",
    "kind": "firsthand-measurement-index",
    "watts": 25,
    "shortWatts": 30,
    "gpu": 1128,
    "total": 1283,
    "cpu": null,
    "notes": "5800U笔记本参考，非同芯片掌机的实测；仅据公开GPU/总分反算CPU。"
  },
  "i1195": {
    "title": "Notebookcheck：Prestige14 / i7-1195G7，GPU1819 / TS2007",
    "url": "https://www.notebookcheck.biz/Test-du-MSI-Prestige-14-Evo-quelles-sont-les-capacites-du-nouveau-Core-i7-1195G7.553362.0.html",
    "kind": "firsthand-review",
    "watts": 36,
    "shortWatts": 64,
    "gpu": 1819,
    "total": 2007,
    "cpu": null,
    "notes": "i7-1195G7、LPDDR4-4266、36W长/64W短；笔记本参考，不冒充掌机实测。"
  },
  "c7": {
    "title": "Notebookcheck：老Claw / 155H，GPU3491 / TS3829",
    "url": "https://www.notebookcheck-cn.com/Claw-Ultra.849315.0.html",
    "kind": "firsthand-review",
    "watts": 43,
    "shortWatts": 45,
    "gpu": 3491,
    "total": 3829,
    "cpu": null,
    "gpuPoints": [
      [
        20,
        2581
      ],
      [
        35,
        3517
      ],
      [
        43,
        3491
      ]
    ],
    "notes": "测试使用AI模式，45W短/约43W持续；另测20W GPU2581、35W GPU3517，保留轻微回落。"
  },
  "gos": {
    "title": "Notebookcheck：Legion Go S / Z2 Go，GPU2058 / TS2229",
    "url": "https://www.notebookcheck-cn.com/Legion-Go-S-Steam-Deck-Windows.962071.0.html",
    "kind": "firsthand-review",
    "watts": 30,
    "shortWatts": 40,
    "gpu": 2058,
    "total": 2229,
    "cpu": null,
    "notes": "明确使用Z2 Go版；不代表Z1 Extreme等其他Go S配置。"
  },
  "m395Data": {
    "title": "Notebookcheck原始数据：395 80W / TS10340",
    "url": "https://www.notebookcheck-cn.com/AMD-Radeon-RX-8060S.964064.0.html",
    "kind": "firsthand-measurement-index"
  },
  "m395Cpu": {
    "title": "Notebookcheck原始数据：MS-S1 Max Time Spy CPU10837",
    "url": "https://www.notebookcheck-cn.com/AMD-Strix-Halo-Point-Zen-5-5c-Ryzen-AI-300-Ryzen-AI-Max-395.945523.0.html",
    "kind": "firsthand-measurement-index"
  },
  "windowsPPM": {
    "title": "Microsoft：默认Balanced与OEM处理器调校",
    "url": "https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/configure-processor-power-management-options",
    "kind": "official-method"
  },
  "windowsEpp": {
    "title": "Microsoft：EPP范围与性能/能耗偏好",
    "url": "https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/options-for-perf-state-engine-perfenergypreference",
    "kind": "official-method"
  },
  "windowsMin": {
    "title": "Microsoft：CPU最小性能状态定义",
    "url": "https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/options-for-perf-state-engine-minperformance",
    "kind": "official-method"
  }
};
SOURCES.g3eManual={title:'Notebookcheck原始评测：G3E手动35/45W，TS GPU6755',url:'https://www.notebookcheck.net/Intel-s-Arc-G3-Extreme-changes-the-handheld-game-MSI-Claw-8-EX-AI-Review.1325574.0.html',kind:'firsthand-review',watts:35,shortWatts:45,gpu:6755,actualPackageWatts:null,checked:'2026-10-16'};
const RAW_IMAGE1=root.YmccGpuCapabilityLibrary.SOURCE_SERIES.ts;
// Image FPS ratios constrain low-power shape, not measured TS scores.
const RELEASE_CURVES=root.YmccGpuCapabilityLibrary.SOURCE_SERIES.release;
SOURCES.g3eChip={title:'CHIP原测：8/17/35W，2077原生1080P中画质19/40/58FPS',url:'https://www.chip.de/test-kaufberatung/gaming-zubehoer/msi-claw-8-ex-ai-cg3em-im-test_498dc46c-c4b6-4f1c-90cb-713541726b95.html',kind:'firsthand-native-game-test',checked:'2026-10-16'};
SOURCES.g3eSteady={title:'超能网原作者：缩短PL2，15/25/35W；35W TS GPU6740',url:'https://post.smzdm.com/p/a032vezw/',kind:'firsthand-test-author-post',gpu:6740,watts:35,ratio15:.60,ratio25:.85,ratiosAreApproximate:true,checked:'2026-10-16'};
SOURCES.g3e20={title:'Hardwareluxx原测：20W Time Spy Graphics4718',url:'https://www.hardwareluxx.de/index.php/artikel/consumer-electronics/konsolen/69480-gut-ausbalancierter-handheld-soc-msi-claw-8-ex-ai-mit-intel-arc-g3-extreme-im-test.html?start=1',kind:'firsthand-test',watts:20,gpu:4718,checked:'2026-10-16'};
SOURCES.userDeck16={title:'用户实测：Steam Deck最大释放16W，TS GPU1400',kind:'user-measured-graphics-anchor',watts:16,gpu:1400,cpu:null,total:null,checked:'2026-10-16'};
const TRADEOFF=[
  {
    "ghz": 0.8,
    "cpuRatio": 0.27,
    "gpuRatio": 1.19,
    "kind": "modeled-low-frequency-extension"
  },
  {
    "ghz": 1.2,
    "cpuRatio": 0.5,
    "gpuRatio": 1.185,
    "kind": "modeled-low-frequency-extension"
  },
  {
    "ghz": 2,
    "cpu": 5129,
    "gpu": 2900,
    "total": 3102,
    "kind": "user-image",
    "cpuRatio": 0.8093735205933407,
    "gpuRatio": 1.1632571199358204
  },
  {
    "ghz": 4,
    "cpu": 5610,
    "gpu": 2737,
    "total": 2964,
    "kind": "user-image",
    "cpuRatio": 0.8852769449266215,
    "gpuRatio": 1.097874047332531
  },
  {
    "ghz": 4.8,
    "cpu": 6337,
    "gpu": 2493,
    "total": 2742,
    "kind": "user-image",
    "unlimited": true,
    "cpuRatio": 1,
    "gpuRatio": 1
  }
];
function totalScore(gpu,cpu){return gpu>0&&cpu>0?1/(.85/gpu+.15/cpu):0;}
function inferCpu(total,gpu){const denom=1/total-.85/gpu;return denom>0?.15/denom:null;}
function interpolate(points,value){if(value<=points[0][0])return points[0][1];if(value>=points.at(-1)[0])return points.at(-1)[1];for(let i=1;i<points.length;i++)if(value<=points[i][0]){const [a,x]=points[i-1],[b,y]=points[i];return x+(y-x)*(value-a)/(b-a);}return points.at(-1)[1];}
const PROFILES={
  "reference": {
    "id": "reference",
    "label": "你的图 · 17W参考样机",
    "short": "用户参考",
    "min": 5,
    "max": 30,
    "defaultWatts": 17,
    "maxGHz": 4.8,
    "idle": 3.2,
    "anchor": {
      "watts": 17,
      "gpu": 2493,
      "cpu": 6337,
      "total": 2742,
      "kind": "user-image-setting-not-metered-power",
      "source": "user2",
      "cpuOrigin": "published"
    },
    "sourceKeys": [
      "user1",
      "user2",
      "ulFormula"
    ],
    "gpuShape": "rog-normalized",
    "confidence": "用户分项校准；其他瓦数与功耗估算",
    "conditions": "17W [24W] 设置；功耗未实测"
  },
  "deck": {
    "id": "deck",
    "label": "Steam Deck · GPU 1544",
    "short": "Steam Deck",
    "min": 5,
    "max": 15,
    "defaultWatts": 15,
    "maxGHz": 3.5,
    "idle": 2.3,
    "anchor": {
      "watts": 15,
      "gpu": 1544,
      "cpu": 3547,
      "total": 1686,
      "kind": "single-published-run-power-unknown",
      "source": "deckRun",
      "runWatts": null,
      "wattsSource": "deckSpec",
      "cpuOrigin": "published"
    },
    "sourceKeys": [
      "deckSpec",
      "deckRun",
      "user2",
      "ulFormula"
    ],
    "gpuShape": "single-anchor",
    "confidence": "同一UL实跑分项；15W仅作设备上限名义锚点，运行实耗未知",
    "conditions": "同一UL实跑1544/3547/1686；功耗未知，15W为名义点"
  },
  "m780": {
    "id": "m780",
    "label": "Radeon 780M · 7840U",
    "short": "780M",
    "min": 5,
    "max": 30,
    "defaultWatts": 17,
    "maxGHz": 5.1,
    "idle": 3.2,
    "anchor": {
      "watts": 30,
      "gpu": 2866,
      "cpu": 8528.33717669056,
      "total": 3183,
      "kind": "gpu-total-published-cpu-derived",
      "source": "m780",
      "cpuOrigin": "derived"
    },
    "sourceKeys": [
      "m780",
      "user1",
      "user2",
      "ulFormula"
    ],
    "gpuShape": "rog-normalized",
    "confidence": "30W图形/总分来自评测；CPU反算；其余拟合",
    "conditions": "OneXPlayer2 Pro 30W；CPU近似反算"
  },
  "c8": {
    "id": "c8",
    "label": "C8 · 258V / Arc 140V",
    "short": "C8 / 258V",
    "min": 8,
    "max": 30,
    "defaultWatts": 17,
    "maxGHz": 4.8,
    "idle": 2.8,
    "anchor": {
      "watts": 30,
      "gpu": 4118,
      "cpu": 8148.071174377211,
      "total": 4448,
      "kind": "gpu-total-published-cpu-derived",
      "source": "c8",
      "cpuOrigin": "derived"
    },
    "sourceKeys": [
      "c8Spec",
      "c8",
      "user2",
      "ulFormula"
    ],
    "gpuShape": "single-anchor",
    "confidence": "30W图形/总分来自评测；其他瓦数/共享功耗估算",
    "conditions": "C8/258V 长30/短37W；CPU近似反算"
  },
  "g3e": {
    "id": "g3e",
    "label": "G3E · Arc B390",
    "short": "G3E",
    "min": 8,
    "max": 35,
    "defaultWatts": 25,
    "maxGHz": 4.7,
    "idle": 3.4,
    "anchor": {
      "watts": 25,
      "gpu": 6178,
      "cpu": 11115.026365860294,
      "total": 6619,
      "kind": "gpu-total-published-cpu-derived",
      "source": "g3e",
      "cpuOrigin": "derived"
    },
    "extraGpu": [
      {
        "watts": 35,
        "gpu": 6755,
        "source": "g3eManual",
        "kind": "published-gpu-score-at-configured-mode",
        "powerKind": "configured-PL1-PL2-not-measured-package-power"
      }
    ],
    "sourceKeys": [
      "g3e",
      "user2",
      "ulFormula"
    ],
    "gpuShape": "two-anchors",
    "confidence": "25/37W与手动35/45W图形锚点均已核；CPU反算，其他瓦数为模型",
    "conditions": "长25/短37W GPU6178/TS6619；手动长35/短45W GPU6755；实耗未知"
  },
  "m395": {
    "id": "m395",
    "label": "395 · Radeon 8060S · 80W",
    "short": "AI Max+395",
    "min": 15,
    "max": 80,
    "defaultWatts": 80,
    "maxGHz": 5.1,
    "idle": 5.5,
    "anchor": {
      "watts": 80,
      "gpu": 10258,
      "cpu": 10837,
      "total": 10340,
      "kind": "published-subscore-reference",
      "source": "m395",
      "cpuOrigin": "published"
    },
    "sourceKeys": [
      "m395",
      "m395Data",
      "m395Cpu",
      "user2",
      "ulFormula"
    ],
    "gpuShape": "single-anchor",
    "gpuExponent": 0.7,
    "confidence": "同机80W Quiet公开GPU/CPU/总分参考；其他瓦数曲线估算",
    "conditions": "80W Quiet（短110W）；GPU10258/CPU10837/TS10340"
  },
  "z2e": {
    "id": "z2e",
    "label": "Z2 Extreme · Claw A8",
    "short": "Z2E",
    "min": 5,
    "max": 30,
    "defaultWatts": 20,
    "maxGHz": 5,
    "idle": 3.2,
    "anchor": {
      "watts": 20,
      "gpu": 3331,
      "cpu": 7673.949367088597,
      "total": 3640,
      "kind": "gpu-total-published-cpu-derived",
      "source": "z2e",
      "cpuOrigin": "derived"
    },
    "sourceKeys": [
      "z2e",
      "user1",
      "user2",
      "ulFormula"
    ],
    "gpuShape": "a8-stock-normalized",
    "confidence": "AI档评测GPU/总分；CPU反算；GPU曲线借用图中A8原版并归一",
    "conditions": "AI档名义20W/短50W；GPU3331/TS3640，CPU反算"
  },
  "m6800": {
    "id": "m6800",
    "label": "6800U · Radeon 680M",
    "short": "6800U",
    "min": 5,
    "max": 28,
    "defaultWatts": 17,
    "maxGHz": 4.7,
    "idle": 3,
    "anchor": {
      "watts": 28,
      "gpu": 2419,
      "cpu": 6600.190881252138,
      "total": 2673,
      "kind": "gpu-total-published-cpu-derived",
      "source": "m6800",
      "cpuOrigin": "derived"
    },
    "sourceKeys": [
      "m6800",
      "user2",
      "ulFormula"
    ],
    "gpuShape": "single-anchor",
    "confidence": "OneXPlayer2评测GPU/总分；CPU反算，其他瓦数估算",
    "conditions": "OneXPlayer2 28W参考；GPU2419/TS2673，CPU反算"
  },
  "m5800": {
    "id": "m5800",
    "label": "5800U · Radeon Vega 8",
    "short": "5800U",
    "min": 5,
    "max": 25,
    "defaultWatts": 15,
    "maxGHz": 4.4,
    "idle": 2.5,
    "anchor": {
      "watts": 25,
      "gpu": 1128,
      "cpu": 5796.624833110812,
      "total": 1283,
      "kind": "gpu-total-published-cpu-derived",
      "source": "m5800",
      "cpuOrigin": "derived"
    },
    "sourceKeys": [
      "m5800",
      "user2",
      "ulFormula"
    ],
    "gpuShape": "single-anchor",
    "gpuExponent": 0.6,
    "confidence": "HP Aero13笔记本参考；CPU反算，不当作掌机实测",
    "conditions": "HP Aero13笔记本25/30W参考；GPU1128/TS1283，CPU反算"
  },
  "i1195": {
    "id": "i1195",
    "label": "i7-1195G7 · Iris Xe 96EU",
    "short": "i7-1195G7",
    "min": 7,
    "max": 36,
    "defaultWatts": 20,
    "maxGHz": 5,
    "idle": 2.6,
    "anchor": {
      "watts": 36,
      "gpu": 1819,
      "cpu": 4843.962406015035,
      "total": 2007,
      "kind": "gpu-total-published-cpu-derived",
      "source": "i1195",
      "cpuOrigin": "derived"
    },
    "sourceKeys": [
      "i1195",
      "user2",
      "ulFormula"
    ],
    "gpuShape": "single-anchor",
    "confidence": "Prestige14笔记本36/64W参考；CPU反算；其他瓦数估算",
    "conditions": "Prestige14笔记本36/64W；GPU1819/TS2007，CPU反算"
  },
  "c7": {
    "id": "c7",
    "label": "老 C7 · Core Ultra 7 155H",
    "short": "Claw7 / 155H",
    "min": 10,
    "max": 43,
    "defaultWatts": 20,
    "maxGHz": 4.8,
    "idle": 3.5,
    "anchor": {
      "watts": 43,
      "gpu": 3491,
      "cpu": 8483.418024116761,
      "total": 3829,
      "kind": "gpu-total-published-cpu-derived",
      "source": "c7",
      "cpuOrigin": "derived"
    },
    "sourceKeys": [
      "c7",
      "user2",
      "ulFormula"
    ],
    "gpuShape": "published-points",
    "gpuPoints": [
      [
        20,
        2581
      ],
      [
        35,
        3517
      ],
      [
        43,
        3491
      ]
    ],
    "confidence": "AI档43/45W参考；保留20W/35W GPU实测点，CPU反算",
    "conditions": "155H版43/45W；GPU3491/TS3829；20/35W GPU2581/3517"
  },
  "gos": {
    "id": "gos",
    "label": "联想 Go S · Ryzen Z2 Go",
    "short": "Legion Go S",
    "min": 5,
    "max": 30,
    "defaultWatts": 20,
    "maxGHz": 4.3,
    "idle": 3,
    "anchor": {
      "watts": 30,
      "gpu": 2058,
      "cpu": 4212.380165289254,
      "total": 2229,
      "kind": "gpu-total-published-cpu-derived",
      "source": "gos",
      "cpuOrigin": "derived"
    },
    "sourceKeys": [
      "gos",
      "user2",
      "ulFormula"
    ],
    "gpuShape": "single-anchor",
    "confidence": "Z2 Go版30/40W评测参考；CPU反算，其他瓦数估算",
    "conditions": "Z2 Go版30/40W；GPU2058/TS2229，CPU反算"
  }
};
PROFILES.g3e.sourceKeys.push('g3eManual');
// Sub-join curves are explicit scenario assumptions. They do not change published anchors.
// 258V low-load strength follows the user's observation, not an invented1W TS measurement.
const priorDeckAnchor={...PROFILES.deck.anchor};
Object.assign(PROFILES.deck,{max:16,defaultWatts:16,gpuShape:'deck-user-game-ratio',gpuCeiling:1400,
 anchor:{watts:16,gpu:1400,cpu:priorDeckAnchor.cpu,total:null,kind:'user-measured-graphics-with-separate-CPU-reference',source:'userDeck16',cpuOrigin:'separate-run-reference-not-same-test'},
 confidence:'GPU1400/16W为用户实测；低瓦形状借用游戏比例；CPU独立UL参考',conditions:'最大释放16W；旧UL1544分功耗未知，单独保留',otherRuns:[priorDeckAnchor],limitEvidence:{kind:'user-tested-max-release',watts:16}});
const priorG3eAnchor={...PROFILES.g3e.anchor};
Object.assign(PROFILES.g3e,{gpuShape:'g3e-steady',gpuPoints:RELEASE_CURVES.g3eSteady.map(r=>[r.watts,r.gpu]),
 anchor:{watts:35,gpu:6740,cpu:priorG3eAnchor.cpu,total:null,kind:'steady-graphics-anchor-with-separate-CPU-reference',source:'g3eSteady',cpuOrigin:'separate-mode-derived-reference'},
 otherRuns:[priorG3eAnchor,...(PROFILES.g3e.extraGpu??[]),{watts:37,gpu:6285,kind:'different-review-mode',source:'g3e20'}],extraGpu:undefined,confidence:'35W GPU6740实测；20W4718另一原测；15/25W为原作者比例拟合',conditions:'持续35W；PL2与其他模式单独保留；1–15W仍为拟合',limitEvidence:{kind:'reviewed-sustained-mode-limit',watts:35,shortLimitWatts:45,source:'g3eManual'}});
PROFILES.c8.gpuShape='intel258v-user-game-ratio';PROFILES.m395.gpuShape='m395-user-ts-shape';
// User-observed upper graphics anchor; preserve the raw watt curve and CPU score.
PROFILES.z2e.gpuCeiling=3682;PROFILES.z2e.gpuCeilingEvidence={kind:'user-observed-graphics-upper-anchor',gpu:3682,sourceWatts:35,scope:'simulation-only'};
const LOW_POWER_DATA=root.YmccLowPowerData;
const LOW_POWER=LOW_POWER_DATA.envelopes;
for(const profile of Object.values(PROFILES)){profile.curveJoinWatts=profile.min;profile.min=1;profile.lowPowerOrigin='scenario-assumption-not-measured';profile.anchor.powerKind='configured-or-nominal-limit-not-measured-package-power';profile.anchor.actualPackageWatts=null;}
function systemBaseWatts(id){if(!LOW_POWER[id])throw Error("未知机型基础开销");return LOW_POWER[id].lightIdle;}
function packageIdle(id){return systemBaseWatts(id);}
const GPU_CAPABILITIES=root.YmccGpuCapabilityLibrary.create(PROFILES);
// Keep historical paired results as provenance when the user's GPU series supplies a new anchor.
for(const [id,p]of Object.entries(PROFILES)){const gpu=GPU_CAPABILITIES.score(id,p.anchor.watts);p.gpuCapabilitySource=GPU_CAPABILITIES.entry(id).source;if(Math.abs(gpu-p.anchor.gpu)>1e-7){p.publishedAnchor={...p.anchor};p.anchor={...p.anchor,gpu,total:null,kind:'GPU-capability-series-with-separate-CPU-reference',source:'user-gpu-capability-library'};}}
function releaseShape(id){return GPU_CAPABILITIES.entry(id).points;}
function stockScores(id,watts){const p=PROFILES[id];if(!p)throw Error('未知机型');if(!Number.isFinite(watts)||watts<p.min||watts>p.max)throw Error('超出机型演示范围');const gpu=GPU_CAPABILITIES.score(id,watts);let cpu;
 if(watts<p.curveJoinWatts){const upper=stockScores(id,p.curveJoinWatts),k=LOW_POWER[id],ratio=Math.max(0,(watts-k.scoreFloor)/(p.curveJoinWatts-k.scoreFloor));cpu=upper.cpu*Math.pow(ratio,k.cpuExponent);}else cpu=p.anchor.cpu*Math.pow(Math.max(.2,Math.min(watts,p.calibratedMaxWatts??p.max)-2)/(p.anchor.watts-2),.5);
 return {gpu,cpu,total:totalScore(gpu,cpu)};}
// The image supplies same-TDP CPU/GPU exchange, not a universal vendor uplift.
// High-power attenuation remains an explicit simulation assumption. Its tail
// approaches zero continuously instead of freezing Intel GPU scores at 30W.
const HEADROOM_TAIL={intel:{startWatts:25,scale:.035,exponent:2},amd:{startWatts:80,scale:.06,exponent:2},deck:{startWatts:30,scale:.15,exponent:2}};
function tailDamping(family,watts){const t=HEADROOM_TAIL[family];return t.scale*Math.pow(t.startWatts/Math.max(t.startWatts,watts),t.exponent);}
const HEADROOM={
 intel:[[5,1],[10,.72],[15,.4],[20,.16],[25,.035],...[30,45,80,150].map(w=>[w,tailDamping('intel',w)])],
 amd:[[5,1.65],[10,1.4],[17,1],[20,.88],[25,.75],[30,.65],[45,.3],[80,.06],[150,tailDamping('amd',150)]],
 deck:[[5,.7],[10,.55],[15,.4],[30,.15]]
};
function modelFamily(id){return ['c8','g3e','i1195','c7'].includes(id)?'intel':id==='deck'?'deck':'amd';}
function gainDamping(id,watts){const family=modelFamily(id),tail=HEADROOM_TAIL[family];return watts>=tail.startWatts?tailDamping(family,watts):interpolate(HEADROOM[family],watts);}
// Both ratios come from the same frequency sweep; no separate GPU UI score.
// Keep the 0.8/1.2GHz extension explicitly modeled and bounded by its old curve.
const CPU_GPU_EXCHANGE=TRADEOFF.map(r=>[r.cpuRatio,r.gpuRatio]).sort((a,b)=>a[0]-b[0]);
function tradeoffAt(referenceGHz){const cpuRatio=interpolate(TRADEOFF.map(r=>[r.ghz,r.cpuRatio]),referenceGHz)*Math.min(1,referenceGHz/TRADEOFF[0].ghz),gpuRatio=interpolate(CPU_GPU_EXCHANGE,cpuRatio);return {cpuRatio,gpuRatio,origin:'user-image-frequency-sweep-with-modeled-sub2GHz-extension'};}
function scores(id,watts,control={}){if(control.frequencyMHz!==undefined&&(!Number.isFinite(control.frequencyMHz)||control.frequencyMHz<0))throw Error('CPU频率无效');if(control.aggressiveness!==undefined&&(!Number.isFinite(control.aggressiveness)||control.aggressiveness<0||control.aggressiveness>100))throw Error('CPU积极性无效');const p=PROFILES[id],base=stockScores(id,watts),unlimited=(control.frequencyMHz??0)===0||control.unlimited===true,effective=unlimited?p.maxGHz:Math.min(p.maxGHz,control.frequencyMHz/1000),frequency=Math.min(control.turbo===false?p.maxGHz*.62:p.maxGHz,effective),referenceGHz=frequency/p.maxGHz*4.8,aggr=control.aggressiveness??100,eppFactor=.85+.15*aggr/100,tradeoff=tradeoffAt(referenceGHz),cpuRatio=tradeoff.cpuRatio*eppFactor,gain=tradeoff.gpuRatio,damping=gainDamping(id,watts),gpu=Math.min(p.gpuCeiling??Infinity,base.gpu),cpu=base.cpu*cpuRatio,total=totalScore(gpu,cpu);
 const exact=id==='reference'&&watts===17&&aggr===100&&control.turbo!==false?TRADEOFF.find(r=>r.kind==='user-image'&&Math.abs(r.ghz-referenceGHz)<1e-8):null;
 const atAnchor=watts===p.anchor.watts&&unlimited&&aggr===100&&control.turbo!==false,reportedTotal=exact?.total??(atAnchor?p.anchor.total??null:null);
 return {gpu,cpu,total,gpuAllocation:{baseScore:base.gpu,referenceCpuRatio:tradeoff.cpuRatio,unattenuatedGpuGainPct:(gain-1)*100,attenuation:damping,appliedGainPct:0,optimizationPath:'resolved-once-in-CPU-GPU-supply',powerScope:'same-configured-tdp',origin:tradeoff.origin,measured:false},reportedTotal,totalRoundingDelta:reportedTotal===null?null:Math.round(total)-reportedTotal,isReferencePoint:atAnchor,gpuReferencePoint:watts===p.anchor.watts&&unlimited&&control.turbo!==false,cpuOrigin:atAnchor?p.anchor.cpuOrigin:'modeled',frequency,base,scoreOrigin:exact?'user-image-exact-subscore':atAnchor?'published-reference-point':'calibrated-model-estimate',actualPowerMeasured:false};}
// User-visible machine names are processor names. The reference remains calibration-only.
const DEFAULT_PROFILE='z2e';
const PROCESSOR_NAMES={deck:['AMD Custom APU（Steam Deck）','AMD Custom APU（Steam Deck）'],m5800:['AMD Ryzen 7 5800U','Ryzen 7 5800U'],i1195:['Intel Core i7-1195G7','Core i7-1195G7'],gos:['AMD Ryzen Z2 Go','Ryzen Z2 Go'],m6800:['AMD Ryzen 7 6800U','Ryzen 7 6800U'],m780:['AMD Ryzen 7 7840U','Ryzen 7 7840U'],z2e:['AMD Ryzen Z2 Extreme','Ryzen Z2 Extreme'],c7:['Intel Core Ultra 7 155H','Core Ultra 7 155H'],c8:['Intel Core Ultra 7 258V','Core Ultra 7 258V'],g3e:['Intel Arc G3 Extreme','Arc G3 Extreme'],m395:['AMD Ryzen AI Max+ 395','Ryzen AI Max+ 395']};
for(const [id,[label,short]]of Object.entries(PROCESSOR_NAMES))Object.assign(PROFILES[id],{label,short});
PROFILES.reference.selectable=false;
function isSelectableProfile(id){return Object.hasOwn(PROFILES,id)&&PROFILES[id].selectable!==false;}
function profileOptions(){return Object.values(PROFILES).filter(p=>isSelectableProfile(p.id)).sort((a,b)=>GPU_CAPABILITIES.score(a.id,a.max)-GPU_CAPABILITIES.score(b.id,b.max));}
const api={GPU_CAPABILITIES,RELEASE_CURVES,releaseShape,HEADROOM_TAIL,CPU_GPU_EXCHANGE,tradeoffAt,DEFAULT_PROFILE,isSelectableProfile,profileOptions,LOW_POWER_DATA,LOW_POWER,systemBaseWatts,packageIdle,SOURCES,RAW_IMAGE1,TRADEOFF,PROFILES,HEADROOM,modelFamily,gainDamping,totalScore,inferCpu,interpolate,stockScores,scores};root.YmccBenchmarkData=api;
})(tsScope);
(function(root){
'use strict';
const MODES={all:'系统混用',bigFirst:'大核为主',bigOnly:'仅大核',littleFirst:'小核为主',littleOnly:'仅小核'};
const sources={z2e:{title:'AMD官方Z2E：3Zen5+5Zen5c，8核16线程，5.0/3.3GHz',url:'https://www.amd.com/en/products/processors/handhelds/ryzen-z-series/z2-series/z2-extreme.html'},c8:{title:'Intel官方258V：4P+4LP-E，8线程，不支持HT',url:'https://www.intel.com/content/www/us/en/products/sku/240957/intel-core-ultra-7-processor-258v-12m-cache-up-to-4-80-ghz/specifications.html'},g3e:{title:'Intel官方G3E：2P+8E+4LP-E，14线程',url:'https://www.intel.com/content/www/us/en/products/sku/245625/intel-arc-g3-extreme-processor-12m-cache-up-to-4-70-ghz/specifications.html'},c7:{title:'Intel官方155H：6P+8E+2LP-E，22线程',url:'https://www.intel.com/content/www/us/en/products/sku/236847/intel-core-ultra-7-processor-155h-24m-cache-up-to-4-80-ghz/specifications.html'},latency:{title:'ChipsandCheese HX370原始微测：跨簇一致性延迟，非Z2E关核FPS对照',url:'https://old.chipsandcheese.com/2024/08/10/amds-strix-point-zen-5-hits-mobile/'},m780:{title:'AMD官方7840U：8个同构Zen4、16线程',url:'https://www.amd.com/en/products/processors/laptop/ryzen/7000-series/amd-ryzen-7-7840u.html'}};
Object.assign(sources,{m395:{title:'AMD官方395：16xZen5、16核32线程，非大小核',url:'https://www.amd.com/en/products/processors/laptop/ryzen/ai-300-series/amd-ryzen-ai-max-plus-395.html'},deck:{title:'Valve官方SteamDeck：4核8线程Zen2',url:'https://www.steamdeck.com/en/tech'},m6800:{title:'Lenovo官方ThinkBook13s规格：6800U8核16线程',url:'https://psref.lenovo.com/syspool/Sys/PDF/ThinkBook/ThinkBook_13s_G4_ARB/ThinkBook_13s_G4_ARB_Spec.pdf'},m5800:{title:'Lenovo官方Yoga Slim7 Carbon规格：5800U8核16线程',url:'https://psref.lenovo.com/syspool/Sys/PDF/Yoga/Yoga_Slim_7_Carbon_14ACN6/Yoga_Slim_7_Carbon_14ACN6_Spec.pdf'},i1195:{title:'Intel官方1195G7：4核8线程',url:'https://www.intel.com/content/www/us/en/products/sku/217187/intel-core-i71195g7-processor-12m-cache-up-to-5-00-ghz/specifications.html'},gos:{title:'AMD官方Z2Go：4核8线程Zen3+',url:'https://www.amd.com/zh-tw/newsroom/press-releases/2025-1-6-amd-announces-new-graphics-and-gaming-products-for.html'}});
// Native cross-cluster scheduling weights are conservative scenario assumptions.
// Counts,SMT and frequency ceilings are hardware specifications. Density/power
// weights, sync participation and low-power redistribution below are assumptions.
const group=(kind,count,ghz,smt,cluster=kind)=>({kind,count,ghz,smt,cluster});
const TOPOLOGY={reference:[],deck:[group('P',4,3.5,true)],m780:[group('P',8,5.1,true)],c8:[group('P',4,4.8,false),group('LP-E',4,3.7,false)],g3e:[group('P',2,4.7,false),group('E',8,3.4,false),group('LP-E',4,3.1,false)],m395:[group('P',8,5.1,true,'CCD0'),group('P',8,5.1,true,'CCD1')],z2e:[group('P',3,5,true),group('E',5,3.3,true)],m6800:[group('P',8,4.7,true)],m5800:[group('P',8,4.4,true)],i1195:[group('P',4,5,true)],c7:[group('P',6,4.8,true),group('E',8,3.8,false),group('LP-E',2,2.5,false)],gos:[group('P',4,4.3,true)]};
const clamp=(n,a,b)=>Math.max(a,Math.min(b,n)),weight=g=>g.kind==='P'?1:g.kind==='E'?.68:.4,draw=g=>g.kind==='P'?2:g.kind==='E'?1:.65;
const GROUP_LABELS={deck:['Zen 2'],m780:['Zen 4'],c8:['P','LP-E'],g3e:['P','E','LP-E'],m395:['Zen 5 · CCD0','Zen 5 · CCD1'],z2e:['Zen 5','Zen 5c'],m6800:['Zen 3+'],m5800:['Zen 3'],i1195:['Willow Cove'],c7:['P','E','LP-E'],gos:['Zen 3+']};
function topology(id){const groups=TOPOLOGY[id]||[];return {groups:groups.map((g,i)=>({...g,label:GROUP_LABELS[id]?.[i]||g.kind})),known:groups.length>0,hybrid:groups.some(g=>g.kind!=='P'),smt:groups.some(g=>g.smt),cores:groups.reduce((n,g)=>n+g.count,0),threads:groups.reduce((n,g)=>n+g.count*(g.smt?2:1),0)};}
function evaluate(id,mode='all',smt=true,watts=30,share=.35,latency={within:30,cross:120}){
 const t=topology(id);if(!MODES[mode])throw Error('未知核心方案');if(!t.known)return {...t,mode:'all',requestedMode:mode,active:[],activeCores:0,activeThreads:0,cpuMulti:1,gameFactor:1,powerRatio:1,gpuFactor:1,maxGHz:Infinity,latencyCost:0,crossProbability:0,latencyOrigin:'unknown-topology-no-invented-effect'};
 const effective=!t.hybrid?'all':mode,active=t.groups.filter(g=>effective==='bigOnly'?g.kind==='P':effective==='littleOnly'?g.kind!=='P':true),allCapacity=t.groups.reduce((n,g)=>n+g.count*weight(g),0),capacity=active.reduce((n,g)=>n+g.count*weight(g),0),allPower=t.groups.reduce((n,g)=>n+g.count*draw(g),0),activePower=active.reduce((n,g)=>n+g.count*draw(g),0),ratio=activePower/allPower;
 const cores=active.reduce((n,g)=>n+g.count,0),threads=active.reduce((n,g)=>n+g.count*(g.smt&&smt?2:1),0),hasDisabledSmt=!smt&&active.some(g=>g.smt),cpuMulti=capacity/allCapacity*(hasDisabledSmt?.8:1);
 const clusterCross=groups=>1-groups.reduce((n,g)=>n+Math.pow(g.count/groups.reduce((a,b)=>a+b.count,0),2),0),nativeCrossWeight=['c8','g3e'].includes(id)?.35:id==='c7'?.65:1,baseCross=clusterCross(t.groups)*nativeCrossWeight,bias=effective==='all'?1:t.hybrid&&(effective==='bigFirst'||effective==='littleFirst')?.55:1,crossProbability=clusterCross(active)*bias*nativeCrossWeight;
 const syncShare=.05+.25*share,ratioNs=Math.max(1,latency.cross/latency.within),baseLatency=1+syncShare*baseCross*(ratioNs-1),activeLatency=1+syncShare*crossProbability*(ratioNs-1),latencyGain=baseLatency/activeLatency,latencyCost=1-1/activeLatency;
 const lowHeadroom=clamp((30-watts)/15,0,1),boost=1+lowHeadroom*(Math.min(1.5,1/Math.sqrt(ratio))-1),maxAll=Math.max(...t.groups.map(g=>g.ghz)),maxGHz=Math.max(...active.map(g=>g.ghz)),serial=effective==='littleOnly'?maxGHz/maxAll:effective==='littleFirst'?(['c8','g3e'].includes(id)?.97:.85):1,parallel=capacity/allCapacity,gameFactor=((1-share)*parallel+share*serial)*latencyGain*boost*(hasDisabledSmt?1-.2*(1-share):1),powerRatio=ratio*(hasDisabledSmt?.94:1),gpuFactor=1+lowHeadroom*(Math.sqrt(1+(1-powerRatio)*.4/.6)-1);
 return {...t,mode:effective,requestedMode:mode,active,activeCores:cores,activeThreads:threads,cpuMulti,gameFactor,powerRatio,gpuFactor,maxGHz,latencyCost,crossProbability,latencyGain,lowHeadroom,latencyOrigin:'editable-assumption-not-current-Z2E-or-Intel-measured-percentage',nativeCrossWeight};
}
function description(v){return v.known?v.active.map(g=>(g.label||g.kind)+' '+g.count).join(' + ')+' · '+v.activeCores+'核/'+v.activeThreads+'线程':'型号未确认 · 不推算核心数';}
function policyLabel(id,mode){const t=topology(id);return !t.known?'核心未识别':!t.hybrid?'全核心':MODES[mode];}
const api={policyLabel,MODES,TOPOLOGY,sources,topology,evaluate,description};root.YmccCoreModel=api;
})(tsScope);

export const calibration=tsScope.YmccBenchmarkData;
export const coreModel=tsScope.YmccCoreModel;
