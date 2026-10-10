// Ephemeral testing in the already running Loader. Reuse its existing RouterHook:
// do NOT connect @decky/api again with the original plugin name (that replaces
// the Loader's plugin event-listener map). Production index uses its normal SDK once.
import { installSteamHudProbe } from '../../decky-plugin/src/steamHudProbe';
declare const SP_REACT:any;
declare const DFL:any;
installSteamHudProbe({host:window,react:SP_REACT,ui:DFL,routerHook:(window as any).DeckyPluginLoader?.routerHook});
