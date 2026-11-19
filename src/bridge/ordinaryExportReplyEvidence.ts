export interface OrdinaryExportOperation {
  schema: 1;
  id: string;
  writerPid: number;
  writerProcessCreation100ns: string;
  ordinal: string;
  startedTickMs: string;
  scope: 'admitted-ordinary-log-export;diagnostic-correlation-only';
}

interface ReplyEvidence {
  schema: 1;
  exportOperation: OrdinaryExportOperation;
  replyOk: boolean;
  elapsedMs: number;
}

let lastReply: ReplyEvidence | undefined;

export function takeOrdinaryExportReplyEvidence(): ReplyEvidence | undefined {
  const reply = lastReply;
  lastReply = undefined;
  return reply;
}

export function rememberOrdinaryExportReply(result: {
  ok?: boolean;
  path?: string;
  exportOperation?: Partial<OrdinaryExportOperation>;
} | undefined, elapsed: number): void {
  const op = result?.exportOperation;
  const elapsedMs = Math.round(elapsed);
  if (!result || typeof result.ok !== 'boolean' || (result.ok && (typeof result.path !== 'string' || !result.path)) ||
      !Number.isSafeInteger(elapsedMs) || elapsed < 0 ||
      !op || op.schema !== 1 || op.scope !== 'admitted-ordinary-log-export;diagnostic-correlation-only' ||
      typeof op.writerPid !== 'number' || !Number.isInteger(op.writerPid) || op.writerPid < 1 || op.writerPid > 0xffffffff ||
      typeof op.writerProcessCreation100ns !== 'string' || !/^[1-9][0-9]{0,19}$/.test(op.writerProcessCreation100ns) ||
      typeof op.ordinal !== 'string' || !/^[1-9][0-9]{0,19}$/.test(op.ordinal) ||
      typeof op.startedTickMs !== 'string' || !/^(0|[1-9][0-9]{0,19})$/.test(op.startedTickMs) ||
      op.id !== `${op.writerPid}-${op.startedTickMs}-${op.ordinal}`) return;

  const { schema, id, writerPid, writerProcessCreation100ns, ordinal, startedTickMs, scope } = op;
  lastReply = { schema: 1, exportOperation: { schema, id, writerPid, writerProcessCreation100ns, ordinal, startedTickMs, scope }, replyOk: result.ok, elapsedMs };
}
