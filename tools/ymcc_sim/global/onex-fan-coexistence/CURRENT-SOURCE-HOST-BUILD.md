# Current G source-bound Host candidate (compile only)

This tool builds the actual current FanLab/real-host/YeManFanHost.csproj and its original Backend project reference from exact byte copies of55 current G source/project/props/content inputs. The9 explicit linked inputs cover ProcessEfficiency, two FanLab dependency contracts and six capability files. Product projects/sources remain unchanged. It does not run Host, producer, HC, SDK or a candidate scenario, and does not package a handheld build.

## Exact reference candidate

- Host SHA256: 2b59965fce6909b1a85058e1cf670477090ed858d64eda5484a2fda38eec484a
- Backend SHA256: 2a3ae2d1fa99c382841dfa6ee690ac3c3c9baf429b726b76a9c67c2d6fcfe236
- Host identity: YeManFanHost, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null
- Backend identity: YeManFanBackend, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null
- Payload: D:\Codex\2026-10-18\kai-s\work\global-fan-current-source-candidate-build-20261021-r1\actual-build-r3\candidate\PowerControl\fan-host
- Runtime: D:\Codex\2026-10-18\kai-s\work\global-fan-current-source-candidate-build-20261021-r1\actual-build-r3\candidate\PowerControl\handheldcompanion-runtime\YMCC-DEVICECORE-20261018-v1
- Shared: D:\Codex\2026-10-18\kai-s\work\global-fan-current-source-candidate-build-20261021-r1\actual-build-r3\candidate\PowerControl\ymcc-shared-runtime
- PAYLOAD-BINDING.json SHA256: 26cfc412c4b977bbabc4f488cd00cec87926e86e85028cc8ce184160bea57743

All three roots belong to the same new owned PowerControl tree and keep the original relative runtime/shared layout. Reviewed stageDeclaredHostPayload validates13 payload members,85 runtime members and8 shared members from exact manifest declarations. The13 members are10 actual build outputs plus3 unchanged current support files. Runtime/shared originals and manifests are byte copies, and a fresh candidate payload manifest binds the new outputs. No old7985 payload is used as the candidate or credited as current source evidence. Existing37 Harness/metadata-authority outputs are checked and reused in place, without copying or recompilation.

## Reusable tool and bounded mode

Four additions are build-current-host-candidate.mjs, CurrentHostCompileAudit.targets, build-current-host-candidate.test.mjs and this document. The entry accepts only compile-only and the exact pinned request schema; arbitrary command/buildArgs/run fields are rejected. It pins reviewed dotnet10.0.400, compiler assets, the metadata-only reader, payload stager, one locally cached Windows SDK nupkg, current sources and original runtime/shared/tool manifests. It copies the exact source subset into a fresh D source-view and uses an offline feed/packages directory.

```powershell
$Node = 'G:/YeManCC-Work/Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe'
$Tool = 'G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/tools/ymcc_sim/global/onex-fan-coexistence/build-current-host-candidate.mjs'
& $Node $Tool --mode compile-only --inputs 'D:/Codex/2026-10-18/kai-s/work/global-fan-current-source-candidate-build-20261021-r1/CURRENT-SOURCE-BUILD-INPUTS-r3.json' --sha256 '8eb31d0ddb26e19f2434f956f94ee39ccdc0d1b3342414d4dbae0be6f9f67afd' --out-dir 'D:/Codex/2026-10-18/kai-s/work/parent-current-host-build-fresh-r1'
```

Until integration, use the overlay tool path. The checked request pins the audit target file by its current overlay path; after integration create a NEW request with that exact integrated target path/SHA. Never edit a sealed request. To build a later G revision, recapture the same scoped input set and all declared pins; do not relabel this candidate current if any original input changed. Exported API: await buildCurrentHostCandidate({mode:'compile-only',inputsPin:{path,sha256},outDir}). No caller-supplied process command is accepted.

The actual build command and process are in actual-build-r3/BUILD-COMMAND.json and BUILD-PROCESS.json. Arguments are:

