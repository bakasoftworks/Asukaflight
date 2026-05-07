using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using Gx12.Launcher.Wpf.Services;
using Gx12.Launcher.Wpf.ViewModels;

namespace Gx12.Launcher.Wpf;

public partial class MainWindow : Window
{
    public MainWindow()
        : this(CompositionRoot.CreateMainViewModel())
    {
    }

    public MainWindow(MainViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
    }

    public void SelectWorkspaceTab(string header)
    {
        foreach (var item in WorkspaceTabs.Items)
        {
            if (item is TabItem tabItem &&
                string.Equals(tabItem.Header?.ToString(), header, System.StringComparison.OrdinalIgnoreCase))
            {
                WorkspaceTabs.SelectedItem = tabItem;
                return;
            }
        }
    }

    protected override void OnClosing(CancelEventArgs e)
    {
        if (DataContext is MainViewModel viewModel)
        {
            viewModel.StopRuntimeOnExit();
        }

        base.OnClosing(e);
    }
}
