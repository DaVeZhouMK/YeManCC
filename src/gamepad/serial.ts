// A controller-owned async lane.
//
// Gamepad actions are intentionally serialized here instead of sharing one of
// the background/application queues.  A slow game-recognition scan or a
// settings refresh must not reorder a controller navigation/action, while a
// controller burst must also not start several route/hardware writes at once.

type GamepadTask<T> = () => T | PromiseLike<T>;

let tail: Promise<void> = Promise.resolve();
let pending = 0;

export function enqueueGamepadTask<T>(task: GamepadTask<T>): Promise<T> {
  pending += 1;
  const run = tail.then(task, task);
  tail = run.then(
    () => {
      pending = Math.max(0, pending - 1);
    },
    () => {
      pending = Math.max(0, pending - 1);
    },
  );
  return run;
}

export function enqueueGamepadTaskDetached(task: GamepadTask<void>): void {
  void enqueueGamepadTask(task).catch((error) => {
    console.error('[gamepad serial]', error);
  });
}

export function gamepadSerialPending(): number {
  return pending;
}

