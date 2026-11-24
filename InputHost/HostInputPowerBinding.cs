using YeManCC.SharedSleepPower;

namespace YeManInputHost.Diagnostics;

// Command permission only. The existing HostSession/tuple owns its retained
// controller and endpoint; finishing this scope never certifies their release.
internal sealed class HostInputPowerBinding
{
    internal readonly record struct Tuple(string RunId, ulong Epoch, string StartupNonce);
    internal interface ILease : IDisposable
    {
        RemotePowerDescriptorV1 Binding { get; }
        bool Current(RemotePowerPurpose purpose);
        bool LocalReturnComplete { get; }
        bool CleanupOnly => false;
        void CancelLocal();
        bool TryFinish(out RemotePowerSettlementV1 receipt);
        RemoteSettlementDelivery Send(RemotePowerSettlementV1 receipt);
    }
    internal delegate ILease? Import(RemotePowerOfferV1 offer, RemotePowerPurpose purpose);
    private sealed class NativeLease : ILease
    {
        private RemoteNativePowerScope? scope;
        private RemoteNativePowerImportOwner? failedLocalOwner;
        internal void Bind(RemoteNativePowerScope value) => scope = value;
        internal void BindFailed(RemoteNativePowerImportOwner value) => failedLocalOwner=value;
        private RemoteNativePowerScope Scope => scope ?? throw new InvalidOperationException("input-import-not-bound");
        public RemotePowerDescriptorV1 Binding => Scope.Binding;
        public bool Current(RemotePowerPurpose purpose) => scope is not null&&scope.InputCurrentLocal(purpose);
        public bool LocalReturnComplete => failedLocalOwner?.LocalReturnComplete??(scope is null||scope.LocalReturnComplete);
        public bool CleanupOnly => failedLocalOwner is not null;
        public void CancelLocal() => scope?.CancelLocal();
        public bool TryFinish(out RemotePowerSettlementV1 receipt)
        {
            receipt=default;
            // An unpublished import already made its one terminal attempt.
            return scope is not null&&scope.TryCreateSettlement(RemotePowerSettlementOutcome.InputOperationReturned,
                RemotePowerSettlementFlags.InputOperationFinished,out receipt);
        }
        public RemoteSettlementDelivery Send(RemotePowerSettlementV1 receipt) => scope?.SendSettlement(receipt)??RemoteSettlementDelivery.Rejected;
        public void Dispose() { if(failedLocalOwner is not null)failedLocalOwner.Dispose();else scope?.Dispose(); }
    }
    internal sealed class Slot(Tuple tuple, string header)
    {
        internal ILease Lease { get; private set; } = null!;
        internal void Bind(ILease lease) => Lease = lease;
        internal readonly Tuple Tuple = tuple;
        internal readonly string Header = header;
        internal int Borrowers;
        internal bool Retired, Settling, Cancelling, CancelStarted, TerminalAttempted;
        internal RemoteSettlementDelivery TerminalDelivery = RemoteSettlementDelivery.Unknown;
    }
    internal sealed class Borrower : IDisposable
    {
        private readonly HostInputPowerBinding owner;
        private readonly Slot slot;
        private bool returned;
        internal Borrower(HostInputPowerBinding owner, Slot slot) { this.owner = owner; this.slot = slot; }
        internal bool Current
        {
            get
            {
                lock (owner.gate)
                    return !returned && !slot.Retired && slot.Lease.Current(slot.Lease.Binding.Purpose);
            }
        }
        internal bool HardwareCurrent
        {
            get
            {
                lock(owner.gate)
                {
                    var binding=slot.Lease.Binding;var power=binding.Captured;
                    return !returned&&!slot.Retired&&binding.Purpose==RemotePowerPurpose.InputLifecycle&&
                        power.Phase==0&&(power.Flags&7)==3&&slot.Lease.Current(binding.Purpose);
                }
            }
        }
        // A background refresh borrows this exact original scope, never a
        // later Ready or the next serial command's mutable current field.
        internal Borrower? Fork()
        {
            lock (owner.gate)
            {
                if (returned || slot.Retired || !slot.Lease.Current(slot.Lease.Binding.Purpose)) return null;
                var fork = new Borrower(owner, slot);
                checked { ++slot.Borrowers; }
                return fork;
            }
        }
        public void Dispose()
        {
            lock (owner.gate)
            {
                if (returned) return;
                returned = true;
                --slot.Borrowers;
            }
            // No pipe write, SDK cleanup or new worker on a Task completion.
            // The existing serial owner performs Maintenance on its next turn.
        }
    }
    private readonly object gate = new();
    private readonly Import import;
    private readonly Dictionary<RemotePowerPurpose, Slot> current = new(2);
    // Two current slots plus the one preallocated in-flight import may all
    // retire at a terminal boundary. No post-import list growth is required.
    private readonly List<Slot> retired = new(MaximumRetired + 3);
    private Slot? importing;
    private bool terminal;
    private int settlementUnknown, settlementRejected;
    private int cancelFailures, settlementExceptions, localReturnFailures, admissionFailures;
    private const int MaximumRetired = 16;

