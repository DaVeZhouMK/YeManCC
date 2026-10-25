// YMCC_SHARED_SPATIAL_BEGIN
var YmccSpatial = (() => {
  var __defProp = Object.defineProperty;
  var __getOwnPropDesc = Object.getOwnPropertyDescriptor;
  var __getOwnPropNames = Object.getOwnPropertyNames;
  var __hasOwnProp = Object.prototype.hasOwnProperty;
  var __export = (target, all) => {
    for (var name in all)
      __defProp(target, name, { get: all[name], enumerable: true });
  };
  var __copyProps = (to, from, except, desc) => {
    if (from && typeof from === "object" || typeof from === "function") {
      for (let key of __getOwnPropNames(from))
        if (!__hasOwnProp.call(to, key) && key !== except)
          __defProp(to, key, { get: () => from[key], enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable });
    }
    return to;
  };
  var __toCommonJS = (mod) => __copyProps(__defProp({}, "__esModule", { value: true }), mod);

  // src/gamepad/spatial.ts
  var spatial_exports = {};
  __export(spatial_exports, {
    getSpatialPosition: () => getSpatialPosition,
    nearestSpatialTarget: () => nearestSpatialTarget,
    spatialNavigationRect: () => spatialNavigationRect,
    spatialNavigationTarget: () => spatialNavigationTarget
  });
  function parseCoordinate(value) {
    if (value == null || value.trim() === "") return null;
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : null;
  }
  function getSpatialPosition(el) {
    if (!el) return null;
    const row = parseCoordinate(el.dataset.gpRow);
    const col = parseCoordinate(el.dataset.gpCol);
    return row == null || col == null ? null : { row, col };
  }
  function spatialNavigationRect(el) {
    const r = el.getBoundingClientRect(), scope = el.closest("[data-gp-scope]");
    let x = 0, y = 0;
    for (let parent = el.parentElement; parent && parent !== scope; parent = parent.parentElement) {
      if (!parent.scrollTop && !parent.scrollLeft) continue;
      const style = getComputedStyle(parent), box = parent.getBoundingClientRect();
      if (/auto|scroll/.test(style.overflowY) && parent.offsetHeight > 0) y += parent.scrollTop * box.height / parent.offsetHeight;
      if (/auto|scroll/.test(style.overflowX) && parent.offsetWidth > 0) x += parent.scrollLeft * box.width / parent.offsetWidth;
    }
    return { left: r.left + x, right: r.right + x, top: r.top + y, bottom: r.bottom + y, width: r.width, height: r.height };
  }
  function nearestSpatialTarget(elements, current, direction) {
    if (direction.dx === 0 === (direction.dy === 0)) return null;
    const horizontal = direction.dx !== 0, sign = horizontal ? direction.dx : direction.dy;
    const scope = current.closest("[data-gp-scope]");
    const b = spatialNavigationRect(current);
    const pane = current.closest("[data-gp-pane]");
    const paneId = pane?.dataset.gpPane;
    const bx = b.left + b.width / 2, by = b.top + b.height / 2;
    const candidates = elements.filter((el) => el !== current && el.closest("[data-gp-scope]") === scope && !el.closest('[hidden],[inert],[aria-hidden="true"]') && !el.matches(':disabled,[aria-disabled="true"],[data-gp-ignore],[tabindex="-1"]')).map((el) => {
      const r = spatialNavigationRect(el), style = getComputedStyle(el);
      const x = r.left + r.width / 2, y = r.top + r.height / 2;
      const forward = (horizontal ? x - bx : y - by) * sign;
      const overlap = horizontal ? Math.min(b.bottom, r.bottom) - Math.max(b.top, r.top) : Math.min(b.right, r.right) - Math.max(b.left, r.left);
      const gap = horizontal ? sign > 0 ? r.left - b.right : b.left - r.right : sign > 0 ? r.top - b.bottom : b.top - r.bottom;
      const closedDetails = el.closest("details:not([open])");
      const collapsed = !!closedDetails && !(el.tagName === "SUMMARY" && el.parentElement === closedDetails);
      return {
        el,
        visible: !collapsed && r.width > 0 && r.height > 0 && style.visibility !== "hidden" && style.display !== "none",
        forward,
        beam: overlap > 0.5 ? 0 : 1,
        separated: gap >= -0.5,
        gap: Math.max(0, gap),
        distance: Math.hypot(x - bx, y - by),
        across: Math.abs(horizontal ? y - by : x - bx),
        sameVisualRow: Math.abs(y - by) <= Math.max(8, Math.min(b.height, r.height) / 2 + 4)
      };
    }).filter((item) => item.visible && item.forward > 1 && (horizontal ? item.beam === 0 && item.sameVisualRow : item.separated));
    if (!horizontal && pane) {
      const local = candidates.filter((item) => {
        const other = item.el.closest("[data-gp-pane]");
        return paneId ? other?.dataset.gpPane === paneId : other === pane;
      });
      const admitted = local.length ? local : candidates.filter((item) => !item.el.closest("[data-gp-pane]"));
      candidates.splice(0, candidates.length, ...admitted);
    }
    if (horizontal) {
      candidates.sort((a, b2) => a.gap - b2.gap || a.across - b2.across || a.distance - b2.distance);
    } else {
      const nearestCenter = Math.min(...candidates.map((item) => item.forward));
      const rowTolerance = Math.max(6, Math.min(12, b.height / 2));
      const adjacentRow = candidates.filter((item) => item.forward <= nearestCenter + rowTolerance);
      adjacentRow.sort((a, b2) => a.across - b2.across || a.gap - b2.gap || a.distance - b2.distance);
      return adjacentRow[0]?.el ?? null;
    }
    return candidates[0]?.el ?? null;
  }
  function spatialNavigationTarget(elements, current, direction) {
    if (current.closest('[data-gp-spatial="nearest"]')) return nearestSpatialTarget(elements, current, direction);
    const currentPosition = getSpatialPosition(current);
    if (!currentPosition) return void 0;
    const candidates = elements.map((el) => ({ el, position: getSpatialPosition(el) })).filter((item) => !!item.position);
    if (direction.dx !== 0 && direction.dy === 0) {
      const sameRow = candidates.filter(({ el, position }) => el !== current && position.row === currentPosition.row).sort((a, b) => a.position.col - b.position.col);
      const horizontal = direction.dx > 0 ? sameRow.find((item) => item.position.col > currentPosition.col) : [...sameRow].reverse().find((item) => item.position.col < currentPosition.col);
      return horizontal?.el ?? null;
    }
    if (direction.dy !== 0 && direction.dx === 0) {
      const rows = [...new Set(candidates.map(({ position }) => position.row))].sort((a, b) => a - b);
      const adjacentRow = direction.dy > 0 ? rows.find((row) => row > currentPosition.row) : [...rows].reverse().find((row) => row < currentPosition.row);
      if (adjacentRow == null) return null;
      const rowCandidates = candidates.filter(({ position }) => position.row === adjacentRow).sort((a, b) => {
        const distance = Math.abs(a.position.col - currentPosition.col) - Math.abs(b.position.col - currentPosition.col);
        return distance || a.position.col - b.position.col;
      });
      return rowCandidates[0]?.el ?? null;
    }
    return null;
  }
  return __toCommonJS(spatial_exports);
})();
// YMCC_SHARED_SPATIAL_END

;
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
const api={checkedAt:'2026-10-16',sources,envelopes,notes};if(typeof module!=='undefined')module.exports=api;else root.YmccLowPowerData=api;
})(globalThis);

;
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
const RAW_IMAGE1={
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
  "fitUsesColumns": [0,1,2,3,4,5,6,7,8,9,10],
  "notes": "2026-10-16用户新图确认最后三列26/30/35W；原始波动保留，拟合取单调包络；跨CPU只迁移形状，不冒充实测。"
};
// Image FPS ratios constrain low-power shape, not measured TS scores.
const RELEASE_CURVES={
 imageWatts:[5,8,10,13,15,17,20,23,26,30,35,40,45,50,55,60,65,70,75,80,85],
 m395Optimized:[null,null,null,2803,2928,3240,3762,4386,5001,5817,6607,7491,8536,9478,10014,10326,10513,10691,10852,10970,11145],
 deckGame:[[5,19],[8,29],[10,34],[13,38],[15,40],[16,40]],
 intel258vGame:[[5,18],[8,29],[10,36],[13,46],[15,51],[17,56],[20,62],[23,68],[26,71],[30,77]],
 g3eGame1080:{points:[[8,19],[17,40],[35,58]],preset:'1080p-medium-native-no-upscaling',source:'g3eChip',powerKind:'configured-PL1-with-PL2-10-19-45W'},
 g3eSteady:[{watts:15,gpu:4044,kind:'ratio-fit',ratioTo35W:.60,source:'g3eSteady',note:'约60%为原作者性能比例；非精确GPU读数'},
 {watts:20,gpu:4718,kind:'published-graphics-score',source:'g3e20',note:'不同评测样机'},
 {watts:25,gpu:5729,kind:'ratio-fit',ratioTo35W:.85,source:'g3eSteady',note:'约85%为原作者性能比例；非精确GPU读数'},
 {watts:35,gpu:6740,kind:'published-graphics-score',source:'g3eSteady'}],
 scope:'image-shape-plus-device-specific-anchors;not-all-points-TS-measured'
};
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
const LOW_POWER_DATA=typeof module!=='undefined'?require('./low-power-data.js'):root.YmccLowPowerData;
const LOW_POWER=LOW_POWER_DATA.envelopes;
for(const profile of Object.values(PROFILES)){profile.curveJoinWatts=profile.min;profile.min=1;profile.lowPowerOrigin='scenario-assumption-not-measured';profile.anchor.powerKind='configured-or-nominal-limit-not-measured-package-power';profile.anchor.actualPackageWatts=null;}
function systemBaseWatts(id){if(!LOW_POWER[id])throw Error("未知机型基础开销");return LOW_POWER[id].lightIdle;}
function packageIdle(id){return systemBaseWatts(id);}
function monotonePoints(points){let last=0;return points.map(([w,v])=>[w,last=Math.max(last,v)]);}
function releaseShape(id){const p=PROFILES[id];
 if(p.gpuShape==='deck-user-game-ratio')return RELEASE_CURVES.deckGame;
 if(p.gpuShape==='intel258v-user-game-ratio')return RELEASE_CURVES.intel258vGame;
 if(p.gpuShape==='m395-user-ts-shape')return RELEASE_CURVES.imageWatts.map((w,i)=>[w,RELEASE_CURVES.m395Optimized[i]]).filter(r=>r[1]!==null);
 if(p.gpuShape==='rog-normalized'||p.gpuShape==='a8-stock-normalized'){const row=p.gpuShape==='rog-normalized'?RAW_IMAGE1.rogOptimized:RAW_IMAGE1.a8Stock;return monotonePoints(RAW_IMAGE1.headerWatts.map((w,i)=>[w,row[i]]));}
 return null;
}
function stockScores(id,watts){const p=PROFILES[id];if(!p)throw Error('未知机型');if(!Number.isFinite(watts)||watts<p.min||watts>p.max)throw Error('超出机型演示范围');if(watts<p.curveJoinWatts){const upper=stockScores(id,p.curveJoinWatts),k=LOW_POWER[id],ratio=Math.max(0,(watts-k.scoreFloor)/(p.curveJoinWatts-k.scoreFloor)),gpu=upper.gpu*Math.pow(ratio,k.gpuExponent),cpu=upper.cpu*Math.pow(ratio,k.cpuExponent);return {gpu,cpu,total:totalScore(gpu,cpu)};}const a=p.anchor;let gpu;
 if(releaseShape(id)){const points=releaseShape(id);gpu=a.gpu*interpolate(points,watts)/interpolate(points,a.watts);}
 else if(p.gpuPoints){const first=p.gpuPoints[0];gpu=watts<first[0]?first[1]*Math.pow((watts-2)/(first[0]-2),.6):interpolate(p.gpuPoints,watts);}
 else if(p.extraGpu&&watts>=a.watts)gpu=interpolate(monotonePoints([[a.watts,a.gpu],...p.extraGpu.map(x=>[x.watts,x.gpu])].sort((a,b)=>a[0]-b[0])),watts);
 else gpu=a.gpu*Math.pow(Math.max(.2,watts-2)/(a.watts-2),p.gpuExponent??(id==='deck'?.68:.48));
 const cpu=a.cpu*Math.pow(Math.max(.2,watts-2)/(a.watts-2),.5);return {gpu,cpu,total:totalScore(gpu,cpu)};}
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
function scores(id,watts,control={}){if(control.frequencyMHz!==undefined&&(!Number.isFinite(control.frequencyMHz)||control.frequencyMHz<0))throw Error('CPU频率无效');if(control.aggressiveness!==undefined&&(!Number.isFinite(control.aggressiveness)||control.aggressiveness<0||control.aggressiveness>100))throw Error('CPU积极性无效');const p=PROFILES[id],base=stockScores(id,watts),unlimited=(control.frequencyMHz??0)===0||control.unlimited===true,effective=unlimited?p.maxGHz:Math.min(p.maxGHz,control.frequencyMHz/1000),frequency=Math.min(control.turbo===false?p.maxGHz*.62:p.maxGHz,effective),referenceGHz=frequency/p.maxGHz*4.8,aggr=control.aggressiveness??100,eppFactor=.85+.15*aggr/100,tradeoff=tradeoffAt(referenceGHz),cpuRatio=tradeoff.cpuRatio*eppFactor,gain=tradeoff.gpuRatio,damping=gainDamping(id,watts),gpu=Math.min(p.gpuCeiling??Infinity,base.gpu*(1+(gain-1)*damping)),cpu=base.cpu*cpuRatio,total=totalScore(gpu,cpu);
 const exact=id==='reference'&&watts===17&&aggr===100&&control.turbo!==false?TRADEOFF.find(r=>r.kind==='user-image'&&Math.abs(r.ghz-referenceGHz)<1e-8):null;
 const atAnchor=watts===p.anchor.watts&&unlimited&&aggr===100&&control.turbo!==false,reportedTotal=exact?.total??(atAnchor?p.anchor.total??null:null);
 return {gpu,cpu,total,gpuAllocation:{baseScore:base.gpu,referenceCpuRatio:tradeoff.cpuRatio,unattenuatedGpuGainPct:(gain-1)*100,attenuation:damping,appliedGainPct:(gain-1)*damping*100,powerScope:'same-configured-tdp',origin:tradeoff.origin,measured:false},reportedTotal,totalRoundingDelta:reportedTotal===null?null:Math.round(total)-reportedTotal,isReferencePoint:atAnchor,gpuReferencePoint:watts===p.anchor.watts&&unlimited&&control.turbo!==false,cpuOrigin:atAnchor?p.anchor.cpuOrigin:'modeled',frequency,base,scoreOrigin:exact?'user-image-exact-subscore':atAnchor?'published-reference-point':'calibrated-model-estimate',actualPowerMeasured:false};}
