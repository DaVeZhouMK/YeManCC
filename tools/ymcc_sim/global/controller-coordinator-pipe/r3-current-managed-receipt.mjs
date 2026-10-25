// Fixture-only receipt serialization. Preserve the exclusive fresh output,
// existing final FlushAsync, payload fields, and original exception path.
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
const hash=text=>createHash('sha256').update(text).digest('hex');
const receiptWriter=String.raw`
// Fixture-only JSON writer. Unrecognized types retain the original serializer.
internal static class ManagedFixtureReceiptJson
{
    private static readonly System.Collections.Generic.Dictionary<System.Type, System.Reflection.PropertyInfo[]> Properties = new();
    private static System.Reflection.PropertyInfo[] PublicProperties(System.Type type)
    {
        if (Properties.TryGetValue(type, out var properties)) return properties;
        properties = type.GetProperties(System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Instance)
            .Where(p => p.GetMethod is not null && p.GetMethod.IsPublic && p.GetIndexParameters().Length == 0).ToArray();
        Properties.Add(type, properties); return properties;
    }
    internal static void Write(System.IO.Stream stream, object receipt)
    {
        using var writer = new System.Text.Json.Utf8JsonWriter(stream, new System.Text.Json.JsonWriterOptions { Indented = true, MaxDepth = 64 });
        Value(writer, receipt); writer.Flush();
    }
    private static void Value(System.Text.Json.Utf8JsonWriter writer, object? value)
    {
        switch (value)
        {
            case null: writer.WriteNullValue(); return;
            case string v: writer.WriteStringValue(v); return;
            case bool v: writer.WriteBooleanValue(v); return;
            case int v: writer.WriteNumberValue(v); return;
            case long v: writer.WriteNumberValue(v); return;
            case uint v: writer.WriteNumberValue(v); return;
            case ulong v: writer.WriteNumberValue(v); return;
            case short v: writer.WriteNumberValue(v); return;
            case ushort v: writer.WriteNumberValue(v); return;
            case byte v: writer.WriteNumberValue(v); return;
            case sbyte v: writer.WriteNumberValue(v); return;
            case double v: writer.WriteNumberValue(v); return;
            case float v: writer.WriteNumberValue(v); return;
            case decimal v: writer.WriteNumberValue(v); return;
            case System.Text.Json.JsonElement v: v.WriteTo(writer); return;
            case System.Text.Json.JsonDocument v: v.RootElement.WriteTo(writer); return;
            case object[] items:
                writer.WriteStartArray(); foreach (var item in items) Value(writer, item); writer.WriteEndArray(); return;
            case System.Collections.Generic.List<object> items:
                writer.WriteStartArray(); foreach (var item in items) Value(writer, item); writer.WriteEndArray(); return;
        }
        var type = value.GetType();
        // Only compiler-generated anonymous objects owned by this fixture use
        // the reflected property fast path. Named types use System.Text.Json.
        if (type.Assembly == typeof(ManagedFixtureReceiptJson).Assembly &&
            type.IsDefined(typeof(System.Runtime.CompilerServices.CompilerGeneratedAttribute), false) &&
            type.Name.StartsWith("<>f__AnonymousType", System.StringComparison.Ordinal))
        {
            writer.WriteStartObject();
            foreach (var p in PublicProperties(type)) { writer.WritePropertyName(p.Name); Value(writer, p.GetValue(value)); }
            writer.WriteEndObject(); return;
        }
        // Preserve specialized converters for dictionaries, byte arrays,
        // records, GUIDs, enums, timestamps, custom types and named classes.
        System.Text.Json.JsonSerializer.Serialize(writer, value, type);
    }
    internal static void WriteAndCompare(System.IO.Stream stream, object receipt)
    {
        using var generated = new System.IO.MemoryStream(); Write(generated, receipt);
        var original = System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(receipt, new System.Text.Json.JsonSerializerOptions { WriteIndented = true });
        if (!generated.ToArray().AsSpan().SequenceEqual(original)) throw new System.InvalidOperationException("fixture-receipt-fast-json-byte-parity-failed");
        generated.Position = 0; generated.CopyTo(stream);
    }
}
`;
export function optimizeManagedReceipt(source){
 const before='await JsonSerializer.SerializeAsync(stream, receipt, new JsonSerializerOptions { WriteIndented = true }).ConfigureAwait(false);';
 const after='if (Environment.GetEnvironmentVariable("YMCC_FIXTURE_RECEIPT_PARITY") == "1") ManagedFixtureReceiptJson.WriteAndCompare(stream, receipt); else ManagedFixtureReceiptJson.Write(stream, receipt);';
 assert.equal(source.split(before).length,2,'Exactly one original fixture receipt writer');
 assert(source.includes('new FileStream(options.Receipt, FileMode.CreateNew, FileAccess.Write, FileShare.None)'),'Owned exclusive fresh receipt stream required');
 assert(source.includes('await stream.FlushAsync().ConfigureAwait(false);'),'Original final receipt flush required');
 assert(!source.includes('class ManagedFixtureReceiptJson'),'Fixture receipt writer already present');
 const text=source.replace(before,after)+receiptWriter;
 const restore=value=>{assert.equal(value.split(after).length,2);assert(value.endsWith(receiptWriter),'Exact appended receipt writer required');const restored=value.slice(0,-receiptWriter.length).replace(after,before);assert.equal(hash(restored),hash(source));return restored;};
 assert.equal(restore(text),source);
 return {text,restore,originalSha256:hash(source),optimizedSha256:hash(text)};
}
