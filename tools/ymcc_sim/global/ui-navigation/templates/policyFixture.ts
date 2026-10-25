const listeners=new Set();
export const getPolicyGame=()=>window.__fixture?.game||null;
export const gamePolicyKey=game=>(game?.path||game?.name||'').split(/[\\/]/).pop().toLowerCase();
export const detectPolicyGame=async()=>getPolicyGame();
export const isPolicyGameInitialized=()=>true;
export const subscribePolicyGameStatus=cb=>{listeners.add(cb);queueMicrotask(()=>cb(getPolicyGame()));return()=>listeners.delete(cb)};
window.__setPolicyGame=game=>{window.__fixture.game=game;for(const cb of [...listeners])cb(game)};
// The current product adds a strict refresh export. This UI-only fixture
// supplies an explicit in-memory observation; game recovery is tested by global.
export const refreshPolicyGameStrict=async()=>getPolicyGame();
