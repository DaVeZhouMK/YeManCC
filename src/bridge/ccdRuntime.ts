// Performance entry for the existing native CCD lease. No boot preset or settings writes.
import { computed, reactive } from 'vue';
import { probeCcd, setCcdMode, type CcdProbe, type CcdSetReceipt } from './cpuctl';

export const ccdState = reactive({
  capability: null as CcdProbe | null,
  mode: 0,
  busy: false,
  receipt: null as CcdSetReceipt | null,
  notice: '',
});
export const ccdAvailable = computed(() => ccdState.capability?.performanceSupported === true &&
  ccdState.capability.supported === true &&
  ccdState.capability.available === true);
export const ccdOptions = computed(() => [
  { value: 0, label: 'CCD · 全部' },
  ...(ccdState.capability?.domains ?? []).map(d => ({
    value: d.index, label: `CCD · ${d.index}`, sub: `${d.logical} 个逻辑处理器`,
  })),
]);
let probe: Promise<void> | null = null;
let noticeTimer: ReturnType<typeof setTimeout> | undefined;
function notify(text: string) {
  if (noticeTimer) clearTimeout(noticeTimer);
  ccdState.notice = text;
  noticeTimer = setTimeout(() => { ccdState.notice = ''; noticeTimer = undefined; }, 4000);
}
export async function refreshCcdRuntime(): Promise<void> {
  if (ccdState.busy) return;
  if (probe) return probe;
  const pending = probeCcd().then(capability => {
    ccdState.capability = capability;
    const mode = capability.lease?.active ? capability.lease.mode : 0;
    ccdState.mode = ccdOptions.value.some(o => o.value === mode) ? mode : 0;
  });
  probe = pending;
  try { await pending; } finally { if (probe === pending) probe = null; }
}
export async function selectCcdMode(value: number | string): Promise<void> {
  if (ccdState.busy) return;
  // Wait for an in-flight read before writing so its old lease cannot replace the ACK.
  if (probe) await probe;
  if (ccdState.busy || !ccdAvailable.value || typeof value !== 'number' ||
      !ccdOptions.value.some(o => o.value === value) ||
      (value === ccdState.mode && (!ccdState.receipt || ccdState.receipt.applicationState === 'applied'))) return;
  ccdState.busy = true;
  ccdState.notice = '';
  try {
    const result = await setCcdMode(value);
    if (!result.ok || !result.receipt) {
      notify(result.msg || 'CCD 调度未应用');
      return;
    }
    ccdState.mode = value;
    ccdState.receipt = result.receipt;
    notify(result.receipt.applicationState === 'applied'
      ? 'CCD 调度：首轮目标亲和性已回读'
      : result.receipt.applicationState === 'failed'
        ? 'CCD 请求已接受，首轮目标未应用成功'
        : 'CCD 请求已接受，部分目标尚未确认');
  } catch {
    notify('CCD 调度请求失败，请重试');
  } finally {
    ccdState.busy = false;
  }
}