```json
[
  "build",
  "D:\\Codex\\2026-10-18\\kai-s\\work\\global-fan-current-source-candidate-build-20261021-r1\\actual-build-r3\\source-view\\FanLab\\real-host\\YeManFanHost.csproj",
  "--artifacts-path",
  "D:\\Codex\\2026-10-18\\kai-s\\work\\global-fan-current-source-candidate-build-20261021-r1\\actual-build-r3\\build",
  "--source",
  "D:\\Codex\\2026-10-18\\kai-s\\work\\global-fan-current-source-candidate-build-20261021-r1\\actual-build-r3\\offline-feed",
  "--packages",
  "D:\\Codex\\2026-10-18\\kai-s\\work\\global-fan-current-source-candidate-build-20261021-r1\\actual-build-r3\\packages",
  "-c",
  "Release",
  "--nologo",
  "-v:n",
  "-m:1",
  "-nodeReuse:false",
  "-p:UseSharedCompilation=false",
  "-p:NuGetAudit=false",
  "-p:NuGetConfigFile=D:\\Codex\\2026-10-18\\kai-s\\work\\global-fan-current-source-candidate-build-20261021-r1\\actual-build-r3\\NuGet.Config",
  "-p:YemanAsyncBoundaryTest=false",
  "-p:ProvideCommandLineArgs=true",
  "-p:YmccInputAuditDir=D:\\Codex\\2026-10-18\\kai-s\\work\\global-fan-current-source-candidate-build-20261021-r1\\actual-build-r3\\compile-audit",
  "-bl:D:\\Codex\\2026-10-18\\kai-s\\work\\global-fan-current-source-candidate-build-20261021-r1\\actual-build-r3\\build.binlog"
]
```

Original Directory.Build.props is preserved. YemanAsyncBoundaryTest=false is explicit; the actual compiler defines do not contain YEMAN_ASYNC_BOUNDARY_TEST. The wrapper's empty root props isolate missing original ancestor build config, and the own targets file only observes compile inputs and emits hashes/argument lists. It adds the existing SDK global-analyzer-config target as a prerequisite for auditing and includes ReferencePathWithRefAssemblies. No product IO leaves or assertions are replaced.

The build uses the project's original framework selection. A global RuntimeFrameworkVersion override was removed after original failed attempt r1, because it makes Windows SDK package resolution request10.0.11 in addition to10.0.19041.57. Product runtimeconfig therefore retains net10.0 with Microsoft.NETCore.App/Microsoft.WindowsDesktop.App10.0.0, as emitted by the original project. It was not executed or rewritten to imply a new Host runtime policy. The reviewed metadata reader runs under its own fixed10.0.11 runtimeconfig.

## Source and compiler binding evidence

INPUTS-BEFORE/AFTER cover221 exact source/tool/runtime/shared/support/Harness input pins;55 source copies are byte matched to G both before and after. Compile auditing captures193 Backend and250 Host input pins (267 unique), including all189 Backend and244 Host actual Csc file arguments. Every source/reference/analyzer/config file argument is matched to its precompile hash and unchanged postcompile hash. Compiler arguments and before/after TSVs are retained per project. SDK imports, generated/project/content items and fixed driver/compiler assets are also hashed. Scope is the exact declared build and explicit Csc file inputs, not a trace of all OS/MSBuild internal reads. Apphost/native hardware/runtime execution is not admitted.

The final build exited0 naturally in5081ms with0 warnings/errors. The existing approved PrepareMetadata tool only read output PE metadata; it exited0 and recorded13 actual loaded tool/framework files, targetRuntimeLoaded=false. It recorded output full identities, AssemblyRefs, entrypoints and module-initializer absence. These are metadata observations, not source/branch or full-runtime execution approval. Newly emitted SDK/WinRT payload assets are not automatically added to any target loading policy.

Eight finite pure gates passed without another compiler launch: actual current build/audit/staging positive; run mode; wrong SDK; arbitrary command; source escape; wrong SHA; pre/post compiler-input drift; conflicting duplicate input. The first restore failure and second audit refusal are preserved in REVIEW-HISTORY.json. They are not silently rewritten or double-counted.

## Parent next step and preserved boundaries

Review SOURCE-CANDIDATE.json, BUILD-OUTPUTS.json, COMPILE-INPUT-AUDIT.json, INPUTS-BEFORE/AFTER and PAYLOAD-BINDING.json. Then derive a fresh exact candidate policy and four-argument invocation for these new Host/Backend bytes under the original60000ms candidate budget and reviewed37-output MTA/Harness infrastructure. CurrentSourceBound=true records the source/compile mapping; sourceApprovalGranted, executionApprovalGranted and fullHostApproved remain false. No previous7985 policy, old151 conservative frontier or a678 old-write proof approves these changed bytes. The151 list is left untouched as historical branch inventory, without recursion.

The full original public run additionally binds its chosen repo/shared selection to the exact owned shared root; this delivery does not assert G path equivalence, copy the entire repository or run the parent. All originals remain unchanged, including G, B51 and prior seals. Original123/119negative/7+2+3/360000ms/3072 and old-write6/9/45000ms gates remain. No original scenario was executed.

## Exact55 current source members

