using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using MidiPedal.ViewModels;

namespace MidiPedal.Views;

public partial class MainView : UserControl
{
    public MainView()
    {
        InitializeComponent();
    }

    private MainViewModel? Vm => DataContext as MainViewModel;

    private IStorageProvider? Storage => TopLevel.GetTopLevel(this)?.StorageProvider;

    private static readonly FilePickerFileType JsonType = new("MidiPedal configuration") { Patterns = new[] { "*.json" } };
    private static readonly FilePickerFileType BinType = new("Firmware image") { Patterns = new[] { "*.bin" } };

    private async void OnExportClick(object? sender, RoutedEventArgs e)
    {
        if (Vm == null || Storage == null) return;
        try
        {
            var file = await Storage.SaveFilePickerAsync(new FilePickerSaveOptions
            {
                Title = "Export configuration",
                SuggestedFileName = $"{Vm.DeviceName}-config.json",
                DefaultExtension = "json",
                FileTypeChoices = new[] { JsonType },
            });
            if (file == null) return;
            await using var stream = await file.OpenWriteAsync();
            var bytes = Encoding.UTF8.GetBytes(Vm.ExportJson());
            await stream.WriteAsync(bytes);
            Vm.StatusText = $"Exported to {file.Name}";
        }
        catch (Exception ex)
        {
            Vm.StatusText = "Export failed: " + ex.Message;
        }
    }

    private async void OnImportClick(object? sender, RoutedEventArgs e)
    {
        if (Vm == null || Storage == null) return;
        try
        {
            var files = await Storage.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = "Import configuration",
                AllowMultiple = false,
                FileTypeFilter = new[] { JsonType },
            });
            if (files.Count == 0) return;
            var json = await ReadAllTextAsync(files[0]);
            if (!Vm.ImportJson(json, out var error)) Vm.StatusText = "Import failed: " + error;
        }
        catch (Exception ex)
        {
            Vm.StatusText = "Import failed: " + ex.Message;
        }
    }

    private async void OnFirmwareClick(object? sender, RoutedEventArgs e)
    {
        if (Vm == null || Storage == null) return;
        try
        {
            var files = await Storage.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = "Choose a MidiPedal firmware .bin",
                AllowMultiple = false,
                FileTypeFilter = new[] { BinType },
            });
            if (files.Count == 0) return;
            await using var stream = await files[0].OpenReadAsync();
            using var ms = new MemoryStream();
            await stream.CopyToAsync(ms);
            await Vm.UpdateFirmwareAsync(ms.ToArray());
        }
        catch (Exception ex)
        {
            Vm.StatusText = "Firmware update failed: " + ex.Message;
        }
    }

    private static async Task<string> ReadAllTextAsync(IStorageFile file)
    {
        await using var stream = await file.OpenReadAsync();
        using var reader = new StreamReader(stream, Encoding.UTF8);
        return await reader.ReadToEndAsync();
    }
}
