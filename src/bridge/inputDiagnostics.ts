import type { InputDiagnosticRecordV1 } from './inputContracts';

/** Diagnostics are observability only; they never admit, retry, or stop input. */
export class InputDiagnosticsBus {
  private enabled = false;
  private records: InputDiagnosticRecordV1[] = [];

  setEnabled(enabled: boolean): void { this.enabled = enabled; }
  isEnabled(): boolean { return this.enabled; }

  append(record: InputDiagnosticRecordV1): boolean {
    if (!this.enabled) return false;
    this.records.push(structuredClone(record));
    return true;
  }

  clear(): void { this.records = []; }
  snapshot(): readonly InputDiagnosticRecordV1[] { return this.records.map((record) => structuredClone(record)); }
  exportSnapshot(): { schemaVersion: 1; exportedAt: string; records: InputDiagnosticRecordV1[] } {
    return { schemaVersion: 1, exportedAt: new Date().toISOString(), records: this.snapshot() as InputDiagnosticRecordV1[] };
  }
}
