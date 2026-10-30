using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using MidiPedal.Models;

namespace MidiPedal.ViewModels;

public partial class PageViewModel : ViewModelBase
{
    public PageViewModel(int index)
    {
        Index = index;
        _name = $"Page {index + 1}";
        for (var i = 0; i < Limits.Switches; i++) Switches.Add(new SwitchViewModel(i));
    }

    [ObservableProperty] [NotifyPropertyChangedFor(nameof(Title))] private int _index;
    [ObservableProperty] [NotifyPropertyChangedFor(nameof(Title))] private string _name;
    [ObservableProperty] private bool _isActive;    // the pedal's current page
    [ObservableProperty] private bool _isSelected;  // the page open in the editor

    public ObservableCollection<SwitchViewModel> Switches { get; } = new();

    public string Title => $"{Index + 1} · {Name}";

    public void Load(PageConfig p)
    {
        Name = p.Name;
        for (var i = 0; i < Limits.Switches; i++)
        {
            if (i < p.Switches.Count) Switches[i].Load(p.Switches[i]);
        }
    }

    public PageConfig ToConfig()
    {
        var p = new PageConfig { Name = SwitchViewModel.Truncate(Name, Limits.MaxNameLength) };
        foreach (var s in Switches) p.Switches.Add(s.ToConfig());
        return p;
    }
}
