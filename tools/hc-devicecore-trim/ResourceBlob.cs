using System.Text;

namespace HcDeviceCoreTrim;

// .resources v2 data is copied as opaque bytes. No object, image, BAML, or native library is loaded.
sealed record ResourceEntry(string Name, int TypeCode, string Type, byte[] Raw, int Offset = 0)
{
    public int Bytes => Raw.Length;
}
sealed class ResourceBlob
{
    public byte[] Header { get; private init; } = [];
    public string ReaderType { get; private init; } = "";
    public string[] Types { get; private init; } = [];
    public List<ResourceEntry> Entries { get; private init; } = [];
    public static ResourceBlob Read(byte[] bytes)
    {
        using var stream = new MemoryStream(bytes, false);
        using var r = new BinaryReader(stream, Encoding.UTF8, true);
        if (r.ReadUInt32() != 0xbeefcace || r.ReadInt32() != 1) throw new InvalidDataException("Unsupported resources header");
        int headerLength = r.ReadInt32(); long headerEnd = stream.Position + headerLength;
        string readerType = r.ReadString(); r.ReadString();
        if (stream.Position > headerEnd) throw new InvalidDataException("Invalid header length");
        stream.Position = headerEnd;
        byte[] header = bytes[..checked((int)headerEnd)];
        if (r.ReadInt32() != 2) throw new InvalidDataException("Only .resources v2 is supported");
        int count = r.ReadInt32(), typeCount = r.ReadInt32();
        if (count < 0 || typeCount < 0 || count > bytes.Length / 8 || typeCount > bytes.Length) throw new InvalidDataException("Invalid resource counts");
        var types = Enumerable.Range(0, typeCount).Select(_ => r.ReadString()).ToArray();
        stream.Position = (stream.Position + 7) & ~7L;
        for (int i = 0; i < count; i++) r.ReadInt32();
        var positions = Enumerable.Range(0, count).Select(_ => r.ReadInt32()).ToArray();
        int dataStart = r.ReadInt32(); long nameStart = stream.Position;
        var names = new List<(string Name, int Offset)>();
        using var unicode = new BinaryReader(stream, Encoding.Unicode, true);
        foreach (int p in positions)
        {
            stream.Position = nameStart + p; string name = unicode.ReadString(); int offset = unicode.ReadInt32();
            if (offset < 0 || dataStart + (long)offset >= bytes.Length) throw new InvalidDataException("Invalid resource data offset");
            names.Add((name, offset));
        }
        if (names.Select(x => x.Name).Distinct(StringComparer.Ordinal).Count() != count || names.Select(x => x.Offset).Distinct().Count() != count)
            throw new InvalidDataException("Duplicate resource name or shared data offsets");
        names.Sort((a,b) => a.Offset.CompareTo(b.Offset));
        var entries = new List<ResourceEntry>();
        for (int i = 0; i < names.Count; i++)
        {
            int start = checked(dataStart + names[i].Offset), end = i+1 < count ? checked(dataStart + names[i+1].Offset) : bytes.Length;
            if (end <= start) throw new InvalidDataException("Invalid resource extent");
            stream.Position = start; int code = r.Read7BitEncodedInt();
            string type = code >= 64 ? types[code-64] : "ResourceTypeCode." + code;
            entries.Add(new(names[i].Name, code, type, bytes[start..end], start));
        }
        return new() { Header = header, ReaderType = readerType, Types = types, Entries = entries };
    }
    public byte[] Write(IEnumerable<ResourceEntry> retained)
    {
        var entries = retained.OrderBy(x => x.Name, StringComparer.Ordinal).ToArray();
        using var names = new MemoryStream(); using var data = new MemoryStream();
        using var nw = new BinaryWriter(names, Encoding.Unicode, true); using var dw = new BinaryWriter(data, Encoding.UTF8, true);
        var positions = new List<(int Hash,int Offset)>();
        foreach (var entry in entries)
        {
            positions.Add((NameHash(entry.Name), checked((int)names.Position)));
            nw.Write(entry.Name); nw.Write(checked((int)data.Position)); dw.Write(entry.Raw);
        }
        using var output = new MemoryStream(); using var w = new BinaryWriter(output, Encoding.UTF8, true);
        w.Write(Header); w.Write(2); w.Write(entries.Length); w.Write(Types.Length);
        foreach (var type in Types) w.Write(type);
        while ((output.Position & 7) != 0) w.Write((byte)0);
        var sorted = positions.OrderBy(x => x.Hash).ThenBy(x => x.Offset).ToArray();
        foreach (var p in sorted) w.Write(p.Hash);
        foreach (var p in sorted) w.Write(p.Offset);
        w.Write(checked((int)(output.Position + 4 + names.Length))); w.Write(names.ToArray()); w.Write(data.ToArray());
        return output.ToArray();
    }
    static int NameHash(string name)
    {
        uint hash = 5381; foreach (char c in name) hash = unchecked(((hash << 5) + hash) ^ c); return unchecked((int)hash);
    }
}

