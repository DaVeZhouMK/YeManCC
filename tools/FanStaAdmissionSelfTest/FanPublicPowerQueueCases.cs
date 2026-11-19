using System.Reflection;

internal static class FanPublicPowerQueueCases
{
    private const BindingFlags Flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
    private static object? Field(object target, string name) => target.GetType().GetField(name, Flags)!.GetValue(target);
    private static object? Call(object target, string name, params object?[] values)
    {
        var type = target as Type ?? target.GetType();
        var method = type.GetMethods(Flags).Single(item => item.Name == name && !item.IsGenericMethod && item.GetParameters().Length >= values.Length && item.GetParameters().Skip(values.Length).All(item => item.IsOptional));
        try { return method.Invoke(target is Type ? null : target, values.Concat(Enumerable.Repeat<object?>(Type.Missing, method.GetParameters().Length - values.Length)).ToArray()); }
        catch (TargetInvocationException error) when (error.InnerException is not null) { throw error.InnerException; }
    }
    private static Exception? Failure(Action action) { try { action(); return null; } catch (Exception error) { return error; } }
    private static string Code(Exception? error) => error?.GetType().GetProperty("Code", Flags)?.GetValue(error)?.ToString() ?? "none";

    internal static void Run(Assembly host, Action<string, bool, string> check, Action<object> scenario, string selection)
    {
        var names = new[] { "queue-public-retain", "queue-public-revoked", "queue-public-stop", "queue-public-rejected", "queue-public-no-capture" };
        var from = selection.StartsWith("queue-from:", StringComparison.Ordinal) ? selection[11..] : null;
        if (selection != "queue-all" && !names.Contains(from ?? selection, StringComparer.Ordinal)) throw new ArgumentException("Unknown explicit public power queue case");
        var selected = from is not null ? names[Array.IndexOf(names, from)..] : names.Where(value => selection == "queue-all" || value == selection);
        foreach (var name in selected)
        {
            Type T(string type) => host.GetType("YeManFanHost." + type, true)!;
            var options = Call(T("HostOptions"), "Parse", new object[] { new[] { "--mock-handshake", "--mock-zero-hardware-evidence" } })!;
            var engine = Activator.CreateInstance(T("FanHostEngine"), Flags, null, new object?[] { options, null }, null)!;
            var server = Activator.CreateInstance(T("LocalFanHostServer"), Flags, null, new object?[] { engine, options, null }, null)!;
            var context = engine.GetType().GetProperty("SharedPowerContext", Flags)!.GetValue(engine)!;
            var target = Enum.Parse(T("PowerIngressTarget"), "Ready");
            var metadata = Activator.CreateInstance(T("PowerEventMetadata"), 2L, "api.resume")!;
            var completion = new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously);
            using var source = FanPublicPowerScopePort.Create(host, 2);
            using var stop = new CancellationTokenSource();
            IDisposable? request = null;
            Task? worker = null;
            var actions = 0;
            var exactScope = false;
            var ok = true;
            void C(string text, bool value) { ok &= value; check(name + ": " + text, value, ""); }
            Func<object> action = () =>
            {
                Interlocked.Increment(ref actions);
                using var borrowed = (IDisposable)Call(context, "Capture")!;
                var binding = borrowed.GetType().GetProperty("Binding", Flags)!.GetValue(borrowed)!;
                var requestKey = binding.GetType().GetField("Request")!.GetValue(binding)!;
                var expectedBinding = typeof(FanPublicPowerScopePort).GetField("binding", Flags)!.GetValue(source)!;
                exactScope = requestKey.Equals(expectedBinding.GetType().GetField("Request")!.GetValue(expectedBinding));
                return new object();
            };
            object Queue() => Call(server, "QueuePowerTransition", "resume", target, metadata, action, completion)!;
            try
            {
                var ingress = Field(server, "powerIngress")!;
                var suspended = Enum.Parse(T("PowerIngressTarget"), "Suspended");
                C("preceding edge is actual ingress suspend", Call(ingress, "TryPublish", suspended, metadata, (Func<bool>)(() => true))!.ToString() == "Queued");
                if (name == "queue-public-no-capture") C("legacy item queued before import", Queue().ToString() == "Queued");
                request = (IDisposable)Call(engine, "BindImportedPowerForRequest", source, source.Purpose)!;
                if (name == "queue-public-rejected")
                {
                    var channel = Field(server, "powerTransitions")!;
                    var writer = channel.GetType().GetProperty("Writer")!.GetValue(channel)!;
                    C("original writer closes", Call(writer, "TryComplete", new object?[] { null }) is true);
                }
                if (name != "queue-public-no-capture")
                    C("original ingress outcome is exact", Queue().ToString() == (name == "queue-public-rejected" ? "Rejected" : "Queued"));
                if (name == "queue-public-revoked") source.Revoke();
                request.Dispose(); request = null;
                if (name is not ("queue-public-rejected" or "queue-public-no-capture"))
                    C("accepted queue retains exact import after HTTP owner", source.Disposals == 0);
                else C("no accepted scope has no extra borrower", source.Disposals == 1);
                if (name == "queue-public-stop") stop.Cancel();
                // Start the actual original Channel reader only after the HTTP
                // context was restored. It must not inherit an imported frame.
                worker = (Task)Call(server, "ProcessPowerTransitionsAsync", stop.Token)!;
                if (name == "queue-public-rejected")
                {
                    if (!worker.Wait(2000)) throw new TimeoutException("closed queue reader not returned");
                    C("rejected queue never executes", actions == 0);
                }
                else
                {
                    if (!SpinWait.SpinUntil(() => completion.Task.IsCompleted, 2000)) throw new TimeoutException("actual queue did not settle completion");
                }
                var completionError = completion.Task.Exception?.InnerException;
                if (name == "queue-public-retain") C("actual reader enters original scope", actions == 1 && exactScope && completionError is null);
                if (name is "queue-public-revoked" or "queue-public-no-capture")
                    C("unowned or cancelled transition never enters action", actions == 0 && Code(completionError) == "FAN_PUBLIC_POWER_SUPERSEDED");
                if (name == "queue-public-stop") C("unexecuted accepted item gets terminal cancellation", actions == 0 && completion.Task.IsCompleted);
                C("returned queue borrower closes only original import", source.Disposals == 1 && source.HandlesClosed);
                C("queue and HTTP do not manufacture resource ACK", source.Settlements == 0);
            }
            catch (Exception error) { ok = false; check(name + ": actual queue scenario completed", false, error.ToString()); }
            finally
            {
                request?.Dispose();
                stop.Cancel();
                if (worker is null) worker = (Task)Call(server, "ProcessPowerTransitionsAsync", stop.Token)!;
                var returned = worker.Wait(2000);
                C("actual reader returned before owner cleanup", returned);
                if (returned) { Call(server, "Dispose"); Call(engine, "Dispose"); }
            }
            scenario(new { name, ok, actualHostChannel = true, actualNativeImporter = false, physicalWrites = 0 });
            if (!ok) return;
        }
    }
}
