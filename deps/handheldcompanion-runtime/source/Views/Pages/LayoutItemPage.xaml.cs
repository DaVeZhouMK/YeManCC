using HandheldCompanion.ViewModels;
using System.Windows;
using Page = System.Windows.Controls.Page;

namespace HandheldCompanion.Views.Pages;

public partial class LayoutItemPage : Page
{
    // page vars
    private ActionSettingsPage actionSettingsPage = null!;
    private LayoutItemPageViewModel viewModel = null!;

    public MappingViewModel? CurrentMapping { get; private set; }

    public LayoutItemPage()
    {
        InitializeComponent();
        viewModel = new LayoutItemPageViewModel();
        DataContext = viewModel;
    }

    public LayoutItemPage(string Tag, object parent) : this()
    {
        this.Tag = Tag;
    }

    // Initialize pages
    public void Initialize()
    {
        if (actionSettingsPage is not null)
            return;

        actionSettingsPage = new ActionSettingsPage();
        ContentFrame.Navigate(actionSettingsPage);
    }

    public void SetMapping(MappingViewModel mapping)
    {
        CurrentMapping = mapping;
        viewModel.CurrentMapping = mapping;

        // Update the settings page with the mapping
        actionSettingsPage?.SetMapping(mapping);
    }

    private void Page_Loaded(object sender, RoutedEventArgs e)
    {
        // Set focus to the page so UIGamepad can track it
        Focus();
    }
}
