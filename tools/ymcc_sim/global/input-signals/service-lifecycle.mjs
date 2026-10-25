// esbuild unrefs its service process. Retain only this caller's owned children
// until their terminal records have actually arrived. No timers or retries.
export async function stopInputCompilerServices(children,stop,terminals) {
  for(const child of children)child.ref();
  stop();
  await Promise.all(terminals);
}