    internal HostInputPowerBinding(RemotePowerProcessV1 originalParent, string originalParentImage)
        : this((offer, purpose) => ImportNative(offer, purpose, originalParent, originalParentImage)) { }

    private static ILease? ImportNative(RemotePowerOfferV1 offer, RemotePowerPurpose purpose,
        RemotePowerProcessV1 originalParent, string image)
    {
        // Allocate the adapter before authenticated Bind. Once import succeeds
        // only a nonthrowing reference store precedes its original owner.
        var lease = new NativeLease();
        if(RemoteNativePowerScope.TryImportFromParent(offer,originalParent,image,purpose,
            out var scope,out _,out var failedLocalOwner)&&scope is not null)
        {lease.Bind(scope);return lease;}
        if(failedLocalOwner is null)return null;
        lease.BindFailed(failedLocalOwner);return lease;
    }

    // Explicit deterministic fixture port. Ordinary production uses only the
    // authenticated Native import constructor above; no permissive fallback.
    internal HostInputPowerBinding(Import import) => this.import = import;

    internal bool TryBorrow(Tuple tuple, RemotePowerPurpose purpose, string? offeredHeader,
        out Borrower? borrower, out string reason)
    {
        borrower = null; reason = "input-power-binding-unavailable";
        if(purpose is not (RemotePowerPurpose.InputLifecycle or RemotePowerPurpose.InputFrame)||
            tuple.Epoch==0||string.IsNullOrEmpty(tuple.RunId)||string.IsNullOrEmpty(tuple.StartupNonce))return false;
        Maintenance();
        lock(gate)
        {
            if(terminal)return false;
            if(current.TryGetValue(purpose,out var cached)&&cached.Tuple==tuple&&
                (offeredHeader is null||offeredHeader==cached.Header))
            {
                if(cached.Retired||!cached.Lease.Current(purpose))return false;
                var nextBorrower=new Borrower(this,cached);
                checked { ++cached.Borrowers; }
                borrower=nextBorrower;reason="";return true;
            }
            if(offeredHeader is null||retired.Count>=MaximumRetired||importing is not null)return false;
        }
        if(!RemotePowerOfferHeaderV1.TryParse(offeredHeader,out var offer)||offer.Binding.Purpose!=purpose||
            offer.Binding.Request.Low!=tuple.Epoch)return false;
        // Ownership bookkeeping and the possible borrower both exist before
        // an authenticated importer can acquire any Native registration.
        var next=new Slot(tuple,offeredHeader!);
        var committedBorrower=new Borrower(this,next);
        lock(gate)
        {
            if(terminal||retired.Count>=MaximumRetired||importing is not null)return false;
            importing=next;
        }
        ILease? lease=null;Slot? previous=null;bool accepted=false;
        try
        {
            lease=import(offer,purpose);
            if(lease is null){reason="input-power-import-rejected";return false;}
            next.Bind(lease);
            lock(gate)
            {
                if(!terminal&&retired.Count<MaximumRetired&&lease.Current(purpose))
                {
                    if(current.TryGetValue(purpose,out previous))
                    {previous.Retired=true;retired.Add(previous);}
                    next.Borrowers=1;current[purpose]=next;borrower=committedBorrower;
                    accepted=true;reason="";
                }
                else reason="input-power-import-rejected";
            }
            // A failure revoking the retired predecessor cannot lose the newly
            // committed borrower before its actual caller receives it.
            if(previous is not null)CancelSlot(previous);
            return accepted;
        }
        catch(Exception)
        {
            lock(gate)Increment(ref admissionFailures);
            reason=accepted?"":"input-power-import-rejected";return accepted;
        }
        finally
        {
            lock(gate)
            {
                if(ReferenceEquals(importing,next))importing=null;
                if(lease is not null&&!accepted){next.Retired=true;retired.Add(next);}
            }
            if(lease is not null&&!accepted){CancelSlot(next);Maintenance();}
        }
    }

