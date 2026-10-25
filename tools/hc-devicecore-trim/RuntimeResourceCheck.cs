using Mono.Cecil;
using System.Resources;
using System.Resources.Extensions;
namespace HcDeviceCoreTrim;
static class RuntimeResourceCheck
{
    public static object Check(string input)
    {
        using var a=Util.Read(input,out var resolver);using var cleanup=resolver;
        int strings=0,streams=0,bytes=0;var rows=new List<object>();
        foreach(var resource in a.MainModule.Resources.OfType<EmbeddedResource>().Where(r=>r.Name.EndsWith(".resources",StringComparison.Ordinal)))
        {
            var blob=ResourceBlob.Read(resource.GetResourceData());
            using var data=resource.GetResourceStream();
            using IResourceReader reader=blob.ReaderType.Contains("DeserializingResourceReader",StringComparison.Ordinal)?new DeserializingResourceReader(data):new ResourceReader(data);
            var expected=blob.Entries.ToDictionary(e=>e.Name,StringComparer.Ordinal);int seen=0;
            var e=reader.GetEnumerator();
            while(e.MoveNext())
            {
                var raw=expected[(string)e.Key];seen++;
                // Never deserialize an arbitrary bitmap/object. Only CLR strings, byte arrays and streams are requested.
                if(raw.TypeCode==1){if(e.Value is not string)throw new InvalidDataException("String not decoded");strings++;}
                else if(raw.TypeCode==32){if(e.Value is not byte[])throw new InvalidDataException("Byte array not decoded");bytes++;}
                else if(raw.TypeCode==33)
                {
                    if(e.Value is not Stream stream)throw new InvalidDataException("Stream not decoded");
                    if(raw.Name=="resources/msiapcfg.dll"){if(stream.ReadByte()!='M'||stream.ReadByte()!='Z')throw new InvalidDataException("MSI native payload header differs");}
                    streams++;
                }
            }
            if(seen!=expected.Count)throw new InvalidDataException("Resource reader entry count differs");
            rows.Add(new{resource.Name,Entries=seen,Reader=blob.ReaderType,UnknownSerializedObjectsDeserialized=false});
        }
        return new{Passed=true,Sha256=Util.HashFile(input),StringsDecoded=strings,ByteArraysDecoded=bytes,StreamsDecoded=streams,Resources=rows,ProductAssemblyLoadedOrExecuted=false};
    }
}
