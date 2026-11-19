import { computeInputConfigHash } from './inputConfigHash';
import { isControllerPersona, type ControllerPersona } from './controllerPersona';

export interface InputApplicationRequest {
  revision: number;
  hash: string;
  persona: ControllerPersona;
  target: boolean;
  gyro: boolean;
}
export interface InputApplicationProof {
  protocol: 'ymcc.input-application.v1';
  boot: string;
  inputRevision: number;
  inputConfigHash: string;
  persona: ControllerPersona;
  inputState: 'waiting' | 'active' | 'disabled';
  gyroState: 'waiting' | 'active' | 'disabled';
  runId: string;
  hostInstanceId: string;
  hostEpoch: string;
  powerGeneration: string;
  outputSequence: string;
  sourceSequence: string;
  sourceIdentity: number;
  sourceTopology: number;
  meaning: 'current-config-local-host-frame;not-game-consumption';
}
const record = (value: unknown): value is Record<string, any> => !!value && typeof value === 'object' && !Array.isArray(value);
const hash = (value: unknown): value is string => typeof value === 'string' && /^sha256:[0-9a-f]{64}$/.test(value);
const revision = (value: unknown): value is number => typeof value === 'number' && Number.isSafeInteger(value) && value >= 0;
const counter = (value: unknown, positive = false): value is string => typeof value === 'string' && /^(0|[1-9][0-9]{0,19})$/.test(value) &&
  BigInt(value) <= 18446744073709551615n && (!positive || value !== '0');
const text = (value: unknown, empty = false): value is string => typeof value === 'string' && value.length <= 4096 && (empty || value.length > 0);

/** Identity of the actual committed configuration, not a draft or an effective overlay. */
export function inputApplicationRequest(input: Record<string, any>): InputApplicationRequest {
  const target = record(input.outputTarget) ? input.outputTarget : {}, motion = record(input.gyroMotion) ? input.gyroMotion : {};
  if (!revision(input.revision) || !isControllerPersona(target.persona)) throw new Error('已保存输入配置的身份无效');
  return { revision: input.revision, hash: computeInputConfigHash(input), persona: target.persona,
    target: target.persona !== 'disabled' && (target.buttonMappingEnabled === true || target.gyroEnabled === true),
    gyro: target.gyroEnabled === true && motion.enabled === true };
}
export function readInputApplicationRequest(raw: unknown): InputApplicationRequest | undefined {
  if (!record(raw) || !revision(raw.revision) || !hash(raw.hash) || !isControllerPersona(raw.persona) ||
      typeof raw.target !== 'boolean' || typeof raw.gyro !== 'boolean') return;
  return { revision: raw.revision, hash: raw.hash, persona: raw.persona, target: raw.target, gyro: raw.gyro };
}

/** Validate the native producer's complete proof and copy all primitives before awaiting. */
export function readInputApplicationProof(raw: unknown): InputApplicationProof | undefined {
  if (!record(raw) || raw.protocol !== 'ymcc.input-application.v1' || !text(raw.boot) || !revision(raw.inputRevision) ||
      !hash(raw.inputConfigHash) || !isControllerPersona(raw.persona) ||
      !['waiting', 'active', 'disabled'].includes(raw.inputState) || !['waiting', 'active', 'disabled'].includes(raw.gyroState) ||
      !text(raw.runId, true) || !text(raw.hostInstanceId, true) ||
      !['hostEpoch', 'powerGeneration', 'outputSequence', 'sourceSequence'].every(key => counter(raw[key])) ||
      !revision(raw.sourceIdentity) || raw.sourceIdentity > 0xffffffff || !revision(raw.sourceTopology) || raw.sourceTopology > 0xffffffff ||
      raw.meaning !== 'current-config-local-host-frame;not-game-consumption') return;
  if (raw.gyroState === 'active' && raw.inputState !== 'active' || raw.persona === 'disabled' && raw.inputState === 'active') return;
  if (raw.inputState === 'active' && (!text(raw.runId) || !text(raw.hostInstanceId) ||
      !['hostEpoch', 'powerGeneration', 'outputSequence', 'sourceSequence'].every(key => counter(raw[key], true)))) return;
  return { protocol: raw.protocol, boot: raw.boot, inputRevision: raw.inputRevision, inputConfigHash: raw.inputConfigHash,
    persona: raw.persona, inputState: raw.inputState, gyroState: raw.gyroState, runId: raw.runId, hostInstanceId: raw.hostInstanceId,
    hostEpoch: raw.hostEpoch, powerGeneration: raw.powerGeneration, outputSequence: raw.outputSequence, sourceSequence: raw.sourceSequence,
    sourceIdentity: raw.sourceIdentity, sourceTopology: raw.sourceTopology, meaning: raw.meaning };
}

/** The enclosing observation must still identify this Host/persona and source. */
export function inputApplicationFromGamepad(raw: unknown): InputApplicationProof | undefined {
  if (!record(raw) || !record(raw.virtualOutput)) return;
  const output = raw.virtualOutput, proof = readInputApplicationProof(output.inputApplication);
  if (!proof || output.requestedPersona !== proof.persona) return;
  if (proof.inputState === 'active' && (output.boundPersona !== proof.persona || !revision(output.hostEpoch) ||
      String(output.hostEpoch) !== proof.hostEpoch || output.hostAlive !== true || output.prepared !== true ||
      output.neutralized !== true || output.frameAccepted !== true || output.sourceLive !== true || output.sourceCurrent !== true ||
      output.physicalReturnPending === true || output.physicalReturnFailed === true)) return;
  if (proof.inputState === 'disabled' && (output.prepared !== false ||
      output.physicalReturnPending !== false || output.physicalReturnFailed !== false)) return;
  return proof;
}

export function sameInputApplication(request: InputApplicationRequest, proof: InputApplicationProof): boolean {
  return request.revision === proof.inputRevision && request.hash === proof.inputConfigHash && request.persona === proof.persona;
}
