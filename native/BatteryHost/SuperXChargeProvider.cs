namespace YeManBatteryHost;

// Existing Controller/UI firmware threshold consumer. Construction is dormant;
// reads use the source's UMA terminal, writes use its distinct checked SIO path.
internal interface IChargeWriteAdmission { void Write(int value,Reading before,Action requireAvailable); }
internal sealed class SuperXChargeProvider : IChargeProvider,IChargeWriteAdmission
{
    readonly ISuperXChargeReader reader;
    readonly ISuperXChargeWriter writer;
    bool stopped,writeAttempted,readerClosed,writerClosed;
    internal SuperXChargeProvider(ISuperXChargeReader? reader=null,ISuperXChargeWriter? writer=null)
    {
        this.reader=reader??new SuperXUmaChargeReader();
        this.writer=writer??new SuperXCheckedEcWriter();
    }
    public bool CanReadLimit=>true;
    void Check(){if(stopped)throw new IOException("Super X 充电终端已停止");}
    public Reading Read()
    {
        Check();byte raw=reader.ReadLimit();
        if(raw<75||raw>100)throw new IOException("Super X 充电阈值不在已验证源范围75至100内");
        return new(raw,raw);
    }
    public void Write(int value,Reading before)=>Write(value,before,static ()=>{});
    public void Write(int value,Reading before,Action requireAvailable)
    {
        Check();
        if(value<75||value>100||value%5!=0)throw new IOException("Super X 充电档位超出已接入范围");
        if(writeAttempted)throw new IOException("Super X 当前写入请求已经消费，不自动重放");
        if(before.Raw is null||before.Raw<75||before.Raw>100)throw new IOException("缺少有效的 Super X 原始充电状态");
        if(Read().Raw!=before.Raw)throw new IOException("充电状态已被其他软件改变，请重新读取");
        requireAvailable(); // Admit only after the final potentially blocking UMA pre-read.
        writeAttempted=true;
        writer.WriteLimit((byte)value);
        // Controller performs the separate subsequent UMA read and compares it
        // with the target. A setter completion alone is never firmware readback.
    }
    public void Dispose()
    {
        stopped=true;
        Exception? failure=null;
        if(!writerClosed)try{writer.Dispose();writerClosed=true;}catch(Exception error){failure=error;}
        if(!readerClosed)try{reader.Dispose();readerClosed=true;}catch(Exception error){failure=failure is null?error:new AggregateException(failure,error);}
        if(failure is not null)System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failure).Throw();
    }
}
