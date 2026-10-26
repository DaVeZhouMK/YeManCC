using System;
using System.Text.Json;
using System.Threading.Tasks;

namespace YeManInputHost.Diagnostics;

// One terminal settlement on the original HostSession. Retry only a known
// pre-Dispose stop timeout, synchronously and at most once on this continuation.
internal static class HostTerminalCleanup
{
    internal const int ReleaseFailedExitCode = 7;
    internal delegate bool Release(out string detail);
    internal readonly record struct Attempt(bool Succeeded, string Detail);
    internal readonly record struct LoopResult(int ExitCode, string Reason, Attempt? ShutdownRelease = null);
    internal sealed record Receipt(int ExitCode, int SourceExitCode, string Reason,
        bool ManagedReleaseComplete, string ResourceDisposition, int ReleaseAttempts,
        int DirectReleaseAttempts, bool Retried, string FirstDetail, string FinalDetail);

    internal static bool CanRetryStopBarrier(string detail)
    {
        if (detail is "steamdeck-idle-stop-failed:InvalidOperationException:steamdeck-idle-handoff:sdk-idle-clock-timeout" ||
            IsAccumulatedStopTimeout(detail)) return true;
        if (string.IsNullOrEmpty(detail)) return false;
        var fields = detail.Split(';');
        if (fields.Length is < 2 or > 3 || !IsAccumulatedStopTimeout(fields[^1])) return false;

        // ReleaseLocalLocked accumulates only Detach, then neutral, before
        // these barriers return without reaching any Dispose or reconcile.
        // The earlier SteamDeck clock barrier never has accumulated details.
        var index = 0;
        if (IsExceptionDetail(fields[index], "game-output-detach:")) index++;
        if (index < fields.Length - 1 && IsNeutralFailure(fields[index])) index++;
        return index == fields.Length - 1;
    }

    private static bool IsAccumulatedStopTimeout(string detail) => detail is
        "controller-output-stop:sdk-output-stop-timeout" or
        "controller-output-stop:sdk-output-stop-lock-timeout" or
        "controller-idle-cts-capture:sdk-idle-clock-timeout";

    private static bool IsNeutralFailure(string detail) => detail is
        "neutral:failed" or "neutral:persona-not-admitted" or "neutral:target-not-ready" ||
        IsExceptionDetail(detail, "neutral:neutral-exception:") || IsExceptionDetail(detail, "neutral:");

    private static bool IsExceptionDetail(string detail, string prefix)
    {
        // Only a simple exception type name is emitted here, never Message.
        // Reject separators, whitespace and embedded cleanup/detail grammar.
        if (!detail.StartsWith(prefix, StringComparison.Ordinal)) return false;
        var name = detail.AsSpan(prefix.Length);
        if (!name.EndsWith("Exception", StringComparison.Ordinal) || name.Length == 0 ||
            !(char.IsAsciiLetter(name[0]) || name[0] == '_')) return false;
        foreach (var c in name)
            if (!(char.IsAsciiLetterOrDigit(c) || c == '_')) return false;
        return true;
    }

    internal static bool IsFailedShutdownResponse(string response)
    {
        // Avoid parsing ordinary frame replies. Only an accepted SHUTDOWN
        // release failure is terminal; protocol/identity rejections stay live.
        if (!response.Contains("\"command\":\"SHUTDOWN\"", StringComparison.Ordinal)) return false;
        using var document = JsonDocument.Parse(response);
        var root = document.RootElement;
        return root.GetProperty("status").GetString() == "shutdown-local-dispose-failed" &&
            root.GetProperty("ok").ValueKind == JsonValueKind.False;
    }

    internal static LoopResult ShutdownResult(string response)
    {
        // The dispatcher already attempted release before returning shutdown=true.
        // Keep a failed neutral/reconcile/detach receipt even if SDK owners are null.
        using var document = JsonDocument.Parse(response);
        var root = document.RootElement;
        var ok = root.GetProperty("ok").GetBoolean();
        var detail = root.GetProperty("detail").GetString() ?? "";
        return new LoopResult(0, "shutdown-ordered", new Attempt(ok, detail));
    }

    internal static async Task<int> RunAsync(Func<Task<LoopResult>> run, Release release,
        Action<string, string> log)
    {
        LoopResult outcome;
        try { outcome = await run().ConfigureAwait(false); }
        catch (Exception ex)
        {
            Log(log, "fatal-exception", ex.GetType().Name + ": " + ex.Message);
            outcome = new LoopResult(4, "fatal-exception");
        }
        var receipt = Settle(outcome, release);
        Log(log, receipt.ManagedReleaseComplete ? "terminal-cleanup-complete" : "terminal-cleanup-failed",
            JsonSerializer.Serialize(receipt));
        return receipt.ExitCode;
    }

    internal static Receipt Settle(LoopResult outcome, Release release)
    {
        int direct = 0;
        Attempt Invoke()
        {
            ++direct;
            try { var ok = release(out var detail); return new Attempt(ok, detail ?? ""); }
            catch (Exception ex) { return new Attempt(false, "terminal-release-exception:" + ex.GetType().Name + ":" + ex.Message); }
        }
        var first = outcome.ShutdownRelease ?? Invoke();
        var final = first;
        bool retry = !first.Succeeded && CanRetryStopBarrier(first.Detail);
        if (retry) final = Invoke();
        var code = final.Succeeded || outcome.ExitCode != 0 ? outcome.ExitCode : ReleaseFailedExitCode;
        return new Receipt(code, outcome.ExitCode, outcome.Reason, final.Succeeded,
            final.Succeeded ? "managed-release-complete" : CanRetryStopBarrier(final.Detail) ? "owner-retained" : "release-unconfirmed",
            direct + (outcome.ShutdownRelease.HasValue ? 1 : 0), direct, retry, first.Detail, final.Detail);
    }

    private static void Log(Action<string, string> log, string reason, string detail)
    {
        try { log(reason, detail); }
        catch { /* Log loss must not change the terminal cleanup result. */ }
    }
}
