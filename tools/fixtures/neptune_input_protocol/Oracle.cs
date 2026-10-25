using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.Json;
using steam_hidapi.net;
using steam_hidapi.net.Hid;

// This executable is compiled twice from independently frozen exact source:
// historical official sources, and shipped-assembly ILSpy pure structure/state.
// No audited assembly is referenced or loaded. No controller/transport is compiled.
internal static class Oracle
{
    public static int Main(string[] args)
    {
        if (!BitConverter.IsLittleEndian) throw new Exception("Oracle requires a little endian host");
        var fields = typeof(NCInput).GetFields(BindingFlags.Public | BindingFlags.Instance)
            .OrderBy(f => Marshal.OffsetOf<NCInput>(f.Name).ToInt32()).ToArray();
        int size = Marshal.SizeOf<NCInput>();
        var layout = fields.Select(f => new {
            name = f.Name, type = f.FieldType.Name,
            offset = Marshal.OffsetOf<NCInput>(f.Name).ToInt32(),
            size = Marshal.SizeOf(f.FieldType)
        }).ToArray();
        var packets = new List<object>();
        foreach (var line in File.ReadLines(args[0]))
        {
            var columns = line.Split('\t');
            var bytes = Convert.FromHexString(columns[1]);
            if (bytes.Length < size) throw new Exception("Short oracle input");
            // Independent field assignment: Marshal determines offsets and BinaryReader
            // determines signed/little endian interpretation. No memory alias or unsafe cast.
            object boxed = new NCInput();
            using var reader = new BinaryReader(new MemoryStream(bytes, false));
            foreach (var f in fields)
            {
                reader.BaseStream.Position = Marshal.OffsetOf<NCInput>(f.Name).ToInt32();
                if (f.FieldType == typeof(byte)) f.SetValue(boxed, reader.ReadByte());
                else if (f.FieldType == typeof(short)) f.SetValue(boxed, reader.ReadInt16());
                else if (f.FieldType == typeof(uint)) f.SetValue(boxed, reader.ReadUInt32());
                else throw new Exception("Unexpected struct field type");
            }
            var input = (NCInput)boxed;
            var state = new NeptuneControllerInputState(input);
            var buttons = Enum.GetValues<NeptuneControllerButton>().Select(b => new {
                id = (int)b, name = b.ToString(), down = state.ButtonState[b]
            }).ToArray();
            var axes = Enum.GetValues<NeptuneControllerAxis>().Select(a => new {
                id = (int)a, name = a.ToString(), value = (int)state.AxesState[a]
            }).ToArray();
            packets.Add(new { id = columns[0], seq = input.seq, buttons, axes });
        }
        Console.WriteLine(JsonSerializer.Serialize(new { size, layout, packets }));
        return 0;
    }
}
