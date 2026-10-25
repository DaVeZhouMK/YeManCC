# HC remaining-model capability regression — OneX and SteamDeck source status

Run from the authoritative source repository:

```powershell
node tools/hc_remaining_model_capabilities_selftest.mjs --mutation-checks --out "G:/YeManCC-Work/Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/model-regression/<fresh>"
```

Default output: `G:/YeManCC-Work/Build/Validation/HC-ModelCapabilities-20261017/OneXApplication/model-regression`.
The frozen preimage remains `Remaining9/main.before.cpp`. `--out`, `--source`,
`--before`, `--vcvars`, `--generate-only` and `--expectations-only` remain available.
A generation-only receipt is not a passed test.

## Independent expectations

`source-pins.json` binds 162 historical HC C#/JSON inputs to commit
`a250b19a4b7e29dac3b383f2d1ac517d1af43e4e`, SHA-256 and Git blob hashes.
`expectations.mjs` derives factory aliases, inheritance, keyboard chords and IMU
fallback from those fixed inputs. Axis remapping follows input-to-output assignment,
then output-axis signs; it does not invert cycle permutations by indexing.

The reviewed YMCC application ownership policy is explicit fixture data, independent
of candidate C++. It declares software capture only for the exact catalog keys
`turbo`, `keyboard`, `home`, `l4`, `r4` on the three new OneX models:

| Model | Turbo / Keyboard | Home | L4 / R4 |
| --- | --- | --- | --- |
| OneXPlayerX2 | keyboard-vk | onex-x2-vendor-click | onex-x2-vendor-held |
| OneXPlayer3 | keyboard-vk | onex-x2-vendor-click | onex-x2-vendor-held |
| OneXPlayerX2MiniPro | keyboard-vk | onex-x2-vendor-click | keyboard-vk (F15/F16) |

Those three exact OneX classes receive `captureImplemented` and `captureRoute`.
All five software capture flags must be boolean true; unknown keys must return false.
Neither field establishes that the device is bound, input was observed, a writer is
ready, or Windows resumed successfully.

The exact pair of family `ValveSteamDeck` and class
`HandheldCompanion.Devices.SteamDeck` retains `supported=true` and the two
Embedded catalog keys `special` / `special2`. Their physical Neptune reader is
unimplemented: the actual helper returns false for catalog and noncatalog keys;
the actual query and its log add exactly `captureImplemented=false`,
`captureRoute="physical-neptune-reader-unimplemented"`, and
`captureReason="physical-neptune-reader-not-integrated"`. These values come from
the reviewed source-status contract, independently of candidate C++.

Legacy comparison first validates those three fields on both returned and logged
SteamDeck keys, then removes only those fields and compares the entire remaining
response/log against the frozen preimage. Jupiter and Galileo are the only two
aliases allowed this normalization. The other 179 aliases retain complete equality.
Separate tests retain compatibility true for other old families' unknown keys,
wrong family with the same class (including Unknown), and nonexact SteamDeck classes.
Synthetic query/log comparisons exercise the same identity boundary.

## Executed boundaries

- Every one of the 63 source-derived alias combinations executes; no early break.
- 80 old classes / 181 old identity combinations retain identity, family/profile IDs,
  canonical bits, complete actual query responses and complete catalog log payloads,
  with only the exact three SteamDeck fields validated/normalized as described above.
- The exact production `oem.keys.get` lambda executes with in-memory log and identity
  sinks, including logging enabled/disabled and full return/log field parity.
- Exact software gate tests include all 256 single-byte unknown keys, spelling,
  whitespace, prefix/suffix and embedded-NUL negatives.
- The actual old `onexCapDescriptorFor` executes for the six old classes and rejects
  all three new models; its code and the existing BUS admission stay byte-identical
  to the frozen preimage.
- Existing keyboard evaluator combinations, AYANEO3 F24, Mini F15/F16 left/right,
  matrices, publication reset and AoAc/power predicates remain covered.
- Relevant extracted source ranges are checked again at completion. Unrelated whole
  file drift is recorded separately; relevant drift requires a new run.

## Fault controls

All controls mutate only isolated copies under output; production is never edited:
identity prefix matching, inverse cycle matrix, AYANEO3 Win+D, removal of implemented
Home capture, acceptance of unknown capture key, Home routed to keyboard, Mini rear
routed to vendor, leaked legacy route fields, and a route dropped only from the log.
A tenth control restores SteamDeck default true; it must fail the separate
SteamDeck gate assertion on a declared catalog key. Each mutant must compile and
fail the expected semantic section; compile errors do not count as detection.

## Limits

The vendor-Turbo/Keyboard/Mini-rear deduplication and absence of WMI takeover are
application policy, not behavior executed by this pure regression. Those producer
claims require the parent's integration tests. This suite performs no Windows
HID enumeration/read/write, hook installation, power transition, Host/Steam execution,
real-device acceptance, deployment or package release.

## 全局模拟器接入（2026-10-19）

现有回归通过 `tools/ymcc_sim` 的 `controller.machine-model-capabilities` 运行，两种构建及原十条编译语义变异全部保留。正式代码段仍提取自当前 native/main.cpp；本轮未修改产品。六条旧源码相等门只规范 CRLF→LF，仍记录每段原始哈希和原始字节是否一致，不忽略空格、注释、字符串或任何代码差异。全文件与实际提取段完成后的哈希／不变性检查继续保留。

编译环境复用仓库已有 `hc_reviewed_calibration/toolchain.mjs`，发现已安装的完整MSVC/WindowsSDK；不安装依赖、不改全局环境。编译子进程只导出编译所需变量。固定历史HC参考为a250b19a，不冒充当前生产运行库或实机验证。旧baseline、manifest/inventory和162参考原件仍为工作区研究输入；统一入口逐文件绑定前后哈希，缺少输入保持BLOCKED，不用新的环境替代。
