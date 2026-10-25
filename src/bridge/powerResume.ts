export interface ResumeCompleteMeta {
  daemonRequired: boolean;
  daemonReady: boolean;
}

export interface PowerResumeTransactionDeps {
  /**
   * HC SystemReady resumes managers and opens the device fire-and-forget: the
   * app becomes Ready immediately and the device catches up in the background.
   * The FanHost F5 therefore never gates the native Ready commit. It is
   * triggered separately on `power.resume-ready` and observed by the fan
   * coordinator; the commit below runs without waiting for it.
   */
  resumeDaemon: (required: boolean) => Promise<boolean>;
  completeResume: (generation: number, meta: ResumeCompleteMeta) => Promise<{ ok: boolean; reason?: string }>;
  isGenerationCurrent?: () => boolean;
  sleep?: (ms: number) => Promise<void>;
  daemonAttempts?: number;
  commitAttempts?: number;
  daemonRetryDelayMs?: number;
  commitRetryDelayMs?: number;
}

export interface PowerResumeTransactionResult {
  committed: boolean;
  daemonRequired: boolean;
  daemonReady: boolean;
  daemonAttempts: number;
  commitAttempts: number;
  reason?: string;
}

const defaultSleep = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms));

/**
 * Run one bounded wake transaction. Native Ready is committed first; the
 * daemon rebuild remains best-effort afterwards. App starts FanHost F5 from
 * power.resume-ready independently, so this transaction never waits for it.
 */
export async function runPowerResumeTransaction(
  generation: number,
  daemonRequired: boolean,
  deps: PowerResumeTransactionDeps,
): Promise<PowerResumeTransactionResult> {
  const sleep = deps.sleep ?? defaultSleep;
  const daemonAttemptsLimit = Math.max(1, Math.min(5, Math.floor(deps.daemonAttempts ?? 2)));
  const commitAttemptsLimit = Math.max(1, Math.min(5, Math.floor(deps.commitAttempts ?? 3)));
  const daemonRetryDelayMs = Math.max(0, deps.daemonRetryDelayMs ?? 350);
  const commitRetryDelayMs = Math.max(0, deps.commitRetryDelayMs ?? 250);
  const isCurrent = () => deps.isGenerationCurrent?.() !== false;

  let commitAttempts = 0;
  let lastReason: string | undefined;
  let commitAccepted = false;

  // HC SystemReady completes synchronously: managers resume and the device
  // open is fire-and-forget in a background task. The fan F5 is already
  // triggered by `power.resume-ready`; the native Ready commit below never
  // waits for it, matching upstream rather than gating the wake on the device.
  for (; commitAttempts < commitAttemptsLimit && !lastReason?.startsWith('superseded'); commitAttempts++) {
    if (!isCurrent()) {
      lastReason = 'superseded';
      break;
    }
    const result = await deps.completeResume(generation, { daemonRequired, daemonReady: false }).catch((error) => ({
      ok: false,
      reason: error instanceof Error ? error.message : String(error),
    }));
    if (result.ok === true) {
      commitAccepted = true;
      commitAttempts += 1;
      lastReason = undefined;
      break;
    }
    lastReason = result.reason;
    if (commitAttempts + 1 < commitAttemptsLimit) await sleep(commitRetryDelayMs);
  }

  // Failure may omit its optional reason; only an explicit ok:true commits.
  if (!commitAccepted) {
    return {
      committed: false,
      daemonRequired,
      daemonReady: false,
      daemonAttempts: 0,
      commitAttempts,
      reason: lastReason ?? 'resume_commit_failed',
    };
  }

  let daemonReady = false;
  let daemonAttempts = 0;
  for (; daemonAttempts < daemonAttemptsLimit && !daemonReady; daemonAttempts++) {
    if (!isCurrent()) {
      return {
        committed: true,
        daemonRequired,
        daemonReady: false,
        daemonAttempts,
        commitAttempts,
        reason: 'superseded_after_commit',
      };
    }
    daemonReady = await deps.resumeDaemon(daemonRequired).catch(() => false);
    if (!daemonReady && daemonAttempts + 1 < daemonAttemptsLimit) {
      await sleep(daemonRetryDelayMs);
    }
  }

  return {
    committed: true,
    daemonRequired,
    daemonReady,
    daemonAttempts,
    commitAttempts,
    reason: daemonReady ? undefined : 'daemon_resume_failed',
  };
}
