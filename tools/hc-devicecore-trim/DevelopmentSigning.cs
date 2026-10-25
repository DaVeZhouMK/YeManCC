using System.Buffers.Binary;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Runtime.InteropServices;
using System.Security.Cryptography;

namespace HcDeviceCoreTrim;
static class DevelopmentSigning
{
    public static byte[] PublicKey(RSA rsa)
    {
        var p=rsa.ExportParameters(false);var modulus=p.Modulus??throw new InvalidOperationException();var exponent=p.Exponent??throw new InvalidOperationException();
        byte[] key=new byte[32+modulus.Length];
        void Put(int offset,int value)=>BinaryPrimitives.WriteInt32LittleEndian(key.AsSpan(offset,4),value);
        Put(0,0x2400);Put(4,0x8004);Put(8,key.Length-12);key[12]=6;key[13]=2;Put(16,0x2400);Put(20,0x31415352);Put(24,modulus.Length*8);
        int exp=0;foreach(byte b in exponent)exp=checked(exp*256+b);Put(28,exp);Array.Reverse(modulus);modulus.CopyTo(key,32);return key;
    }
    public static int PublicKeyOffset(MetadataReader md)
    {
        var h=md.GetAssemblyDefinition().PublicKey;int start=md.GetHeapMetadataOffset(HeapIndex.Blob)+MetadataTokens.GetHeapOffset(h);
        int n=md.GetBlobBytes(h).Length;return start+(n<128?1:n<16384?2:4);
    }
    public static string Token(byte[] publicKey){var h=SHA1.HashData(publicKey);var token=h[^8..];Array.Reverse(token);return Convert.ToHexString(token).ToLowerInvariant();}
    public static byte[] Hash(byte[] bytes)
    {
        using var pe=new PEReader(new MemoryStream(bytes,false));var h=pe.PEHeaders;var ph=h.PEHeader!;var cor=h.CorHeader!;
        int dir=h.PEHeaderStartOffset+(ph.Magic==PEMagic.PE32Plus?112:96), headerEnd=h.PEHeaderStartOffset+h.CoffHeader.SizeOfOptionalHeader+h.SectionHeaders.Length*40;
        byte[] headers=bytes[..headerEnd];Array.Clear(headers,h.PEHeaderStartOffset+64,4);Array.Clear(headers,dir+4*8,8);
        using var hash=IncrementalHash.CreateHash(HashAlgorithmName.SHA1);hash.AppendData(headers);
        int sig=PeCompactor.Offset(h,cor.StrongNameSignatureDirectory.RelativeVirtualAddress),sigEnd=sig+cor.StrongNameSignatureDirectory.Size;
        foreach(var s in h.SectionHeaders)
        {
            int start=s.PointerToRawData,end=start+s.SizeOfRawData;
            if(sig>=start&&sigEnd<=end){hash.AppendData(bytes,start,sig-start);hash.AppendData(bytes,sigEnd,end-sigEnd);}
            else hash.AppendData(bytes,start,s.SizeOfRawData);
        }
        return hash.GetHashAndReset();
    }
    public static bool Verify(byte[] bytes)
    {
        using var pe=new PEReader(new MemoryStream(bytes,false));var md=pe.GetMetadataReader();var key=md.GetBlobBytes(md.GetAssemblyDefinition().PublicKey);var cor=pe.PEHeaders.CorHeader!;
        if(key.Length<32||cor.StrongNameSignatureDirectory.Size==0)return false;
        int exp=BinaryPrimitives.ReadInt32LittleEndian(key.AsSpan(28,4));byte[] exponent=BitConverter.GetBytes(exp);Array.Reverse(exponent);exponent=exponent.SkipWhile(x=>x==0).ToArray();
        byte[] modulus=key[32..];Array.Reverse(modulus);using var rsa=RSA.Create();rsa.ImportParameters(new RSAParameters{Modulus=modulus,Exponent=exponent});
        int offset=PeCompactor.Offset(pe.PEHeaders,cor.StrongNameSignatureDirectory.RelativeVirtualAddress);byte[] signature=bytes[offset..(offset+cor.StrongNameSignatureDirectory.Size)];Array.Reverse(signature);
        return rsa.VerifyHash(Hash(bytes),signature,HashAlgorithmName.SHA1,RSASignaturePadding.Pkcs1);
    }
    public static byte[] Resign(byte[] bytes,string keyPath,string expectedPublicKey)
    {
        using var rsa=RSA.Create();rsa.ImportFromPem(File.ReadAllText(keyPath));byte[] key=PublicKey(rsa);
        if(!Convert.ToHexString(key).Equals(expectedPublicKey,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("YMCC signing key differs from pinned public identity");
        using(var pe=new PEReader(new MemoryStream(bytes,false)))
        {
            var md=pe.GetMetadataReader();if(md.GetBlobBytes(md.GetAssemblyDefinition().PublicKey).Length!=key.Length)throw new InvalidDataException("New public key must fit original blob to preserve every metadata token/offset");
            var cor=pe.PEHeaders.CorHeader!;if(cor.StrongNameSignatureDirectory.Size!=rsa.KeySize/8)throw new InvalidDataException("New signature length differs from original reserved area");
            int meta=PeCompactor.Offset(pe.PEHeaders,cor.MetadataDirectory.RelativeVirtualAddress);key.CopyTo(bytes,meta+PublicKeyOffset(md));
            int offset=PeCompactor.Offset(pe.PEHeaders,cor.StrongNameSignatureDirectory.RelativeVirtualAddress);Array.Clear(bytes,offset,cor.StrongNameSignatureDirectory.Size);
            byte[] signature=rsa.SignHash(Hash(bytes),HashAlgorithmName.SHA1,RSASignaturePadding.Pkcs1);Array.Reverse(signature);signature.CopyTo(bytes,offset);
            BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(pe.PEHeaders.PEHeaderStartOffset+64,4),0);
            uint sum=0;for(int i=0;i<bytes.Length;i+=2){sum+=(uint)(bytes[i]|(i+1<bytes.Length?bytes[i+1]<<8:0));sum=(sum&0xffff)+(sum>>16);}sum=(sum&0xffff)+(sum>>16);sum+=checked((uint)bytes.Length);
            BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(pe.PEHeaders.PEHeaderStartOffset+64,4),unchecked((int)sum));
        }
        if(!Verify(bytes))throw new InvalidDataException("YMCC strong-name signature verification failed");return bytes;
    }
    [DllImport("mscoree.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    [return:MarshalAs(UnmanagedType.Bool)]
    static extern bool StrongNameSignatureVerificationEx(string path,[MarshalAs(UnmanagedType.Bool)]bool force,[MarshalAs(UnmanagedType.Bool)]out bool verified);
    public static object WindowsVerify(string path)
    {
        bool success=StrongNameSignatureVerificationEx(Path.GetFullPath(path),true,out bool verified);return new{Api="Windows mscoree StrongNameSignatureVerificationEx(force=true)",Success=success,Verified=verified,Error=success?0:Marshal.GetLastWin32Error()};
    }
}
