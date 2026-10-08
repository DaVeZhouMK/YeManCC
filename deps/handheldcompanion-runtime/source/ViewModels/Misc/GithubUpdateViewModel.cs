using HandheldCompanion.Managers;
using System.Windows;

namespace HandheldCompanion.ViewModels.Misc
{
    public enum UpdateFileState
    {
        Available,
        Downloading,
        Downloaded,
        Failed
    }

    public class GithubUpdateViewModel : BaseViewModel
    {
        public UpdateFile UpdateFile { get; }

        public string Filename => UpdateFile.filename;
        public string FilesizeMB => UpdateFile.filesize > 0
            ? $"{UpdateFile.filesize / 1_048_576.0:F1} MB"
            : string.Empty;

        private UpdateFileState _state = UpdateFileState.Available;
        public UpdateFileState State
        {
            get => _state;
            private set
            {
                if (_state != value)
                {
                    _state = value;
                    OnPropertyChanged(nameof(State));
                    OnPropertyChanged(nameof(DownloadButtonVisibility));
                    OnPropertyChanged(nameof(ProgressVisibility));
                    OnPropertyChanged(nameof(InstallButtonVisibility));
                }
            }
        }

        private int _progress;
        public int Progress
        {
            get => _progress;
            private set
            {
                if (_progress != value)
                {
                    _progress = value;
                    OnPropertyChanged(nameof(Progress));
                    OnPropertyChanged(nameof(ProgressText));
                }
            }
        }

        public string ProgressText => $"{Properties.Resources.SettingsPage_DownloadingPercentage}{Progress} %";

        public Visibility DownloadButtonVisibility => State == UpdateFileState.Available ? Visibility.Visible : Visibility.Collapsed;
        public Visibility ProgressVisibility => State == UpdateFileState.Downloading ? Visibility.Visible : Visibility.Collapsed;
        public Visibility InstallButtonVisibility => State == UpdateFileState.Downloaded ? Visibility.Visible : Visibility.Collapsed;

        public DelegateCommand DownloadCommand { get; }
        public DelegateCommand InstallCommand { get; }

        public delegate void InstallFailedEventHandler(GithubUpdateViewModel vm);
        public event InstallFailedEventHandler? OnInstallFailed;

        public delegate void InstalledEventHandler(GithubUpdateViewModel vm);
        public event InstalledEventHandler? OnInstalled;

        public GithubUpdateViewModel(UpdateFile updateFile)
        {
            UpdateFile = updateFile;
            DownloadCommand = new DelegateCommand(async () => await UpdateManager.DownloadUpdateFile(UpdateFile));
            InstallCommand = new DelegateCommand(() =>
            {
                if (!UpdateManager.InstallUpdate(UpdateFile))
                    OnInstallFailed?.Invoke(this);
                else if (UpdateFile.isGameControllerDb)
                    OnInstalled?.Invoke(this);
            });
        }

        public void OnDownloadStarted()
        {
            State = UpdateFileState.Downloading;
        }

        public void OnProgressChanged(int percent)
        {
            Progress = percent;
        }

        public void OnDownloadCompleted()
        {
            State = UpdateFileState.Downloaded;
        }

        public void OnFailed()
        {
            State = UpdateFileState.Failed;
        }

        public void ResetToAvailable()
        {
            State = UpdateFileState.Available;
            Progress = 0;
        }
    }
}
