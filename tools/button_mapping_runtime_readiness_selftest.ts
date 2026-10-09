import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

const root = process.cwd();
const source = readFileSync(resolve(root, 'src/views/ButtonMappingView.vue'), 'utf8');
const editor = readFileSync(resolve(root, 'src/views/ControllerShortcutEditorView.vue'), 'utf8');

// A13 同步（2026-09-20）：原断言要求本视图存在 fail-closed 的
// `const targetClosure = computed(() => 'UNENCLOSED');`。用户裁决（2026-09-16，
// ButtonMappingView.vue:65-68）已删除「配置状态 / 运行时闭口」展示块的 computed：
// 页面**不再呈现** applyStatus / closure 字段 ⇒ 属性以"不再做运行时闭口声明"的方式
// 满足（且更强：连展示面都不存在）。若将来重新引入该展示字段，以下断言会失败。
if (source.includes('targetClosure') || source.includes("computed(() => 'UNENCLOSED')")) {
  throw new Error('button mapping view must not present persisted applyStatus/closure as a runtime claim (2026-09-16 ruling)');
}
if (!source.includes('页面不再呈现内部账本字段')) {
  throw new Error('button mapping view must document the removed runtime-closure presentation block');
}
if (source.includes('snapshot.value?.outputTarget?.closure')) {
  throw new Error('button mapping runtime closure still trusts persisted outputTarget.closure');
}
if (source.includes('InputHost 已确认运行') || source.includes('InputHost 未接受这项配置')) {
  throw new Error('button mapping status still promotes persisted applyStatus to a runtime claim');
}
if (!editor.includes("const closure = computed(() => 'UNENCLOSED');")) {
  throw new Error('shortcut editor runtime closure must fail closed without a runtime receipt');
}
if (editor.includes('snapshot.value?.buttonMapping?.closure')) {
  throw new Error('shortcut editor runtime closure still trusts persisted buttonMapping.closure');
}

const evidence = {
  evidenceId: 'BUS-P48-BUTTON-MAPPING-RUNTIME-READINESS-STATIC-20260905',
  status: 'SPEC-READY',
  systemMutation: false,
  assertions: {
    persistedOutputTargetClosureIgnored: true,
    persistedButtonMappingClosureIgnored: true,
    runtimeReceiptRequiredBeforeClosed: true,
    runtimeClosure: 'UNENCLOSED',
  },
  safeStop: 'runtime-receipt-unproven',
};
const output = resolve(process.cwd(), '../../Build/Validation/GyroVirtual/BUS-P48-button-mapping-runtime-readiness-static-20260905.json');
mkdirSync(resolve(output, '..'), { recursive: true });
writeFileSync(output, JSON.stringify(evidence, null, 2));
console.log(`button mapping runtime readiness selftest: PASS (${output})`);