// User-visible machine names are processor names. The reference remains calibration-only.
const DEFAULT_PROFILE='z2e';
const PROCESSOR_NAMES={deck:['AMD Custom APU（Steam Deck）','AMD Custom APU（Steam Deck）'],m5800:['AMD Ryzen 7 5800U','Ryzen 7 5800U'],i1195:['Intel Core i7-1195G7','Core i7-1195G7'],gos:['AMD Ryzen Z2 Go','Ryzen Z2 Go'],m6800:['AMD Ryzen 7 6800U','Ryzen 7 6800U'],m780:['AMD Ryzen 7 7840U','Ryzen 7 7840U'],z2e:['AMD Ryzen Z2 Extreme','Ryzen Z2 Extreme'],c7:['Intel Core Ultra 7 155H','Core Ultra 7 155H'],c8:['Intel Core Ultra 7 258V','Core Ultra 7 258V'],g3e:['Intel Arc G3 Extreme','Arc G3 Extreme'],m395:['AMD Ryzen AI Max+ 395','Ryzen AI Max+ 395']};
for(const [id,[label,short]]of Object.entries(PROCESSOR_NAMES))Object.assign(PROFILES[id],{label,short});
PROFILES.reference.selectable=false;
function isSelectableProfile(id){return Object.hasOwn(PROFILES,id)&&PROFILES[id].selectable!==false;}
function profileOptions(){return Object.values(PROFILES).filter(p=>isSelectableProfile(p.id)).sort((a,b)=>a.anchor.gpu-b.anchor.gpu);}
const api={RELEASE_CURVES,releaseShape,HEADROOM_TAIL,CPU_GPU_EXCHANGE,tradeoffAt,DEFAULT_PROFILE,isSelectableProfile,profileOptions,LOW_POWER_DATA,LOW_POWER,systemBaseWatts,packageIdle,SOURCES,RAW_IMAGE1,TRADEOFF,PROFILES,HEADROOM,modelFamily,gainDamping,totalScore,inferCpu,interpolate,stockScores,scores};if(typeof module!=='undefined')module.exports=api;else root.YmccBenchmarkData=api;
})(globalThis);

;
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
const api={policyLabel,MODES,TOPOLOGY,sources,topology,evaluate,description};if(typeof module!=='undefined')module.exports=api;else root.YmccCoreModel=api;
})(globalThis);

;
// Saving level owns only downshift timing/size. Recovery is shared by all levels.
(function(root){
'use strict';
const LEVELS=Object.freeze({
 none:{label:'无浮动',reductionPct:0,downIntervalMs:0,stepPct:0,idleStepPct:0,minStepWatts:0,stableSamples:2},
 small:{label:'小幅',reductionPct:20,downIntervalMs:8000,stepPct:3,idleStepPct:6,minStepWatts:1,stableSamples:2},
 medium:{label:'中幅',reductionPct:30,downIntervalMs:5000,stepPct:5,idleStepPct:10,minStepWatts:1,stableSamples:2},
 large:{label:'大幅',reductionPct:40,downIntervalMs:3000,stepPct:8,idleStepPct:16,minStepWatts:1,stableSamples:2},
 aggressive:{label:'激进',reductionPct:50,downIntervalMs:2000,stepPct:12,idleStepPct:24,minStepWatts:1,stableSamples:2}
});
const RECOVERY=Object.freeze({sampleMs:1000,fpsFloorPct:75,lowFloorPct:50,observeMs:1000,probeSamples:1,backoffMs:4000,cpuBusyPct:85,cpuBurstFpsPct:90,mode:'restore-allowed-TDP-and-CPU-ceilings'});
function forLevel(key){const value=LEVELS[key];if(!value)throw Error('未知浮动节能挡位');return {...value};}
const api=Object.freeze({LEVELS,RECOVERY,forLevel});if(typeof module!=='undefined')module.exports=api;else root.YmccFloatCadence=api;
})(globalThis);

;
(function(root){
'use strict';
// Existing YMCC gpuAggrCap thresholds are policy, not measured silicon constants.
// The extra modes below are telemetry-only simulation candidates.
const DEFAULT_MODE='off',MODES=['off','legacy','responsive','headroom'];
const THRESHOLDS=Object.freeze([{gpuPct:80,aggressivenessCap:70},{gpuPct:95,aggressivenessCap:60},{gpuPct:99,aggressivenessCap:50}]);
function legacyCap(gpu){return gpu>=99?50:gpu>=95?60:gpu>=80?70:100;}
function describe(mode=DEFAULT_MODE){if(!MODES.includes(mode))throw Error('未知GPU保护策略');return {mode,thresholds:THRESHOLDS.map(x=>({...x})),scope:'automatic-cpu-only',tdpRecoveryAllowed:true,frequencyHardLimitPreserved:true,source:'existing-YMCC-autofloat-gpuAggrCap',candidate:mode==='responsive'||mode==='headroom',cpuServiceTimesSource:'simulation-model-estimate-not-hardware-readback',releaseSamples:mode==='legacy'?1:2,releaseHysteresisPct:mode==='legacy'?0:3};}
function create({mode=DEFAULT_MODE,baseAggressiveness,enabled=true}={}){
 describe(mode);if(!Number.isFinite(baseAggressiveness)||baseAggressiveness<0||baseAggressiveness>100)throw Error('GPU保护CPU积极性无效');
 let band=0,releaseCount=0,state={mode,enabled,gpuPct:null,gpuBand:0,baseAggressiveness,aggressiveness:baseAggressiveness,cap:100,blockCpuRise:false,cpuShortage:false,cpuToGpuFrameRatio:null,reason:'await-telemetry',safetyRestore:null};
 function snapshot(){return {...state};}
 function update(sample){
  state.safetyRestore=null;
  if(!enabled||mode==='off'){state={...state,aggressiveness:baseAggressiveness,cap:100,blockCpuRise:false,reason:!enabled?'fixed-policy':'disabled'};return snapshot();}
  const gpu=sample.gpu,validGpu=Number.isFinite(gpu)&&gpu>=0&&gpu<=100;
  if(!validGpu){state={...state,gpuPct:null,blockCpuRise:mode!=='legacy',reason:'invalid-gpu-telemetry',...(mode==='legacy'?{aggressiveness:baseAggressiveness,cap:100}:{})};return snapshot();}
  const ratio=Number.isFinite(sample.cpuFrameMs)&&sample.cpuFrameMs>0&&Number.isFinite(sample.gpuFrameMs)&&sample.gpuFrameMs>0?sample.cpuFrameMs/sample.gpuFrameMs:null;
  const cpuShortage=ratio!==null&&(ratio>=1.03||(ratio>=.95&&Number.isFinite(sample.cpuCritical)&&sample.cpuCritical>=.85));
  const rawBand=gpu>=99?3:gpu>=95?2:gpu>=80?1:0;
  if(mode==='legacy'){band=rawBand;state={...state,gpuPct:gpu,gpuBand:band,cap:legacyCap(gpu),aggressiveness:Math.min(baseAggressiveness,legacyCap(gpu)),blockCpuRise:false,cpuShortage,cpuToGpuFrameRatio:ratio,reason:rawBand?'legacy-aggressiveness-cap':'gpu-has-headroom'};return snapshot();}
  // Waiting samples are not evidence that the CPU has surplus capacity.
  if(sample.externalWait===true){state={...state,gpuPct:gpu,reason:'hold-during-external-wait',blockCpuRise:true};return snapshot();}
  if(rawBand>band){band=rawBand;releaseCount=0;}
  else if(rawBand<band&&gpu<THRESHOLDS[band-1].gpuPct-3){if(++releaseCount>=2){band=rawBand;releaseCount=0;}}
  else releaseCount=0;
  let cap=band?THRESHOLDS[band-1].aggressivenessCap:100;
  const blockCpuRise=band>0&&!cpuShortage;
  if(cpuShortage)cap=100;
  else if(mode==='headroom'&&band>0&&ratio!==null)cap=Math.min(cap,Math.round(baseAggressiveness*(.4+.6*Math.max(0,Math.min(1,ratio)))));
  const target=Math.min(baseAggressiveness,cap),aggressiveness=target>state.aggressiveness?Math.min(target,state.aggressiveness+10):target;
  state={...state,gpuPct:gpu,gpuBand:band,cap,aggressiveness,blockCpuRise,cpuShortage,cpuToGpuFrameRatio:ratio,reason:cpuShortage?'cpu-service-bottleneck':blockCpuRise?'gpu-busy-protect-headroom':'gpu-has-headroom'};
  return snapshot();
 }
 function authorizeRollback(reason){if(!enabled||mode==='off'||mode==='legacy')return;state={...state,blockCpuRise:false,safetyRestore:reason,reason:'restore-measured-safe-cpu-probe'};}
 return {snapshot,update,authorizeRollback};
}
const api=Object.freeze({DEFAULT_MODE,MODES,THRESHOLDS,legacyCap,describe,create});if(typeof module!=='undefined')module.exports=api;else root.YmccGpuCpuGuard=api;
})(globalThis);

;
(function(root){
'use strict';
const freeze=value=>{if(value&&typeof value==='object'){Object.values(value).forEach(freeze);Object.freeze(value);}return value;};
// Frozen references. Derive CPU hidden values through the canonical performance policy.
const VERSIONS=freeze({
 v1:{id:'v1',label:'浮动 V1',revision:'2026-10-16-frozen',owner:'user',bias:{ac:55,dc:65},saving:{ac:'large',dc:'aggressive'},target:{ac:120,dc:60},searchMode:'baseline',gpuProtection:'off',historicalGpuProtection:'legacy'},
 v2:{id:'v2',label:'浮动 V2',revision:'2026-10-16-frozen',owner:'assistant',bias:{ac:55,dc:55},saving:{ac:'aggressive',dc:'aggressive'},target:{ac:120,dc:60},searchMode:'baseline',gpuProtection:'off',historicalGpuProtection:'legacy'}
});
const SEARCH_MODES=freeze(['baseline']);
function get(id){if(!Object.hasOwn(VERSIONS,id))throw Error('未知浮动预设');return VERSIONS[id];}
function apply(snapshot,id){const p=get(id),out=structuredClone(snapshot);out.auto.bias=structuredClone(p.bias);out.auto.energySaving=structuredClone(p.saving);out.auto.floatSearchMode=p.searchMode;for(const side of ['ac','dc']){out.frames[side]={fps:p.target[side],ceiling:p.target[side],lastFps:p.target[side]};out.auto.coreRules[side]={splitWatts:Math.min(15,out.machine.maxTdp),lowerMode:'only-small',upperMode:'big'};}return out;}
function identify(snapshot){for(const p of Object.values(VERSIONS)){if((snapshot.auto.floatSearchMode??'baseline')!==p.searchMode)continue;if(['ac','dc'].every(side=>snapshot.auto.bias[side]===p.bias[side]&&snapshot.auto.energySaving[side]===p.saving[side]&&snapshot.frames[side].fps===p.target[side]&&snapshot.auto.coreRules[side].splitWatts===Math.min(15,snapshot.machine.maxTdp)&&snapshot.auto.coreRules[side].lowerMode==='only-small'&&snapshot.auto.coreRules[side].upperMode==='big'))return {id:p.id,label:p.label,revision:p.revision};}return {id:'custom',label:'自定义浮动',searchMode:snapshot.auto.floatSearchMode??'baseline'};}
const api=freeze({VERSIONS,SEARCH_MODES,get,apply,identify,defaultVersion:'v2'});if(typeof module!=='undefined')module.exports=api;else root.YmccFloatingPresets=api;
})(globalThis);

;
(function(root){
'use strict';
const B=typeof module!=='undefined'?require('./floating-optimization-20261015/floating-lab/benchmark-data.js'):root.YmccBenchmarkData;
const CEILINGS=[0,15,30,45,60,90,120,200,300],CORE_TO_MODEL={big:'bigFirst','only-big':'bigOnly',small:'littleFirst','only-small':'littleOnly'};
// Shared by the performance page and every active automatic controller.
// Preserve the approved mid-curve points; raise only the automatic lower bound.
const MIN_AUTO_MHZ=1200;
function automaticCpuCurve(bias,depth=0){
 const release=(100-Math.max(0,Math.min(100,bias)))/100,floorGHz=MIN_AUTO_MHZ/1000;
 const maxGHz=Number(Math.max(floorGHz,.8+4.2*release).toFixed(1)),baseGHz=Number(Math.max(floorGHz,.8+2.7*release).toFixed(1)),minGHz=bias===0&&depth===0?floorGHz:Number(Math.max(floorGHz,maxGHz-(maxGHz-baseGHz)*depth/3).toFixed(1));
 return {minGHz,maxGHz,baseGHz,minMHz:Math.round(minGHz*1000),maxMHz:bias===0?0:Math.round(maxGHz*1000)};
}
function minimumFromFrequency(mhz,maxGHz=5){return mhz===0?100:Math.max(0,Math.min(100,Math.round(mhz/(maxGHz*1000)*100)));}
function resolve(snapshot){
 const side=snapshot[snapshot.mode].side,profile=B.isSelectableProfile(snapshot.simulationProfile)?snapshot.simulationProfile:B.DEFAULT_PROFILE,a=snapshot.resolvedAutoPolicy[side],m=snapshot.resolvedManualPolicy[side],rule=snapshot.auto.coreRules[side],frame=snapshot.frames[side],smt=snapshot.gameThreadPolicy?.hyperThreadMode!=='off';
 return {version:1,revision:snapshot.syncRevision??0,profile,mode:snapshot.mode,side,actualSide:snapshot.power.actualSide,machineMaxTdp:snapshot.machine.maxTdp,requestedMachineMaxTdp:snapshot.machine.maxTdp,cpuMaxGHz:B.PROFILES[profile].maxGHz,budget:a.tdpWatts,target:frame.fps,ceiling:frame.ceiling,bias:snapshot.auto.bias[side],saving:a.energySaving,auto:{tdpWatts:a.tdpWatts,targetTdp:a.tdpTarget,cadence:{...a.floatCadence},recovery:{...a.floatRecovery},gpuProtection:{...a.gpuProtection},floatingPreset:{...a.floatingPreset},floatSearchMode:a.floatSearchMode??'baseline',minMHz:Math.max(MIN_AUTO_MHZ,Math.round((a.cpuRange?.minGHz??MIN_AUTO_MHZ/1000)*1000)),maxMHz:a.cpuTargetMhz===0?0:Math.max(MIN_AUTO_MHZ,a.cpuTargetMhz),aggr:a.aggr,epp:a.epp,turbo:a.turbo,minState:a.minState,throttle:a.throttle,throttleSource:a.throttleSource,coreControl:a.coreControl,coreThreshold:a.coreControl.splitWatts,requestedCoreThreshold:rule.splitWatts,coreLow:CORE_TO_MODEL[rule.lowerMode],coreHigh:CORE_TO_MODEL[rule.upperMode],smt},manual:{controlMode:'continuous',tdpWatts:m.tdpWatts,frequencyMHz:Math.round(m.freq*1000),aggressiveness:m.aggr,epp:m.epp,minState:m.minState,turbo:m.turbo,throttle:m.throttle,coreControl:m.coreControl,coreMode:CORE_TO_MODEL[m.coreMode],smt},bounds:{min:snapshot.tdpLimits.previewMinWatts,max:Math.min(snapshot.machine.maxTdp,B.PROFILES[profile].max),lower:snapshot.tdpLimits.previewMinWatts},frames:{...frame},tdpLimits:{...snapshot.tdpLimits},policySource:'performance-preview'};
}
const api=Object.freeze({CEILINGS,CORE_TO_MODEL,MIN_AUTO_MHZ,automaticCpuCurve,minimumFromFrequency,resolve});if(typeof module!=='undefined')module.exports=api;else root.YmccPerformancePolicy=api;
})(globalThis);

