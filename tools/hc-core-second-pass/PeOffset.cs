using System.Reflection.PortableExecutable;
namespace HcDeviceCoreTrim;
static class PeCompactor {
 public static int Offset(PEHeaders h,int rva){foreach(var s in h.SectionHeaders)if(rva>=s.VirtualAddress&&rva<s.VirtualAddress+Math.Max(s.VirtualSize,s.SizeOfRawData))return checked(s.PointerToRawData+rva-s.VirtualAddress);if(rva<h.PEHeader!.SizeOfHeaders)return rva;throw new InvalidDataException("RVA not mapped");}
}
