using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;

namespace SystemIntelligence.PixelMini;

// Talks to the existing sysintel.exe CLI over its JSON commands rather than
// linking against core/ directly -- the UI stays a thin, replaceable client
// of a backend that already works standalone (see UI_HANDOFF.md).
internal sealed class TelemetryClient
{
    public string? ExePath { get; }

    public TelemetryClient()
    {
        ExePath = LocateSysIntelExe();
    }

    private static string? LocateSysIntelExe()
    {
        var overridePath = Environment.GetEnvironmentVariable("SYSINTEL_EXE");
        if (!string.IsNullOrEmpty(overridePath) && File.Exists(overridePath))
        {
            return overridePath;
        }

        // PixelMini.exe runs from ui/PixelMini/bin (csc.exe fallback build)
        // or ui/PixelMini/bin/<config>/<tfm> (dotnet SDK build) -- walk up
        // looking for <repo-root>/build/sysintel.exe rather than assuming a
        // fixed depth.
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (var depth = 0; depth < 8 && dir is not null; depth++, dir = dir.Parent)
        {
            var candidate = Path.Combine(dir.FullName, "build", "sysintel.exe");
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }
        return null;
    }

    public async Task<JsonValue?> RunJsonAsync(string arguments, TimeSpan timeout)
    {
        if (ExePath is null)
        {
            return null;
        }

        try
        {
            var startInfo = new ProcessStartInfo
            {
                FileName = ExePath,
                Arguments = arguments,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true
            };

            using var process = Process.Start(startInfo);
            if (process is null)
            {
                return null;
            }

            var outputTask = process.StandardOutput.ReadToEndAsync();
            var exited = await Task.Run(() => process.WaitForExit((int)timeout.TotalMilliseconds)).ConfigureAwait(true);
            if (!exited)
            {
                TryKill(process);
                return null;
            }

            var output = await outputTask.ConfigureAwait(true);
            if (string.IsNullOrWhiteSpace(output))
            {
                return null;
            }
            return JsonValue.Parse(output.Trim());
        }
        catch
        {
            // Missing exe, JSON we didn't expect, a process that refused to
            // start -- all collapse to "no reading this tick", never to a
            // fabricated value.
            return null;
        }
    }

    private static void TryKill(Process process)
    {
        try
        {
            process.Kill();
        }
        catch
        {
            // Best-effort only; the process may have exited between the
            // WaitForExit timeout and this call.
        }
    }
}

// A live picture of the machine, assembled from sysintel.exe's JSON output.
// Every field a real machine might not have (no battery, thermal zones the
// OEM doesn't expose, no network adapters) stays nullable so "we don't know"
// is never rendered as zero.
internal sealed class TelemetrySnapshot
{
    public bool Available;
    public double? CpuPercent;
    public double? MemoryLoadPercent;
    public bool BatteryPresent;
    public double? BatteryPercent;
    public bool OnAcPower;
    public bool Charging;
    public bool ThermalAvailable;
    public double? ThermalCelsius;
    public ulong? DiskTotalBytes;
    public ulong? DiskFreeBytes;
    public bool? NetworkUp;

    public static TelemetrySnapshot FromStatusJson(JsonValue root)
    {
        var snapshot = new TelemetrySnapshot { Available = true };

        var battery = root["battery"];
        if (battery["present"].AsBool())
        {
            snapshot.BatteryPresent = true;
            snapshot.BatteryPercent = battery["charge_percent"].AsDouble();
            snapshot.OnAcPower = battery["on_ac_power"].AsBool();
            snapshot.Charging = battery["charging"].AsBool();
        }

        snapshot.CpuPercent = root["cpu"]["utilization_percent"].AsDouble();
        snapshot.MemoryLoadPercent = root["memory"]["load_percent"].AsDouble();

        double? hottestZone = null;
        foreach (var zone in root["thermal"]["zones"].Items)
        {
            var reading = zone["temperature_celsius"].ReadingDoubleOrNull();
            if (reading.HasValue && (!hottestZone.HasValue || reading.Value > hottestZone.Value))
            {
                hottestZone = reading.Value;
            }
        }
        snapshot.ThermalAvailable = hottestZone.HasValue;
        snapshot.ThermalCelsius = hottestZone;

        var diskSpace = root["disk_space"];
        if (!diskSpace.IsNull)
        {
            snapshot.DiskTotalBytes = diskSpace["total_bytes"].AsUInt64();
            snapshot.DiskFreeBytes = diskSpace["free_bytes"].AsUInt64();
        }

        return snapshot;
    }

    // sysintel network --json returns a bare array of adapters; "no physical
    // adapters at all" is left as unknown (null) rather than asserted as
    // offline, since that shape is more likely a VM/odd hardware quirk than
    // a real connectivity loss.
    public static bool? NetworkUpFromJson(JsonValue adaptersArray)
    {
        if (adaptersArray.Kind != JsonKind.Array || adaptersArray.Items.Count == 0)
        {
            return null;
        }
        foreach (var adapter in adaptersArray.Items)
        {
            if (adapter["operational"].AsBool())
            {
                return true;
            }
        }
        return false;
    }
}
