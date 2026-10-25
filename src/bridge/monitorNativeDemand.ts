// Resource hints on the existing unified monitor demand, never a data reader.
export interface MonitorDemandNeed { top?: boolean; fps?: boolean; fileSystem?: boolean; }
export function monitorFileSystemDemand(need: MonitorDemandNeed): boolean {
  // Unspecified/unknown callers preserve the complete legacy top channel.
  return need.top === true && need.fileSystem !== false;
}