| Relative current G source | SHA256 |
| --- | --- |
| FanLab/real-host/ARMOURY-COMPATIBLE-TEST-README-20260826.md | 461e22853884a2f60021f985c1486669a6629a2cc360755d7f5aa0bfce50c7f4 |
| FanLab/real-host/AUTHORIZATION-ARMOURY-COMPATIBLE-ROG-XBOX-ALLY-X-20260826.md | ff2e0675619117fe0e2f872bba2b1ae426897555afac8718a92f1f00d730b52e |
| FanLab/real-host/build-safe-host.ps1 | 58d1b7e334a18f614732d33a8a5e53dcd9391a7633f7936ce5e2b0d4c5fb098a |
| FanLab/real-host/DeferredHostSuccessor.cs | 70caf39e6c44d6c557c8d26d58638760b3a139fe3887855908d861a4dcd7a29f |
| FanLab/real-host/Directory.Build.props | 17638e7d1d252b85bdd00f08a3cd888f20910c16b51536eca21df0f88a5e1480 |
| FanLab/real-host/FanCapabilityPolicy.cs | 0330205e9d996c6668065fe28178a2773825caf8b0560700cc9ae885e3bdf2ad |
| FanLab/real-host/FORMAL-PACKAGE-SCOPE-20260821.md | 5bbeda40ca2a6e206f7b2d2388d57f263b515607cdb24e2929b229b184414f45 |
| FanLab/real-host/G1FanContractAdapter.cs | 399004d8cb76145cfe2579a9492faadfec43f6f506f66d52f8506a8505a2f387 |
| FanLab/real-host/HostProcessLifetime.cs | bbbb1b224508b32b394c384a87c3c8472ea5a8d0d51386f8677fcab896831040 |
| FanLab/real-host/MsiFamilyFanCurve.cs | b5021e27129957de14798cd934205c208a58d9c791ed996601cc845ad1c27d83 |
| FanLab/real-host/MsiFamilyFanCurveSelfTest.cs | f697aa7385ae763b03938e69b03021c34d448627c337d5b6f334a79258fc7392 |
| FanLab/real-host/MsiFanDeviceLifecycle.cs | c6a21a708269b31284b0cb9f867d53bc4dd16efa85cf448b41b7239f3bedca13 |
| FanLab/real-host/MsiWmiClassRegistration.cs | a0eebef2e4df02a723e142bcb4a0707e6e013d4606ec20907c43b47d17e6c605 |
| FanLab/real-host/MsiWmiPayloadResource.cs | 39f025987159eb5ac4a236d343d13f56e844e13fc0e320af5a0f9d87f035f883 |
| FanLab/real-host/OneXFanDeviceLifecycle.cs | 16c9f1f5546340329a6270a05e4d1dfe4206b38a5157d5c2fb7c8a4c89cbf549 |
| FanLab/real-host/Program.cs | 276068d4031942fbc550b1ecd98f55903ff0307c0e8193b236770e187adc9eb6 |
| FanLab/real-host/README.md | 82ae01d5004c38d96797f4ac3018b17882942a36f71281c11e1c3bc8e3b0d14c |
| FanLab/real-host/REAL-FAN-TEST-README-20260822.md | 1eca169c2dd81b1846ecefb51fb114a3c3750e4797d9ca1b65a132d712df5859 |
| FanLab/real-host/REAL-FAN-TEST-README-20260823-P1.md | 887c0dcf79435fb86ea2f46c1dae686864a60a65f29fe3b6bcb9c4a171929429 |
| FanLab/real-host/REAL-HOST-TEST-INSTRUCTIONS-20260821.md | 6bae439de7dddd03e45b8db205a00046f99ca69274da1bfcc99b16e0daf07b22 |
| FanLab/real-host/REAL-WRITE-ACCEPTANCE-ROG-XBOX-ALLY-X-20260821.md | 2a6b32d45fc3a41183c74c2aa8b7064eb771f6368f45c3e2f14562e11c242de6 |
| FanLab/real-host/run-armoury-compatible-emergency-restore.ps1 | 576364f146e636afd68c493fa01098ad46d26243e7e1269d96f56261de0ad969 |
| FanLab/real-host/run-chain-regression.ps1 | e07132ce00f2f14e2e5eab6760748a37b85a8956ba440c0d6c0185c209e09bba |
| FanLab/real-host/run-emergency-fan-restore.ps1 | 5aff8250714941245f1ab86bf80d2c08ea814395b0de3eff6c27a37e65e47fa4 |
| FanLab/real-host/run-msi-readonly-cli-gate.ps1 | e84769240fd6fcd46ee3e8253be75646328bae027bc78e0666360d0d55ad9a37 |
| FanLab/real-host/run-open-events-409-http-mock.ps1 | 0d5e4ab93808446bd8064937473ebcd2a88440b96e055e0fb2a475133415ff15 |
| FanLab/real-host/run-r1-r4-mock-regression.ps1 | aa61ac9e6ebef6e1b6231b59cd1d4d9e4d8eb3ca87e8f4361c291533ffc6f6bb |
| FanLab/real-host/run-r2-mock-regression.ps1 | b4512645c07fd7d9477920e4cc257a1e7a319ea94bbfddf10621a35ab677f147 |
| FanLab/real-host/run-real-fan-event-observation.ps1 | 7211189b21984e84212bd5370429bb295458e924f923b9543eef8a8638b74a61 |
| FanLab/real-host/run-real-fan-test.ps1 | e7168cc8e80cc31381e3095ee52ec094b60b2e62c1c2fec57ed05deacaf41e14 |
| FanLab/real-host/Section18CandidateFakes.cs | ef35d49c85f1ff17a3be2452e7227fa5c4a13096d0afc2ff7463d313647bd7c6 |
| FanLab/real-host/Section18CandidateLifecycle.cs | 70201596d0c0f569aeae8979e8851c708da2c3eb53e5595fe36a038d83fc7033 |
| FanLab/real-host/Section19CandidateLifecycle.cs | 4715f9d7722d8b73c9f95cd7a5ee46c136e5d3265e5e4ba5d05efab99e453272 |
| FanLab/real-host/Section20CandidateLifecycle.cs | 0f58e93da6e3fa2ef14916219cf0fe71b7defa0e18bf5727fe52369f80a404fa |
| FanLab/real-host/Section21CandidateLifecycle.cs | 5ba1da1fddcbb07b70ae57229c27561c7af8cb2f26249a12d72f5b50bc28b278 |
| FanLab/real-host/Section22CandidateLifecycle.cs | 3ee412a754824439899c95da855ff66980416534cb29c6023257097891032dca |
| FanLab/real-host/Section23CandidateLifecycle.cs | 5e0b4ffa33aeece951d79e07d1339786ca1233991caf99e5773f164a97ac3891 |
| FanLab/real-host/TEST-PACKAGE-README-20260823.md | 5e4739c6ded6457f29825fc4d06e092a970793d2b93378886c72e06bdc9d3370 |
| FanLab/real-host/YeManFanHost.authorization.md | 75957e11f53ea5852d1a63442be1f6803ec2cd1508c18deb704c8804e19c7403 |
| FanLab/real-host/YeManFanHost.csproj | 791307d8d7bb132f940923b045a7f07e409531801125ad18c006aba462d2fda0 |
| FanLab/real-host/YeManFanHost.json | b2316017b344c11ed08556a0f6932a2ae6ac6f5e04bbe188d9a89f7596be362e |
| FanLab/fan-backend/Asus/AsusFanBackendController.cs | 8cae7c7611fb46686d3b5a4d2c7be6ead9ee4c3b17955e9ec0c00afdd222d303 |
| FanLab/fan-backend/Asus/AsusFanBackendReceipts.cs | 2f00a3f2178d5713b73696595533bbb5df89bce41e2f7693bb1b12454af5afe2 |
| FanLab/fan-backend/Asus/AsusFanBackendTransport.cs | cbf4100fbbc5fe412874366540d81bd07106eea8b65342d2c04146d136d851de |
| FanLab/fan-backend/Asus/AsusFanCurveWriter.cs | 14b09f20ee7ea896ad825ab9fa3c06b135cd24673c07ae15608a50fb4bb03f56 |
| FanLab/fan-backend/YeManFanBackend.csproj | ef89f0f1059d68f35e15f717bf24eb60a9942b22fc46ebb555a972fa417fae58 |
| native/ProcessEfficiency.cs | 4d59a116497f00a161e61790eba9c48ceb6e196c512f5e97e8f7ec255187d60c |
| FanLab/DeviceCoreSharedDependencies.cs | 9ba5524cea21d2a7552a4a9025539c93d1c52072a882c351f61086f850cfd9e7 |
| FanLab/RuntimeDependencyContract.cs | 4f0fa4d26102fba1a2ed8cd013eb029457ab2049dad649809c1a17e2f1cf71c1 |
| capabilities/HcDeviceCapabilityCatalog.cs | eeae40ef0a3b2c8065f79d74479086a01ea0a54806eda6528426c627896e40b8 |
| capabilities/HcDeviceCapabilityCatalog.generated.cs | 718d9b0340bec0ff81d4ebff8e1d96e3724d2a362b8169ca72788bf92002b674 |
| capabilities/FanCapabilityCatalog.cs | c07f040ddff59e4894364667a19a547df8998d7e02f771e29c473ef98f547c78 |
| capabilities/AsusFanCapabilitySession.cs | 45a0e2ef7d907f82c2a8a818a05db4202750800cdde6e8d1600862788b173dee |
| capabilities/HcLightingCapabilityCatalog.cs | 46145ce59b799cb46bc2f4ef55c88c6c153d7a438dda8b4337c5fc0ad6147a0e |
| capabilities/HcLightingCapabilityCatalog.generated.cs | 73fda1026ceac203317f868e81d3fee7444aa306cc46a6654433906f5ab5aab4 |
