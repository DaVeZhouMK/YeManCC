// Startup work only: never a hardware owner or a resident polling service.
export class StartupWorkBarrier {
  private tasks: Promise<boolean>[] = [];
  private sealed = false;

  track<T>(work: () => Promise<T>): Promise<T> {
    // Register before executing so synchronous failures are observed too.
    const task = Promise.resolve().then(work);
    if (!this.sealed) this.tasks.push(task.then(() => true, () => false));
    return task;
  }

  async settle(): Promise<boolean> {
    this.sealed = true;
    return (await Promise.all(this.tasks)).every(Boolean);
  }
}

export const initialPageWork = new StartupWorkBarrier();

// A timeout is a failure, NOT permission to show a still-loading normal UI.
// The caller must render an actionable fallback before acknowledging it.
export async function settleStartup(
  work: Promise<boolean>, timeoutMs: number,
): Promise<boolean> {
  let timer: ReturnType<typeof setTimeout> | undefined;
  try {
    return await Promise.race([
      work.catch(() => false),
      new Promise<boolean>((resolve) => { timer = setTimeout(() => resolve(false), timeoutMs); }),
    ]);
  } finally {
    if (timer !== undefined) clearTimeout(timer);
  }
}