;
// Shared setting identity and three-way merge for simulator working copies.
(function(root){
'use strict';
const KEYS=['schema','exampleOnly','fixture','mode','gameMode','gameThreads','machine','simulationProfile','auto','frames','manual'];
const clone=x=>JSON.parse(JSON.stringify(x));
function settings(snapshot){if(!snapshot||typeof snapshot!=='object')throw Error('参数快照无效');return Object.fromEntries(KEYS.filter(k=>Object.hasOwn(snapshot,k)).map(k=>[k,clone(snapshot[k])]));}
const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
function merge(base,draft,current){
 const conflicts=[];
 function visit(a,b,c,path){if(same(a,b))return c;if(same(a,c)||same(b,c))return b;
  const objects=[a,b,c].every(v=>v&&typeof v==='object'),arrays=objects&&[a,b,c].every(Array.isArray);
  if(objects&&(![a,b,c].some(Array.isArray)||arrays&&a.length===b.length&&b.length===c.length)){
   const result=arrays?[]:{},keys=new Set([...Object.keys(a),...Object.keys(b),...Object.keys(c)]);for(const key of keys){if(['__proto__','constructor','prototype'].includes(key))throw Error('无效参数字段');const value=visit(a[key],b[key],c[key],path?path+'.'+key:key);if(value!==undefined)result[key]=value;}return result;
  }
  conflicts.push(path);return c;
 }
 const merged=visit(settings(base),settings(draft),settings(current),'');return {ok:conflicts.length===0,conflicts,settings:merged};
}
const api=Object.freeze({settings,same,merge});if(typeof module!=='undefined')module.exports=api;else root.YmccSimulationSession=api;
})(globalThis);

