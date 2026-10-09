/**
 * Curved-array editing contract shared by GyroMotionView (responseCurvePoints /
 * motionSensitivityArray) and gyro_ui_forward_reverse_selftest.
 *
 * Mirrors the settingsRepository normalize rules and native main.cpp validation
 * (inputCaptureRefreshRealStickSettings 5263-5288 / 5215): a custom curve must
 * contain at least two finite 0..1 points with strictly ascending keys, else it
 * fails closed to null (absent = HC default curve / linear identity). Invalid
 * edits must never persist.
 */
import { createDefaultMotionSensitivityArray, HC_DEFAULT_RESPONSE_CURVE, type SensitivityNode } from './hcInputUtils';

export type CurvePair = SensitivityNode;
export type CurvePairs = ReadonlyArray<readonly [number, number]>;
export type EditableCurve = CurvePairs | null;

export interface CurveRow {
  key: number | null;
  value: number | null;
}

/** HC AxisActions default 6-point linear identity curve (AxisActions.cs:36-44). */
export function defaultResponseCurvePairs(): CurvePairs {
  return HC_DEFAULT_RESPONSE_CURVE;
}

/** HC editor default 49 nodes at i/48, value 0.5 (MotionManager CustomSensitivity default). */
export function defaultSensitivityCurvePairs(): CurvePairs {
  return createDefaultMotionSensitivityArray();
}

export function validCurveValue(value: number | null): value is number {
  return value !== null && Number.isFinite(value) && value >= 0 && value <= 1;
}

/** Build editor draft rows: the persisted custom curve when valid, else the defaults. */
export function draftCurveRows(value: EditableCurve, defaults: CurvePairs): CurveRow[] {
  const source = value && value.length >= 2 ? value : defaults;
  return source.map(([key, value]) => ({ key, value }));
}

/** Fail-closed commit: invalid row / duplicate or non-ascending key / fewer than 2 rows -> null. */
export function commitCurveRows(rows: readonly CurveRow[]): EditableCurve {
  if (!Array.isArray(rows) || rows.length < 2) return null;
  if (rows.some((row) => !validCurveValue(row.key) || !validCurveValue(row.value))) return null;
  const sorted = [...rows].sort((left, right) => (left.key as number) - (right.key as number));
  for (let i = 1; i < sorted.length; i += 1) {
    if ((sorted[i].key as number) <= (sorted[i - 1].key as number)) return null;
  }
  return sorted.map((row) => [row.key as number, row.value as number]);
}

/** Human-readable draft error; null when the draft is committable. */
export function curveRowsIssue(rows: readonly CurveRow[], emptyHint: string): string | null {
  if (!Array.isArray(rows) || rows.length < 2) return emptyHint;
  for (const row of rows) {
    if (!validCurveValue(row.key) || !validCurveValue(row.value)) return '输入与输出值必须在 0..1 之间';
  }
  for (let i = 1; i < rows.length; i += 1) {
    if ((rows[i].key as number) <= (rows[i - 1].key as number)) return '输入值必须严格递增（不能重复或乱序）';
  }
  return null;
}