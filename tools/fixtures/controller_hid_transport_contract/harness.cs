using System;
using System.Collections.Generic;
using System.Linq;

// Fixture substitutes only the native read call. The three tested methods and
// event type are copied verbatim from hash-verified reference source/IL output.
namespace hidapi {
public static class HidApiNative {
    public readonly record struct Reply(int Count, byte[] Bytes);
    public static Queue<Reply> Replies = new();
    public static List<int> Counts = new();
    public static void Reset(params Reply[] replies) { Replies = new(replies); Counts.Clear(); }
    public static int hid_read_timeout(IntPtr handle, byte[] output, uint capacity, int timeout) {
        if (handle != new IntPtr(1) || output.Length < capacity) throw new Exception("bad fake seam");
        if (Replies.Count == 0) throw new Exception("bounded fake exhausted");
        var reply = Replies.Dequeue(); Counts.Add(reply.Count);
        if (reply.Count > 0) {
            if (reply.Count > capacity || reply.Bytes.Length < reply.Count) throw new Exception("bad fake count");
            Array.Copy(reply.Bytes, output, reply.Count);
        }
        return reply.Count;
    }
}
public sealed class HidDevice {
    private readonly object _lock = new();
    private readonly IntPtr _deviceHandle = new(1);
    private readonly ushort _inputBufferLen;
    private readonly byte[] _buffer, _readReturnBuffer;
    private readonly HidDeviceInputReceivedEventArgs _eventArgs;
    private bool _reading, _halting;
    public Action<HidDeviceInputReceivedEventArgs> OnInputReceived;
    public HidDevice(ushort capacity) {
        _inputBufferLen=capacity; _buffer=new byte[capacity]; _readReturnBuffer=new byte[capacity];
        _eventArgs=new(this,new byte[capacity],true); _halting=false;
    }
    private void ThrowIfDeviceInvalid() { if (_deviceHandle != new IntPtr(1)) throw new Exception("invalid fake"); }
    public void RunBoundedLoop() { _reading=true; ReadLoop(); }
    // REFERENCE_METHODS
}
}
public static class Program {
    private static int checks;
    private static void Check(bool condition, string label) { ++checks; if (!condition) throw new Exception(label); }
    private static hidapi.HidApiNative.Reply Reply(int count, byte[] bytes = null) => new(count, bytes ?? Array.Empty<byte>());
    private static byte[] Packet(int capacity) {
        var bytes = Enumerable.Repeat((byte)0xA5,capacity).ToArray();
        bytes[0]=1; bytes[1]=0; bytes[2]=9; return bytes;
    }
    public static void Main() {
        foreach(ushort capacity in new ushort[]{64,65}) {
            for(int shortLength=1; shortLength<60; ++shortLength) {
                var full=Packet(capacity); var shortPacket=full.Take(shortLength).ToArray();
                var device=new hidapi.HidDevice(capacity); var events=new List<byte[]>();
                device.OnInputReceived=e=>events.Add((byte[])e.Buffer.Clone());
                hidapi.HidApiNative.Reset(Reply(capacity,full),Reply(shortLength,shortPacket),Reply(-1));
                device.RunBoundedLoop();
                Check(events.Count==2,"two positive reads, two callbacks");
                Check(events[1].Length==capacity,"callback exposes capacity rather than actual length");
                Check(events[1][59]==0xA5,"previous packed report tail survives short positive read");
                Check(events[1][0]==1 && events[1][1]==0 && events[1][2]==9,"old/full header survives short positive read");
                Check(hidapi.HidApiNative.Counts.SequenceEqual(new[]{(int)capacity,shortLength,-1}),"fake consumed actual counts");
            }
            var polling=new hidapi.HidDevice(capacity); var packet=Packet(capacity);
            hidapi.HidApiNative.Reset(Reply(capacity,packet),Reply(0),Reply(-1));
            var first=(byte[])polling.Read().Clone(); var timeout=(byte[])polling.Read().Clone(); var error=(byte[])polling.Read().Clone();
            Check(first.SequenceEqual(timeout),"byte-array Read exposes previous contents on timeout");
            Check(first.SequenceEqual(error),"byte-array Read exposes previous contents on error");
            var explicitCount=new hidapi.HidDevice(capacity); var output=new byte[capacity];
            hidapi.HidApiNative.Reset(Reply(3,new byte[]{1,0,9}));
            Check(explicitCount.Read(output)==3,"buffer overload preserves actual count");
            bool rejected=false;
            try { explicitCount.Read(new byte[capacity-1]); } catch(ArgumentException) { rejected=true; }
            Check(rejected,"smaller destination rejected");
            var quiet=new hidapi.HidDevice(capacity); var quietEvents=new List<byte[]>();
            quiet.OnInputReceived=e=>quietEvents.Add((byte[])e.Buffer.Clone());
            hidapi.HidApiNative.Reset(Reply(0),Reply(capacity,new byte[capacity]),Reply(-1)); quiet.RunBoundedLoop();
            Check(quietEvents.Count==1 && quietEvents[0].All(b=>b==0),"timeout is silent; actual zero payload delivered");
        }
        var props=typeof(hidapi.HidDeviceInputReceivedEventArgs).GetProperties().Select(p=>p.Name).Order().ToArray();
        Check(props.SequenceEqual(new[]{"Buffer","Device"}),"event args have no actual-length property");
        Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new { status="pass", checks, shortReports=118,
            seam="source methods + in-memory native-read substitute; no DLL/device/thread executed" }));
    }
}
