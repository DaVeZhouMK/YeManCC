using System.Text.Json;
namespace YeManBatteryHost;

// One existing helper/pipe, independent lazy domains. Display-only use never opens charge settings or replays charge journals.
internal sealed class HostDomainRouter(Func<Controller> createBattery,
    Func<Func<bool>,SecondaryDisplayController> createDisplay,Func<bool>? parentReady=null)
{
    readonly object gate=new();
    Controller? battery;
    SecondaryDisplayController? display;
    volatile bool suspended,stopped;
    bool Ready()=>!suspended&&!stopped&&(parentReady?.Invoke()??true);
    Controller Battery()
    {
        if(battery is not null)return battery;
        if(!Ready())throw new IOException("电池会话尚未就绪，未创建或恢复充电控制器");
        battery=createBattery();battery.RecoverSoftware();return battery;
    }
    public object Dispatch(JsonElement request)
    {
        lock(gate)
        {
            string op=request.GetProperty("op").GetString()??"";
            if(stopped)throw new IOException("辅助进程已停止接收操作");
            if(op.StartsWith("secondaryDisplay.",StringComparison.Ordinal))
            {
                if(op is not ("secondaryDisplay.get" or "secondaryDisplay.set" or "secondaryDisplay.toggle" or "secondaryDisplay.release"))throw new IOException("未知副屏操作");
                if(display is null&&!Ready())throw new IOException("副屏会话尚未就绪");
                display??=createDisplay(Ready);
                return op switch
                {
                    "secondaryDisplay.get"=>display.Read(),
                    "secondaryDisplay.set"=>display.Apply(request.GetProperty("fence").GetString()??"",request.GetProperty("target").GetInt16()),
                    "secondaryDisplay.toggle"=>display.Toggle(request.GetProperty("fence").GetString()??""),
                    _=>display.Release()
                };
            }
            if(op is not ("get" or "refresh" or "release" or "set" or "restore"))throw new IOException("未知电池操作");
            var current=Battery();
            return Program.DispatchBattery(current,request);
        }
    }
    public void Tick(){lock(gate){if(!stopped)battery?.Tick();}}
    public void Suspend(){suspended=true;lock(gate){display?.Suspend();battery?.Suspend();}}
    public void Resume(){lock(gate){if(stopped)return;suspended=false;display?.Resume();battery?.Resume();}}
    public void Shutdown(){stopped=true;lock(gate){display?.Shutdown();battery?.Shutdown();}}
}
