/** Only settingsRepository imports this port; every path is confined by the runner. */
export const fs={exists:(...args:any[])=>globalThis.__bootDiskPort.exists(...args),readTextFile:(...args:any[])=>globalThis.__bootDiskPort.readTextFile(...args),rename:(...args:any[])=>globalThis.__bootDiskPort.rename(...args)};
export const settingsStore={location:()=>globalThis.__bootDiskPort.location(),read:(...args:any[])=>globalThis.__bootDiskPort.read(...args),write:(...args:any[])=>globalThis.__bootDiskPort.write(...args)};
