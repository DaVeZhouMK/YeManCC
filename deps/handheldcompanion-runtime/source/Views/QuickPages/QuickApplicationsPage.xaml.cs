using HandheldCompanion.ViewModels;
using System.Windows.Controls;

namespace HandheldCompanion.Views.QuickPages;

public partial class QuickApplicationsPage : Page
{
    public QuickApplicationsPage(string Tag) : this()
    {
        this.Tag = Tag;
    }

    public QuickApplicationsPage()
    {
        DataContext = new QuickApplicationsPageViewModel();
        InitializeComponent();

        // Detach SnapDialog from the visual tree so that iNKORE's ContentDialog.ShowAsync
        // places the dialog itself (not just its LayoutRoot via a Popup) into the AdornerLayer.
        // When base.Parent == null, DataContext inheritance works correctly on every open.
        RootGrid.Children.Remove(SnapDialog);
        SnapDialog.DataContext = DataContext;
    }

    public void Close()
    {
        ((QuickApplicationsPageViewModel)DataContext).Dispose();
    }
}