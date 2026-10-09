using System;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Threading;

namespace HandheldCompanion.Views
{
    /// <summary>
    /// Interaction logic for SplashScreen.xaml
    /// </summary>
    public partial class SplashScreen : Window
    {
        public SplashScreen()
        {
            InitializeComponent();
        }

        protected override void OnSourceInitialized(EventArgs e)
        {
            base.OnSourceInitialized(e);
            // Clear WS_MAXIMIZE so Windows does not show this window maximized when first
            // displayed. This has no ShowWindow side-effect unlike SetWindowPlacement.
            WinAPI.ClearMaximizeStyle(new WindowInteropHelper(this).Handle);
        }

        public void SetStatus(string status)
        {
            if (FindName("StatusTextBlock") is System.Windows.Controls.TextBlock statusTextBlock)
                statusTextBlock.Text = status;
        }
    }

    /// <summary>
    /// Hosts a <see cref="SplashScreen"/> on the main UI thread.
    /// <see cref="Show"/> and <see cref="Close"/> must be called from the UI thread.
    /// <see cref="SetStatus"/> is safe to call from any thread.
    /// </summary>
    public sealed class SplashScreenHost : IDisposable
    {
        private SplashScreen? _window;

        /// <summary>Must be called from the UI thread.</summary>
        public void Show()
        {
            if (_window is not null)
                return;

            _window = new SplashScreen();
            _window.Show();
        }

        /// <summary>
        /// Updates the status text. Safe to call from any thread.
        /// </summary>
        public void SetStatus(string status)
        {
            if (_window is null || string.IsNullOrWhiteSpace(status))
                return;

            var dispatcher = _window.Dispatcher;
            if (dispatcher.CheckAccess())
            {
                _window.SetStatus(status);
            }
            else
            {
                dispatcher.InvokeAsync(() => _window?.SetStatus(status), DispatcherPriority.Normal);
            }
        }

        /// <summary>Must be called from the UI thread.</summary>
        public void Close()
        {
            if (_window is null)
                return;

            _window.Close();
            _window = null;
        }

        public void Dispose() => Close();
    }
}
