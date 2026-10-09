using System.Windows.Input;

namespace HandheldCompanion.ViewModels
{
    public class CollectionMenuItemViewModel
    {
        public string Name { get; }
        public bool IsChecked { get; }
        public bool IsCheckable { get; }
        public bool IsSeparator { get; }
        public ICommand? ToggleCommand { get; }

        public CollectionMenuItemViewModel(string name, bool isChecked, ICommand toggleCommand, bool isCheckable = true)
        {
            Name = name;
            IsChecked = isChecked;
            IsCheckable = isCheckable;
            ToggleCommand = toggleCommand;
        }

        private CollectionMenuItemViewModel() { IsSeparator = true; }

        public static CollectionMenuItemViewModel Separator => new();
    }
}
