using Android.App;
using Android.Content.PM;
using Android.OS;
using Avalonia.Android;

namespace MidiPedal.Android;

[Activity(
    Label = "MidiPedal",
    Theme = "@style/MyTheme.NoActionBar",
    Icon = "@drawable/icon",
    MainLauncher = true,
    ConfigurationChanges = ConfigChanges.Orientation | ConfigChanges.ScreenSize | ConfigChanges.UiMode)]
public class MainActivity : AvaloniaMainActivity
{
    private const int BlePermissionRequest = 1001;

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        RequestBlePermissions();
    }

    private void RequestBlePermissions()
    {
        string[] wanted = Build.VERSION.SdkInt >= BuildVersionCodes.S
            ? new[] { global::Android.Manifest.Permission.BluetoothScan, global::Android.Manifest.Permission.BluetoothConnect }
            : new[] { global::Android.Manifest.Permission.AccessFineLocation };

        var missing = System.Array.FindAll(wanted, p => CheckSelfPermission(p) != Permission.Granted);
        if (missing.Length > 0) RequestPermissions(missing, BlePermissionRequest);
    }
}
