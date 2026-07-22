using Avalonia.Controls;
using Avalonia.Threading;

namespace SwimSid.Player.Views;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();

        // Keep the log scrolled to the newest line.
        LogBox.PropertyChanged += (_, e) =>
        {
            if (e.Property == TextBox.TextProperty)
                Dispatcher.UIThread.Post(() => LogBox.CaretIndex = LogBox.Text?.Length ?? 0);
        };
    }
}
