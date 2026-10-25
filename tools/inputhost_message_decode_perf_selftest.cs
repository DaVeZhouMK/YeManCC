// R24-H1 fixture: only source-extracted codecs and BCL facilities are compiled.
using System.Diagnostics;
using System.IO.Pipes;
using System.Reflection;
using System.Text;
using System.Text.Json;

internal abstract class CodecState
{
    private readonly object _instance;
    private readonly FieldInfo _write;
    internal readonly MemoryStream Body;
    internal readonly byte[] Buffer;
    protected CodecState(object instance)
    {
        _instance = instance;
        var t = instance.GetType();
        Body = (MemoryStream)t.GetField("_readBody", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(instance)!;
        Buffer = (byte[])t.GetField("_readBuffer", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(instance)!;
        _write = t.GetField("_writeBuffer", BindingFlags.Instance | BindingFlags.NonPublic)!;
    }
    internal byte[] WriteBuffer => (byte[])_write.GetValue(_instance)!;
}
internal sealed class PipeHandle(object value, Func<PipeStream, CancellationToken, Task<string?>> read,
    Func<PipeStream, string, CancellationToken, Task> write) : CodecState(value)
{
    internal readonly Func<PipeStream, CancellationToken, Task<string?>> Read = read;
    internal readonly Func<PipeStream, string, CancellationToken, Task> Write = write;
}
internal sealed class SeamHandle(object value, Func<ReadSeam, CancellationToken, Task<string?>> read,
    Func<PipeStream, string, CancellationToken, Task> write) : CodecState(value)
{
    internal readonly Func<ReadSeam, CancellationToken, Task<string?>> Read = read;
    internal readonly Func<PipeStream, string, CancellationToken, Task> Write = write;
}
internal sealed record ReadStep(byte[] Bytes, bool Complete, Exception? Failure = null);

// One substitution in a separate full extracted class: ReadAsync parameter type.
// Scripts reproduce boundaries/failures. Held reads force asynchronous behavior.
// Transport mode limits actual Windows message-mode reads to force fragmentation.
internal sealed class ReadSeam
{
    private readonly ReadStep[]? _steps;
    private readonly byte[]? _payload;
    private readonly PipeStream? _transport;
    private readonly int _chunk;
    private int _offset, _index;
    private bool _complete;
    internal int ReadCalls, ActiveReads, MaximumOutstanding, CompleteCalls;
    internal readonly List<int>? CompletionReadIndices;
    internal CancellationToken LastToken;
    internal int HoldAtRead = -1, CompletionFailureAt = -1;
    internal Exception? CompletionFailure;
    internal TaskCompletionSource<bool>? Release;
    internal ReadSeam(ReadStep[] steps)
    { _steps = steps; _chunk = 16384; CompletionReadIndices = []; }
    internal ReadSeam(byte[] payload, int chunk)
    { _payload = payload; _chunk = chunk; }
    internal ReadSeam(PipeStream transport, int chunk)
    { _transport = transport; _chunk = chunk; CompletionReadIndices = []; }
    internal bool IsMessageComplete
    {
        get
        {
            ++CompleteCalls;
            CompletionReadIndices?.Add(ReadCalls - 1);
            if (ReadCalls - 1 == CompletionFailureAt) throw CompletionFailure!;
            return _transport is null ? _complete : _transport.IsMessageComplete;
        }
    }
    internal void Reset()
    {
        if (ActiveReads != 0) throw new InvalidOperationException("reset-with-outstanding-read");
        _offset = _index = ReadCalls = CompleteCalls = MaximumOutstanding = 0;
        _complete = false;
        CompletionReadIndices?.Clear();
    }
    internal ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken token)
    {
        LastToken = token;
        ++ReadCalls;
        ++ActiveReads;
        MaximumOutstanding = Math.Max(MaximumOutstanding, ActiveReads);
        if (_transport is not null) return ReadTransport(buffer, token);
        if (ReadCalls - 1 == HoldAtRead) return ReadHeld(buffer, token);
        try { return new ValueTask<int>(ReadNow(buffer, token)); }
        finally { --ActiveReads; }
    }
    private async ValueTask<int> ReadHeld(Memory<byte> buffer, CancellationToken token)
    {
        try
        {
            await Release!.Task.WaitAsync(token).ConfigureAwait(false);
            return ReadNow(buffer, token);
        }
        finally { --ActiveReads; }
    }
    private async ValueTask<int> ReadTransport(Memory<byte> buffer, CancellationToken token)
    {
        try { return await _transport!.ReadAsync(buffer[..Math.Min(buffer.Length, _chunk)], token).ConfigureAwait(false); }
        finally { --ActiveReads; }
    }
    private int ReadNow(Memory<byte> buffer, CancellationToken token)
    {
        token.ThrowIfCancellationRequested();
        if (_steps is not null)
        {
            if (_index >= _steps.Length) throw new InvalidOperationException("unexpected-extra-read");
            var step = _steps[_index++];
            if (step.Failure is not null) throw step.Failure;
            if (step.Bytes.Length > buffer.Length) throw new InvalidOperationException("invalid-seam-count");
            step.Bytes.AsMemory().CopyTo(buffer);
            _complete = step.Complete;
            return step.Bytes.Length;
        }
        int count = Math.Min(Math.Min(_chunk, buffer.Length), _payload!.Length - _offset);
        _payload.AsMemory(_offset, count).CopyTo(buffer);
        _offset += count;
        _complete = _offset == _payload.Length;
        return count;
    }
}
internal sealed class PrivatePipe : IAsyncDisposable
{
    internal readonly NamedPipeServerStream Server;
    internal readonly NamedPipeClientStream Client;
    private PrivatePipe(string name)
    {
        Server = new NamedPipeServerStream(name, PipeDirection.InOut, 1, PipeTransmissionMode.Message,
            PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly, 65536, 65536);
        Client = new NamedPipeClientStream(".", name, PipeDirection.InOut,
            PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
    }
    internal static async Task<PrivatePipe> Open(CancellationToken token)
    {
        var p = new PrivatePipe($"YMCC.R24H1.decode.{Environment.ProcessId}.{Guid.NewGuid():N}");
        try
        {
            var wait = p.Server.WaitForConnectionAsync(token);
            await p.Client.ConnectAsync(10000, token);
            await wait;
            p.Client.ReadMode = PipeTransmissionMode.Message;
            p.Server.ReadMode = PipeTransmissionMode.Message;
            return p;
        }
        catch { await p.DisposeAsync(); throw; }
    }
    public async ValueTask DisposeAsync()
    { await Client.DisposeAsync(); await Server.DisposeAsync(); }
}
internal static class DecodeFixture
{
    private const int Cap = 16384;
    private static int _assertions, _seamCases, _realPipeCases;
    private static readonly List<object> Evidence = [];
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    private static void Check(bool value, string label)
    {
        ++_assertions;
        if (!value) throw new InvalidOperationException("ASSERT: " + label);
    }
    private static void Save(string path, object value) => File.WriteAllText(path, JsonSerializer.Serialize(value, JsonOptions) + "\n");
    private static byte[] Bytes(int size, int seed = 17) => Enumerable.Range(0, size).Select(i => (byte)('!' + (i + seed) % 90)).ToArray();
    private static ReadStep[] Split(byte[] bytes, int chunk) => Enumerable.Range(0, (bytes.Length + chunk - 1) / chunk)
        .Select(i => new ReadStep(bytes.Skip(i * chunk).Take(chunk).ToArray(), (i + 1) * chunk >= bytes.Length)).ToArray();
    private sealed record Outcome(string? Text, string? Error, string? Message, bool CanceledWithToken);
    private static async Task<Outcome> Observe(Func<Task<string?>> action, CancellationToken token = default)
    {
        try { return new(await action(), null, null, false); }
        catch (Exception e) { return new(null, e.GetType().FullName, e.Message, e is OperationCanceledException c && c.CancellationToken == token); }
    }
    private static async Task SeamCase(string name, ReadStep[] steps, string? expected, string? error = null,
        bool eligible = false, bool retainEvidence = false)
    {
        var before = BaselineSeam.Create();
        var after = CandidateSeam.Create();
        var a = new ReadSeam(steps);
        var b = new ReadSeam(steps);
        var ao = await Observe(() => before.Read(a, default));
        var bo = await Observe(() => after.Read(b, default));
        Check(ao == bo, name + " outcome parity");
        Check(bo.Text == expected && bo.Error == error, name + " expected outcome");
        Check(a.ReadCalls == b.ReadCalls && a.ReadCalls <= steps.Length, name + " same bounded read count");
        Check(a.MaximumOutstanding == 1 && b.MaximumOutstanding == 1 && a.ActiveReads == 0 && b.ActiveReads == 0, name + " one reader");
        Check(before.Buffer.Length == Cap && after.Buffer.Length == Cap, name + " cap-sized read buffers");
        if (eligible)
        {
            Check(a.ReadCalls == 1 && steps[0].Bytes.Length > 0 && steps[0].Complete, name + " first positive read eligible");
            Check(before.Body.Length == steps[0].Bytes.Length && after.Body.Length == 0 && after.Body.Position == 0, name + " copied versus bypassed");
        }
        else
        {
            Check(before.Body.Length == after.Body.Length && before.Body.Position == after.Body.Position, name + " unchanged accumulated length/position");
            Check(before.Body.GetBuffer().AsSpan(0, (int)before.Body.Length).SequenceEqual(after.Body.GetBuffer().AsSpan(0, (int)after.Body.Length)), name + " unchanged accumulated bytes");
        }
        if (error == typeof(InvalidDataException).FullName && bo.Message == "message-too-large")
        {
            Check(!b.CompletionReadIndices!.Contains(b.ReadCalls - 1), name + " cap checked before final completion query");
        }
        if (retainEvidence) Evidence.Add(new { name, transport = "deterministic-source-extracted-read-seam", eligible, outcome = bo,
            reads = b.ReadCalls, maximumOutstanding = b.MaximumOutstanding, beforeBodyBytes = before.Body.Length,
            afterBodyBytes = after.Body.Length, baselineCompletionQueries = a.CompletionReadIndices, candidateCompletionQueries = b.CompletionReadIndices });
        ++_seamCases;
    }
    private static async Task VerifySeams()
    {
        foreach (int length in new[] { 1, 2, 63, 64, 767, 768, 4095, 4096, 4097, 8192, Cap - 1, Cap })
        {
            var p = Bytes(length);
            await SeamCase($"complete-{length}", [new(p, true)], Encoding.UTF8.GetString(p), eligible: true, retainEvidence: true);
            foreach (int chunk in new[] { 1, 3, 127, 1024, 4096 })
                if (chunk < length) await SeamCase($"fragment-{length}-by-{chunk}", Split(p, chunk), Encoding.UTF8.GetString(p), retainEvidence: chunk == 1024);
        }
        foreach (var p in new[] {
            Encoding.UTF8.GetBytes("ASCII 中文 / 🕹️ / e\u0301 / \0 / end"),
            new byte[] { 0xff, 0x80, 0xc0, 0xaf, 0xe2, 0x82, 0x41, 0xf0, 0x9f, 0x8e },
            new byte[] { 0xe2, 0x82 }, new byte[] { 0xed, 0xa0, 0x80 },
            new byte[] { 0xf4, 0x90, 0x80, 0x80 }, Encoding.UTF8.GetBytes("\ufeff\ufffd") })
        {
            string expected = Encoding.UTF8.GetString(p);
            await SeamCase("utf8-complete-" + Convert.ToHexString(p), [new(p, true)], expected, eligible: true, retainEvidence: true);
            for (int cut = 1; cut < p.Length; ++cut)
                await SeamCase($"utf8-split-{Convert.ToHexString(p)}-{cut}", [new(p[..cut], false), new(p[cut..], true)], expected);
        }
        // Exhaust every positive partition of 1..9 bytes.
        for (int size = 1; size <= 9; ++size)
        {
            var p = Bytes(size);
            for (int mask = 0; mask < (1 << (size - 1)); ++mask)
            {
                List<ReadStep> steps = [];
                int start = 0;
                for (int i = 0; i < size; ++i)
                    if (i == size - 1 || (mask & (1 << i)) != 0)
                    { steps.Add(new(p[start..(i + 1)], i == size - 1)); start = i + 1; }
                await SeamCase($"partition-{size}-{mask}", steps.ToArray(), Encoding.UTF8.GetString(p), eligible: mask == 0);
            }
        }
        var random = new Random(2401);
        for (int i = 0; i < 100; ++i)
        {
            var p = new byte[random.Next(1, Cap + 1)];
            random.NextBytes(p);
            int chunk = random.Next(1, 1000);
            await SeamCase("random-invalid-utf8-" + i, Split(p, chunk), Encoding.UTF8.GetString(p), eligible: p.Length <= chunk);
        }
        await SeamCase("eof-empty", [new([], false)], null, retainEvidence: true);
        await SeamCase("eof-partial", [new([0xe2, 0x82], false), new([], true)], null, retainEvidence: true);
        await SeamCase("eof-at-cap-still-incomplete", [new(Bytes(Cap), false), new([], false)], null, retainEvidence: true);
        foreach (int excess in new[] { 1, 2, 4000, Cap })
            await SeamCase("over-cap-" + excess, [new(Bytes(Cap), false), new(Bytes(excess), true)], null,
                typeof(InvalidDataException).FullName, retainEvidence: true);
        foreach (Exception failure in new Exception[] { new IOException("injected-io"), new InvalidDataException("injected-data"), new ObjectDisposedException("read-pipe") })
        {
            await SeamCase("exception-first-" + failure.GetType().Name, [new([], false, failure)], null, failure.GetType().FullName, retainEvidence: true);
            await SeamCase("exception-after-partial-" + failure.GetType().Name, [new([0xe2], false), new([], false, failure)], null, failure.GetType().FullName, retainEvidence: true);
            foreach (var factory in new Func<SeamHandle>[] { BaselineSeam.Create, CandidateSeam.Create })
            {
                var codec = factory();
                var seam = new ReadSeam([new([42], true)]) { CompletionFailureAt = 0, CompletionFailure = failure };
                Exception? observed = null;
                try { await codec.Read(seam, default); } catch (Exception e) { observed = e; }
                Check(ReferenceEquals(observed, failure), "completion exception identity propagated");
                Check(seam.ReadCalls == 1 && seam.ActiveReads == 0, "completion exception no additional read");
                Check(await codec.Read(new ReadSeam([new([65], true)]), default) == "A", "reset after completion exception");
            }
        }
        await VerifyResetAndAsync();
    }
    private static async Task VerifyResetAndAsync()
    {
        foreach (bool candidate in new[] { false, true })
        {
            var c = candidate ? CandidateSeam.Create() : BaselineSeam.Create();
            var originalReadBuffer = c.Buffer;
            var originalBodyBuffer = c.Body.GetBuffer();
            var writeBuffer = c.WriteBuffer;
            var large = Bytes(8192);
            string? owned = await c.Read(new ReadSeam([new(large, true)]), default);
            Check(await c.Read(new ReadSeam([new([65], true)]), default) == "A", "no stale suffix after long then short");
            Check(owned == Encoding.UTF8.GetString(large), "returned string owns its bytes across reuse");
            Check(await c.Read(new ReadSeam([new([0xe2], false), new([0x82, 0xac], true)]), default) == "€", "multibyte fragmented after complete");
            Check(await c.Read(new ReadSeam([new([70], false), new([], false)]), default) is null, "partial EOF remains null");
            Check(await c.Read(new ReadSeam([new([66], true)]), default) == "B", "reset after partial EOF");
            var failure = new IOException("same-instance");
            Exception? observed = null;
            try { await c.Read(new ReadSeam([new([70], false), new([], false, failure)]), default); }
            catch (Exception e) { observed = e; }
            Check(ReferenceEquals(observed, failure), "read exception instance propagated");
            Check(await c.Read(new ReadSeam([new([67], true)]), default) == "C", "reset after read exception");
            Check(ReferenceEquals(c.Buffer, originalReadBuffer) && ReferenceEquals(c.Body.GetBuffer(), originalBodyBuffer)
                && ReferenceEquals(c.WriteBuffer, writeBuffer), "GP-CPU2 storage reuse retained");
            using var canceled = new CancellationTokenSource();
            canceled.Cancel();
            var pre = new ReadSeam([new([68], true)]);
            var outcome = await Observe(() => c.Read(pre, canceled.Token), canceled.Token);
            Check(outcome.Error == typeof(OperationCanceledException).FullName && outcome.CanceledWithToken, "pre-canceled token propagated");
            Check(pre.ReadCalls == 1 && pre.CompleteCalls == 0, "no completion query after canceled read");
            using var during = new CancellationTokenSource();
            var held = new ReadSeam([new([0xe2], false), new([0x82, 0xac], true)])
            { HoldAtRead = 1, Release = new(TaskCreationOptions.RunContinuationsAsynchronously) };
            var pending = c.Read(held, during.Token);
            Check(!pending.IsCompleted && held.ReadCalls == 2 && held.MaximumOutstanding == 1 && held.ActiveReads == 1, "one asynchronous fragmented read outstanding");
            during.Cancel();
            var canceledOutcome = await Observe(() => pending, during.Token);
            Check(canceledOutcome.Error == typeof(TaskCanceledException).FullName && canceledOutcome.CanceledWithToken, "in-flight cancellation token propagated");
            Check(held.LastToken == during.Token && held.ReadCalls == 2 && held.ActiveReads == 0, "same token and no retry after cancel");
            var heldComplete = new ReadSeam([new([0xf0, 0x9f], false), new([0x8e, 0xae], true)])
            { HoldAtRead = 0, Release = new(TaskCreationOptions.RunContinuationsAsynchronously) };
            var resumed = c.Read(heldComplete, default);
            Check(!resumed.IsCompleted && heldComplete.ReadCalls == 1, "forced asynchronous first read");
            heldComplete.Release.SetResult(true);
            Check(await resumed == "🎮" && heldComplete.MaximumOutstanding == 1, "fragmented async continuation parity");
            var over = await Observe(() => c.Read(new ReadSeam([new(Bytes(Cap), false), new([65], true)]), default));
            Check(over.Error == typeof(InvalidDataException).FullName && over.Message == "message-too-large", "oversize rejected before next reset");
            Check(await c.Read(new ReadSeam([new([69], true)]), default) == "E", "reset after over-cap");
        }
        Evidence.Add(new { name = "reuse-reset-and-async", verified = true,
            coverage = "owned strings; stale suffix; multibyte; EOF partial; exception identity; pre/in-flight cancellation; one outstanding read; reset after oversize; same session buffers" });
    }
    private static async Task VerifyRealPipes(CancellationToken token)
    {
        foreach (bool candidate in new[] { false, true })
        {
            var c = candidate ? CandidatePipe.Create() : BaselinePipe.Create();
            await using (var p = await PrivatePipe.Open(token))
            {
                Check(p.Server.TransmissionMode == PipeTransmissionMode.Message && p.Client.ReadMode == PipeTransmissionMode.Message, "actual Windows message mode");
                foreach (var payload in new[] { Bytes(1), Bytes(64), Bytes(768), Bytes(4095), Bytes(4096), Bytes(4097), Bytes(8192), Bytes(Cap - 1), Bytes(Cap),
                    new byte[] { 0xff, 0xe2, 0x82 }, Encoding.UTF8.GetBytes("真实消息 / 中文 / 🎮 / \0 / done") })
                {
                    var write = p.Server.WriteAsync(payload.AsMemory(), token).AsTask();
                    string? text = await c.Read(p.Client, token);
                    await write;
                    Check(text == Encoding.UTF8.GetString(payload), "real pipe decoded exact bytes");
                    Check(c.Body.Length == (candidate ? 0 : payload.Length), "real pipe complete-first-read bypass proven");
                    Check(p.Client.IsMessageComplete, "real pipe exact message boundary");
                    Evidence.Add(new { name = "real-complete", candidate, bytes = payload.Length, bodyBytes = c.Body.Length, complete = p.Client.IsMessageComplete });
                    ++_realPipeCases;
                }
                await p.Server.WriteAsync(Encoding.UTF8.GetBytes("first-message"), token);
                await p.Server.WriteAsync(Encoding.UTF8.GetBytes("z"), token);
                Check(await c.Read(p.Client, token) == "first-message", "first of queued message boundaries");
                Check(await c.Read(p.Client, token) == "z", "second of queued message boundaries");
                _realPipeCases += 2;
                using var already = new CancellationTokenSource();
                already.Cancel();
                var pre = await Observe(() => c.Read(p.Client, already.Token), already.Token);
                Check(pre.CanceledWithToken && pre.Error is not null, "real pipe pre-cancellation");
                using var during = CancellationTokenSource.CreateLinkedTokenSource(token);
                var read = c.Read(p.Client, during.Token);
                Check(!read.IsCompleted, "real pipe read pending before cancellation");
                during.Cancel();
                var canceled = await Observe(() => read, during.Token);
                Check(canceled.CanceledWithToken && canceled.Error is not null, "real pipe in-flight cancellation");
                await p.Server.WriteAsync(Encoding.UTF8.GetBytes("after-cancel"), token);
                Check(await c.Read(p.Client, token) == "after-cancel", "real pipe reset after cancellation");
                _realPipeCases += 3;
                p.Server.Dispose();
                Check(await c.Read(p.Client, token) is null, "real pipe EOF returns null");
                ++_realPipeCases;
            }
            foreach (int size in new[] { Cap + 1, Cap + 128, Cap * 2 })
            {
                await using var p = await PrivatePipe.Open(token);
                var send = p.Server.WriteAsync(Bytes(size).AsMemory(), token).AsTask();
                var outcome = await Observe(() => c.Read(p.Client, token));
                await send;
                Check(outcome.Error == typeof(InvalidDataException).FullName && outcome.Message == "message-too-large", "real pipe oversized message exact error");
                Check(c.Body.Length > Cap, "real pipe fragmented oversize accumulator used");
                Evidence.Add(new { name = "real-over-cap", candidate, bytes = size, outcome, bodyBytes = c.Body.Length });
                ++_realPipeCases;
            }
            await using (var p = await PrivatePipe.Open(token))
            {
                p.Client.ReadMode = PipeTransmissionMode.Byte;
                await p.Server.WriteAsync(new byte[] { 65 }, token);
                var outcome = await Observe(() => c.Read(p.Client, token));
                Check(outcome.Error == typeof(InvalidOperationException).FullName, "real byte-mode completion exception propagated");
                ++_realPipeCases;
            }
            // Accepted fragments through actual Windows reads, explicitly limited
            // by a seam; unmodified PipeStream signatures are tested above.
            foreach (int chunk in new[] { 1, 127, 1024, 4096 })
            {
                await using var p = await PrivatePipe.Open(token);
                var s = candidate ? CandidateSeam.Create() : BaselineSeam.Create();
                var payload = Encoding.UTF8.GetBytes(string.Concat(Enumerable.Repeat("x🎮中", 1024)));
                var seam = new ReadSeam(p.Client, chunk);
                var send = p.Server.WriteAsync(payload.AsMemory(), token).AsTask();
                Check(await s.Read(seam, token) == Encoding.UTF8.GetString(payload), "real constrained read fragmentation decode");
                await send;
                Check(seam.ReadCalls > 1 && seam.MaximumOutstanding == 1 && s.Body.Length == payload.Length, "real constrained fragmentation retains accumulation");
                Evidence.Add(new { name = "real-pipe-with-constrained-read-seam", candidate, chunk, bytes = payload.Length, reads = seam.ReadCalls, bodyBytes = s.Body.Length });
                ++_realPipeCases;
            }
            await VerifyWrites(c, candidate, token);
        }
    }
    private static async Task VerifyWrites(PipeHandle codec, bool candidate, CancellationToken token)
    {
        await using var p = await PrivatePipe.Open(token);
        foreach (string text in new[] { "short", new string('a', 4096), new string('中', 2000), "\ud800x\udc00🎮\0", new string('a', Cap), "z" })
        {
            var wanted = Encoding.UTF8.GetBytes(text);
            var send = codec.Write(p.Client, text, token);
            var received = new byte[Cap + 1];
            int count = 0;
            do { count += await p.Server.ReadAsync(received.AsMemory(count), token); } while (!p.Server.IsMessageComplete);
            await send;
            Check(received.AsSpan(0, count).SequenceEqual(wanted), "unchanged WriteAsync actual UTF8 bytes and exact length");
            Check(p.Server.IsMessageComplete, "unchanged WriteAsync one message boundary");
            ++_realPipeCases;
        }
        Exception? tooLarge = null;
        try { await codec.Write(p.Client, new string('a', Cap + 1), token); } catch (Exception e) { tooLarge = e; }
        Check(tooLarge is InvalidDataException && tooLarge.Message == "response-too-large", "unchanged write byte cap");
        using var canceled = new CancellationTokenSource();
        canceled.Cancel();
        Exception? cancel = null;
        try { await codec.Write(p.Client, "canceled", canceled.Token); } catch (Exception e) { cancel = e; }
        Check(cancel is OperationCanceledException ce && ce.CancellationToken == canceled.Token, "unchanged write cancellation");
        await codec.Write(p.Client, "", token); // Existing zero-byte behavior; don't await a nonexistent peer message.
        Evidence.Add(new { name = "real-write-parity", candidate, verified = true,
            coverage = "short/4096/growing multibyte/surrogate replacement/cap/short-after-large, one message per nonempty write, exact length, cap rejection, cancel, empty" });
        _realPipeCases += 3;
    }
    private sealed record Sample(double NsPerOperation, double AllocatedBytesPerOperation, long Checksum);
    private sealed record Pair(int Round, string Order, Sample Baseline, Sample Candidate);
    private static Sample Measure(SeamHandle codec, ReadSeam seam, int iterations)
    {
        long checksum = 0;
        long allocated = GC.GetAllocatedBytesForCurrentThread();
        long start = Stopwatch.GetTimestamp();
        for (int i = 0; i < iterations; ++i)
        {
            seam.Reset();
            var task = codec.Read(seam, CancellationToken.None);
            if (!task.IsCompletedSuccessfully) throw new InvalidOperationException("benchmark seam must complete synchronously");
            var text = task.GetAwaiter().GetResult()!;
            checksum += text.Length + text[0] + text[^1];
        }
        long ticks = Stopwatch.GetTimestamp() - start;
        long bytes = GC.GetAllocatedBytesForCurrentThread() - allocated;
        return new(ticks * (1_000_000_000d / Stopwatch.Frequency) / iterations, bytes / (double)iterations, checksum);
    }
    private static void Warmup(SeamHandle codec, ReadSeam seam, int iterations)
    {
        for (int i = 0; i < iterations; ++i)
        {
            seam.Reset();
            var t = codec.Read(seam, default);
            if (!t.IsCompletedSuccessfully) throw new InvalidOperationException("non-synchronous warmup");
            GC.KeepAlive(t.GetAwaiter().GetResult());
        }
    }
    private static double Median(IEnumerable<double> values)
    { var a = values.Order().ToArray(); return a[a.Length / 2]; }
    // Dormant until explicit user measurement release; never called by verify.
    private static void Benchmark(string outDir, string criteriaPath)
    {
        using var criteria = JsonDocument.Parse(File.ReadAllText(criteriaPath));
        var plan = criteria.RootElement;
        Check(plan.GetProperty("version").GetString() == "R24-H1-predeclared-v1", "fixed criteria version");
        Check(plan.GetProperty("rounds").GetInt32() == 11, "fixed eleven pairs");
        Check(plan.GetProperty("warmupOperationsPerArmPerScenario").GetInt32() == 10000, "fixed warmup");
        var results = new List<object>();
        bool eligiblePass = true, controlPass = true;
        foreach (var item in plan.GetProperty("cases").EnumerateArray())
        {
            string name = item.GetProperty("name").GetString()!;
            int length = item.GetProperty("bytes").GetInt32();
            int chunk = item.GetProperty("chunkBytes").GetInt32();
            int iterations = item.GetProperty("iterations").GetInt32();
            bool eligible = item.GetProperty("eligible").GetBoolean();
            var before = BaselineSeam.Create();
            var after = CandidateSeam.Create();
            var bytes = Bytes(length);
            var a = new ReadSeam(bytes, chunk);
            var b = new ReadSeam(bytes, chunk);
            Warmup(before, a, 10000);
            Warmup(after, b, 10000);
            var pairs = new List<Pair>();
            for (int round = 0; round < 11; ++round)
            {
                Sample ba, ca;
                if ((round & 1) == 0) { ba = Measure(before, a, iterations); ca = Measure(after, b, iterations); }
                else { ca = Measure(after, b, iterations); ba = Measure(before, a, iterations); }
                Check(ba.Checksum == ca.Checksum, name + " benchmark result parity");
                pairs.Add(new(round + 1, (round & 1) == 0 ? "AB" : "BA", ba, ca));
            }
            int wins = pairs.Count(p => p.Candidate.NsPerOperation < p.Baseline.NsPerOperation);
            double baselineMedian = Median(pairs.Select(p => p.Baseline.NsPerOperation));
            double candidateMedian = Median(pairs.Select(p => p.Candidate.NsPerOperation));
            double pairedSavings = Median(pairs.Select(p => p.Baseline.NsPerOperation - p.Candidate.NsPerOperation));
            double pairedRatio = Median(pairs.Select(p => p.Candidate.NsPerOperation / p.Baseline.NsPerOperation));
            bool allocations = pairs.All(p => p.Candidate.AllocatedBytesPerOperation <= p.Baseline.AllocatedBytesPerOperation);
            bool pass = eligible ? wins >= 10 && pairedSavings >= baselineMedian * 0.05 && pairedRatio <= 0.95 && allocations
                : pairedRatio <= 1.05 && allocations;
            if (eligible) eligiblePass &= pass; else controlPass &= pass;
            results.Add(new { name, eligible, iterations, pairs, wins, baselineMedian, candidateMedian,
                medianPairedSavingsNs = pairedSavings, medianCandidateToBaselineRatio = pairedRatio,
                allocationGate = allocations, pass, path = eligible ? "complete-first-read-copy-bypass" : "unchanged-fragmented-accumulation" });
        }
        Save(Path.Combine(outDir, "benchmark.json"), new { benchmarkExecuted = true, pass = eligiblePass && controlPass,
            eligiblePass, fragmentedControlPass = controlPass,
            scope = "isolated source-extracted synchronous read seam; no overall CPU claim", results });
    }
    internal static async Task<int> Main(string[] args)
    {
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("Windows message-mode pipes required");
        if (args.Length < 3) throw new ArgumentException("verify|benchmark output-directory criteria-file [--measurement-authorized]");
        Directory.CreateDirectory(args[1]);
        if (args[0] == "benchmark")
        {
            if (args.Length != 4 || args[3] != "--measurement-authorized") throw new InvalidOperationException("BENCHMARK_LOCKED");
            Benchmark(args[1], args[2]);
            return 0;
        }
        if (args[0] != "verify" || args.Length != 3) throw new ArgumentException("Unknown fixture mode");
        using var budget = new CancellationTokenSource(TimeSpan.FromSeconds(90));
        await VerifySeams().WaitAsync(budget.Token);
        await VerifyRealPipes(budget.Token);
        Save(Path.Combine(args[1], "correctness.json"), new { pass = true, benchmarkExecuted = false,
            assertions = _assertions, seamCases = _seamCases, realPipeCases = _realPipeCases,
            windows = Environment.OSVersion.VersionString, runtime = Environment.Version.ToString(),
            processArchitecture = System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString(),
            realPipeSecurity = "unique GUID name, one server instance, CurrentUserOnly server/client, local dot host",
            originalPipeSignatureTested = true, constrainedRealReadSeamAlsoTested = true,
            productionMainCompiledOrExecuted = false, deviceSdkReferencedOrExecuted = false,
            fragmentedPath = "Original Write, cap check, loop completion, GetBuffer/decode retained; first incomplete read has an extra completion observation to determine eligibility.",
            evidence = Evidence });
        Console.WriteLine($"PASS {_assertions} assertions; {_seamCases} deterministic cases; {_realPipeCases} real-pipe cases; benchmarkExecuted=false");
        return 0;
    }
}
