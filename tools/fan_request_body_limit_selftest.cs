using System.IO;
using System.Net.Http;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Security.Cryptography;
internal static class BodyLimitChecks
{
    const int Limit=1024*1024;
    static int checks;static readonly List<string> failures=new();
    static MethodInfo reader=null!;
    sealed class ChunkContent(byte[] bytes):HttpContent
    {
        protected override bool TryComputeLength(out long length){length=0;return false;}
        protected override Task SerializeToStreamAsync(Stream stream,TransportContext? context)=>stream.WriteAsync(bytes).AsTask();
    }
    static byte[] Body(int bytes)=>Encoding.UTF8.GetBytes("{\"padding\":\""+new string('x',bytes-14)+"\"}");
    static async Task Check(string name,byte[] bytes,bool chunked,string? error=null)
    {
        checks++;HttpListener? listener=null;
        try
        {
            var portProbe=new TcpListener(IPAddress.Loopback,0);portProbe.Start();var port=((IPEndPoint)portProbe.LocalEndpoint).Port;portProbe.Stop();
            listener=new HttpListener();listener.Prefixes.Add($"http://127.0.0.1:{port}/");listener.Start();
            var accepted=listener.GetContextAsync();using var client=new HttpClient{Timeout=TimeSpan.FromSeconds(8)};
            using HttpContent content=chunked?new ChunkContent(bytes):new ByteArrayContent(bytes);content.Headers.ContentType=new MediaTypeHeaderValue("application/json");
            var send=client.PostAsync($"http://127.0.0.1:{port}/fixture",content);
            var context=await accepted.WaitAsync(TimeSpan.FromSeconds(4));
            string? code=null;JsonElement result=default;
            try{var task=(Task<JsonElement>)reader.Invoke(null,new object[]{context.Request})!;result=await task.WaitAsync(TimeSpan.FromSeconds(4));}
            catch(Exception ex){while(ex is TargetInvocationException&&ex.InnerException is not null)ex=ex.InnerException;code=ex is JsonException?"INVALID_JSON":ex.GetType().GetProperty("Code")?.GetValue(ex) as string;if(code is null)throw;}
            finally{context.Response.StatusCode=200;context.Response.ContentLength64=0;context.Response.Close();}
            try{using var response=await send;}catch(HttpRequestException){/* server-side exception is the authoritative parser result; over-limit close may end the sender */}
            if(code!=error)throw new InvalidOperationException($"expected={error??"success"} actual={code??"success"} length={bytes.Length}");
            if(error is null&&result.ValueKind!=JsonValueKind.Object)throw new InvalidOperationException("valid request no longer has an owned JSON object");
            Console.WriteLine("PASS "+name);
        }
        catch(Exception ex){failures.Add(name+": "+ex.GetBaseException().Message);Console.WriteLine("FAIL "+name+": "+ex.GetBaseException().Message);}
        finally{listener?.Close();}
    }
    sealed class TrackingStream(byte[] bytes):Stream
    {
        public long Consumed {get;private set;}
        public bool Disposed {get;private set;}
        public override int Read(Span<byte> buffer){var count=(int)Math.Min(buffer.Length,bytes.Length-Consumed);bytes.AsSpan((int)Consumed,count).CopyTo(buffer);Consumed+=count;return count;}
        public override int Read(byte[] buffer,int offset,int count)=>Read(buffer.AsSpan(offset,count));
        public override ValueTask<int> ReadAsync(Memory<byte> buffer,CancellationToken token=default)=>ValueTask.FromResult(Read(buffer.Span));
        public override Task<int> ReadAsync(byte[] buffer,int offset,int count,CancellationToken token)=>Task.FromResult(Read(buffer.AsSpan(offset,count)));
        public override bool CanRead=>true;
        public override bool CanSeek=>false;
        public override bool CanWrite=>false;
        public override long Length=>bytes.Length;
        public override long Position{get=>Consumed;set=>throw new NotSupportedException();}
        public override void Flush(){}
        public override long Seek(long offset,SeekOrigin origin)=>throw new NotSupportedException();
        public override void SetLength(long value)=>throw new NotSupportedException();
        public override void Write(byte[] buffer,int offset,int count)=>throw new NotSupportedException();
        protected override void Dispose(bool disposing){Disposed=true;base.Dispose(disposing);}
    }
    static async Task StreamCheck(Type limiter,bool asynchronous,bool overLimit)
    {
        checks++;var name=$"actual bounded stream async={asynchronous} overLimit={overLimit}";
        try
        {
            var input=new TrackingStream(Body(overLimit?2*Limit:Limit));bool rejected=false;
            using(var bounded=(Stream)Activator.CreateInstance(limiter,BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance,null,new object[]{input},null)!)
            {
                var buffer=new byte[8192];
                try{while((asynchronous?await bounded.ReadAsync(buffer.AsMemory()):bounded.Read(buffer,0,buffer.Length))>0){}}
                catch(Exception ex)when(ex.GetType().GetProperty("Code")?.GetValue(ex)as string=="BODY_TOO_LARGE"){rejected=true;}
            }
            if(rejected!=overLimit)throw new InvalidOperationException("wrong cumulative byte limit");
            if(input.Consumed>(overLimit?Limit+1:Limit))throw new InvalidOperationException("over-limit body was consumed past one probe byte");
            if(!input.Disposed)throw new InvalidOperationException("underlying request stream was not disposed");
            Console.WriteLine($"PASS {name} consumed={input.Consumed}");
        }
        catch(Exception ex){failures.Add(name+": "+ex.GetBaseException().Message);Console.WriteLine("FAIL "+name+": "+ex.GetBaseException().Message);}
    }
    public static async Task<int> Main(string[] args)
    {
        var dll=Path.GetFullPath(args[0]);var assembly=Assembly.LoadFrom(dll);reader=assembly.GetType("YeManFanHost.LocalFanHostServer",true)!.GetMethod("ReadJsonAsync",BindingFlags.NonPublic|BindingFlags.Static)!;
        await Check("fixed normal",Body(64),false);
        await Check("chunked normal",Body(64),true);
        await Check("fixed exactly 1MiB",Body(Limit),false);
        await Check("chunked exactly 1MiB",Body(Limit),true);
        await Check("fixed 1MiB+1",Body(Limit+1),false,"BODY_TOO_LARGE");
        await Check("chunked 1MiB+1",Body(Limit+1),true,"BODY_TOO_LARGE");
        await Check("chunked 2MiB",Body(2*Limit),true,"BODY_TOO_LARGE");
        var unicode=Encoding.UTF8.GetBytes("{\"padding\":\""+new string('风',Limit/2)+"\"}");
        await Check("chunked unicode byte limit",unicode,true,"BODY_TOO_LARGE");
        await Check("UTF8 BOM unchanged",Encoding.UTF8.GetPreamble().Concat(Encoding.UTF8.GetBytes("{\"value\":1}")).ToArray(),true);
        await Check("UTF16 BOM unchanged",Encoding.Unicode.GetPreamble().Concat(Encoding.Unicode.GetBytes("{\"value\":1}")).ToArray(),true);
        await Check("malformed still invalid JSON",Encoding.UTF8.GetBytes("{broken"),true,"INVALID_JSON");
        await Check("empty chunked remains empty object",Array.Empty<byte>(),true);
        await Check("next current request after rejections",Body(64),true);
        var limiter=reader.DeclaringType!.GetNestedType("RequestBodyLimitStream",BindingFlags.NonPublic);
        if(limiter is not null)foreach(var asynchronous in new[]{false,true})foreach(var overLimit in new[]{false,true})await StreamCheck(limiter,asynchronous,overLimit);
        Console.WriteLine(JsonSerializer.Serialize(new{ok=failures.Count==0,checks,failures,hostDllSha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(dll))).ToLowerInvariant(),hardwareCalls=0,scope="actual private Host HTTP reader on real temporary HttpListener requests; no Host RunAsync/engine/HC/device; no CPU benchmark"}));return failures.Count==0?0:1;
    }
}
