using CommunityToolkit.Mvvm.ComponentModel;
using MidiPedal.Models;

namespace MidiPedal.ViewModels;

/// <summary>One entry of a macro / long-press / double-tap action list.</summary>
public partial class ActionViewModel : ViewModelBase
{
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Summary), nameof(IsMidi), nameof(IsPage), nameof(IsPreset), nameof(HasValue))]
    private string _type = ActionTypes.ControlChange;

    [ObservableProperty] [NotifyPropertyChangedFor(nameof(Summary))] private int _channel;
    [ObservableProperty] [NotifyPropertyChangedFor(nameof(Summary), nameof(PresetSlot))] private int _number;
    [ObservableProperty] [NotifyPropertyChangedFor(nameof(Summary))] private int _value = 127;

    public bool IsMidi => Type is ActionTypes.ControlChange or ActionTypes.NoteOn or ActionTypes.NoteOff or ActionTypes.ProgramChange;
    public bool IsPage => Type == ActionTypes.Page;
    public bool IsPreset => Type == ActionTypes.Preset;
    public bool HasValue => Type is ActionTypes.ControlChange or ActionTypes.NoteOn or ActionTypes.NoteOff;

    /// <summary>1-based preset slot shown in the UI; stored 0-based in Number.</summary>
    public int PresetSlot
    {
        get => Number + 1;
        set { if (value - 1 != Number) Number = value - 1; }
    }

    public string Summary
    {
        get
        {
            var ch = Channel == 0 ? "global ch" : $"ch {Channel}";
            return Type switch
            {
                ActionTypes.NoteOn => $"Note on {Number} vel {Value} · {ch}",
                ActionTypes.NoteOff => $"Note off {Number} · {ch}",
                ActionTypes.ProgramChange => $"PC {Number} · {ch}",
                ActionTypes.Page => Number switch { PageTargets.Previous => "Previous page", PageTargets.Next => "Next page", _ => $"Page {Number + 1}" },
                ActionTypes.Preset => $"Load preset {Number + 1}",
                _ => $"CC {Number} = {Value} · {ch}",
            };
        }
    }

    partial void OnTypeChanged(string value)
    {
        // Keep the number in a sensible range when the meaning of Number changes.
        if (value == ActionTypes.Page && Number > Limits.MaxPages - 1) Number = PageTargets.Next;
        if (value == ActionTypes.Preset && (Number < 0 || Number > Limits.MaxPresets - 1)) Number = 0;
        if (IsMidi && Number < 0) Number = 0;
    }

    public void Load(MidiAction a)
    {
        Type = a.Type;
        Channel = a.Channel;
        Number = a.Number;
        Value = a.Value;
    }

    public MidiAction ToModel() => new() { Type = Type, Channel = Channel, Number = Number, Value = Value };
}
