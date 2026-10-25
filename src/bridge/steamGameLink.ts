import { invoke, isNativeRuntime } from './ipc';

export interface SteamNativeFocusTuple {
  appid: number;
  pid: number;
  windowid: number;
  exeName: string | null;
}

/** Native callback receipt; native cache age and process birth are unknown. */
export interface SteamNativeFocusFeedback {
  implementation: 'steam-native-focus-feedback-v1';
  authority: 'diagnostic-only-no-admission';
  available: boolean;
  focusedRelation: string;
  focusableRelation: string;
  originalIdentityBracketMatched?: boolean;
  fullGameIdCarried: false;
  processBirthCarried: false;
  nativeCacheFreshnessKnown: false;
  originalInstanceReattached: false;
  gameInputVerified: false;
  continueVerified: false;
  receipt?: {
    source: 'steam-native-focus-subscription';
    available: boolean;
    reason: string;
    registrationAttempted: boolean;
    callbackReceived: boolean;
    ownSubscriptionUnregistered: boolean;
    readElapsedMs?: number;
    callbackAgeAtReadbackMs?: number | null;
    focused: SteamNativeFocusTuple | null;
    focusable: SteamNativeFocusTuple[] | null;
  };
}

/** Observed association only. No method here grants Steam/game/device writes. */
export interface SteamAppLifetimeEvent {
  sequence: number;
  receivedAtUnixMs: number;
  receivedAtPerformanceMs: number;
  unAppID: number;
  /** Steam session instance identity; never an OS PID or process birth. */
  nInstanceID: string;
  bRunning: boolean;
}

export interface SteamNativeAppLifetimeFeedback {
  implementation: 'steam-app-lifetime-feedback-v1';
  authority: 'diagnostic-only-no-admission';
  available: boolean;
  eventRelation: string;
  instanceRelation: string;
  originalAppRunningKnown: boolean;
  originalAppRunning: boolean;
  latestEventSequence: number;
  originalEventCount: number;
  otherAppEventCount: number;
  schemaInvalidEvents: number;
  fullGameIdCarried: false;
  processBirthCarried: false;
  gameInputVerified: false;
  continueVerified: false;
  lastOriginalSteamInstanceID?: string;
  receipt?: Record<string, unknown>;
  events?: SteamAppLifetimeEvent[];
}

export interface SteamGameLinkSnapshot {
  schemaVersion: 1;
  implementation?: 'steam-running-link-v1';
  phase: string;
  reason?: string;
  readOnly: true;
  linked: boolean;
  /** Original game and Steam identity evidence agree in one read-only sample. */
  contactConnected: boolean;
  /** A same-account, non-mutating Steam request/response was observed. */
  transportConnected: boolean;
  active: boolean;
  fresh?: boolean;
  repairAuthorized: false;
  gameInputVerified: false;
  continueButtonVerified: false;
  steamWrites: 0;
  gameLaunches: 0;
  focusWrites: 0;
  deviceWrites: 0;
  sleepWrites: 0;
  linkSerial?: string;
  configEpoch?: string;
  observationSequence?: string;
  originalGame?: { pid: number; created: string; path: string; windowsSession: number };
  appid?: number;
  trackerGameIdExactString?: string;
  verifiedReadAvailable?: boolean;
  gameIdentitySame?: boolean;
  steamIdentitySame?: boolean;
  accountMatched?: boolean;
  trackerExact?: boolean;
  nativePrimaryExact?: boolean;
  ymccSinglePidAccepted?: boolean;
  nativeOverviewAssociated?: boolean;
  nativeFocusFeedback?: SteamNativeFocusFeedback;
  nativeAppLifetimeFeedback?: SteamNativeAppLifetimeFeedback;
  observedAtTick?: number;
  snapshotAtTick?: number;
  dueTick?: number;
  readFailures?: number;
  sharedCandidate?: { status: string; pid?: number; created?: string; modified: false };
  osWindow?: { source: string; available: boolean; hwnd: string; SteamWindowIdMapping: 'unknown' };
  osForeground?: { role: string; pid: number; created: string; readOnly: true };
  history?: unknown[];
}

export function getSteamGameLink(): Promise<SteamGameLinkSnapshot> {
  if (!isNativeRuntime) {
    return Promise.resolve({
      schemaVersion: 1, phase: 'unavailable', reason: 'native-runtime-required',
      readOnly: true, linked: false, contactConnected: false, transportConnected: false, active: false, fresh: false,
      repairAuthorized: false, gameInputVerified: false, continueButtonVerified: false,
      steamWrites: 0, gameLaunches: 0, focusWrites: 0, deviceWrites: 0, sleepWrites: 0,
    });
  }
  return invoke<SteamGameLinkSnapshot>('steamGameLink.get', {});
}
