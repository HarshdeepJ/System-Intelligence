using System;
using System.Collections.Generic;

namespace SystemIntelligence.PixelMini;

internal sealed class PixelDecision
{
    public PixelDecision(ExpressionKind expression, string subtitle, string? message)
    {
        Expression = expression;
        Subtitle = subtitle;
        Message = message;
    }

    public ExpressionKind Expression { get; }
    public string Subtitle { get; }
    public string? Message { get; }
}

// The one place system state turns into a single dominant PixelState, per
// UI_HANDOFF.md's "next milestone". Deliberately rule-based, not an LLM
// call: state-to-language mapping stays deterministic until real cases show
// rules are inadequate.
//
// Hysteresis: each condition below must stay true for its own dwell time
// before it can win (so a one-second CPU blip never changes Pixel's face),
// and once nothing is active any more, Pixel only settles back to Calm
// after staying healthy for RecoveryDwell -- otherwise it would flicker
// between states every poll near a threshold's edge.
internal sealed class PixelStateArbiter
{
    private static readonly TimeSpan MildDwell = TimeSpan.FromSeconds(12);
    private static readonly TimeSpan AttentionDwell = TimeSpan.FromSeconds(5);
    private static readonly TimeSpan RecoveryDwell = TimeSpan.FromSeconds(20);
    private static readonly TimeSpan MessageCooldown = TimeSpan.FromMinutes(10);

    private readonly Dictionary<string, DateTime?> _since = new(StringComparer.Ordinal);
    private readonly Dictionary<string, DateTime> _lastMessageAt = new(StringComparer.Ordinal);
    private ExpressionKind _current = ExpressionKind.Calm;
    private DateTime? _healthySince;

    public PixelDecision Evaluate(TelemetrySnapshot s, DateTime now)
    {
        var specs = BuildSpecs(s);

        ConditionSpec? winner = null;
        var anyActive = false;

        foreach (var spec in specs)
        {
            if (!spec.Active)
            {
                _since[spec.Key] = null;
                continue;
            }

            anyActive = true;
            if (_since.TryGetValue(spec.Key, out var since) && since.HasValue)
            {
                // keep the existing start time
            }
            else
            {
                _since[spec.Key] = now;
            }

            var sustainedFor = now - _since[spec.Key]!.Value;
            if (winner is null && sustainedFor >= spec.Dwell)
            {
                winner = spec;
            }
        }

        string subtitle;
        string? messageKey = null;
        string? messageText = null;

        if (winner is not null)
        {
            _current = winner.Expression;
            subtitle = winner.Message;
            messageKey = winner.Key;
            messageText = winner.Message;
            _healthySince = null;
        }
        else if (!anyActive)
        {
            _healthySince ??= now;
            if (now - _healthySince.Value >= RecoveryDwell)
            {
                _current = ExpressionKind.Calm;
            }
            subtitle = SubtitleFor(_current);
        }
        else
        {
            // Something is active but hasn't cleared its own dwell time yet
            // -- hold the current face rather than jump early.
            _healthySince = null;
            subtitle = SubtitleFor(_current);
        }

        string? spokenMessage = null;
        if (messageKey is not null)
        {
            var cooldownElapsed = !_lastMessageAt.TryGetValue(messageKey, out var lastAt) ||
                                   now - lastAt >= MessageCooldown;
            if (cooldownElapsed)
            {
                spokenMessage = messageText;
                _lastMessageAt[messageKey] = now;
            }
        }

        return new PixelDecision(_current, subtitle, spokenMessage);
    }

    private static List<ConditionSpec> BuildSpecs(TelemetrySnapshot s)
    {
        var diskFreeFraction = s.DiskTotalBytes is > 0 && s.DiskFreeBytes.HasValue
            ? (double?)s.DiskFreeBytes.Value / s.DiskTotalBytes.Value
            : null;

        // Ordered most severe first: the first one that has cleared its
        // dwell time wins, so a critical battery reading always outranks a
        // merely-busy CPU even if both are sustained.
        return new List<ConditionSpec>
        {
            new(
                "battery-critical",
                s.BatteryPresent && !s.OnAcPower && s.BatteryPercent is < 10,
                ExpressionKind.Worried,
                AttentionDwell,
                "Could you plug me in? I'm almost out."),
            new(
                "thermal-critical",
                s.ThermalAvailable && s.ThermalCelsius is >= 90,
                ExpressionKind.Worried,
                AttentionDwell,
                s.ThermalCelsius.HasValue
                    ? $"I'm at {s.ThermalCelsius.Value:F0}°C -- that's too hot. Could we let me cool down?"
                    : "Could we let me cool down for a bit?"),
            new(
                "disk-critical",
                diskFreeFraction is < 0.05,
                ExpressionKind.Worried,
                AttentionDwell,
                "I'm running out of room in here."),
            new(
                "network-lost",
                s.NetworkUp == false,
                ExpressionKind.Offline,
                AttentionDwell,
                "Did the internet disappear?"),
            new(
                "thermal-warm",
                s.ThermalAvailable && s.ThermalCelsius is >= 80,
                ExpressionKind.Warm,
                MildDwell,
                "Um… I'm getting a little warm."),
            new(
                "battery-low",
                s.BatteryPresent && !s.OnAcPower && s.BatteryPercent is < 20,
                ExpressionKind.Sleepy,
                MildDwell,
                "I'm getting sleepy…"),
            new(
                "charging",
                s.Charging,
                ExpressionKind.Happy,
                TimeSpan.Zero,
                "Ahh, thank you."),
            new(
                "cpu-high",
                s.CpuPercent is >= 85,
                ExpressionKind.Working,
                MildDwell,
                "I'm juggling quite a lot right now."),
            new(
                "memory-high",
                s.MemoryLoadPercent is >= 90,
                ExpressionKind.Working,
                MildDwell,
                "I'm trying to remember a lot right now.")
        };
    }

    private static string SubtitleFor(ExpressionKind expression) => expression switch
    {
        ExpressionKind.Working => "I'm working pretty hard right now.",
        ExpressionKind.Warm => "Um… I'm getting a little warm.",
        ExpressionKind.Worried => "Something feels a little off.",
        ExpressionKind.Sleepy => "I'm getting sleepy…",
        ExpressionKind.Happy => "Ahh, thank you.",
        ExpressionKind.Offline => "Did the internet disappear?",
        _ => "Everything feels good."
    };

    private sealed class ConditionSpec
    {
        public ConditionSpec(string key, bool active, ExpressionKind expression, TimeSpan dwell, string message)
        {
            Key = key;
            Active = active;
            Expression = expression;
            Dwell = dwell;
            Message = message;
        }

        public string Key { get; }
        public bool Active { get; }
        public ExpressionKind Expression { get; }
        public TimeSpan Dwell { get; }
        public string Message { get; }
    }
}