;
'use strict';
(() => {
  const KEY='ymcc-performance-design-v29-20261015';
  const isSimulationDraft=new URLSearchParams(location.search).get('simulationDraft')==='1';
  const LEGACY_KEYS=['ymcc-performance-design-v28-20261015','ymcc-performance-design-v27-20261015','ymcc-performance-design-v26-20261015','ymcc-performance-design-v25-20261015','ymcc-performance-design-v24-20261015','ymcc-performance-design-v23-20261015','ymcc-performance-design-v22-20261015','ymcc-performance-design-v21-20261015','ymcc-performance-design-v20-20261015','ymcc-performance-design-v19-20261015','ymcc-performance-design-v18-20261015','ymcc-performance-design-v17-20261015','ymcc-performance-design-v16-20261015','ymcc-performance-design-v15-20261015','ymcc-performance-design-v14-20261015','ymcc-performance-design-v13-20261015','ymcc-performance-design-v12-20261015','ymcc-performance-design-v11-20261015','ymcc-performance-design-v10-20261015','ymcc-performance-design-v9-20261015','ymcc-performance-design-v8-20261015','ymcc-performance-design-v7-20261015','ymcc-performance-design-v6-20261015','ymcc-performance-design-v5-20261015'];
  const CEILINGS=window.YmccPerformancePolicy.CEILINGS;
  // User-confirmed application ceiling; never evidence of a device/firmware maximum.
  const APPLICATION_TDP_MAX=150;
  const TDP_CEILINGS=[16,30,45,55,120,APPLICATION_TDP_MAX];
  // Legacy automatic-tier migration only; current controls are continuous.
  const WEIGHTS=[.2,.3,.45,.6,.8,1];
  const SAVING_LEVELS={none:{label:'无浮动',minDrop:0,ratio:0,cpuDepth:0,minState:100},small:{label:'小幅',minDrop:5,ratio:.2,cpuDepth:1,minState:30},medium:{label:'中幅',minDrop:7,ratio:.3,cpuDepth:2,minState:20},large:{label:'大幅',minDrop:10,ratio:.4,cpuDepth:3,minState:10},aggressive:{label:'激进',minDrop:15,ratio:.5,cpuDepth:4,minState:0}};
  const GPU_TURBO_OFF_BIAS=70;
  // Keep legacy IDs: 2 was already only-small. Display order must not change saved meaning.
  const CORE_MODES=[{value:0,key:'big',label:'大核为主'},{value:1,key:'only-big',label:'仅大核'},{value:3,key:'small',label:'小核为主'},{value:2,key:'only-small',label:'仅小核'}];
  const defaultCoreRule=()=>({splitWatts:15,lowerMode:'only-small',upperMode:'big'});
  const coreMode=value=>CORE_MODES.find(mode=>mode.key===value);
  // Explicit preview observations, independent of requested power-plan settings.
  const CORE_TOPOLOGY_SCENARIOS={
    unknown:{label:'未接入回读',source:'not-connected',classification:'unknown',types:[{key:'unknown',label:'CPU',total:null,threads:null,enabled:null,disabled:null}]},
    hybrid3:{label:'三类核心 · 演示',source:'illustrative-readback',types:[{key:'p',label:'P 核',total:4,threads:8,enabled:3,disabled:1},{key:'e',label:'E 核',total:8,threads:8,enabled:6,disabled:2},{key:'lp-e',label:'LP-E 核',total:2,threads:2,enabled:2,disabled:0}]},
    hybrid2:{label:'两类核心 · 演示',source:'illustrative-readback',types:[{key:'p',label:'P 核',total:4,threads:8,enabled:4,disabled:0},{key:'e',label:'E 核',total:8,threads:8,enabled:6,disabled:2}]},
    unclassified:{label:'三类未确认名称 · 演示',source:'illustrative-readback',classification:'unverified-labels',types:[{key:'class-2',label:'级别2',total:4,threads:8},{key:'class-1',label:'级别1',total:8,threads:8},{key:'class-0',label:'级别0',total:2,threads:2}]},
    noSmt:{label:'无超线程 · 演示',source:'illustrative-readback',types:[{key:'p',label:'P 核',total:4,threads:4,enabled:4,disabled:0},{key:'e',label:'E 核',total:8,threads:8,enabled:8,disabled:0}]},
    uniform:{label:'同构核心 · 演示',source:'illustrative-readback',types:[{key:'uniform',label:'CPU 核',total:8,threads:16,enabled:6,disabled:2}]}
  };
  const DEFAULT_TOP_GAME={name:'game-a.exe',path:'C:/Preview/GameA/game-a.exe',pid:4201,processCreated:'133000000000000001'};
  const FIXTURES={
    unknown:{min:1,requestMax:APPLICATION_TDP_MAX,verifiedMax:null,initial:120,label:'未匹配机型 · 默认 120W',source:'暂无已确认机型数据，120W 为兜底配置，仍需设备确认'},
    reference:{min:5,requestMax:APPLICATION_TDP_MAX,verifiedMax:null,initial:30,label:'演示参考值 · 30W',source:'假设场景，未采纳 CTW / HC 数值'},
    verified:{min:8,requestMax:APPLICATION_TDP_MAX,verifiedMax:35,initial:35,label:'设备范围示例 · 8–35W',source:'模拟设备范围 8–35W，不代表本机硬件回读'}
  };
  const clone=x=>JSON.parse(JSON.stringify(x));
  const clamp=(x,a,b)=>Math.max(a,Math.min(b,x));
  const $=(selector,root=document)=>root.querySelector(selector);
  const $$=(selector,root=document)=>[...root.querySelectorAll(selector)];
  const snap=(v,min,max,step=1)=>clamp(Number((Math.round(v/step)*step).toFixed(2)),min,max);
  function allocate(max,min){return WEIGHTS.map(w=>clamp(Math.round(max*w),min,max));}
  function defaults(){
    const make=(freq,aggr,turbo)=>({freq,aggr,turbo,unlimited:false,minOverride:null,core:0});
    return {schema:'ymcc-performance-prototype-v29',exampleOnly:true,fixture:'unknown',mode:'auto',gameMode:'auto',gameThreads:{},machine:{maxTdp:120},simulationProfile:window.YmccBenchmarkData.DEFAULT_PROFILE,auto:{floatSearchMode:'baseline',coreRules:{ac:defaultCoreRule(),dc:defaultCoreRule()},bias:{ac:55,dc:55},energySaving:{ac:'aggressive',dc:'aggressive'},side:'ac',tdp:{ac:120,dc:36}},frames:{ac:{fps:120,ceiling:120,lastFps:120},dc:{fps:60,ceiling:60,lastFps:60}},manual:{side:'ac',controls:{ac:make(4,60,true),dc:make(2,20,false)}}};
  }
  function valid(s){
    if(!s||s.schema!=='ymcc-performance-prototype-v29'||s.exampleOnly!==true||!Object.hasOwn(FIXTURES,s.fixture)||!['auto','manual'].includes(s.mode)||!['auto','manual'].includes(s.gameMode))return false;
    if(!window.YmccFloatingPresets.SEARCH_MODES.includes(s.auto?.floatSearchMode??'baseline'))return false;
    if(!window.YmccBenchmarkData.isSelectableProfile(s.simulationProfile))return false;
    if(!s.gameThreads||Array.isArray(s.gameThreads)||typeof s.gameThreads!=='object')return false;
    for(const [exe,profile] of Object.entries(s.gameThreads))if(exe!==exe.toLowerCase()||!exe.endsWith('.exe')||/[\\/]/.test(exe)||!profile||!['on','off'].includes(profile.hyperThreadMode))return false;
    const f=FIXTURES[s.fixture],max=Math.min(APPLICATION_TDP_MAX,f.verifiedMax??f.requestMax);
    if(!Number.isInteger(s.machine?.maxTdp)||s.machine.maxTdp<f.min||s.machine.maxTdp>max||!['ac','dc'].includes(s.auto?.side)||!['ac','dc'].includes(s.manual?.side))return false;
    for(const side of ['ac','dc']){
      if(!Number.isInteger(s.auto.bias?.[side])||s.auto.bias[side]<0||s.auto.bias[side]>100||s.auto.bias[side]%5!==0||!Number.isInteger(s.auto.tdp?.[side])||s.auto.tdp[side]<f.min||s.auto.tdp[side]>s.machine.maxTdp)return false;
      if(!Object.hasOwn(SAVING_LEVELS,s.auto.energySaving?.[side]))return false;
      const rule=s.auto.coreRules?.[side];if(!rule||!Number.isInteger(rule.splitWatts)||rule.splitWatts<0||rule.splitWatts>f.requestMax||!coreMode(rule.lowerMode)||!coreMode(rule.upperMode))return false;
      const fr=s.frames?.[side];if(!fr||!CEILINGS.includes(fr.ceiling)||!Number.isInteger(fr.fps)||!Number.isInteger(fr.lastFps)||fr.lastFps<15||fr.lastFps>300)return false;
      if(fr.ceiling===0?fr.fps!==0:fr.fps<15||fr.fps>fr.ceiling)return false;
      const p=s.manual.controls?.[side];
      if(!p||!Number.isFinite(p.freq)||p.freq<.8||p.freq>7.2||!Number.isInteger(p.aggr)||p.aggr<0||p.aggr>100||typeof p.turbo!=='boolean'||typeof p.unlimited!=='boolean'||(p.minOverride!==null&&(!Number.isInteger(p.minOverride)||p.minOverride<0||p.minOverride>100))||!CORE_MODES.some(mode=>mode.value===p.core))return false;
      if(s.manual.active!==undefined||s.manual.profiles!==undefined)return false;
    }return true;
  }
  function restoreDraft(saved){
    if(!saved||!['ymcc-performance-prototype-v5','ymcc-performance-prototype-v6','ymcc-performance-prototype-v7','ymcc-performance-prototype-v8','ymcc-performance-prototype-v9','ymcc-performance-prototype-v10','ymcc-performance-prototype-v11','ymcc-performance-prototype-v12','ymcc-performance-prototype-v13','ymcc-performance-prototype-v14','ymcc-performance-prototype-v15','ymcc-performance-prototype-v16','ymcc-performance-prototype-v17','ymcc-performance-prototype-v18','ymcc-performance-prototype-v19','ymcc-performance-prototype-v20','ymcc-performance-prototype-v21','ymcc-performance-prototype-v22','ymcc-performance-prototype-v23','ymcc-performance-prototype-v24','ymcc-performance-prototype-v25','ymcc-performance-prototype-v26','ymcc-performance-prototype-v27','ymcc-performance-prototype-v28','ymcc-performance-prototype-v29'].includes(saved.schema))return null;
    const next=clone(saved),f=FIXTURES[next.fixture];
    if(!f||!Number.isInteger(next.machine?.maxTdp)||next.machine.maxTdp<f.min)return null;
    // Clamp the active copy while leaving every legacy storage key unchanged.
    next.machine.maxTdp=Math.min(next.machine.maxTdp,APPLICATION_TDP_MAX,f.verifiedMax??f.requestMax);
    const reverseBias=!['ymcc-performance-prototype-v14','ymcc-performance-prototype-v15','ymcc-performance-prototype-v16','ymcc-performance-prototype-v17','ymcc-performance-prototype-v18','ymcc-performance-prototype-v19','ymcc-performance-prototype-v20','ymcc-performance-prototype-v21','ymcc-performance-prototype-v22','ymcc-performance-prototype-v23','ymcc-performance-prototype-v24','ymcc-performance-prototype-v25','ymcc-performance-prototype-v26','ymcc-performance-prototype-v27','ymcc-performance-prototype-v28','ymcc-performance-prototype-v29'].includes(next.schema);
    next.schema='ymcc-performance-prototype-v29';
    next.simulationProfile=window.YmccBenchmarkData.isSelectableProfile(next.simulationProfile)?next.simulationProfile:window.YmccBenchmarkData.DEFAULT_PROFILE;
    // Migrate selected AC/DC CPU settings once; original tier storage stays under its legacy key.
    if(!next.manual?.controls){
      if(saved.schema==='ymcc-performance-prototype-v29')return null;
      const controls={};for(const side of ['ac','dc']){const i=next.manual?.active?.[side],profiles=next.manual?.profiles?.[side];if(!Number.isInteger(i)||i<0||i>5||!Array.isArray(profiles)||profiles.length!==6||!profiles[i])return null;const p=profiles[i];controls[side]={freq:p.freq,aggr:p.aggr,turbo:p.turbo,unlimited:p.unlimited,minOverride:p.minOverride??null,core:p.core};}
      next.manual={side:next.manual.side,controls};
    }
    next.gameMode=next.gameMode??'auto';
    next.gameThreads=next.gameThreads??{};
    next.auto.tdp=next.auto.tdp||{};
    next.auto.floatSearchMode='baseline';
    next.auto.coreRules=next.auto.coreRules??{ac:defaultCoreRule(),dc:defaultCoreRule()};
    // The two-range editor replaces the retired auto-core switch. Old storage is untouched.
    delete next.auto.coreAuto;
    next.auto.energySaving=next.auto.energySaving||next.auto.tdpFloat||{ac:'large',dc:'aggressive'};
    delete next.auto.tdpFloat;
    for(const side of ['ac','dc']){
      const fr=next.frames?.[side];
      if(fr&&Number.isInteger(fr.fps)&&Number.isInteger(fr.lastFps)&&Number.isInteger(fr.ceiling)){
        fr.lastFps=clamp(fr.lastFps,15,300);
        if(fr.ceiling===0)fr.fps=0;
        else{fr.fps=clamp(fr.fps,15,fr.ceiling);}
      }
      const rule=next.auto.coreRules[side];
      if(rule&&Number.isInteger(rule.splitWatts))rule.splitWatts=clamp(rule.splitWatts,0,APPLICATION_TDP_MAX);
      // Older drafts stored CPU priority on the right. Preserve their chosen policy.
      if(reverseBias){if(!Number.isInteger(next.auto.bias?.[side]))return null;next.auto.bias[side]=100-next.auto.bias[side];}
      if(!Number.isInteger(next.auto.bias?.[side])||next.auto.bias[side]<0||next.auto.bias[side]>100)return null;
      next.auto.bias[side]=snap(next.auto.bias[side],0,100,5);
      const oldTier=next.auto.active?.[side];
      if(next.auto.tdp[side]===undefined)next.auto.tdp[side]=allocate(next.machine.maxTdp,f.min)[oldTier??(side==='ac'?3:1)];
      next.auto.tdp[side]=clamp(next.auto.tdp[side],f.min,next.machine.maxTdp);

    }
    delete next.auto.active;
    return valid(next)?next:null;
  }
  let state=defaults(),restored=false;
  for(const key of (isSimulationDraft?[]:[KEY,...LEGACY_KEYS])){
    try{const saved=restoreDraft(JSON.parse(localStorage.getItem(key)));if(saved){state=saved;restored=true;break;}}catch{}
  }
  // Simulated live power is separate from both editing sides and saved configuration.
  let actualPowerSide=state.power?.actualSide==='dc'?'dc':'ac';
  state.simulationProfile=window.YmccBenchmarkData.isSelectableProfile(state.simulationProfile)?state.simulationProfile:window.YmccBenchmarkData.DEFAULT_PROFILE;
  let coreTopologyScenario=state.previewRuntime?.coreTopologyScenario??state.coreObservation?.scenario??'unknown';
  let topGame=state.previewRuntime&&Object.hasOwn(state.previewRuntime,'topGame')?clone(state.previewRuntime.topGame):clone(DEFAULT_TOP_GAME);
  for(const key of Object.keys(state))if(!Object.hasOwn(defaults(),key))delete state[key];
  const app=$('#app');
  const fixture=()=>FIXTURES[state.fixture];
  const maxRequest=()=>Math.min(APPLICATION_TDP_MAX,fixture().verifiedMax??fixture().requestMax);
  const effectiveMaxTdp=()=>Math.min(state.machine.maxTdp,maxRequest());
  const manualProfile=s=>state.manual.controls[s];
  const notice=text=>$('#notice').textContent=text;
  // Mirrors the current YMCC bridge default; these are Windows THROTTLING values.
  function throttleForFrequency(freqMhz){return freqMhz>0&&freqMhz<=2000?1:2;}
  function throttleLabel(value){return ({0:'关闭',1:'开启',2:'自动'})[value];}
  function powerCorePolicy(s){
    const rule=state.auto.coreRules[s],splitWatts=Math.min(rule.splitWatts,effectiveMaxTdp());
    const activeRange=state.auto.tdp[s]<=splitWatts?'lower':'upper';
    const mode=activeRange==='lower'?rule.lowerMode:rule.upperMode;
    return {backend:'power-plan',scope:'system',enabled:true,mode,hardwareApplied:false,bindingStatus:'pending-device-validation',
      budgetSource:'configured-current-tdp',requestedSplitWatts:rule.splitWatts,splitWatts,rangeMaxWatts:effectiveMaxTdp(),
      lowerMode:rule.lowerMode,upperMode:rule.upperMode,activeRange,boundaryOwner:'lower',processAffinity:false};
  }
  function coreObservation(){
    const topology=window.YmccCoreModel.topology(state.simulationProfile);
    if(topology.known){const selected=state[state.mode].side,requested=state.mode==='auto'?powerCorePolicy(selected).mode:CORE_MODES.find(mode=>mode.value===manualProfile(selected).core).key,active=window.YmccCoreModel.evaluate(state.simulationProfile,window.YmccPerformancePolicy.CORE_TO_MODEL[requested],true,state.auto.tdp[selected]);
      return {scenario:state.simulationProfile,source:'shared-simulation-profile',classification:'profile-specifications',hardwareReadback:false,actualPowerSide,types:topology.groups.map((group,i)=>({key:group.kind==='P'?'p-'+i:group.kind==='E'?'e':'lp-e',label:group.label,total:group.count,threads:group.count*(group.smt?2:1),enabled:active.active.some(a=>a.kind===group.kind&&a.cluster===group.cluster)?group.count:0,disabled:active.active.some(a=>a.kind===group.kind&&a.cluster===group.cluster)?0:group.count}))};
    }
    const scenario=CORE_TOPOLOGY_SCENARIOS[coreTopologyScenario];return {
    scenario:coreTopologyScenario,source:scenario.source,classification:scenario.classification??'simulated-labels',hardwareReadback:false,actualPowerSide,types:clone(scenario.types)};
  }
  function gameThreadPolicy(){
    const game=topGame,exe=game?(game.path.split(/[\\/]/).pop()||game.name).toLowerCase():null;
    const observation=coreObservation(),known=observation.types.every(type=>Number.isInteger(type.total)&&type.total>0&&Number.isInteger(type.threads)&&type.threads>=type.total);
    const supported=known&&observation.types.some(type=>type.threads>type.total);
    const mode=exe?(state.gameThreads[exe]?.hyperThreadMode??'on'):'default';
    const available=Boolean(game&&supported);
    return {scope:'game-process',source:'existing-top-game-preview',exe,target:game?{pid:game.pid,processCreated:game.processCreated}:null,
      hyperThreadMode:mode,supported,available,applied:false,hardwareApplied:false,systemSmtChanged:false,
      status:!game?'no-game':!known?'unknown-topology':!supported?'unsupported':'preview-pending',
      requestedOperation:available?{operation:'game.corePolicy.apply',mode:'default',hyperThreadMode:mode,pid:game.pid,processCreated:game.processCreated}:null,
      projectedGameThreads:available?observation.types.map(type=>({key:type.key,physicalCores:type.total,logicalThreads:mode==='off'?type.total:type.threads})):null};
  }
  function gameThreadTarget(){return gameThreadPolicy().exe??'未识别游戏';}
  function gameThreadWidget(){
    const policy=gameThreadPolicy(),types=coreObservation().types;
    const projected=new Map((policy.projectedGameThreads??[]).map(type=>[type.key,type.logicalThreads]));
    const label=type=>({p:'P',e:'E','lp-e':'LP-E',uniform:'CPU',unknown:'CPU'}[type.key]??type.label);
    return '<div class="game-thread-controls" role="group" aria-label="当前游戏模拟可用核心与线程" data-count-source="projected-game-policy" style="--topology-columns:'+types.length+'">'+types.map(type=>
      '<span class="thread-topology-value" data-thread-type="'+type.key+'" title="当前游戏模拟可用线程，不代表全局硬件变更"><span>'+label(type)+'</span><b>'+ (type.total??'—')+'核/'+(projected.get(type.key)??type.threads??'—')+'线程</b></span>').join('')+'</div>';
  }
  function gameThreadToggle(extraClass=''){
    const policy=gameThreadPolicy();
    const text=!policy.exe?'未识别':policy.status==='unknown-topology'?'待检测':!policy.supported?'不支持':policy.hyperThreadMode==='off'?'关闭':'开启';
    return `<button type="button" class="game-thread-toggle ${extraClass}" data-action="game-thread" data-game-exe="${policy.exe??''}" aria-label="${gameThreadTarget()} 超线程${text}" aria-pressed="${policy.hyperThreadMode==='on'}" ${policy.available?'':'disabled'} title="仅应用于 ${gameThreadTarget()} 的游戏线程策略"><span>超线程</span><b>${text}</b></button>`;
  }
  function policy(s=state.auto.side){
    // Bias owns the CPU ceiling; the saving level owns CPU floating depth.
    // Neither value is derived from the current watts or the machine watt limit.
    const tdpWatts=clamp(Math.round(state.auto.tdp[s]),fixture().min,effectiveMaxTdp());
    const release=(100-state.auto.bias[s])/100,energySaving=state.auto.energySaving[s],spec=SAVING_LEVELS[energySaving];
    const cpuCurve=window.YmccPerformancePolicy.automaticCpuCurve(state.auto.bias[s],spec.cpuDepth);
    const cpuMax=cpuCurve.maxGHz;
    const cpuMin=cpuCurve.minGHz;
    const cpuUnlimited=state.auto.bias[s]===0;
    const cpuFloatEnabled=spec.cpuDepth>0;
    const cpuFloating=cpuFloatEnabled&&(cpuUnlimited||cpuMin<cpuMax);
    const cpuMode=cpuFloatEnabled?'floating':cpuUnlimited?'unlimited':'fixed';
    const cpu=cpuUnlimited?null:[cpuMin,cpuMax];
    // Null upper bound means no software frequency ceiling, not no CPU floating.
    const cpuRange=cpuUnlimited&&!cpuFloating?null:{minGHz:cpuMin,maxGHz:cpuUnlimited?null:cpuMax};
    // This preview starts at the configured upper target (0 means uncapped).
    // THROTTLING is one setting for that target, never a list of range endpoints.
    const cpuTargetMhz=cpuUnlimited?0:Math.round(cpuMax*1000);
    const throttle=throttleForFrequency(cpuTargetMhz);
    const drop=Math.max(spec.minDrop,Math.ceil(tdpWatts*spec.ratio));
    const tdpTarget=energySaving==='none'?tdpWatts:Math.max(fixture().min,Math.floor(tdpWatts-drop));
    const aggr=Math.round(10+90*release),epp=100-aggr;
    const turbo=state.auto.bias[s]<GPU_TURBO_OFF_BIAS&&(cpuUnlimited||cpuMax>2);
    return {tdpWatts,cpu,cpuRange,cpuUnlimited,cpuFloatEnabled,cpuFloating,cpuMode,energySaving,floatCadence:{...window.YmccFloatCadence.forLevel(energySaving),stableSamples:['quick-stable','quick-power'].includes(state.auto.floatSearchMode)?1:2},floatRecovery:{...window.YmccFloatCadence.RECOVERY},
      tdpFloat:energySaving, // Compatibility alias for existing header consumers.
      tdpTarget,aggr,epp,minState:spec.minState,minStateSource:'energy-saving',turbo,core:coreMode(powerCorePolicy(s).mode).value,coreMode:powerCorePolicy(s).mode,coreControl:powerCorePolicy(s),
      cpuTargetMhz,throttle,throttleSource:'cpu-target',
      floatingPreset:window.YmccFloatingPresets.identify(state),floatSearchMode:state.auto.floatSearchMode??'baseline',aggressivenessMode:'bias-only',gpuAggrOverride:false,gpuProtection:window.YmccGpuCpuGuard.describe(),gpuPowerAllocation:null};
  }
  function cpuDescription(p){
    if(p.cpuUnlimited)return p.cpuFloating?p.cpuRange.minGHz.toFixed(1)+' GHz–不限':'不限频率';
    return (p.cpuFloating?p.cpu.map(v=>v.toFixed(1)).join('–'):p.cpu[1].toFixed(1))+' GHz';
  }
  function readout(value,attrs,label){return `<output ${attrs.replace(/\bdisabled\b/g,'')} aria-label="${label}">${value}</output>`;}
  function budgetOptions(){return [...new Set([...TDP_CEILINGS,effectiveMaxTdp(),maxRequest()])].sort((a,b)=>a-b).map(w=>`<option value="${w}" ${w===effectiveMaxTdp()?'selected':''} ${w<fixture().min||w>maxRequest()?'disabled':''}>最大 ${w} W</option>`).join('');}
  function budgetSelect(){return `<select id="tdp-ceiling" class="header-select" data-action="budget-select" aria-label="TDP 上限">${budgetOptions()}</select>`;}
  function slider(value,min,max,step,attrs,label,classes=''){
    const pct=max===min?0:(value-min)/(max-min)*100;
    return `<div class="track-wrap ${classes}"><div class="rail"></div><div class="fill" style="--position:${pct}%"></div><input type="range" value="${value}" min="${min}" max="${max}" step="${step}" ${attrs} aria-label="${label}"></div>`;
  }
  function frameCard(s,compact=false){
    const f=state.frames[s],off=f.ceiling===0,attrs=`data-action="frame" data-side="${s}" ${off?'disabled':''}`;
    const value=`<div class="frame-value">${off?'<span class="frame-off">已关闭</span>':`${readout(f.fps,attrs,`${s.toUpperCase()} 帧数目标`)}<span>FPS</span>`}</div>`;
    const select=`<select id="ceiling-${s}" class="header-select" data-action="ceiling" data-side="${s}" aria-label="${s.toUpperCase()} 帧数目标上限">${CEILINGS.map(v=>`<option value="${v}" ${v===f.ceiling?'selected':''}>${v===0?'关闭锁帧':v+' FPS'}</option>`).join('')}</select>`;
    const track=`<div class="frame-control compact-track">${slider(f.fps,off?0:15,f.ceiling||15,5,attrs,`${s.toUpperCase()} 帧数目标`)}<div class="scale"><span>${off?'':'15 FPS'}</span><span>${off?'':f.ceiling+' FPS'}</span></div></div>`;
    if(compact)return `<section class="frame-card compact-control-row ${s}"><div class="compact-label"><span class="schedule-field-label">目标帧率</span>${value}</div>${track}${select}</section>`;
    return `<section class="frame-card ${s}"><div class="frame-head"><span class="schedule-field-label">目标帧率</span><div class="heading-controls">${value}${select}</div></div>${track}</section>`;
  }
  // Reuse the existing YMCC icon paths; no external icon/font dependency.
  const UI_ICONS={"plug": "<path d=\"M9 3v3.5M15 3v3.5M7 6.5h10v4.2c0 2.6-2 4.3-5 4.3s-5-1.7-5-4.3V6.5M12 15v3c0 1.5-1 2.5-2.2 2.5\"/>", "battery": "<rect x=\"3\" y=\"8\" width=\"15\" height=\"9\" rx=\"2\"/><path d=\"M21 11v3\"/><path d=\"M6.5 11v3\"/>"};
  function uiIcon(name){return `<svg class="ui-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true" focusable="false">${UI_ICONS[name]||''}</svg>`;}
  function sideSwitch(mode){
    return `<div class="power-toolbar" role="group" aria-label="供电配置编辑侧">${['ac','dc'].map(s=>{
      const current=state[mode].side===s;
      return `<button type="button" class="${s}" data-action="side" data-mode="${mode}" data-side="${s}" aria-label="${s==='ac'?'交流电模式':'电池模式'}，编辑 ${s.toUpperCase()} 配置" aria-pressed="${current}">${uiIcon(s==='ac'?'plug':'battery')}<span>${s==='ac'?'交流电模式':'电池模式'}</span></button>`;
    }).join('')}</div>`;
  }
  function paintPowerStatus(){
    const status=$('#power-status');
    status.textContent=actualPowerSide==='ac'?'当前插电 AC 模式':'当前电池 DC 模式';status.setAttribute('aria-label',(actualPowerSide==='ac'?'当前插电 AC 模式':'当前电池 DC 模式')+'；下方交流电与电池按钮选择编辑配置');
    status.dataset.side=actualPowerSide;
    $('#power-source').value=actualPowerSide;
  }
  function autoTdpMetric(s){const p=policy(s);return `<div class="tdp-metric" role="group" aria-label="${s.toUpperCase()} 功耗与浮动目标">
    <div class="tdp-saving-result"><span>浮动目标</span><div class="control-value"><output id="auto-tdp-${s}" aria-label="${s.toUpperCase()} 浮动目标">${p.tdpTarget}</output><span>W</span></div></div>
    <span class="tdp-saving-arrow" aria-hidden="true">←</span>
    <div class="tdp-current-result"><span>当前 TDP</span><div class="control-value">${readout(p.tdpWatts,`data-action="auto-tdp" data-side="${s}"`,`${s.toUpperCase()} 自动 TDP`)}<span>W</span></div></div>
  </div>`;}
  function autoSummary(s){return `<section class="auto-summary ${s}" role="group" aria-label="${s.toUpperCase()} 调度数据"><div class="auto-summary-row">${autoTdpMetric(s)}${autoFeedback(s)}</div></section>`;}
  function coreRuleOptions(selected){return CORE_MODES.map(mode=>`<option value="${mode.key}" ${mode.key===selected?'selected':''}>${mode.label}</option>`).join('');}
  function coreColor(mode){return mode==='big'||mode==='only-big'?'var(--cpu)':'var(--gpu)';}
  function coreRuleEditor(s){const rule=powerCorePolicy(s);return `<section class="core-rule-editor" data-core-rule-side="${s}" style="--core-split:${rule.splitWatts/rule.rangeMaxWatts*100}%;--core-lower:${coreColor(rule.lowerMode)};--core-upper:${coreColor(rule.upperMode)}" aria-label="${s.toUpperCase()} TDP 核心分界">
    <div class="core-rule-heading"><div class="core-rule-title"><label for="core-split-${s}">核心调度</label></div><div class="core-split-value">${readout(rule.splitWatts,`data-action="core-split" data-side="${s}"`,`${s.toUpperCase()} 核心 TDP 分界`)}<span>W</span></div>${gameThreadWidget()}</div>
    ${slider(rule.splitWatts,0,rule.rangeMaxWatts,1,`id="core-split-${s}" data-action="core-split" data-side="${s}"`,`${s.toUpperCase()} 核心 TDP 分界`,'core-split-track')}
    <div class="core-rule-selects">
      <label class="core-rule-field lower" data-active="${rule.activeRange==='lower'}"><span class="core-interval" id="core-lower-label-${s}">0–${rule.splitWatts} W</span><select class="header-select" id="core-lower-${s}" data-action="core-rule-mode" data-band="lowerMode" data-side="${s}" aria-label="${s.toUpperCase()} 分界点及以下的电源核心方案">${coreRuleOptions(rule.lowerMode)}</select></label>
      <label class="core-rule-field upper" data-active="${rule.activeRange==='upper'}"><span class="core-interval" id="core-upper-label-${s}">${rule.splitWatts}–${rule.rangeMaxWatts} W</span><select class="header-select" id="core-upper-${s}" data-action="core-rule-mode" data-band="upperMode" data-side="${s}" aria-label="${s.toUpperCase()} 高于分界点的电源核心方案">${coreRuleOptions(rule.upperMode)}</select></label>
      ${gameThreadToggle()}
    </div>
  </section>`;}
  function paintCoreRule(s){
    const editor=$('.core-rule-editor');if(!editor)return;
    const rule=powerCorePolicy(s),input=$('input[data-action="core-split"]',editor);
    input.max=rule.rangeMaxWatts;paint(`[data-action="core-split"][data-side="${s}"]`,rule.splitWatts);
    editor.style.setProperty('--core-split',rule.splitWatts/rule.rangeMaxWatts*100+'%');
    editor.style.setProperty('--core-lower',coreColor(rule.lowerMode));editor.style.setProperty('--core-upper',coreColor(rule.upperMode));
    $('#core-lower-label-'+s).textContent='0–'+rule.splitWatts+' W';$('#core-upper-label-'+s).textContent=rule.splitWatts+'–'+rule.rangeMaxWatts+' W';
    $('#core-lower-'+s).value=rule.lowerMode;$('#core-upper-'+s).value=rule.upperMode;
    $('.lower',editor).dataset.active=String(rule.activeRange==='lower');$('.upper',editor).dataset.active=String(rule.activeRange==='upper');
    input.setAttribute('aria-valuetext',rule.splitWatts+' W，以下'+coreMode(rule.lowerMode).label+'，以上'+coreMode(rule.upperMode).label);
  }
  function setCoreSplit(s,value){state.auto.coreRules[s].splitWatts=snap(value,0,effectiveMaxTdp());paintPolicy();notice(`${s.toUpperCase()} 核心分界 ${powerCorePolicy(s).splitWatts}W，两侧电源方案独立保存。`);}
  function manualCoreControl(s){const p=manualProfile(s);return `<label class="manual-core-field"><span>大小核心</span><select class="header-select" data-action="manual-core" data-side="${s}" aria-label="${s.toUpperCase()} 大小核心">${CORE_MODES.map(mode=>`<option value="${mode.value}" ${p.core===mode.value?'selected':''}>${mode.label}</option>`).join('')}</select></label>`;}
  function autoTdpControl(s){const p=policy(s);return `<section class="auto-tdp-control compact-control-row">
    <label class="compact-label" for="auto-tdp-slider-${s}"><span>TDP</span><span class="tdp-row-value">${readout(p.tdpWatts,`data-action="auto-tdp" data-side="${s}"`,`${s.toUpperCase()} 当前 TDP`)}<span>W</span></span></label>
    <div class="compact-track">${slider(p.tdpWatts,fixture().min,effectiveMaxTdp(),1,`id="auto-tdp-slider-${s}" data-action="auto-tdp" data-side="${s}"`,`${s.toUpperCase()} 自动 TDP`)}<div class="scale"><span>${fixture().min} W</span><span id="auto-max-label">${effectiveMaxTdp()} W</span></div></div>
    ${budgetSelect()}
  </section>`;}
  function linkageRows(rows){return `<div class="link-grid">${rows.map(([label,value])=>`<div class="link-row"><span>${label}</span><b>${value}</b></div>`).join('')}</div>`;}
  function biasCard(s){return `
    <section class="bias-board bias-card ${s}" data-bias-card="${s}">
      <div class="bias-ends"><div><strong>CPU 优先</strong></div><div><strong>GPU 优先</strong></div></div>
      ${slider(state.auto.bias[s],0,100,5,'data-action="bias" data-side="'+s+'"',s.toUpperCase()+' 倾向：左侧 CPU 优先，右侧 GPU 优先','bias-track')}
      <div class="bias-scale" aria-hidden="true">${Array.from({length:21},(_,i)=>`<span class="bias-tick ${i%5===0?'major':''}" style="--tick:${i*5}%">${i%5===0?`<span>${i*5}</span>`:''}</span>`).join('')}</div>
    </section>`;}
  function autoFeedback(s){const p=policy(s);return `<div class="auto-feedback" data-feedback-side="${s}">
    <div data-feedback="frequency"><div class="frequency-heading"><span class="feedback-label">CPU 频率</span><span class="turbo-status" data-feedback="turbo" id="auto-turbo-${s}" role="status" aria-live="off" aria-label="${s.toUpperCase()} 睿频${p.turbo?'开启':'关闭'}" data-enabled="${p.turbo}" title="睿频${p.turbo?'开启':'关闭'}">${p.turbo?'开睿频':'关睿频'}</span></div><b id="auto-cpu-${s}">${cpuDescription(p)}</b></div>
    <div data-feedback="aggressiveness"><span class="feedback-label">CPU 积极性</span><b id="auto-aggr-${s}">${p.aggr}%</b></div>
  </div>`;}
  function floatDescription(p){
    const spec=SAVING_LEVELS[p.energySaving];
    return p.energySaving==='none'?'无浮动':spec.label+' · 降低约 '+(p.energySaving==='large'?42:Math.round(spec.ratio*100))+'%';
  }
  function floatIndex(key){return ['none','small','medium','large','aggressive'].indexOf(key);}
  function floatControl(s){const p=policy(s);return `<div class="float-control"><div><span>浮动节能</span><b id="float-label-${s}">${floatDescription(p)}</b></div>${slider(floatIndex(p.energySaving),0,4,1,`data-action="energy-saving" data-side="${s}"`,`${s.toUpperCase()} 浮动节能`,'float-track')}<div class="float-ticks"><span>无浮动</span><span>小幅</span><span>中幅</span><span>大幅</span><span>激进</span></div></div>`;}
  function autoView(){const s=state.auto.side;return `
    <section class="tier-section">${sideSwitch('auto')}${autoSummary(s)}<section class="auto-tier-card ${s}">${autoTdpControl(s)}${frameCard(s,true)}</section></section>
    <section class="schedules-section" aria-label="性能调度"><section class="schedule-card ${s}" data-schedule-side="${s}">${floatControl(s)}${biasCard(s)}${coreRuleEditor(s)}</section></section>
    `;}
  function manualBounds(){return [fixture().min,effectiveMaxTdp()];}
  function enforceGlobalMax(){for(const side of ['ac','dc'])state.auto.tdp[side]=clamp(Math.round(state.auto.tdp[side]??effectiveMaxTdp()),fixture().min,effectiveMaxTdp());}
  function control(label,value,min,max,step,key,s,unit,extra=''){
    const attrs=`data-action="manual" data-key="${key}" data-side="${s}" ${extra}`;
    return `<section class="control"><div class="control-head"><span class="label">${label}</span><div class="control-value">${readout(key==='freq'?value.toFixed(1):value,attrs,`${s.toUpperCase()} ${label}`)}<span>${unit}</span></div></div>${slider(value,min,max,step,attrs,`${s.toUpperCase()} ${label}`)}<div class="scale"><span>${min} ${unit}</span><span>${max} ${unit}</span></div></section>`;
  }
  function toggleButton(action,side,label,enabled){
    return `<button type="button" class="state-button" data-action="${action}" data-side="${side}" aria-label="${side.toUpperCase()} ${label}" aria-pressed="${enabled}"><span>${label}</span><b>${enabled?'开启':'关闭'}</b></button>`;
  }
  function manualTdpControl(s){const [lo,hi]=manualBounds(),value=state.auto.tdp[s],attrs=`data-action="manual" data-key="tdp" data-side="${s}" ${lo===hi?'disabled':''}`;return `<section class="manual-tdp-control compact-control-row">
    <label class="compact-label" for="manual-tdp-slider-${s}"><span>TDP</span><span class="tdp-row-value">${readout(value,attrs,`${s.toUpperCase()} 当前 TDP`)}<span>W</span></span></label>
    <div class="compact-track">${slider(value,lo,hi,1,`id="manual-tdp-slider-${s}" ${attrs}`,`${s.toUpperCase()} 手动 TDP`)}<div class="scale manual-tdp-bounds"><span id="manual-tdp-lower">${lo} W</span><span id="manual-tdp-upper">${hi} W</span></div></div>
    ${budgetSelect()}
  </section>`;}
  function manualView(){const s=state.manual.side,p=manualProfile(s);return `
    ${sideSwitch('manual')}
    <section class="manual-board ${s}">
      ${manualTdpControl(s)}${frameCard(s,true)}<div class="divider"></div>
      <div class="two-columns"><div>${control(p.unlimited?'CPU 主频 · 不限制':'CPU 主频上限',p.freq,.8,7.2,.1,'freq',s,'GHz',p.unlimited?'disabled':'')}</div><div>${control('CPU 积极性',p.aggr,0,100,1,'aggr',s,'%')}</div></div>
      <div class="minimum-readback">CPU 最小状态 <b id="manual-minimum-readback">${manualPolicy(s).minState}%</b></div><div class="manual-actions-grid">${toggleButton('unlimited',s,'不限频率',p.unlimited)}${toggleButton('turbo',s,'睿频',p.turbo)}${manualCoreControl(s)}${gameThreadToggle('state-button')}</div>
    </section>`;
  }
  function render(){
    // Rendering a selected simulation device must not rewrite the shared AC/DC banks.
    // policy()/manualPolicy() already cap evaluated power to the device envelope.
    document.dispatchEvent(new Event('ymcc-preview-before-render'));
    const opened=$$('details[open]',app).map(d=>d.dataset.detail),focus=document.activeElement,meta=focus?.dataset.action?Object.fromEntries([...Object.entries(focus.dataset).filter(([k])=>!k.startsWith('gp')),...[['type',focus.type]]]):null;
    app.innerHTML=state.mode==='auto'?autoView():manualView();
    document.body.dataset.mode=state.mode;
    $$('.mode-switch [data-mode]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.mode===state.mode)));
    $('#fixture').value=state.fixture;
    $('#core-topology').value=coreTopologyScenario;
    paintPowerStatus();
    const modeNote=$('#mode-note');if(modeNote)modeNote.textContent='';
    for(const name of opened){const d=$(`[data-detail="${name}"]`,app);if(d)d.open=true;}
    if(meta){const next=$$('[data-action]',app).find(e=>Object.entries(meta).every(([k,v])=>k==='type'?e.type===v:e.dataset[k]===v));if(next)next.focus({preventScroll:true});}
    document.dispatchEvent(new Event('ymcc-preview-rendered'));
  }
  function paint(selector,value){if(state.mode==='manual'){const readback=$('#manual-minimum-readback');if(readback)readback.textContent=manualPolicy(state.manual.side).minState+'%';}$$(selector,app).forEach(input=>{input.value=input.tagName==='OUTPUT'&&input.dataset.key==='freq'?Number(value).toFixed(1):String(value);if(input.type==='range'){const min=Number(input.min),max=Number(input.max),pct=max===min?0:(value-min)/(max-min)*100;$('.fill',input.closest('.track-wrap')).style.setProperty('--position',pct+'%');}});}
  function paintPolicy(){
    if(state.mode!=='auto')return;
    const s=state.auto.side,p=policy(s);
    $('#auto-aggr-'+s).textContent=p.aggr+'%';
    $('#auto-cpu-'+s).textContent=cpuDescription(p);
    const turbo=$('#auto-turbo-'+s);turbo.dataset.enabled=String(p.turbo);turbo.textContent=p.turbo?'开睿频':'关睿频';turbo.title='睿频'+(p.turbo?'开启':'关闭');turbo.setAttribute('aria-label',s.toUpperCase()+' '+turbo.title);
    $('#auto-tdp-'+s).value=String(p.tdpTarget);
    paint(`[data-action="auto-tdp"][data-side="${s}"]`,p.tdpWatts);
    $('#float-label-'+s).textContent=floatDescription(p);
    paintCoreRule(s);
    paint(`[data-action="energy-saving"][data-side="${s}"]`,floatIndex(p.energySaving));
  }
  function setAutoTdp(s,raw){state.auto.tdp[s]=snap(raw,fixture().min,effectiveMaxTdp());paint(`[data-action="auto-tdp"][data-side="${s}"]`,state.auto.tdp[s]);paintPolicy();notice(`${s.toUpperCase()} 自动 TDP ${state.auto.tdp[s]}W；按 1W 调节。`);}
  function setEnergySaving(s,value){const keys=['none','small','medium','large','aggressive'];state.auto.energySaving=state.auto.energySaving||{};state.auto.energySaving[s]=keys[snap(value,0,4)];paintPolicy();notice(`${s.toUpperCase()} 浮动节能：${floatDescription(policy(s))}`);}
  function setBudget(raw){
    state.machine.maxTdp=snap(raw,fixture().min,maxRequest());
    enforceGlobalMax();
    if(state.mode==='auto'){
      $('#tdp-ceiling').innerHTML=budgetOptions();
      $$('input[data-action="auto-tdp"]',app).forEach(input=>input.max=effectiveMaxTdp());
      $('#auto-max-label').textContent=effectiveMaxTdp()+' W';
      paintPolicy();
    }else{
      const side=state.manual.side,[lo,hi]=manualBounds();
      $('#tdp-ceiling').innerHTML=budgetOptions();
      $$('input[data-action="manual"][data-key="tdp"]',app).forEach(input=>{input.min=lo;input.max=hi;input.disabled=lo===hi;});
      paint('[data-action="manual"][data-key="tdp"]',state.auto.tdp[side]);
      $('#manual-tdp-lower').textContent=lo+' W';$('#manual-tdp-upper').textContent=hi+' W';
    }
    notice('全局 TDP 上限 '+state.machine.maxTdp+'W；自动与手动共用，AC/DC 分别保留。');
  }
  function setBias(s,value){state.auto.bias[s]=snap(value,0,100,5);paint(`[data-action="bias"][data-side="${s}"]`,state.auto.bias[s]);paintPolicy();notice(`${s.toUpperCase()} 倾向已调整为 ${state.auto.bias[s]}，当前 TDP 仍为 ${policy(s).tdpWatts}W；另一侧的倾向与 CPU 策略保留。`);}
  function setFrame(s,raw){const f=state.frames[s];if(f.ceiling===0)return;f.fps=snap(raw,15,f.ceiling,5);f.lastFps=f.fps;paint(`[data-action="frame"][data-side="${s}"]`,f.fps);notice(`${s.toUpperCase()} 帧数目标 ${f.fps} FPS；另一侧不变。`);}
  function setCeiling(s,value){const f=state.frames[s];if(!CEILINGS.includes(value))return;
    if(value===0){if(f.fps>0)f.lastFps=f.fps;f.fps=0;f.ceiling=0;}
    else{f.ceiling=value;f.fps=Math.min(value,f.fps||f.lastFps||90);f.lastFps=f.fps;}
    render();notice(value===0?`${s.toUpperCase()} 上限为 0，锁帧已关闭；再次选择上限会恢复记忆值。`:`${s.toUpperCase()} 上限 ${value} FPS，当前目标 ${f.fps} FPS。`);
  }
  function setManual(s,key,raw){const p=manualProfile(s);let value;
    if(key==='tdp'){value=snap(raw,fixture().min,effectiveMaxTdp());state.auto.tdp[s]=value;}
    else if(key==='freq'){value=snap(raw,.8,7.2,.1);p.freq=value;}
    else{value=snap(raw,0,100);p[key]=value;}
    paint(`[data-action="manual"][data-side="${s}"][data-key="${key}"]`,value);
    notice(`${s.toUpperCase()} 手动调节：TDP ${state.auto.tdp[s]}W，CPU 参数与联动已同步。`);
  }
  function onValue(input,commit=false){
    const d=input.dataset;if(input.disabled)return;
    if(input.value.trim()===''||!Number.isFinite(Number(input.value))){render();notice('输入无效，已恢复原值。');return;}
    const value=Number(input.value);
    if(d.action==='bias')setBias(d.side,value);
    if(d.action==='energy-saving')setEnergySaving(d.side,value);
    if(d.action==='auto-tdp')setAutoTdp(d.side,value);
    if(d.action==='core-split')setCoreSplit(d.side,value);
    if(d.action==='frame')setFrame(d.side,value);
    if(d.action==='manual')setManual(d.side,d.key,value);
  }
  app.addEventListener('input',e=>{if(e.target.matches('input[type="range"]'))onValue(e.target);});
  app.addEventListener('change',e=>{const el=e.target,d=el.dataset,s=d.side;
    if(el.matches('input[type="range"]'))onValue(el,true);
    if(d.action==='ceiling')setCeiling(s,Number(el.value));
    if(d.action==='manual-core'&&CORE_MODES.some(mode=>mode.value===Number(el.value))){manualProfile(s).core=Number(el.value);render();}
    if(d.action==='budget-select')setBudget(Number(el.value));
    if(d.action==='core-rule-mode'&&['lowerMode','upperMode'].includes(d.band)&&coreMode(el.value)){state.auto.coreRules[s][d.band]=el.value;paintPolicy();notice(`${s.toUpperCase()} 核心区间方案已修改，当前 TDP ${state.auto.tdp[s]}W。`);}
  });
  app.addEventListener('click',e=>{const b=e.target.closest('button[data-action]');if(!b)return;const d=b.dataset;
    if(d.action==='unlimited'||d.action==='turbo'){const p=manualProfile(d.side);p[d.action]=!p[d.action];render();}
    if(d.action==='side'){state.auto.side=d.side;state.manual.side=d.side;render();notice(`正在编辑 ${d.side.toUpperCase()}；当前供电仍为 ${actualPowerSide.toUpperCase()}。`);}
    if(d.action==='game-thread'){const policy=gameThreadPolicy();if(!policy.available||!policy.exe||b.dataset.gameExe!==policy.exe)return;state.gameThreads[policy.exe]={hyperThreadMode:policy.hyperThreadMode==='off'?'on':'off'};render();notice(`${policy.exe} 超线程${state.gameThreads[policy.exe].hyperThreadMode==='on'?'开启':'关闭'}，按EXE保存，仅预览该游戏线程策略。`);}
    if(d.action==='core'&&CORE_MODES.some(mode=>mode.value===Number(d.value))){manualProfile(d.side).core=Number(d.value);render();}
  });
  app.addEventListener('pointerdown',e=>{if(e.target.matches('input'))return;const wrap=e.target.closest('.track-wrap');if(!wrap)return;const input=$('input[type="range"]',wrap);if(!input||input.disabled)return;const r=wrap.getBoundingClientRect(),ratio=clamp((e.clientX-r.left)/r.width,0,1),min=Number(input.min),max=Number(input.max),step=Number(input.step);input.value=String(min+Math.round(ratio*(max-min)/step)*step);onValue(input,true);if(input.isConnected)input.focus({preventScroll:true});});
  $$('.mode-switch [data-mode]').forEach(b=>b.addEventListener('click',()=>{state.mode=b.dataset.mode;render();notice(`已切换${state.mode==='auto'?'自动连续 TDP':'手动连续调节'}；帧数设置沿用 AC/DC 独立配置。`);}));
  $('#fixture').addEventListener('change',e=>{state.fixture=e.target.value;state.machine.maxTdp=fixture().initial;enforceGlobalMax();render();notice(`已切换演示条件。全局 TDP 上限 ${state.machine.maxTdp}W；自动按 1W 调节，自动与手动共用上限。`);});
  $('#core-topology').addEventListener('change',e=>{if(!Object.hasOwn(CORE_TOPOLOGY_SCENARIOS,e.target.value))return;coreTopologyScenario=e.target.value;render();notice('已切换核心演示数据，不代表当前机器拓扑或真实回读。');});
  $('#power-source').addEventListener('change',e=>{actualPowerSide=e.target.value;paintPowerStatus();notice(`模拟供电切换为 ${actualPowerSide.toUpperCase()}，编辑侧保持 ${state[state.mode].side.toUpperCase()}。`);});
  $('#theme').addEventListener('click',()=>{const light=document.documentElement.dataset.theme!=='light';document.documentElement.dataset.theme=light?'light':'dark';$('#theme').textContent=light?'深色':'浅色';});
  $('#reset').addEventListener('click',()=>{state=defaults();actualPowerSide='ac';coreTopologyScenario='unknown';topGame=clone(DEFAULT_TOP_GAME);render();notice('已重置示例：左侧 CPU、右侧 GPU；自动与手动共用全局 TDP，均可连续调节。');});
  function manualPolicy(side){
    const p=manualProfile(side);
    return {controlMode:'continuous',tdpWatts:clamp(state.auto.tdp[side],fixture().min,effectiveMaxTdp()),freq:p.unlimited?0:p.freq,aggr:p.aggr,epp:100-p.aggr,
      minState:window.YmccPerformancePolicy.minimumFromFrequency(p.unlimited?0:p.freq*1000,window.YmccBenchmarkData.PROFILES[state.simulationProfile??window.YmccBenchmarkData.DEFAULT_PROFILE].maxGHz),minStateSource:'cpu-frequency-cap',turbo:p.turbo,core:p.core,coreMode:CORE_MODES.find(mode=>mode.value===p.core).key,coreControl:{backend:'power-plan',scope:'system',mode:CORE_MODES.find(mode=>mode.value===p.core).key,hardwareApplied:false,bindingStatus:'pending-device-validation',processAffinity:false},throttle:throttleForFrequency((p.unlimited?0:p.freq)*1000)};
  }
  function snapshot(){
    const resolvedAutoPolicy={ac:policy('ac'),dc:policy('dc')};
    const resolvedManualPolicy={ac:manualPolicy('ac'),dc:manualPolicy('dc')};
    // The top-level game UI can consume both banks without mounting another editor here.
    const headerProfiles={gameThreads:clone(state.gameThreads),machineMaxTdp:state.machine.maxTdp,version:2,manualControl:'continuous',maximumSource:'global-tdp-ceiling',frames:clone(state.frames),
      auto:resolvedAutoPolicy,manual:resolvedManualPolicy};
    return {...clone(state),previewRuntime:{topGame:clone(topGame),coreTopologyScenario},tdpLimits:{applicationMaxWatts:APPLICATION_TDP_MAX,intelPl2OffsetWatts:1,intelMaxRequestedPl2Watts:APPLICATION_TDP_MAX+1,configuredMaxWatts:state.machine.maxTdp,defaultUnmatchedMaxWatts:120,previewMinWatts:fixture().min,previewMaxWatts:effectiveMaxTdp(),profileMaxWatts:maxRequest(),rangeSource:'selected-profile-release-envelope',machineMaximumKnown:false,hardwareReadback:false,hardwareApplied:false},coreObservation:coreObservation(),gameThreadPolicy:gameThreadPolicy(),gameThreadIntegration:{scope:'game-exe',targetSource:'existing-top-game-detector',profileOwner:'existing-top-game-custom-profile',nativeOperation:'game.corePolicy.apply',systemSmtChanged:false},coreScheduling:{backend:'power-plan',scope:'system',processAffinity:false,hardwareApplied:false,manualModes:CORE_MODES.map(mode=>({id:mode.value,key:mode.key,label:mode.label})),headerGameIntegration:'deferred',headerGameBackend:'power-plan',headerGameModeKeys:CORE_MODES.map(mode=>mode.key),automaticRule:'two-tdp-ranges',boundaryOwner:'lower'},referenceData:{status:'awaiting-user-confirmation',adoptedSources:[]},
      automaticControl:{minCpuMHz:window.YmccPerformancePolicy.MIN_AUTO_MHZ,stepWatts:1,biasStep:5,biasDirection:'cpu-to-gpu',cpuPriorityEndpoint:0,gpuPriorityEndpoint:100,gpuTurboOffBias:GPU_TURBO_OFF_BIAS,cpuIndependentOfTdp:true,cpuRangeControls:['bias','energy-saving'],minStateControl:'energy-saving',curveStatus:'user-approved',curveApprovedOn:'2026-10-15'},
      power:{source:'simulation',actualSide:actualPowerSide,editingSide:state[state.mode].side},
      activePolicy:{mode:state.mode,side:actualPowerSide,...(state.mode==='auto'?resolvedAutoPolicy:resolvedManualPolicy)[actualPowerSide]},
      resolvedAutoPolicy,resolvedManualPolicy,headerProfiles,applicationOrder:['tdp','cpu-policy'],
      optimizationTargets:{ac:state.frames.ac.fps||state.frames.ac.lastFps,dc:state.frames.dc.fps||state.frames.dc.lastFps}};
  }
  $('#save').addEventListener('click',()=>{try{publish();notice('草稿已保存：自动连续 TDP、手动连续调节、全局 TDP 上限、AC/DC 倾向与隐藏联动均保留。');}catch{notice('浏览器无法本地保存，可导出完整原型配置。');}});
  $('#export').addEventListener('click',()=>{const blob=new Blob([JSON.stringify(snapshot(),null,2)],{type:'application/json'}),url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download='ymcc-performance-design-v29.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);notice('已导出第二十九版原型配置，供方案评审使用。');});
  // Prototype-only feed for the existing top game's DetectedGame result. No discovery or polling.
  Object.defineProperty(window,'designPreview',{value:Object.freeze({setTopGame(game){
    if(game!==null&&(!game||!Number.isInteger(game.pid)||game.pid<=0||typeof game.processCreated!=='string'||!/^\d+$/.test(game.processCreated)||typeof game.name!=='string'||!game.name.toLowerCase().endsWith('.exe')||typeof game.path!=='string'))throw new TypeError('Expected the existing top game result or null');
    topGame=game===null?null:clone(game);render();publish();
  }})});
  let syncRevision=0,applyingStorage=false;try{syncRevision=Number(JSON.parse(localStorage.getItem(KEY)||'null')?.syncRevision)||0;}catch{}delete state.syncRevision;
  const syncState=()=>({...snapshot(),syncRevision});
  let lastSyncState=restored?JSON.stringify(snapshot()):'';
  function publish(force=false){
    if(applyingStorage)return;const fingerprint=JSON.stringify(snapshot());if(!force&&fingerprint===lastSyncState)return;
    lastSyncState=fingerprint;syncRevision=Math.max(Date.now(),syncRevision+1);if(!isSimulationDraft)try{localStorage.setItem(KEY,JSON.stringify(syncState()));}catch{}
    document.dispatchEvent(new CustomEvent('ymcc-performance-state',{detail:syncState()}));
  }
  function receiveStored(saved){const restoredState=restoreDraft(saved);if(!restoredState)return;applyingStorage=true;state=restoredState;topGame=saved.previewRuntime&&Object.hasOwn(saved.previewRuntime,'topGame')?clone(saved.previewRuntime.topGame):topGame;coreTopologyScenario=saved.previewRuntime?.coreTopologyScenario??saved.coreObservation?.scenario??coreTopologyScenario;for(const key of Object.keys(state))if(!Object.hasOwn(defaults(),key))delete state[key];state.simulationProfile=window.YmccBenchmarkData.isSelectableProfile(state.simulationProfile)?state.simulationProfile:window.YmccBenchmarkData.DEFAULT_PROFILE;syncRevision=Number(saved.syncRevision)||syncRevision;actualPowerSide=saved.power?.actualSide==='dc'?'dc':'ac';render();lastSyncState=JSON.stringify(snapshot());applyingStorage=false;document.dispatchEvent(new CustomEvent('ymcc-performance-state',{detail:syncState()}));}
  window.addEventListener('storage',event=>{if(!isSimulationDraft&&event.key===KEY&&event.newValue){try{receiveStored(JSON.parse(event.newValue));}catch{}}});
  const sourceApi={snapshot:syncState,
    loadSimulationDraft(saved){if(!isSimulationDraft)throw Error('仅临时副本可载入');if(!restoreDraft(saved))throw Error('参数快照无效');receiveStored(saved);return syncState();},
    updateSimulationRuntime(saved){
      if(!isSimulationDraft)throw Error('运行状态只同步到临时副本');
      const side=saved?.power?.actualSide,runtime=saved?.previewRuntime,game=runtime?.topGame;
      if(!['ac','dc'].includes(side)||!runtime||!Object.hasOwn(CORE_TOPOLOGY_SCENARIOS,runtime.coreTopologyScenario)||!Object.hasOwn(runtime,'topGame'))throw Error('运行状态无效');
      if(game!==null&&(!game||!Number.isInteger(game.pid)||game.pid<=0||typeof game.processCreated!=='string'||!/^\d+$/.test(game.processCreated)||typeof game.name!=='string'||!game.name.toLowerCase().endsWith('.exe')||typeof game.path!=='string'))throw Error('当前游戏身份无效');
      actualPowerSide=side;topGame=clone(game);coreTopologyScenario=runtime.coreTopologyScenario;render();publish();return syncState();
    },
    resetSimulationDraft(){if(!isSimulationDraft)throw Error('仅临时副本可重置');const fresh={...defaults(),fixture:state.fixture,simulationProfile:state.simulationProfile,previewRuntime:{topGame:clone(topGame),coreTopologyScenario},power:{actualSide:actualPowerSide}};const restoredState=restoreDraft(fresh);if(!restoredState)throw Error('默认参数无效');receiveStored({...restoredState,previewRuntime:fresh.previewRuntime,power:fresh.power});return syncState();},
    commitSimulationDraft(base,draft){
      if(isSimulationDraft)throw Error('临时副本不能保存正式参数');
      let stored=null;try{stored=JSON.parse(localStorage.getItem(KEY)||'null');}catch{}if(stored&&stored.syncRevision>syncRevision)receiveStored(stored);
      const merged=window.YmccSimulationSession.merge(base,draft,syncState());if(!merged.ok)return {ok:false,reason:'conflict',conflicts:merged.conflicts,snapshot:syncState()};
      if(!valid(merged.settings))return {ok:false,reason:'invalid-settings',snapshot:syncState()};
      const before=clone(state),oldRevision=syncRevision,oldFingerprint=lastSyncState;
      state=clone(merged.settings);render();syncRevision=Math.max(Date.now(),syncRevision+1);
      try{localStorage.setItem(KEY,JSON.stringify(syncState()));}catch(error){state=before;syncRevision=oldRevision;lastSyncState=oldFingerprint;render();return {ok:false,reason:'无法持久保存：'+error.message,snapshot:syncState()};}
      lastSyncState=JSON.stringify(snapshot());document.dispatchEvent(new CustomEvent('ymcc-performance-state',{detail:syncState()}));return {ok:true,snapshot:syncState()};
    },edit(command){
    let stored=null;if(!isSimulationDraft)try{stored=JSON.parse(localStorage.getItem(KEY)||'null');}catch{}if(stored&&stored.syncRevision>syncRevision)receiveStored(stored);
    const {key,value}=command||{},side=state[state.mode].side,p=manualProfile(side);
    if(key==='floatingPreset'){const fresh=window.YmccFloatingPresets.apply(state,value);state.auto=fresh.auto;state.frames=fresh.frames;}
    else if(key==='floatSearchMode'&&window.YmccFloatingPresets.SEARCH_MODES.includes(value))state.auto.floatSearchMode=value;
    else if(key==='mode'&&['auto','manual'].includes(value))state.mode=value;
    else if(key==='side'&&['ac','dc'].includes(value)){state.auto.side=value;state.manual.side=value;}
    else if(key==='profile'&&window.YmccBenchmarkData.isSelectableProfile(value))state.simulationProfile=value;
    else if(key==='simulationBudget'&&Number.isFinite(value))setAutoTdp(side,value);
    else if(key==='budget'&&Number.isFinite(value)){if(state.mode==='auto')setAutoTdp(side,value);else setManual(side,'tdp',value);}
    else if(key==='machineMax'&&Number.isFinite(value))setBudget(value);
    else if(key==='target'&&Number.isFinite(value))setFrame(side,value);
    else if(key==='ceiling'&&CEILINGS.includes(value))setCeiling(side,value);
    else if(key==='bias'&&Number.isFinite(value))setBias(side,value);
    else if(key==='saving'&&Object.hasOwn(SAVING_LEVELS,value)){state.auto.energySaving[side]=value;}
    else if(key==='manualMHz'&&Number.isFinite(value)){p.unlimited=value===0;if(value>0)setManual(side,'freq',value/1000);}
    else if(key==='manualAggressiveness'&&Number.isFinite(value))setManual(side,'aggr',value);
    else if(key==='manualTurbo'&&typeof value==='boolean')p.turbo=value;
    else if(key==='manualCoreMode'&&['bigFirst','bigOnly','littleFirst','littleOnly'].includes(value)){p.core={'bigFirst':0,'bigOnly':1,'littleFirst':3,'littleOnly':2}[value];}
    else if(key==='autoCoreThreshold'&&Number.isFinite(value))state.auto.coreRules[side].splitWatts=snap(value,0,maxRequest());
    else if(['autoCoreLow','autoCoreHigh'].includes(key)&&['bigFirst','bigOnly','littleFirst','littleOnly'].includes(value)){state.auto.coreRules[side][key==='autoCoreLow'?'lowerMode':'upperMode']={'bigFirst':'big','bigOnly':'only-big','littleFirst':'small','littleOnly':'only-small'}[value];}
    else if(['manualSmt','autoSmt'].includes(key)&&typeof value==='boolean'){const game=gameThreadPolicy(),expected=command.gameTarget;if(expected&&(!game.target||expected.exe!==game.exe||expected.pid!==game.target.pid||expected.processCreated!==game.target.processCreated))throw Error('当前游戏已变化，请重新调整超线程');if(game.available&&game.exe)state.gameThreads[game.exe]={hyperThreadMode:value?'on':'off'};}
    else return syncState();
    render();publish();return syncState();
  }};
  Object.defineProperty(window,'YmccPerformanceSource',{value:Object.freeze(sourceApi)});
  for(const event of ['input','change','click'])document.addEventListener(event,()=>queueMicrotask(()=>publish()));
  Object.defineProperty(window,'designSnapshot',{get:snapshot});
  render();publish();if(restored)notice('已恢复原型草稿：左侧 CPU、右侧 GPU，自动与手动共用全局 TDP 上限。');
})();



;
// Prototype adapter only. The direction resolver is generated from YMCC's
// existing spatial.ts; no production input loop or hardware writer is added.
(() => {
  'use strict';
  const main=document.querySelector('main'),selector='button,select,input[type="range"],summary',spatial=window.YmccSpatial;
  if(!main||!spatial)throw Error('Missing YMCC preview navigation dependency');
  let menu=null,editing=null,lastTarget=null,beforeRender=null,padIdentity=null,held=new Map();
  const visible=el=>!!el?.isConnected&&!el.disabled&&el.getAttribute('aria-disabled')!=='true'&&!el.closest('[hidden],[inert],[aria-hidden="true"]')&&el.getClientRects().length>0&&getComputedStyle(el).visibility!=='hidden'&&!el.closest('details:not([open])');
  const key=el=>el?.id||[el?.dataset.action||'mode',el?.dataset.mode,el?.dataset.side,el?.dataset.key,el?.dataset.band,el?.dataset.value].filter(v=>v!==undefined).join(':');
  const context=()=>{const s=window.designSnapshot;return s.mode+':'+s[s.mode].side;};
  function endEdit(commit=false){const previous=editing;editing=null;if(!previous)return;delete previous.el.dataset.gpEditing;if(commit&&visible(previous.el)&&previous.context===context())previous.el.dispatchEvent(new Event('change',{bubbles:true}));}
  function setup(){
    main.dataset.gpScope='performance-preview';
    const place=(query,row,lane)=>{for(const el of main.querySelectorAll(query)){const r=el.getBoundingClientRect();el.dataset.gpRow=String(row);el.dataset.gpCol=String(r.left+r.width/2);el.dataset.gpKey=key(el);if(lane)el.dataset.gpVerticalLane=lane;else delete el.dataset.gpVerticalLane;}};
    place('.mode-switch [data-mode],#simulation-open',0);place('[data-action="side"]',10);
    if(window.designSnapshot.mode==='auto'){
      place('input[data-action="auto-tdp"],#tdp-ceiling',20);place('input[data-action="frame"],select[data-action="ceiling"]',30);place('input[data-action="energy-saving"]',40);place('input[data-action="bias"]',50);place('input[data-action="core-split"]',60);place('select[data-action="core-rule-mode"],button[data-action="game-thread"]',70);
    }else{
      place('input[data-action="manual"][data-key="tdp"],#tdp-ceiling',20);place('input[data-action="frame"],select[data-action="ceiling"]',30);place('input[data-key="freq"]',40);place('.manual-switches button',50);place('input[data-key="aggr"]',60);place('button[data-action="core"][data-value="0"],button[data-action="core"][data-value="1"]',70);place('button[data-action="core"][data-value="3"],button[data-action="core"][data-value="2"]',80);
    }
  }
  function controls(){return [...(menu?menu.node:main).querySelectorAll(menu?'button':selector)].filter(el=>visible(el)&&(menu||el.dataset.gpKey));}
  function focus(el){if(!visible(el))return false;el.focus({preventScroll:true});const r=el.getBoundingClientRect();if(r.top<12||r.bottom>innerHeight-12)el.scrollIntoView({block:'nearest',inline:'nearest'});return document.activeElement===el;}
  function sameTarget(id){return [...main.querySelectorAll('[data-gp-key]')].find(el=>el.dataset.gpKey===id);}
  function repair(old=lastTarget){
    if(!old)return;const el=sameTarget(old.id);if(visible(el)){focus(el);return;}
    const fallback=old.action==='manual'&&old.key==='freq'?main.querySelector('[data-action="unlimited"]'):old.action==='frame'?main.querySelector('select[data-action="ceiling"]'):null;
    if(focus(fallback))return;
    const all=controls();all.sort((a,b)=>Math.abs(Number(a.dataset.gpRow)-old.row)-Math.abs(Number(b.dataset.gpRow)-old.row)||Math.abs(Number(a.dataset.gpCol)-old.col)-Math.abs(Number(b.dataset.gpCol)-old.col));focus(all[0]);
  }
  function sourceTarget(source){return source?.id?document.getElementById(source.id):source?.dataset.gpKey?sameTarget(source.dataset.gpKey):source;}
  function closeMenu(restore=true){if(!menu)return;const source=menu.source;menu.node.remove();menu=null;source.setAttribute('aria-expanded','false');if(restore)focus(sourceTarget(source));}
  function positionMenu(){if(!menu)return;if(!visible(menu.source)){closeMenu(false);return;}const r=menu.source.getBoundingClientRect(),node=menu.node;node.style.width=Math.min(innerWidth-16,Math.max(140,r.width))+'px';node.style.maxHeight=Math.max(80,Math.min(340,innerHeight-16))+'px';const height=node.getBoundingClientRect().height;node.style.left=Math.max(8,Math.min(r.left,innerWidth-node.getBoundingClientRect().width-8))+'px';node.style.top=(r.bottom+6+height<=innerHeight-8?r.bottom+6:Math.max(8,r.top-height-6))+'px';}
  function commitSelect(source,value){closeMenu(false);const active=sourceTarget(source);if(!active||active.disabled)return;focus(active);if(active.value!==value){active.value=value;active.dispatchEvent(new Event('change',{bubbles:true}));}focus(sourceTarget(active));}
  function openMenu(source){
    if(!visible(source)||menu?.source===source)return;endEdit();closeMenu(false);const node=document.createElement('div');node.className='controller-menu';node.setAttribute('role','listbox');node.setAttribute('aria-label',source.getAttribute('aria-label')||'选择');
    for(const option of source.options){const b=document.createElement('button');b.type='button';b.textContent=option.textContent;b.disabled=option.disabled;b.dataset.value=option.value;b.setAttribute('role','option');b.setAttribute('aria-selected',String(option.selected));b.addEventListener('click',()=>commitSelect(source,option.value));node.append(b);}
    if(!node.querySelector('button:not(:disabled)'))return;document.body.append(node);menu={source,node};source.setAttribute('aria-expanded','true');positionMenu();focus(node.querySelector('[aria-selected="true"]:not(:disabled)')||node.querySelector('button:not(:disabled)'));
  }
  function menuMove(delta){const all=controls();if(!all.length)return;const index=all.indexOf(document.activeElement);focus(all[index<0?0:(index+delta+all.length)%all.length]);}
  function move(dx,dy){
    const el=document.activeElement,all=controls();if(!all.includes(el)){focus(main.querySelector('.mode-switch [aria-pressed="true"]'));return;}
    const lane=dy?el.dataset.gpVerticalLane:null,laneCandidates=lane?all.filter(target=>target!==el&&target.dataset.gpVerticalLane===lane&&(Number(target.dataset.gpRow)-Number(el.dataset.gpRow))*dy>0):[];
    const next=spatial.spatialNavigationTarget(laneCandidates.length?[el,...laneCandidates]:all,el,{dx,dy});if(next)focus(next);
  }
  function adjust(el,delta,value){const min=Number(el.min),max=Number(el.max),next=Math.max(min,Math.min(max,value??Number((Number(el.value)+delta*Number(el.step||1)).toFixed(6))));if(next!==Number(el.value)){el.value=String(next);el.dispatchEvent(new Event('input',{bubbles:true}));}}
  function command(action){
    if(window.YmccSimulationPreview?.isOpen()){window.YmccSimulationPreview.forward(action);return;}
    if(editing&&(!visible(editing.el)||editing.el!==document.activeElement||editing.context!==context()))endEdit();
    if(menu){if(action==='back')closeMenu();else if(action==='up')menuMove(-1);else if(action==='down')menuMove(1);else if(action==='activate')document.activeElement?.click();return;}
    const el=document.activeElement;
    if(action==='back'){if(editing)endEdit();return;}
    if(!controls().includes(el)){focus(main.querySelector('.mode-switch [aria-pressed="true"]'));return;}
    if(action==='activate'){if(el.matches('input[type="range"]')){if(editing)endEdit(true);else{editing={el,context:context()};el.dataset.gpEditing='true';}}else if(el.tagName==='SELECT')openMenu(el);else el.click();return;}
    if(['left','right','up','down'].includes(action)){
      if(editing){if(action==='left'||action==='right')adjust(el,action==='left'?-1:1);else endEdit();return;}
      move(action==='left'?-1:action==='right'?1:0,action==='up'?-1:action==='down'?1:0);
    }
  }
  document.addEventListener('ymcc-preview-before-render',()=>{beforeRender=main.contains(document.activeElement)?lastTarget:null;endEdit();closeMenu(false);});
  document.addEventListener('ymcc-preview-rendered',()=>{setup();if(beforeRender){const old=beforeRender;beforeRender=null;repair(old);}});
  document.addEventListener('focusin',e=>{if(editing&&e.target!==editing.el)endEdit();if(main.contains(e.target)&&e.target.dataset.gpKey)lastTarget={id:e.target.dataset.gpKey,action:e.target.dataset.action,key:e.target.dataset.key,row:Number(e.target.dataset.gpRow),col:Number(e.target.dataset.gpCol)};});
  document.addEventListener('pointerdown',e=>{delete document.body.dataset.input;endEdit();const select=e.target.closest('select');if(select){e.preventDefault();focus(select);openMenu(select);}else if(menu&&!menu.node.contains(e.target))closeMenu(false);},true);
  document.addEventListener('click',e=>{const select=e.target.closest('select');if(select){e.preventDefault();openMenu(select);}});
  document.addEventListener('keydown',e=>{
    if(e.ctrlKey||e.altKey||e.metaKey)return;const el=document.activeElement;
    if(menu&&(e.key==='Home'||e.key==='End')){e.preventDefault();const all=controls();focus(e.key==='Home'?all[0]:all.at(-1));return;}
    if(menu&&e.key==='Tab'){e.preventDefault();menuMove(e.shiftKey?-1:1);return;}
    if(main.contains(el)&&el?.matches('input[type="range"]')&&['Home','End'].includes(e.key)){e.preventDefault();if(editing)adjust(el,0,Number(e.key==='Home'?el.min:el.max));return;}
    const action={ArrowUp:'up',ArrowDown:'down',ArrowLeft:'left',ArrowRight:'right',Escape:'back',Enter:'activate',' ':'activate'}[e.key];
    if(action&&(menu||main.contains(el)&&el?.matches(selector))){e.preventDefault();if(!e.repeat||!['activate','back'].includes(action))command(action);}
  });
  const relinquish=()=>{endEdit();closeMenu(false);held.clear();padIdentity=null;};
  window.addEventListener('blur',relinquish);document.addEventListener('visibilitychange',()=>{if(document.hidden)relinquish();});
  window.addEventListener('resize',()=>{setup();positionMenu();});window.addEventListener('scroll',positionMenu,true);
  new MutationObserver(()=>{setup();if(editing&&!visible(editing.el))endEdit();const current=document.activeElement;if(lastTarget&&(!current||current===document.body||main.contains(current)&&!visible(current)))repair();}).observe(document.querySelector('#app'),{childList:true,subtree:true,attributes:true,attributeFilter:['disabled','hidden','aria-hidden']});
  function poll(now){
    if(document.visibilityState==='visible'&&document.hasFocus()&&!window.YmccSimulationPreview?.isOpen()){
      let pads=[];try{pads=navigator.getGamepads?.()||[];}catch{}
      const pad=[...pads].find(p=>p?.connected&&p.mapping==='standard');
      if(pad){
        const pressed=i=>!!pad.buttons[i]?.pressed,id=String(pad.index??0)+':'+String(pad.id??'standard'),actions={up:pressed(12)||pad.axes[1]<-.6,down:pressed(13)||pad.axes[1]>.6,left:pressed(14)||pad.axes[0]<-.6,right:pressed(15)||pad.axes[0]>.6,activate:pressed(0),back:pressed(1)};
        if(id!==padIdentity){endEdit();held.clear();padIdentity=id;for(const [action,on]of Object.entries(actions))if(on)held.set(action,Infinity);}
        for(const [action,on]of Object.entries(actions)){if(!on){held.delete(action);continue;}const previous=held.get(action);if(previous===undefined||!['activate','back'].includes(action)&&now>=previous){document.body.dataset.input='gamepad';command(action);held.set(action,now+(previous===undefined?350:100));}}
        if(editing){const triggers=Number(pad.buttons[7]?.value??0)-Number(pad.buttons[6]?.value??0);if(Math.abs(triggers)>.55){const name='trigger',previous=held.get(name);if(previous===undefined||now>=previous){adjust(editing.el,triggers>0?1:-1);held.set(name,now+100);}}else held.delete('trigger');}
      }else if(padIdentity!==null){relinquish();}
    }else if(padIdentity!==null||editing)relinquish();
    requestAnimationFrame(poll);
  }
  setup();Object.defineProperty(window,'YmccPreviewController',{value:Object.freeze({command,controls,snapshot:()=>({editing:editing?.el.dataset.gpKey??null,menu:menu?.source.dataset.gpKey??menu?.source.id??null,contract:'native-spatial-with-preview-input-adapter'})})});requestAnimationFrame(poll);
})();

;
// Preview-only full-window route. There are no YMCC/native or hardware writes.
(() => {
  'use strict';
  const params=new URLSearchParams(location.search),clientOrigin=location.origin;
  function sourceMessage(target,requestId){target?.postMessage({type:'ymcc-simulation-source',snapshot:window.YmccPerformanceSource.snapshot(),requestId},clientOrigin);}
  function saveDraft(target,data){try{const result=window.YmccPerformanceSource.commitSimulationDraft(data.base,data.draft);target.postMessage({type:'ymcc-simulation-save-result',requestId:data.requestId,...result},clientOrigin);}catch(error){target.postMessage({type:'ymcc-simulation-save-result',requestId:data.requestId,ok:false,reason:error.message},clientOrigin);}}
  if(params.get('simulationBridge')==='1'){
    const isDraft=params.get('simulationDraft')==='1';let seeded=!isDraft;
    const send=requestId=>window.parent.postMessage({type:isDraft?'ymcc-simulation-draft-source':'ymcc-simulation-source',snapshot:window.YmccPerformanceSource.snapshot(),requestId},clientOrigin);
    window.addEventListener('message',event=>{if(event.origin!==clientOrigin||event.source!==window.parent)return;const data=event.data;
      try{
        if(isDraft&&data?.type==='ymcc-simulation-draft-start'){seeded=true;window.YmccPerformanceSource.loadSimulationDraft(data.snapshot);send(data.requestId);}
        else if(isDraft&&seeded&&data?.type==='ymcc-simulation-draft-reset'){window.YmccPerformanceSource.resetSimulationDraft();send(data.requestId);}
        else if(isDraft&&seeded&&data?.type==='ymcc-simulation-draft-edit'){window.YmccPerformanceSource.edit(data.command);send(data.requestId);}
        else if(isDraft&&seeded&&data?.type==='ymcc-simulation-draft-runtime'){window.YmccPerformanceSource.updateSimulationRuntime(data.snapshot);send(data.requestId);}
        else if(seeded&&data?.type===(isDraft?'ymcc-simulation-draft-request':'ymcc-simulation-source-request'))send(data.requestId);
        else if(!isDraft&&data?.type==='ymcc-simulation-source-save')saveDraft(window.parent,data);
      }catch(error){window.parent.postMessage({type:'ymcc-simulation-draft-error',requestId:data?.requestId,reason:error.message},clientOrigin);}
    });
    document.addEventListener('ymcc-performance-state',()=>{if(seeded)send();});
    if(isDraft)window.parent.postMessage({type:'ymcc-simulation-draft-ready'},clientOrigin);else send();return;
  }
  const entry=document.getElementById('simulation-open');
  const dialog=document.getElementById('simulation-window');
  const frame=document.getElementById('simulation-frame');
  const fallback=document.getElementById('simulation-fallback');
  const simulationOrigin=location.origin;
  let ready=false,timeout=null;
  const send=(action,command)=>frame.contentWindow?.postMessage({type:'ymcc-simulation-host',action,command},simulationOrigin);
  function close(){if(dialog.open)dialog.close();}
  function forward(command){
    if(!dialog.open)return;
    if(!ready){if(command==='back')close();return;}
    send('command',{activate:'a',back:'b'}[command]||command);
  }
  function connect(){
    ready=false;fallback.hidden=true;clearTimeout(timeout);
    frame.src=simulationOrigin+'/?embedded=1';
    timeout=setTimeout(()=>{if(!ready&&dialog.open){fallback.hidden=false;document.getElementById('simulation-back').focus();}},5000);
  }
  function open(){
    if(dialog.open)return;
    dialog.showModal();document.body.classList.add('simulation-open');
    if(!frame.hasAttribute('src'))connect();
    else if(ready){frame.contentWindow.postMessage({type:'ymcc-simulation-session-start',snapshot:window.YmccPerformanceSource.snapshot()},simulationOrigin);send('resume');frame.focus({preventScroll:true});}
    else connect();
  }
  entry.addEventListener('click',open);
  document.getElementById('simulation-retry').addEventListener('click',connect);
  document.getElementById('simulation-back').addEventListener('click',close);
  dialog.addEventListener('cancel',event=>{event.preventDefault();forward('back');});
  dialog.addEventListener('close',()=>{
    clearTimeout(timeout);send('suspend');fallback.hidden=true;
    document.body.classList.remove('simulation-open');entry.focus({preventScroll:true});
  });
  window.addEventListener('message',event=>{
    if(event.origin!==simulationOrigin||event.source!==frame.contentWindow)return;
    if(event.data?.type==='ymcc-simulation-ready'){
      ready=true;clearTimeout(timeout);fallback.hidden=true;frame.contentWindow.postMessage({type:'ymcc-simulation-session-start',snapshot:window.YmccPerformanceSource.snapshot()},simulationOrigin);
      send(dialog.open?'resume':'suspend');if(dialog.open)frame.focus({preventScroll:true});
    }else if(event.data?.type==='ymcc-simulation-source-save'){saveDraft(frame.contentWindow,event.data);
    }else if(event.data?.type==='ymcc-simulation-source-request'){sourceMessage(frame.contentWindow,event.data.requestId);
    }else if(event.data?.type==='ymcc-simulation-return'&&dialog.open)close();
  });
  document.addEventListener('ymcc-performance-state',()=>{if(ready)frame.contentWindow?.postMessage({type:'ymcc-simulation-source',snapshot:window.YmccPerformanceSource.snapshot()},simulationOrigin);});
  window.addEventListener('keydown',event=>{
    if(!dialog.open||event.defaultPrevented)return;
    const action={Escape:'back',Enter:'activate',' ':'activate',ArrowUp:'up',ArrowDown:'down',ArrowLeft:'left',ArrowRight:'right'}[event.key];
    if(action){event.preventDefault();if(!event.repeat||!['back','activate'].includes(action))forward(action);}
  });
  Object.defineProperty(window,'YmccSimulationPreview',{value:Object.freeze({open,close,forward,isOpen:()=>dialog.open,isReady:()=>ready})});
})();