    internal bool HasBorrowers
    {
        get { lock (gate) return current.Values.Any(s => s.Borrowers != 0) || retired.Any(s => s.Borrowers != 0); }
    }
    internal (int Unknown, int Rejected) SettlementFailures
    { get { lock (gate) return (settlementUnknown, settlementRejected); } }

    internal (int Cancel, int Settlement, int LocalReturn, int Admission) CleanupFailures
    { get { lock(gate)return(cancelFailures,settlementExceptions,localReturnFailures,admissionFailures); } }
    internal int PendingLocalReturns { get { lock(gate)return retired.Count; } }
    internal bool HasUnreturnedLocalOwnership
    { get { lock(gate)return importing is not null||retired.Count!=0; } }
    private static void Increment(ref int value) { if(value<int.MaxValue)++value; }

    private void CancelSlot(Slot slot)
    {
        lock(gate)
        {
            if(slot.CancelStarted||slot.Settling)return;
            slot.CancelStarted=true;slot.Cancelling=true;
        }
        try { slot.Lease.CancelLocal(); }
        catch(Exception) { lock(gate)Increment(ref cancelFailures); }
        finally { lock(gate)slot.Cancelling=false; }
    }

    internal void RetireAll()
    {
        Slot[] slots;
        lock(gate)
        {
            slots=current.Values.ToArray();terminal=true;
            foreach(var slot in slots){slot.Retired=true;retired.Add(slot);}
            current.Clear();
        }
        foreach(var slot in slots)CancelSlot(slot);
        Maintenance();
    }

    internal void Maintenance()
    {
        Slot[] completed;
        lock(gate)
        {
            if(retired.Count==0)return;
            completed=retired.Where(s=>s.Borrowers==0&&!s.Settling&&!s.Cancelling).ToArray();
            foreach(var slot in completed){slot.Settling=true;slot.CancelStarted=true;}
        }
        foreach(var slot in completed)
        {
            bool attempt;
            lock(gate){attempt=!slot.TerminalAttempted;slot.TerminalAttempted=true;}
            if(attempt&&!slot.Lease.CleanupOnly)
            {
                var delivery=RemoteSettlementDelivery.Rejected;
                try { if(slot.Lease.TryFinish(out var receipt))delivery=slot.Lease.Send(receipt); }
                catch(Exception)
                {delivery=RemoteSettlementDelivery.Unknown;lock(gate)Increment(ref settlementExceptions);}
                lock(gate)
                {
                    slot.TerminalDelivery=delivery;
                    if(delivery==RemoteSettlementDelivery.Unknown)Increment(ref settlementUnknown);
                    else if(delivery==RemoteSettlementDelivery.Rejected)Increment(ref settlementRejected);
                }
            }
            bool returned=false,failed=false;
            try { slot.Lease.Dispose();returned=slot.Lease.LocalReturnComplete; }
            catch(Exception)
            {
                failed=true;
                try{returned=slot.Lease.LocalReturnComplete;}catch(Exception){returned=false;}
            }
            finally
            {
                lock(gate)
                {
                    if(failed||!returned)Increment(ref localReturnFailures);
                    if(returned)retired.Remove(slot);
                    // Only local ownership may continue next serial turn. The
                    // original terminal attempt above is never replayed/refunded.
                    slot.Settling=false;
                }
            }
        }
    }
}
