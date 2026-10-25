import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

// Frozen from selected current source and the original inventory; never derived
// from a result's PASS/FAIL mask. This module does not spawn or load a target.
const BASIS={
  "schemaVersion": 1,
  "caseCount": 123,
  "inventoryCount": 80,
  "activeTypes": 79,
  "automaticIdentityCases": 27,
  "diagnosticAdmissionCases": 79,
  "businessBudgetMs": 60000,
  "compileBudgetMs": 120000,
  "generationBudgetMs": 30000,
  "roleOrder": [
    "positive",
    "first-start",
    "readiness",
    "continuation",
    "native",
    "native-path"
  ],
  "sourceRows": [
    {
      "relative": "joined-factory-proof.mjs",
      "sha256": "d0c31361814fb667551689e9e85c3bff0ef2107bafa00c11fc0fd80502a9cc4a"
    },
    {
      "relative": "fault-payload-staging.mjs",
      "sha256": "58d91272f6ed82e56f29fd84465325592b31037a57f7e3ea3756bcb9eab55e43"
    },
    {
      "relative": "JoinedFactoryProbe.csproj",
      "sha256": "159aa79e6adb6e6d3064a9879095a1be49cff687ed39cea65c7d0a6e2faeaec3"
    },
    {
      "relative": "JoinedFactoryProbe.cs",
      "sha256": "da6fe877d16abc596d7d9ec9d7430a6f5c6727fe317e24b51f646140aadc3a1c"
    },
    {
      "relative": "JoinedFactoryEntry.cs",
      "sha256": "39e302e7937f9e3acfd1bfccbd23dcceea7a4dddfa6797127a337a6288a70f3d"
    },
    {
      "relative": "JoinedLifecycleCases.cs",
      "sha256": "47ac79b25f65e74e6bb46502b12ea4e5c986faa1f924bb1c3574e2376474e2f2"
    },
    {
      "relative": "JoinedMsiLifecycleCases.cs",
      "sha256": "6e502397f3d8da2aeb6b91ea8e60b3e00295b1e273bf2f52679149dbe7fcab3f"
    },
    {
      "relative": "JoinedMsiManualCases.cs",
      "sha256": "e08215e14da6bd07e665ac5bc4b894fe5c3274cbdc06c225815c89c79526131d"
    },
    {
      "relative": "JoinedMsiReplayCases.cs",
      "sha256": "e643de2aad9c43e736c199ba3457d3ea396ce0afb6ddeb5b768427286bad9ab3"
    },
    {
      "relative": "ConstructorIoPorts.csproj",
      "sha256": "3ef60c5b820fe91af571299873d90c1556e2db0cb2ad1d21000ba8c747302492"
    },
    {
      "relative": "ConstructorIoPorts.cs",
      "sha256": "e57d37ccdc3db699517ee670f1f7bd9f8ccc2a10cd9fa008690dcc1bd4cf3a0f"
    },
    {
      "relative": "LifecycleIoPorts.cs",
      "sha256": "dee1b4083a0c0b9f2accd95e9e23232b4155ee573ce1eae40a66d7cf41d78db9"
    },
    {
      "relative": "MsiLifecycleIoPorts.cs",
      "sha256": "318b3cfa4d37f85923d7b2f887bd59cc0105dc460e5482b505e88dd132c65d27"
    },
    {
      "relative": "MsiHidLoad.cs",
      "sha256": "b9de0032c36ffbe1e157360c5c45c20c083690e0c33ae8409e4c910b19a8a88d"
    },
    {
      "relative": "MsiManualIoPorts.cs",
      "sha256": "430e844eb828a78fa2e8e2885d9a917cc3a67040378dfff44f88c41708639b4e"
    },
    {
      "relative": "MsiReplayIoPorts.cs",
      "sha256": "b019a5d80615eded45564749f0b5d15e7ea3bd937cc0e0598be3c769a51388ba"
    },
    {
      "relative": "load-authority/ScopedLoadAuthority.csproj",
      "sha256": "16b8b66c9b7f7c161dd1b74bb3e38ca1a482c86323b39383e099ddfefa838089"
    },
    {
      "relative": "load-authority/ScopedLoadAuthority.cs",
      "sha256": "55553ce49d1a85dc517e8248b55745383b29fe53aa41fc7606b69173119c5275"
    },
    {
      "relative": "load-authority/ExpectedAbsentProbe.cs",
      "sha256": "21f240014b2b6d22355665da5e5d7e34c1db58d4431ffd3892d24a880a43d2b5"
    },
    {
      "relative": "load-authority/ToolDefaultLoads.cs",
      "sha256": "d16d06e2821aa4099838d33c48f8e4e037b2da94e7e615987b6588f2b5bc15c3"
    },
    {
      "relative": "load-authority/PinnedPreEntryResolver.cs",
      "sha256": "21985dc1df6854b9d7ccf5046393c004d9bdccb093a1b954b317634a47b9a8aa"
    },
    {
      "relative": "load-authority/ToolRuntime.props",
      "sha256": "0f0fee36ae5450365e8206cf7dff886ae5e9297db2d2f8d58cf8586d07ae5899"
    },
    {
      "relative": "Harness.cs",
      "sha256": "c362b198a0ba0c095bbada1028ed01c5ece13bdd410a90adefd14fd8a3b4def8"
    },
    {
      "relative": "JoinedFactoryInstrumentation.cs",
      "sha256": "a5d5806701ad15e693aa15825a60e154caf2001e71e87af7a627f366332ba032"
    },
    {
      "relative": "FactoryCases.cs",
      "sha256": "bb62318b9984696ccc9af34bfd844fdfb43b5f27cafd5103d8f3c637ca2b123b"
    },
    {
      "relative": "InitializationCases.cs",
      "sha256": "c288b970a29ff1e5680ff71e6dd28603711d51afd12376f7bf37f1c575573b75"
    },
    {
      "relative": "ConstructorCases.cs",
      "sha256": "95bd9e9f30687c5b94f97a4e56432ced75ddb808dc40a8e74d32c3ba5e83e4b7"
    }
  ],
  "inventoryRows": [
    {
      "type": "HandheldCompanion.Devices.MinisforumV3",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.LokiMiniPro",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.LokiZero",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.LokiMax6600U",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.LokiMax6800U",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AOKZOEA1",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AOKZOEA1Pro",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AOKZOEA1X",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AOKZOEA2",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEOAIR",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEOAIRPro",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEOAIR1S",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEOAIRLite",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEO2021",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEO2021Pro",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEOKUN",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEOSlide",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEONEXT",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEONEXTLite",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEONEXTLite4500U",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEO2",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEOAIRPlusAMD",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEOAIRPlusAMDMendocino",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEOAIRPlusIntel",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEO2S",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEOFlipKB",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEOFlipDS",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEO.AYANEOFlip1SDS",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.AYANEO.AYANEOFlip1SKB",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.SuiPlay0X1",
      "inactive": true
    },
    {
      "type": "HandheldCompanion.Devices.GPDWin2",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWinMini_7640U",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWinMini",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWinMini_8840U",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWinMini_HX370",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWin3",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWin4",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWin4_2023_7640U",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWin4_2023",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWin4_2024_8640U",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWin4_2024",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWin4_2024_HX370",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWin5",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWinMax2Intel",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWinMax2_2022_6800U",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWinMax2_2023_7640U",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWinMax2_2023_7840U",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWinMax2_2024_8640U",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWinMax2_2024_8840U",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.GPDWinMax2_2024_HX370",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.OneXPlayerX1Intel",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.OneXPlayerX1AMD",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.OneXPlayerX1Mini",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.OneXPlayerX1Pro",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.OneXPlayer.OneXPlayerApex",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.OneXPlayerG1Intel",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.OneXPlayerG1AMD",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.OneXPlayerOneXFly",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.OneXPlayerOneXFlyF1Pro",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.OneXPlayerMiniAMD",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.OneXPlayerMiniIntel",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.OneXPlayerMiniPro",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.OneXPlayer2",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.OneXPlayer2Pro",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.ROGAlly",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.ROGAllyX",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.XboxROGAlly",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.XboxROGAllyX",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.ASUS.ASUS",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.SteamDeck",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.Lenovo.LegionGoTablet",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.Lenovo.LegionGoTablet2",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.Lenovo.LegionGoSZ2",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.Lenovo.LegionGoSZ1",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.ClawA1M",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.ClawA2VM",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.ClawBZ2EM",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.MSI.ClawCG3EM",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.Zotac.GamingZone",
      "inactive": false
    },
    {
      "type": "HandheldCompanion.Devices.DefaultDevice",
      "inactive": false
    }
  ],
  "models": [
    [
      "ASUSTeK COMPUTER INC.",
      "RC71L",
      "unused",
      "ROGAlly"
    ],
    [
      "ASUSTEK COMPUTER INC.",
      "RC72LA",
      "unused",
      "ROGAllyX"
    ],
    [
      "ASUSTEK COMPUTER INC.",
      "RC73YA",
      "unused",
      "XboxROGAlly"
    ],
    [
      "ASUSTEK COMPUTER INC.",
      "RC73XA",
      "unused",
      "XboxROGAllyX"
    ],
    [
      "ASUSTEK COMPUTER INC.",
      "unknown-asus",
      "unused",
      "ASUS"
    ],
    [
      "AYANEO",
      "FLIP DS",
      "unused",
      "AYANEOFlipDS"
    ],
    [
      "AYANEO",
      "FLIP KB",
      "unused",
      "AYANEOFlipKB"
    ],
    [
      "AYANEO",
      "FLIP 1S DS",
      "unused",
      "AYANEOFlip1SDS"
    ],
    [
      "AYANEO",
      "FLIP 1S KB",
      "unused",
      "AYANEOFlip1SKB"
    ],
    [
      "ONE-NETBOOK",
      "ONEXPLAYER X1 i",
      "unused",
      "OneXPlayerX1Intel"
    ],
    [
      "ONE-NETBOOK TECHNOLOGY CO., LTD.",
      "ONEXPLAYER X1 A",
      "unused",
      "OneXPlayerX1AMD"
    ],
    [
      "ONE-NETBOOK",
      "ONEXPLAYER X1z",
      "unused",
      "OneXPlayerX1AMD"
    ],
    [
      "ONE-NETBOOK",
      "ONEXPLAYER X1Pro",
      "unused",
      "OneXPlayerX1Pro"
    ],
    [
      "ONE-NETBOOK",
      "ONEXPLAYER X1 mini",
      "unused",
      "OneXPlayerX1Mini"
    ],
    [
      "ONE-NETBOOK",
      "ONEXPLAYER APEX",
      "unused",
      "OneXPlayerApex"
    ],
    [
      "ONE-NETBOOK",
      "ONEXPLAYER G1 i",
      "unused",
      "OneXPlayerG1Intel"
    ],
    [
      "ONE-NETBOOK",
      "ONEXPLAYER G1 A",
      "unused",
      "OneXPlayerG1AMD"
    ],
    [
      "Valve",
      "Jupiter",
      "unused",
      "SteamDeck"
    ],
    [
      "Valve",
      "Galileo",
      "unused",
      "SteamDeck"
    ],
    [
      "LENOVO",
      "different-product",
      "83E1",
      "LegionGoTablet"
    ],
    [
      "LENOVO",
      "different-product",
      "83N1",
      "LegionGoTablet2"
    ],
    [
      "LENOVO",
      "different-product",
      "83L3",
      "LegionGoSZ2"
    ],
    [
      "LENOVO",
      "different-product",
      "83N6",
      "LegionGoSZ1"
    ],
    [
      "MICRO-STAR INTERNATIONAL CO., LTD.",
      "MS-1T42",
      "unused",
      "ClawA2VM"
    ],
    [
      "MICRO-STAR INTERNATIONAL CO., LTD.",
      "MS-1T91",
      "unused",
      "ClawCG3EM"
    ],
    [
      "ZOTAC",
      "G0A1W",
      "unused",
      "GamingZone"
    ],
    [
      "unknown-vendor",
      "unknown-product",
      "unknown-model",
      "DefaultDevice"
    ]
  ],
  "failureMasks": {
    "first-start": [
      "joined-auto:AYANEOFlipDS:FLIP DS",
      "joined-auto:AYANEOFlip1SDS:FLIP 1S DS",
      "joined-diagnostic:HandheldCompanion.Devices.AYANEOFlipDS"
    ],
    "readiness": [
      "joined-retry:device.sensors",
      "joined-reentrant-handshake",
      "joined-dispose-during-original-stage",
      "joined-owner-replacement:device",
      "joined-owner-replacement:assembly",
      "joined-disposed-factory-stays-refused"
    ],
    "continuation": [
      "joined-retry:board:baseboard:Manufacturer",
      "joined-retry:device.sensors"
    ],
    "native": [
      "joined-native:missing",
      "joined-native:invalid",
      "joined-native:long-missing",
      "joined-native:long-invalid"
    ],
    "native-path": [
      "joined-native:long-valid"
    ]
  },
  "transformSignatures": {
    "hc": [
      "System.Void HandheldCompanion.Devices.IDevice::PullSensors()",
      "System.Void HandheldCompanion.Managers.SettingsManager::SetProperty(System.String,System.Object,System.Boolean,System.Boolean)"
    ],
    "host": [
      "System.Void YeManFanHost.RealHcBackend::ConfigureHcRuntime()",
      "System.Void YeManFanHost.RealHcBackend::ValidateDeploymentGenerationForHcLoad()",
      "System.Void YeManFanHost.RealHcBackend::ValidateFormalSessionCapabilityAcl()"
    ],
    "original7": [
      "System.Boolean HandheldCompanion.Devices.IDevice::GetPhysicallyInstalledSystemMemory(System.UInt64&)",
      "System.Boolean HandheldCompanion.Devices.OneXPlayerX1::CheckIsBatteryProtectionSupported()",
      "System.Boolean HandheldCompanion.Devices.SteamDeck::Open()",
      "System.Object HandheldCompanion.Managers.SettingsManager::GetProperty(System.String,System.Boolean)",
      "System.Object HandheldCompanion.MotherboardInfo::queryCacheValue(System.String,System.String)",
      "System.Void HandheldCompanion.Managers.ManagerFactory::.cctor()",
      "System.Void HandheldCompanion.MotherboardInfo::.cctor()"
    ]
  }
};
function deepFreeze(v){if(v&&typeof v==='object'){Object.freeze(v);for(const x of Object.values(v))deepFreeze(x);}return v;}
export const JOINED_ORIGINAL123_BASIS=deepFreeze(BASIS);
const oracles=new WeakSet();
function refuse(code,detail){throw Object.assign(new Error(code),{code,detail,joinedOriginal123Blocked:true});}
function need(v,code,detail){if(!v)refuse(code,detail);}
const sameJson=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const full=p=>path.resolve(p).toUpperCase();
const same=(a,b)=>typeof a==='string'&&typeof b==='string'&&full(a)===full(b);
const sorted=a=>[...a].sort();
export const joinedFileHash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
export function checkJoinedPin(p){need(p&&typeof p.path==='string'&&path.isAbsolute(p.path)&&/^[a-f0-9]{64}$/.test(p.sha256??''),'JOINED123_PIN_SHAPE',p);need(fs.existsSync(p.path)&&fs.statSync(p.path).isFile(),'JOINED123_PIN_MISSING',p);need(joinedFileHash(p.path)===p.sha256,'JOINED123_PIN_CHANGED',p);return p;}
const read=p=>JSON.parse(fs.readFileSync(checkJoinedPin(p).path,'utf8').replace(/^\uFEFF/,''));
function exactPin(a,b,code){checkJoinedPin(a);checkJoinedPin(b);need(same(a.path,b.path)&&a.sha256===b.sha256,code,{a,b});}
function segment(t,start,end,code){const at=t.indexOf(start),last=t.indexOf(end,at+start.length);need(at>=0&&last>at&&t.indexOf(start,at+1)<0,code);return t.slice(at,last+end.length);}
function strings(t){return [...t.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map(m=>JSON.parse('"'+m[1]+'"'));}
function knownOracle(o){need(oracles.has(o),'JOINED123_SOURCE_ORACLE_REQUIRED');}
function sourceBodies(sourcePins){need(Array.isArray(sourcePins)&&sourcePins.length===BASIS.sourceRows.length,'JOINED123_SOURCE_SET');const out={};for(const r of sourcePins){need(!out[r.relative],'JOINED123_SOURCE_DUPLICATE',r.relative);const expected=BASIS.sourceRows.find(x=>x.relative===r.relative);need(expected&&r.sha256===expected.sha256,'JOINED123_SOURCE_NOT_REVIEWED',r.relative);checkJoinedPin(r);out[r.relative]=fs.readFileSync(r.path,'utf8');}return out;}
function deriveSourceOracle(t,rows){
 const model=segment(t['JoinedFactoryProbe.cs'],'static readonly (string vendor,string product,string model,string expected)[] Models=[','];','JOINED123_MODELS_SOURCE');
 const tuples=[...model.matchAll(/\("((?:[^"\\]|\\.)*)","((?:[^"\\]|\\.)*)","((?:[^"\\]|\\.)*)","((?:[^"\\]|\\.)*)"\)/g)].map(m=>m.slice(1).map(s=>JSON.parse('"'+s+'"')));
 need(sameJson(tuples,BASIS.models),'JOINED123_MODELS_CHANGED');
 const matrix=segment(t['JoinedFactoryProbe.cs'],'static void RunMatrix(string inventory)','    static void RunFailures()','JOINED123_MATRIX_SOURCE');
 for(const literal of ['rows.Where(r=>r!["inactive"]!.GetValue<bool>()==false)','Run("joined-auto:"+row.expected+":"+row.product','Run((nested?"joined-diagnostic-refused:":"joined-diagnostic:")+type','Run("joined-all-admitted-models-loaded"'])need(matrix.includes(literal),'JOINED123_MATRIX_SEMANTICS',literal);
 const tail=segment(t['JoinedFactoryProbe.cs'],'static void RunFailures()','    [STAThread]','JOINED123_FAILURE_SOURCE');
 const kinds=strings((/foreach\(var kind in new\[\]\{([^}]+)\}\)Run\("joined-native:"/.exec(tail)??[])[1]??'');
 const stages=strings((/foreach\(var stage in new\[\]\{([^}]+)\}\)Run\("joined-retry:"/.exec(tail)??[])[1]??'');
 const owners=strings((/foreach\(var kind in new\[\]\{([^}]+)\}\)Run\("joined-owner-replacement:"/.exec(tail)??[])[1]??'');
 need(sameJson(kinds,['missing','invalid','long-valid','long-missing','long-invalid']),'JOINED123_NATIVE_KINDS');
 need(sameJson(stages,['validation.acl','validation.payload','runtime.temp-root','board:baseboard:Manufacturer','device.sensors']),'JOINED123_RETRY_STAGES');
 need(owners.slice(0,2).join('|')==='device|assembly','JOINED123_OWNER_KINDS');
 const proof=t['joined-factory-proof.mjs'],failureFragment=segment(proof,'const failures={','};','JOINED123_MASK_SOURCE'),masks={};
 for(const m of failureFragment.matchAll(/(?:'([^']+)'|([a-z]+)):\[([^\]]*)\]/g))masks[m[1]??m[2]]=[...m[3].matchAll(/'([^']*)'/g)].map(x=>x[1]);
 need(sameJson(masks,BASIS.failureMasks),'JOINED123_MASKS_CHANGED');
 const roleFragment=segment(proof,"for(const mode of [",']','JOINED123_ROLE_SOURCE'),roles=[...roleFragment.matchAll(/'([^']*)'/g)].map(m=>m[1]);need(sameJson(roles,BASIS.roleOrder),'JOINED123_ROLES_CHANGED');
 need(proof.includes("relative(path.join(dir,'run.log')),60000,mode==='positive'?0:2)"),'JOINED123_BUDGET_OR_EXIT_CHANGED');
 for(const s of ['result.cases.length!==123','result.uniqueConstructedTypes.length!==79','result.deniedNative!==0','result.nativeMathBindings<=0','result.legacyFixtureReferenced!==false','result.managerStartCalled!==false','result.actualDeviceOpenBodyExecuted!==false','result.hostMainStarted!==false','result.hardwareIo!==false','result.nativeBoundaryFiles?.length!==1'])need(proof.includes(s),'JOINED123_ORIGINAL_GATE_CHANGED',s);
 for(const s of ['Check(file.Length==259','259-BoundaryRoot.Length-2-"GamepadMotion.dll".Length','finally{End(backend);}','finally{DisposeDevice(device);}','[STAThread]','else{RunMatrix(args[5]);RunFailures();}'])need(t['JoinedFactoryProbe.cs'].includes(s),'JOINED123_TERMINAL_SOURCE_CHANGED',s);
 const active=rows.filter(r=>r.inactive===false),automatic=tuples.map(r=>'joined-auto:'+r[3]+':'+r[1]);
 const diagnostics=active.map(r=>(r.type.slice(0,r.type.lastIndexOf('.'))!=='HandheldCompanion.Devices'?'joined-diagnostic-refused:':'joined-diagnostic:')+r.type);
 const tailIds=['joined-all-admitted-models-loaded',...kinds.map(k=>'joined-native:'+k),'joined-platform-unsupported-sensors',...stages.map(k=>'joined-retry:'+k),'joined-reentrant-handshake','joined-dispose-during-original-stage',...['device','assembly'].map(k=>'joined-owner-replacement:'+k),'joined-disposed-factory-stays-refused'];
 const ids=[...automatic,...diagnostics,...tailIds];need(ids.length===123&&new Set(ids).size===123,'JOINED123_ORACLE_NOT123');
 for(const idsForRole of Object.values(masks))need(idsForRole.every(id=>ids.includes(id)),'JOINED123_MASK_UNKNOWN_ID');
 return{ids,automaticIds:automatic,diagnosticIds:diagnostics,tailIds,activeTypes:active.map(r=>r.type),masks,roles};
}
function projectClosure(t){
 const projects=[],sources=new Set(),inputs=new Set(),visited=new Set();
 function visit(file){need(!visited.has(file),'JOINED123_PROJECT_CYCLE',file);visited.add(file);need(typeof t[file]==='string','JOINED123_PROJECT_INPUT_MISSING',file);inputs.add(file);const body=t[file];
  need(!body.includes('EnableDefaultCompileItems>true'),'JOINED123_DEFAULT_COMPILE_FORBIDDEN');if(file.endsWith('.csproj'))projects.push(file);
  for(const m of body.matchAll(/<(Compile|ProjectReference|Import)\s+(?:Include|Project)="([^"]+)"/g)){
   need(!/[$*?;]/.test(m[2]),'JOINED123_DYNAMIC_PROJECT_INPUT',m[2]);const resolved=path.posix.normalize(path.posix.join(path.posix.dirname(file),m[2].replaceAll('\\','/')));need(!resolved.startsWith('..')&&typeof t[resolved]==='string','JOINED123_PROJECT_OUTSIDE_REVIEW',resolved);inputs.add(resolved);
   if(m[1]==='Compile')sources.add(resolved);else if(!visited.has(resolved))visit(resolved);
  }
 }
 visit('JoinedFactoryProbe.csproj');need(projects.length===3,'JOINED123_PROJECT_GRAPH_CHANGED');need(t['JoinedFactoryProbe.csproj'].includes('<StartupObject>JoinedFactoryEntry</StartupObject>')&&t['JoinedFactoryProbe.csproj'].includes('<FrameworkReference Include="Microsoft.WindowsDesktop.App.WPF"/>'),'JOINED123_JOINED_PROJECT_SHAPE');
 need(t['load-authority/ToolRuntime.props'].includes('<RuntimeFrameworkVersion>10.0.11</RuntimeFrameworkVersion><RollForward>Disable</RollForward>'),'JOINED123_RUNTIME_POLICY');
 return{projects,sources:[...sources].sort(),inputs:[...inputs].sort(),targetFramework:'net10.0',frameworkReference:'Microsoft.WindowsDesktop.App.WPF',runtimeVersion:'10.0.11',rollForward:'Disable',startupObject:'JoinedFactoryEntry'};
}
export function freezeJoinedOriginal123Oracle({sourcePins,inventoryPin}){
 const t=sourceBodies(sourcePins),inventory=read(inventoryPin);need(Array.isArray(inventory.rows),'JOINED123_INVENTORY_SHAPE');const rows=inventory.rows.map(r=>({type:r.type,inactive:r.inactive}));
 need(sameJson(rows,BASIS.inventoryRows),'JOINED123_INVENTORY_TYPE_ORDER_CHANGED');
 const derived=deriveSourceOracle(t,rows),graph=projectClosure(t);
 const value=deepFreeze({schemaVersion:1,status:'FROZEN_SOURCE123_NOT_RUNTIME_PASS',...derived,sourcePins:sourcePins.map(p=>({...p})),inventoryPin:{...inventoryPin},compileGraph:graph,businessBudgetMs:60000,expectedExit:{positive:0,negative:2},initializeRole:{status:'UNKNOWN',reason:'Current joined-factory-proof has readiness, not initialize. No implicit alias or invented mask is accepted.'},cleanup:{sourceFinally:true,perOwnerExportedCount:'UNKNOWN_NO_EXPORTED_COUNTER'},sourceOrderStricterThanOriginalSortedSetCheck:true});oracles.add(value);return value;
}
export function verifyJoinedOriginal123SourceInputs(oracle,{includeInventory=true}={}){knownOracle(oracle);sourceBodies(oracle.sourcePins);if(includeInventory)checkJoinedPin(oracle.inventoryPin);return{status:'PASS_EXACT_SELECTED_SOURCE_INPUT_PINS',runtimeApproved:false};}
export function joinedOriginal123Role(oracle,role){knownOracle(oracle);need(oracle.roles.includes(role),role==='initialize'?'JOINED123_UNKNOWN_INITIALIZE_ROLE':'JOINED123_UNKNOWN_ROLE',role);return{role,expectedExit:role==='positive'?0:2,failedIds:role==='positive'?[]:[...oracle.masks[role]],markers:role==='positive'?[]:role==='first-start'?['JOINED_SETTING_WRITE_REFUSED:']:role==='continuation'?['cannot replay completed runtime configuration']:role==='native-path'?['GamepadMotion.dll could not load']:role==='native'?['Joined consumer must refuse GamepadMotion.dll']:['cannot publish readiness','Joined consumer must refuse HC_NOT_INITIALIZED','cannot publish late readiness','Replacement cannot inherit','Joined consumer must refuse ObjectDisposedException']};}
export function assessJoinedOriginal123Result({oracle,role,result,hostSha256,hcSha256,nativeMathSha256,resultPath}){
 knownOracle(oracle);const spec=joinedOriginal123Role(oracle,role),r=result;
 need(r&&Array.isArray(r.cases),'JOINED123_RESULT_SHAPE');need(sameJson(r.cases.map(c=>c.id),oracle.ids),'JOINED123_CASE_IDS_OR_ORDER');
 need(r.cases.every(c=>['PASS','FAIL'].includes(c.status)&&Number.isInteger(c.checks)&&c.checks>=0)&&Number.isInteger(r.checks)&&r.checks>=0,'JOINED123_CASE_STATUS_OR_CHECKS');
 const failed=r.cases.filter(c=>c.status==='FAIL');need(sameJson(sorted(failed.map(c=>c.id)),sorted(spec.failedIds)),role==='positive'?'JOINED123_POSITIVE_FAILED':'JOINED123_EXACT_FAILURE_MASK',{role,expected:spec.failedIds,observed:failed.map(c=>c.id)});
 need(r.status===(role==='positive'?'PASS':'FAIL'),'JOINED123_AGGREGATE_STATUS');
 need(failed.every(c=>typeof c.error==='string'&&spec.markers.some(s=>c.error.includes(s))),'JOINED123_INTENDED_ERROR_FRAGMENT');
 need(/^[a-f0-9]{64}$/.test(hostSha256)&&r.hostSha256===hostSha256&&/^[a-f0-9]{64}$/.test(hcSha256)&&r.hcSha256===hcSha256,'JOINED123_HOST_HC_PIN');
 need(Array.isArray(r.uniqueConstructedTypes)&&r.uniqueConstructedTypes.length===79&&new Set(r.uniqueConstructedTypes).size===79&&sameJson(sorted(r.uniqueConstructedTypes),sorted(oracle.activeTypes)),'JOINED123_EXACT79_CONSTRUCTED_TYPES');
 for(const key of ['legacyFixtureReferenced','managerStartCalled','actualDeviceOpenBodyExecuted','actualOriginalMsiBaseOpenExecuted','hostMainStarted','hardwareIo','publicEntry','originalPublicLoadAndHandshakeExecuted','actualStaMessagePumpExecuted','lifecycleMode','msiLifecycleMode','msiManualMode','msiReplayMode'])need(r[key]===false,'JOINED123_FORBIDDEN_SCOPE_FLAG',key);
 for(const key of ['actualDefaultAssemblyLoadExecuted','actualOriginalGetCurrentAndConstructorsExecuted','actualConfigureAndNativeAdmissionExecuted','actualIdentityCapabilityAndAuthorizationExecuted'])need(r[key]===true,'JOINED123_ORIGINAL_EXECUTION_FLAG',key);
 need(r.deniedNative===0&&Array.isArray(r.nativeDenials)&&r.nativeDenials.length===0&&Number.isInteger(r.nativeMathBindings)&&r.nativeMathBindings>0,'JOINED123_NATIVE_SCOPE');
 need(Array.isArray(r.probeReferences)&&!r.probeReferences.includes('HandheldCompanion')&&!r.probeReferences.includes('OneXFanCoexistence'),'JOINED123_FOREIGN_STATIC_REFERENCE');
 need(r.forcedThreadCleanup===0&&Array.isArray(r.threadEvidence)&&r.threadEvidence.length===0&&Array.isArray(r.lifecycleIo)&&r.lifecycleIo.length===0&&r.readOnlyOsThreadIdentityCalls===0&&r.inertPowerLineReads===0,'JOINED123_CLEANUP_OR_NONPUBLIC_SCOPE');
 need(typeof resultPath==='string'&&path.isAbsolute(resultPath),'JOINED123_RESULT_PATH');const boundaryRoot=path.join(path.dirname(resultPath),'n');need(typeof r.nativeBoundaryRoot==='string'&&path.resolve(r.nativeBoundaryRoot)===boundaryRoot&&boundaryRoot.length<239,'JOINED123_NATIVE_BOUNDARY_ROOT');
 need(Array.isArray(r.nativeBoundaryFiles)&&r.nativeBoundaryFiles.length===1,'JOINED123_ONE_NATIVE_BOUNDARY_FILE');
 const boundary=r.nativeBoundaryFiles[0];need(boundary&&typeof boundary.path==='string'&&path.isAbsolute(boundary.path)&&boundary.path.length===259&&path.basename(boundary.path)==='GamepadMotion.dll'&&path.dirname(boundary.path).startsWith(boundaryRoot+path.sep)&&path.relative(boundaryRoot,boundary.path).split(path.sep).length===2&&boundary.sha256===nativeMathSha256&&/^[a-f0-9]{64}$/.test(nativeMathSha256),'JOINED123_NATIVE259_EXACT_PIN');
 return{status:'PASS_ORIGINAL123_RESULT_CONTRACT_ONLY',role,caseCount:123,automaticIdentityCases:27,diagnosticAdmissionCases:79,uniqueOriginalTypes:79,checks:r.checks,failedIds:failed.map(c=>c.id),expectedExit:spec.expectedExit,nativeBoundaryFiles:[{...boundary}],cleanup:{sourceFinallyReviewed:true,forcedThreadCleanup:0,perOwnerNativeReleaseCounters:'UNKNOWN_NOT_EXPORTED'},hardwareIo:false,hostMainStarted:false,productRuntimeApproval:false};
}
export function assessJoinedOriginal123ResultFiles({oracle,role,resultPin,processPin,hostPin,hcPin,nativeMathPin,argv}){
 verifyJoinedOriginal123SourceInputs(oracle);for(const p of [resultPin,processPin,hostPin,hcPin,nativeMathPin])checkJoinedPin(p);
 const process=read(processPin),spec=joinedOriginal123Role(oracle,role),args=argv;
 need(process.code===spec.expectedExit&&process.signal===null&&process.error===null&&process.expired===false&&process.budgetMs===60000&&process.naturalExit!==false,'JOINED123_ORIGINAL60S_TERMINAL');
 const duration=process.durationMs??process.elapsedMs;need(Number.isFinite(duration)&&duration>=0&&duration<=60000,'JOINED123_ORIGINAL60S_DURATION');need(Array.isArray(args)&&args.length===8&&same(args[0],hostPin.path)&&same(args[1],hcPin.path)&&same(args[7],resultPin.path),'JOINED123_ORIGINAL_EIGHT_ARGS');
 need(Array.isArray(process.args)&&process.args.length>=9&&sameJson(process.args.slice(-8),args),'JOINED123_PROCESS_EXACT_ARGV');
 const result=read(resultPin),assessed=assessJoinedOriginal123Result({oracle,role,result,hostSha256:hostPin.sha256,hcSha256:hcPin.sha256,nativeMathSha256:nativeMathPin.sha256,resultPath:resultPin.path});
 for(const p of assessed.nativeBoundaryFiles)checkJoinedPin(p);return{...assessed,resultPin,processPin,recordedActualResultMatched:true,notIndependentHardwareApproval:true};
}
export function classifyJoinedOriginal123HistoricalResult({oracle,resultPin}){knownOracle(oracle);const r=read(resultPin);need(Array.isArray(r.cases)&&sameJson(r.cases.map(c=>c.id),oracle.ids),'JOINED123_HISTORICAL_IDS');return{status:'HISTORICAL_OBSERVATION_ONLY_NOT_RUNTIME_ADMISSION',resultPin,caseCount:r.cases.length,failCount:r.cases.filter(c=>c.status==='FAIL').length,checks:r.checks,reportedStatus:r.status,uniqueTypes:r.uniqueConstructedTypes?.length??null,nativeMathBindings:r.nativeMathBindings,positiveAccepted:false,notAFaultOracle:true};}
export function validateJoinedOriginal123Transformations({constructorProofPin,originalPortsProofPin,hcSourcePin,hcPortedPin,hcProofPin,hostSourcePin,hostPortedPin,hostProofPin}){
 for(const p of [constructorProofPin,originalPortsProofPin,hcSourcePin,hcPortedPin,hcProofPin,hostSourcePin,hostPortedPin,hostProofPin])checkJoinedPin(p);
 const c=read(constructorProofPin),original=read(originalPortsProofPin),hc=read(hcProofPin),host=read(hostProofPin);
 need(c.status==='PASS'&&c.originalConstructorsExecuted===79,'JOINED123_ORIGINAL79_PREREQUISITE');need(original.status==='PASS'&&sameJson(sorted(original.changedMethods),sorted(BASIS.transformSignatures.original7)),'JOINED123_ORIGINAL7_PORTS');
 need(hc.status==='PASS'&&hc.sourceSha256===hcSourcePin.sha256&&hc.instrumentedSha256===hcPortedPin.sha256&&sameJson(sorted(hc.changedMethods),sorted(BASIS.transformSignatures.hc))&&hc.originalAllInstanceConstructorsGetCurrentAndInitializeUnchanged===true&&hc.originalNativeMathUnchanged===true&&hc.hardwareIo===false,'JOINED123_TWO_HC_LEAVES');
 need(host.status==='PASS'&&host.sourceSha256===hostSourcePin.sha256&&host.instrumentedSha256===hostPortedPin.sha256&&sameJson(sorted(host.changedMethods),sorted(BASIS.transformSignatures.host)),'JOINED123_THREE_HOST_LEAVES');
 for(const k of ['configureBodyExactExceptSingleTempGetter','actualNativeAdmissionPreserved','actualAssemblyLoadPreserved','actualConfigureAndResolverPreserved','actualFactoryIdentityReadinessAndCapabilityPreserved'])need(host[k]===true,'JOINED123_HOST_BODY_GUARD',k);
 need(host.hardwareIo===false&&host.publicStaEntryPreserved===false&&host.publicPowerIoIsolated===false,'JOINED123_NONPUBLIC_HOST_PORTS_ONLY');
 return{status:'PASS_ORIGINAL_PROOF_FIELDS_AND_IMAGE_PINS_ONLY',changedHc:2,changedHost:3,originalConstructorPorts:7,typedRawProofStillRequired:true,constructorProofDoesNotReplaceJoinedPositive:true,oldHostBytesNotCurrentSourceApproval:true,productRuntimeApproved:false};
}
export function describeJoinedOriginal123Preflight(oracle){knownOracle(oracle);return{schemaVersion:1,kind:'joined-original123-metadata-preflight',businessArgs:['portedHost','portedHC','runtimeRoot','sharedRoot','runtimePinsJson','exactInventoryJson','hostConfigJson','freshResultJson'],businessBudgetMs:60000,generationBudgetMs:30000,compileBudgetMs:120000,compileGraph:oracle.compileGraph,expectedCaseIds:oracle.ids,roles:oracle.roles,initializeRole:oracle.initializeRole,required:['compileReceiptPin','profilePin','metadataProcessPin','metadataReceiptPin','exact two/three-leaf transformation proof pins','parent selected synchronous/native terminal review'],compileReceiptShape:{schemaVersion:1,kind:'joined-original123-compile-source-closure',fields:['project','program','compiler','process','command','sources[{relative,source,compilerInput}]','inputs[]','producer','deps','runtimeconfig']},profileShape:{schemaVersion:1,kind:'joined-original123-metadata-preflight',fields:['role','producer','launcher','validator','sharedAuthority','program','policy','metadataProcess','metadataReceipt','metadataCwd','host','hc','nativeMath','args[8]','runtimePins','config','resultPath','compileReceipt']},missingCurrentCompiledOrMetadata:'BLOCKED_MISSING_EXACT_INPUTS_NOT_RUNTIME_APPROVED',runtimeApprovalGranted:false};}
export function validateJoinedOriginal123CompileReceipt({oracle,receiptPin}){
 verifyJoinedOriginal123SourceInputs(oracle);const r=read(receiptPin);need(r.schemaVersion===1&&r.kind==='joined-original123-compile-source-closure','JOINED123_COMPILE_RECEIPT_SHAPE');
 for(const p of [r.project,r.program,r.compiler,r.process,r.command,r.producer,r.deps,r.runtimeconfig])checkJoinedPin(p);
 const graph=oracle.compileGraph,pinFor=relative=>oracle.sourcePins.find(p=>p.relative===relative);need(r.project.sha256===pinFor('JoinedFactoryProbe.csproj').sha256,'JOINED123_COMPILE_PROJECT');
 need(Array.isArray(r.sources)&&r.sources.length===graph.sources.length&&sameJson(sorted(r.sources.map(s=>s.relative)),graph.sources),'JOINED123_COMPILE_SOURCE_SET');
 need(Array.isArray(r.inputs)&&r.inputs.length>0,'JOINED123_COMPILE_INPUTS_REQUIRED');const seen=new Map();for(const p of r.inputs){checkJoinedPin(p);need(!seen.has(full(p.path)),'JOINED123_COMPILE_DUPLICATE_INPUT');seen.set(full(p.path),p);}
 for(const s of r.sources){checkJoinedPin(s.source);checkJoinedPin(s.compilerInput);need(s.source.sha256===pinFor(s.relative)?.sha256&&s.compilerInput.sha256===s.source.sha256,'JOINED123_COMPILED_SOURCE_BINDING',s.relative);need(seen.get(full(s.compilerInput.path))?.sha256===s.compilerInput.sha256,'JOINED123_SOURCE_NOT_ACTUAL_COMPILER_INPUT',s.relative);}
 for(const p of [r.program,r.compiler,...graph.inputs.filter(x=>!x.endsWith('.cs')).map(pinFor)])need([...seen.values()].some(x=>x.sha256===p.sha256),'JOINED123_COMPILE_TOOL_OR_PROJECT_INPUT_MISSING',p.path);
 const command=read(r.command),process=read(r.process);exactPin(command.program,r.program,'JOINED123_COMPILE_PROGRAM');need(Array.isArray(command.args)&&command.args[0]==='build'&&same(command.args[1],r.project.path),'JOINED123_COMPILE_COMMAND');
 need(process.code===0&&process.signal===null&&process.error===null&&process.expired===false&&process.budgetMs===120000&&process.naturalExit!==false,'JOINED123_COMPILE_TERMINAL');need(sameJson(process.args,command.args),'JOINED123_COMPILE_ACTUAL_ARGS');
 exactPin(process.program??process.dotnet,r.program,'JOINED123_COMPILE_PROCESS_PROGRAM');need(path.basename(r.producer.path)==='JoinedFactoryProbe.dll','JOINED123_COMPILED_PRODUCER_NAME');
 const cfg=read(r.runtimeconfig).runtimeOptions,frameworks=cfg?.frameworks??[cfg?.framework];need(cfg?.rollForward==='Disable'&&['Microsoft.NETCore.App','Microsoft.WindowsDesktop.App'].every(name=>frameworks.some(f=>f?.name===name&&f.version==='10.0.11')),'JOINED123_COMPILED_RUNTIME_POLICY');
 return{status:'PASS_EXACT_PINNED_COMPILE_RECEIPT_SHAPE_AND_SOURCE_BINDINGS',receiptPin,producer:r.producer,compileGraph:graph,actualCompilerTraceReviewStillRequired:true,productRuntimeApproved:false};
}
export function validateJoinedOriginal123MetadataPreflight({oracle,profilePin}){
 knownOracle(oracle);const p=read(profilePin);need(p.schemaVersion===1&&p.kind==='joined-original123-metadata-preflight','JOINED123_PREFLIGHT_SHAPE');joinedOriginal123Role(oracle,p.role);
 const fields=['producer','launcher','validator','sharedAuthority','program','policy','metadataProcess','metadataReceipt','host','hc','nativeMath','runtimePins','config','compileReceipt'];const missing=fields.filter(k=>!p[k]);if(missing.length)return{status:'BLOCKED_MISSING_EXACT_INPUTS_NOT_RUNTIME_APPROVED',missing,runtimeApprovalGranted:false};
 verifyJoinedOriginal123SourceInputs(oracle);for(const k of fields)checkJoinedPin(p[k]);const compilation=validateJoinedOriginal123CompileReceipt({oracle,receiptPin:p.compileReceipt});exactPin(compilation.producer,p.producer,'JOINED123_PREFLIGHT_COMPILED_PRODUCER');
 need(Array.isArray(p.args)&&p.args.length===8&&same(p.args[0],p.host.path)&&same(p.args[1],p.hc.path)&&same(p.args[4],p.runtimePins.path)&&same(p.args[5],oracle.inventoryPin.path)&&same(p.args[6],p.config.path)&&same(p.args[7],p.resultPath),'JOINED123_PREFLIGHT_ORIGINAL8_ARGS');
 need(path.isAbsolute(p.args[2])&&path.isAbsolute(p.args[3])&&path.isAbsolute(p.resultPath)&&!fs.existsSync(p.resultPath),'JOINED123_PREFLIGHT_FRESH_RESULT');const pins=read(p.runtimePins);for(const [f,h]of Object.entries(pins))checkJoinedPin({path:f,sha256:h});for(const pin of [p.host,p.hc,p.nativeMath])need(pins[pin.path]===pin.sha256,'JOINED123_PREFLIGHT_RUNTIME_PINS');
 const policy=read(p.policy),def=policy.contexts?.filter(c=>c.id==='Default');need(def?.length===1&&same(policy.entry,p.producer.path)&&def[0].managed.some(r=>same(r.path,p.producer.path)&&r.sha256===p.producer.sha256),'JOINED123_PREFLIGHT_ENTRY');
 need(def[0].managed.some(r=>r.sha256===p.sharedAuthority.sha256&&path.basename(r.path)==='ScopedLoadAuthority.dll'),'JOINED123_PREFLIGHT_SHARED_AUTHORITY');
 for(const c of policy.contexts??[])for(const r of c.managed??[])checkJoinedPin(r);for(const r of policy.metadata??[])checkJoinedPin(r);
 const process=read(p.metadataProcess),receipt=read(p.metadataReceipt);exactPin(process.program??process.dotnet,p.program,'JOINED123_METADATA_PROGRAM');
 need(process.code===0&&process.signal===null&&process.error===null&&process.expired===false&&process.budgetMs===60000&&process.naturalExit!==false,'JOINED123_METADATA_TERMINAL');need(same(process.cwd,p.metadataCwd)&&path.isAbsolute(p.metadataCwd),'JOINED123_METADATA_CWD');
 need(sameJson(process.args,[p.validator.path,'validate',p.policy.path,p.policy.sha256,p.metadataReceipt.path]),'JOINED123_METADATA_EXACT_VALIDATE_ARGS');need(receipt.exitCode===0&&receipt.failure===null&&receipt.productLoaded===false&&sameJson(receipt.events,['prepare-metadata-only']),'JOINED123_METADATA_RECEIPT');
 need(Array.isArray(receipt.actualLoadedManaged),'JOINED123_METADATA_LOADED_GRAPH');for(const r of receipt.actualLoadedManaged){checkJoinedPin(r);need(!['JoinedFactoryProbe','OneXFanCoexistence','YeManFanHost','YeManFanBackend','HandheldCompanion','Microsoft.Windows.SDK.NET','WinRT.Runtime'].includes(r.identity?.split(',')[0]),'JOINED123_METADATA_TARGET_LOADED');}
 return{status:'PASS_METADATA_PREFLIGHT_FIELDS_ONLY_NO_JOINED_EXECUTION_APPROVAL',profilePin,compilation,caseCount:123,businessBudgetMs:60000,rawTwoThreeLeafDiffAndTerminalsStillRequired:true,originalProofConsumerMustRemainEnabled:true,runtimeApprovalGranted:false};
}
