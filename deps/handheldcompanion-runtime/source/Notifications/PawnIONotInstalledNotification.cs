using iNKORE.UI.WPF.Modern.Controls;

namespace HandheldCompanion.Notifications
{
    public class PawnIONotInstalledNotification : Notification
    {
        public PawnIONotInstalledNotification() : base(
            "PawnIO is not installed",
            "AMD low-level RyzenSMU features require PawnIO. Install PawnIO to enable these controls.",
            string.Empty,
            InfoBarSeverity.Warning)
        { }
    }
}
