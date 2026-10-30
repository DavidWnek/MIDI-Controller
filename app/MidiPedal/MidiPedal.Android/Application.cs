using Android.App;
using Android.Runtime;
using Avalonia;
using Avalonia.Android;
using MidiPedal.Services;

namespace MidiPedal.Android
{
    [Application]
    public class Application : AvaloniaAndroidApplication<App>
    {
        protected Application(nint javaReference, JniHandleOwnership transfer) : base(javaReference, transfer)
        {
        }

        protected override AppBuilder CustomizeAppBuilder(AppBuilder builder)
        {
            // Android talks to the pedal over BLE (permissions are requested in MainActivity).
            if (!LinkProviders.All.Exists(p => p is BleLinkProvider))
                LinkProviders.All.Add(new BleLinkProvider());
            return base.CustomizeAppBuilder(builder)
            .WithInterFont();
        }
    }
}
