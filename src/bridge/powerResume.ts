export interface ResumeCompleteMeta {
  daemonRequired: boolean;
  daemonReady: boolean;
}

export interface PowerResumeTransactionDeps {
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
 * Run one bounded wake transaction. Native input/gate recovery commits first;
 * daemon handle rebuilding is best-effort and runs afterwards. This prevents
 * a slow or broken PawnIO reopen from crossing the native renderer watchdog
 * deadline and reloading a healthy page while all hardware writes are gated.
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
  for (; commitAttempts < commitAttemptsLimit && !lastReason?.startsWith('superseded'); commitAttempts++) {
    if (!isCurrent()) {
      lastReason = 'superseded';
      break;
    }
    const result = await deps.completeResume(generation, { daemonRequired, daemonReady: false }).catch((error) => ({
      ok: false,
      reason: error instanceof Error ? error.message : String(error),
    }));
    if (result.ok) {
      commitAttempts += 1;
      lastReason = undefined;
      break;
    }
    lastReason = result.reason;
    if (commitAttempts + 1 < commitAttemptsLimit) await sleep(commitRetryDelayMs);
  }

  if (lastReason || commitAttempts === 0) {
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
