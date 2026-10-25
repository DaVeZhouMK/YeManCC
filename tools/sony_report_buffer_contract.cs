// Contract shared by the two isolated copies of the actual Host source.
// There is no product entry point, controller constructor or fake SDK here.
namespace SonyReportBufferContracts;
public interface ISonyFixture : System.IDisposable
{
    bool IsCandidate { get; }
    string Persona { get; }
    string Handle(string message);
    void ResetForSample();
    long SuccessfulSubmits { get; }
    uint SdkSequence { get; }
    byte[] ReadPayload();
    byte[] ReadSdkBuffer();
    byte[] Fresh(string message, long counter, int profileSize = 64);
    void Fill(string message, long counter, byte[]? destination, int profileSize = 64);
    void SubmitRaw(byte[] data);
    void MarkSdkDisposed(bool disposed);
    byte[]? SessionBuffer { get; }
    bool SdkDirectlyRetains(byte[] data);
    bool Parse(string message, out string error);
    void SubmitNeutral();
    void ConcurrentFillAndSubmit(string message, long counter);
}
