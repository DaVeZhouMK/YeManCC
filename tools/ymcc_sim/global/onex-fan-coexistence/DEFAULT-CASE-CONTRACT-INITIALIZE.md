# Default case contract and first initialize counterexample

This additive tool slice does not run Harness, a mutator, Host, HC, SDK or a target initializer. It reads fixed C# sources, constant case-label expressions and reviewed raw PE metadata, prepares an exact generation policy and exposes a separate metadata-only replay preparation API. Original product consumers and assertions remain unchanged.

## Which case count means what

The original default final gate in run.mjs is behavior.status PASS, cases >=69, checks >=280, with unchanged sources and inputs. It never asserted default cases ===123. The eight exact compiler-source files independently expand to 192 labels. Their source-derived set equals the parent's actual 192 IDs, with its actual order retained separately for fault replay:

| Default source family | Cases |
| --- | ---: |
| Original OneX: Harness36 + StrictWriteCases33 | 69 |
| Generic EC writes | 15 |
| Generic close | 8 |
| Generic ownership | 22 |
| Mixed events including reentry/debt/manager flags | 41 |
| Events admission | 28 |
| Initialization | 9 |

69 + (15+8+22+41+28+9) =192. The additional default123 is numerically coincidental with joined123. Source expressions and their exact loop domains are stored per ID; no observed result is used to seed expected labels or to replace an original assertion. run.mjs mixedIds includes all41 mixed cases and flagIds is an overlapping subset8, not eight extra cases. Its eventIds declaration plus push covers all28 admission cases.

The historical full-entry 123 cases /119 FAIL belong to joined-factory/positive/case/result.json, consumed by JoinedFactoryProbe and joined-factory-proof.mjs. All123 IDs and the preserved119 failed IDs are included; their intersection with default192 is empty. They are not119 expected-negative default cases and are not repaired by default192 passing. The original joined failure and the parent's initial mistaken default ===123 summary failure remain untouched. Later publicFactory also has its own same123-case contract and different entry/lifecycle; this slice does not execute or approve either.

CASE-CONTRACT-original-fixed.json rechecks the explicit original run.mjs.before SHA bf1064446ee6d495b0186dc1eb1395cda2b9b32cdd48ce2e14309132f0fefeac. CASE-CONTRACT-final.json uses the independently captured current ee79814a source. ORIGINAL-CURRENT-CONSUMER-BINDING.json proves original initialize args/assertions/three failure IDs and >=69/280 gate unchanged after the parent's seven fresh-output staging replacements. The first mutable-G bf pin is retained as historical; it is not re-signed. All eight case sources equal their original37 compiler source-view bytes.

## Prepared generation

Actual plan: D:/Codex/2026-10-18/kai-s/work/global-fan-current-default-case-contract-next-initialize-20261021-r1/actual-prepare-r1/INITIALIZE-GENERATION-PLAN.json

Plan SHA256: 4bac2fa38c2c0d3c941a7f5eb6b988e0b84bbf2cf47d622c4ee0e25bfedc85a2

The existing SDK-free MTA, Harness and all37 original artifacts are exact copies. Default44 is retained, with no Host/Backend custom runtime contexts in generation mode. Clean current Host is a metadata pin. Exactly13 other payload members are copied; fault-target/YeManFanHost.dll starts absent. Source read-only attributes and ACLs are untouched. The manifest remains original fixture data and is not advertised as a deployable mutated payload.

The exact producer arguments are:

    ["fault-initialize-first-start", exactCurrentHost2b599, freshFaultHostOutput, freshMutationProofOutput]

The plan's dotnetArguments and metadataValidateArguments are exact DATA vectors. Generation keeps45000ms / expected exit0. Replay keeps60000ms / expected exit2, parent360000ms and 7+2+3 ports unchanged. A different mode, preexisting fault target or unsupported policy context is refused. This tool never launches those vectors.

Regenerate preparation only in a new directory:

    node prepare-initialize-first-start.mjs --mode prepare-only --inputs D:/Codex/2026-10-18/kai-s/work/global-fan-current-default-case-contract-next-initialize-20261021-r1/INITIALIZE-PREPARE-INPUTS.json --sha256 651c5353e6218fcb66cdc453f0e66f1adb8d88814f53b74db678484310bac859 --out-dir <fresh-owned-directory>

## Generation/FirstStart metadata boundary

Twenty selected MethodDefs were read across the original mutator/helpers, Harness type initializer and exact synthetic first-start terminals. The current callback is:

    System.Void YeManFanHost.RealHcBackend::<LoadAssemblyAndFactory>b__574_6()

