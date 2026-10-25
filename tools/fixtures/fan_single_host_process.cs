using YeManFanHost;
var mode = args[0];
IDisposable? lease;
if (mode.StartsWith("legacy")) lease = LegacyHostInstanceLease.TryAcquire();
else {
    var owned = HostInstanceLease.TryAcquire();
    owned?.HoldUntilProcessExit();
    lease = owned;
}
if (lease is null) return 2;
using (lease) {
    if (mode.EndsWith("check")) {
        if (args.Length > 1) File.WriteAllText(args[1], Environment.ProcessId.ToString());
        return 0;
    }
    if (mode == "deferred" || mode == "deferred-stuck") {
        using var launcher = DeferredHostSuccessor.Start(Environment.ProcessPath!, new[] { "check", args[1] }, mode == "deferred" ? 6000 : 1200);
        File.WriteAllText(args[2], launcher.Id.ToString());
        Console.WriteLine("READY"); Console.Out.Flush();
        if (mode == "deferred") { Thread.Sleep(1000); return 0; }
    }
    if (mode.StartsWith("deadline")) {
        Action<string,object?> record = (name, details) => { Console.WriteLine(name); Console.Out.Flush(); };
        if (mode == "deadline-logblocked") record = (_,_) => Thread.Sleep(Timeout.Infinite);
        var watchdog = new HostProcessLifetime(record);
        watchdog.Arm("injected-close-stuck", 900);
        watchdog.Arm("late-duplicate-stop-must-not-extend", 9000);
        if (mode == "deadline-processexit") AppDomain.CurrentDomain.ProcessExit += (_,_) => Thread.Sleep(Timeout.Infinite);
    }
    new Thread(() => Thread.Sleep(Timeout.Infinite)) { IsBackground = false }.Start();
    Console.WriteLine("READY"); Console.Out.Flush();
    return 0; // old Main retired, foreground STA still alive
}