Only its first Boolean false becomes true. The second Boolean, all other instructions/calls/guards, original callback selection and original assertions stay unchanged. Original mutator compares all method sets/canonical bodies and demands exactly this one change; this worker did not execute that comparison or create a DLL.

Harness dispatches the generation mode before AuthFile/environment/Candidate Load. Generation uses Cecil ReadAssembly/Write and original metadata callbacks; it does not Load the target. Runtime FirstStart=true reaches IDevice's no-op, FlipDS four settings writes and Flip1SDS its base four plus one setting. Their SettingsManager is syntheticHC6d7: own dictionary, Trace and existing fixture event delegates, with no registry/persistent settings/device IO. The previous default branch proof covers those synthetic callbacks. This is not approval for raw HC FirstStart or a real device.

## Replay binding after parent generation

Replay remains BLOCKED_AWAITING_PARENT_EXACT_GENERATION_AND_RAW_METADATA. No fault DLL SHA is invented. Parent must supply:

- Original generated proof at plan.faultProof and new DLL at plan.faultTarget.
- MTA generation receipt at plan.launchReceipt, including exact Harness/MTA bindings, plus its separate natural-exit/budget/process and managed-allowance audit.
- A reviewed raw PE report of that exact DLL/callback. The metadata API checks the pinned original callback canonical digest, exactly the first false->true instruction, no new module initializer and no PInvoke.

Use the exported file API; its implementation only verifies/copies metadata and writes a fresh policy/plan:

    import {prepareInitializationReplay} from './prepare-initialize-first-start.mjs';
    const result=prepareInitializationReplay({
      mode:'accept-generated-metadata-only',
      generationPin:{path:exactGenerationPlan,sha256:exactGenerationPlanSha},
      proofPin:{path:exactPlanFaultProof,sha256:parentGeneratedProofSha},
      faultPin:{path:exactPlanFaultTarget,sha256:parentGeneratedDllSha},
      rawPin:{path:parentRawCallbackReport,sha256:parentRawReportSha},
      generationReceiptPin:{path:exactPlanLaunchReceipt,sha256:parentLaunchReceiptSha},
      originalCallbackPin:{path:'D:/Codex/2026-10-18/kai-s/work/global-fan-current-default-case-contract-next-initialize-20261021-r1/ORIGINAL-INITIALIZE-CALLBACK.json',sha256:'148deac2dc9a535590089ee60eb22bf43e1bec5817fe3b3296fe9a27afae622c'},
      outDir:exactGenerationOwnedRoot+'/replay-final'
    });

The resulting replay uses Default44, FaultHost+Backend2 in its exact Harness.ProductContext, and old Light1 in a separate context. RawHC08a is metadata only; runtime HC is synthetic6d7. No simple-name merge or unknown fallback is added. Parent still owns MTA validate, Default preload, process/managed allowance, actual replay and posthash checks.

assessInitializationFaultResult requires expected exit2, FAIL, native0, hardwareIo=false, HostMain=false, the complete source-derived case set and original192 order. Exactly these original three IDs may fail, with their original assertion messages; every other case must pass:

- initialization-actual-callback-arguments-and-virtual-dispatch
- initialization-pinned-no-first-start:AYANEOFlipDS
- initialization-pinned-no-first-start:AYANEOFlip1SDS

## Evidence and integration

FINITE-GATES-final.json has10 finite source/plan/metadata controls. Modeled mutation/replay records are explicitly in-memory validator examples, not actual fault execution. Initial source-extractor failures (unknown point variable, then a range excluding default RunMixedReentryCases) and the first partial eventIds expression result are retained; the final extractor follows the actual default root and includes eventIds.push. Earlier attempts and rechecks are not added to the final10 count.

Four additive files: default-case-contract.mjs, prepare-initialize-first-start.mjs, initialize-first-start.test.mjs and this document. They do not edit G run/adapter/CLI, old seals, original counters/assertions, product code or physical ports. No bool-fault work is performed. Full parent/Host approval remains absent.

## Actual Cecil metadata relocation

The first parent generation produced the exact intended callback change but Cecil reassigned MethodDef/FieldDef/TypeRef row numbers. The validator now binds the unique full callback signature to the generated PE SHA and records its own MethodDef identity; it requires the same body size, flags, method kind, instruction offsets, opcodes and fully resolved operands. Only the first false becomes true. Operand token table kinds must remain unchanged; duplicate signatures, a wrong token kind, changed call owners and any other instruction change are rejected. Original module-initializer, whole-method-set mutation proof, loaded-assembly restrictions, original assertion messages and complete192-order checks remain. Structural examples do not count as actual replay.
