using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using System.Windows.Threading;

namespace SystemIntelligence.PixelMini;

internal enum ExpressionKind
{
    Calm,
    Curious,
    Working,
    Warm,
    Worried,
    Sleepy,
    Happy,
    Offline
}

internal sealed class PixelWindow : Window
{
    private const double CompactWidth = 260;
    private const double CompactHeight = 72;
    private const double BubbleWidth = 320;
    private const double BubbleHeight = 162;
    private const double ExpandedWidth = 540;
    private const double ExpandedHeight = 112;
    private const double ChatWidth = 560;
    private const double ChatHeight = 330;
    private static readonly TimeSpan PeekAfter = TimeSpan.FromSeconds(10);

    private static readonly Color Ink = Color.FromRgb(35, 47, 82);
    private static readonly Color Blue = Color.FromRgb(100, 145, 255);
    private static readonly Color PaleBlue = Color.FromRgb(235, 243, 255);

    private readonly Grid _root = new();
    private readonly Border _bubble;
    private readonly TextBlock _bubbleText;
    private readonly Border _healthCard;
    private readonly FrameworkElement _character;
    private readonly ScaleTransform _breathScale = new(1, 1);
    private readonly RotateTransform _idleSway = new(0);
    private readonly RotateTransform _cursorTilt = new(0);
    private readonly TranslateTransform _perkOffset = new(0, 0);
    private readonly DispatcherTimer _cursorTimer;
    private readonly DispatcherTimer _bubbleTimer;
    private readonly DispatcherTimer _blinkTimer;
    private readonly Dictionary<string, PreviewState> _states;
    private readonly Random _random = new();
    private DateTime? _cornerEntered;
    private readonly bool _layoutSmokeTest;
    private GradientStop _haloA = null!;
    private GradientStop _haloB = null!;
    private GradientStop _haloCoreStop = null!;
    private GradientStop _haloC = null!;
    private GradientStop _haloD = null!;
    private Border _haloBloom = null!;
    private Border _haloCore = null!;
    private Border _haloWisp = null!;
    private readonly TranslateTransform _haloWispOffset = new();
    private EyeVisual _leftEye = null!;
    private EyeVisual _rightEye = null!;
    private Border _leftBrow = null!;
    private Border _rightBrow = null!;
    private Border _mouth = null!;
    private Border _leftCheek = null!;
    private Border _rightCheek = null!;
    private Border _sweatDrop = null!;
    private DateTime _lastNearby = DateTime.UtcNow;
    private ExpressionKind _expression = ExpressionKind.Calm;
    private int _positionAnimationRevision;
    private bool _cardOpen;
    private bool _isPeeking;
    private readonly TelemetryClient _telemetry = new();
    private readonly PixelStateArbiter _arbiter = new();
    private TelemetrySnapshot _snapshot = new();
    private DispatcherTimer? _telemetryTimer;
    private DispatcherTimer? _networkTimer;
    private bool _telemetryBusy;
    private bool _networkBusy;
    private bool _previewActive;
    private readonly PixelChatClient _chat;
    private readonly string _dbPath;
    private readonly List<ChatTurn> _chatHistory = new();
    private readonly Border _chatIcon;
    private readonly Border _chatPanel;
    private readonly StackPanel _chatMessages;
    private readonly ScrollViewer _chatScroll;
    private readonly TextBox _chatInput;
    private readonly Border _chatComposer;
    private readonly Border _chatResultSurface;
    private readonly TextBlock _chatHint;
    private readonly DispatcherTimer _chatIdleTimer;
    private bool _chatOpen;
    private bool _chatBusy;
    private readonly VoiceAssistant _voice = new();
    private bool _voiceEnabled;
    private TextBlock _cardSubtitleText = null!;
    private HealthMetricRow _cpuMetric = null!;
    private HealthMetricRow _memoryMetric = null!;
    private HealthMetricRow _tempMetric = null!;
    private HealthMetricRow _batteryMetric = null!;
    private HealthMetricRow _diskMetric = null!;

    public PixelWindow(bool layoutSmokeTest = false, bool visualCheck = false)
    {
        _layoutSmokeTest = layoutSmokeTest || visualCheck;
        Title = "System Intelligence";
        Width = CompactWidth;
        Height = CompactHeight;
        WindowStyle = WindowStyle.None;
        AllowsTransparency = true;
        Background = Brushes.Transparent;
        Topmost = true;
        ShowInTaskbar = false;
        ResizeMode = ResizeMode.NoResize;
        Focusable = false;
        UseLayoutRounding = true;
        RenderOptions.SetBitmapScalingMode(this, BitmapScalingMode.HighQuality);

        _states = BuildStates();
        _bubbleText = new TextBlock
        {
            FontFamily = new FontFamily("Segoe UI Variable Text, Segoe UI"),
            FontSize = 15,
            FontWeight = FontWeights.SemiBold,
            Foreground = new SolidColorBrush(Ink),
            TextWrapping = TextWrapping.Wrap,
            LineHeight = 21
        };
        _bubble = BuildBubble(_bubbleText);
        _healthCard = BuildHealthCard();
        _character = BuildCharacter();

        var repoRoot = _telemetry.ExePath is not null
            ? Directory.GetParent(System.IO.Path.GetDirectoryName(_telemetry.ExePath)!)?.FullName
            : null;
        _chat = new PixelChatClient(repoRoot, _telemetry.ExePath);
        _dbPath = repoRoot is not null ? System.IO.Path.Combine(repoRoot, "sysintel.db") : "sysintel.db";
        (_chatPanel, _chatMessages, _chatScroll, _chatInput, _chatComposer, _chatResultSurface, _chatHint) = BuildChatPanel();
        _chatIcon = BuildChatIcon();
        _chatIdleTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(5) };
        _chatIdleTimer.Tick += (_, _) => HideIdleChatComposer();

        _root.Children.Add(_bubble);
        _root.Children.Add(_healthCard);
        _root.Children.Add(_chatPanel);
        _root.Children.Add(_character);
        _root.Children.Add(_chatIcon);
        Content = _root;

        _character.MouseLeftButtonUp += (_, _) => ToggleHealthCard();
        _character.ContextMenu = BuildContextMenu();
        _character.ToolTip = "Click for a quick check-in";
        PreviewKeyDown += (_, e) =>
        {
            if (e.Key == Key.Escape && _chatOpen)
            {
                e.Handled = true;
                CloseChatPanel();
            }
        };

        Loaded += async (_, _) =>
        {
            _isPeeking = !layoutSmokeTest && !visualCheck;
            DockToCorner();
            StartBreathing();
            if (layoutSmokeTest)
            {
                await RunLayoutSmokeTest();
            }
            if (visualCheck)
            {
                _blinkTimer?.Stop();
                UpdateLayout();
                var render = new RenderTargetBitmap(780, 216, 288, 288, PixelFormats.Pbgra32);
                render.Render(_root);
                var encoder = new PngBitmapEncoder();
                encoder.Frames.Add(BitmapFrame.Create(render));
                using (var stream = File.Create(System.IO.Path.Combine(AppContext.BaseDirectory, "visual-check.png")))
                    encoder.Save(stream);
                Close();
            }
        };
        SystemParameters.StaticPropertyChanged += (_, args) =>
        {
            if (args.PropertyName == nameof(SystemParameters.WorkArea))
            {
                DockToCorner();
            }
        };

        _cursorTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(16) };
        _cursorTimer.Tick += (_, _) => FollowCursor();
        _cursorTimer.Start();

        _bubbleTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(5) };
        _bubbleTimer.Tick += (_, _) => HideBubble();

        _blinkTimer = new DispatcherTimer();
        _blinkTimer.Tick += (_, _) => Blink();
        ScheduleNextBlink();

        if (!_layoutSmokeTest && _telemetry.ExePath is not null)
        {
            _telemetryTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(5) };
            _telemetryTimer.Tick += async (_, _) => await PollTelemetryAsync();
            _telemetryTimer.Start();
            _ = PollTelemetryAsync();

            _networkTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(20) };
            _networkTimer.Tick += async (_, _) => await PollNetworkAsync();
            _networkTimer.Start();
            _ = PollNetworkAsync();
        }

        _voice.WakeWordDetected += OnVoiceWakeWordDetected;
        _voice.TranscriptionChanged += OnVoiceTranscriptionChanged;
        _voice.QuestionCaptured += OnVoiceQuestionCaptured;
        _voice.ListenTimedOut += OnVoiceListenTimedOut;

        Closed += (_, _) =>
        {
            _cursorTimer.Stop();
            _blinkTimer.Stop();
            _bubbleTimer.Stop();
            _chatIdleTimer.Stop();
            _telemetryTimer?.Stop();
            _networkTimer?.Stop();
            _voice.Stop();
        };
    }

    // These fire from VoiceAssistant's recognition engine, not necessarily
    // on the UI thread -- BeginInvoke marshals back before touching any
    // WPF element, same reasoning as the chat/telemetry async handlers
    // already need for their own background work.
    private void OnVoiceWakeWordDetected() => Dispatcher.BeginInvoke(new Action(() =>
    {
        if (_isPeeking)
        {
            WakeFromPeek();
        }
        if (!_chatOpen)
        {
            OpenChatPanel();
        }
        ApplyExpression(ExpressionKind.Curious, true);
        _chatInput.Text = string.Empty;
        _chatHint.Text = "Listening…";
        _chatHint.Visibility = Visibility.Visible;
    }));

    private void OnVoiceTranscriptionChanged(string text) => Dispatcher.BeginInvoke(new Action(() =>
    {
        if (!_chatOpen || _chatBusy)
        {
            return;
        }
        _chatInput.Text = text;
        _chatInput.CaretIndex = text.Length;
    }));

    private void OnVoiceQuestionCaptured(string question) => Dispatcher.BeginInvoke(new Action(() =>
    {
        _chatInput.Text = question;
        _ = SendChatMessageAsync(question, speakReply: true);
    }));

    private void OnVoiceListenTimedOut() => Dispatcher.BeginInvoke(new Action(() =>
    {
        _chatHint.Text = "Ask your computer…";
        _chatHint.Visibility = _chatInput.Text.Length == 0 ? Visibility.Visible : Visibility.Collapsed;
        if (!_previewActive)
        {
            ApplyExpression(ExpressionKind.Calm, true);
        }
        RestartChatIdleTimer();
    }));

    private void ToggleVoice(MenuItem menuItem)
    {
        if (_voiceEnabled)
        {
            _voice.Stop();
            _voiceEnabled = false;
            menuItem.Header = "Enable voice (“Hey Pixel”)";
            return;
        }

        if (_voice.Start())
        {
            _voiceEnabled = true;
            menuItem.Header = "Disable voice";
        }
        else
        {
            ShowMessage(
                "I can't hear anything -- no microphone available, or Windows Speech Recognition isn't set up on this machine.",
                ExpressionKind.Worried);
        }
    }

    private async Task PollTelemetryAsync()
    {
        if (_telemetryBusy)
        {
            return;
        }
        _telemetryBusy = true;
        try
        {
            var root = await _telemetry.RunJsonAsync("status --json", TimeSpan.FromSeconds(4));
            if (root is null)
            {
                return; // transient failure -- keep showing the last known reading
            }

            var fresh = TelemetrySnapshot.FromStatusJson(root);
            fresh.NetworkUp = _snapshot.NetworkUp;
            _snapshot = fresh;

            var decision = _arbiter.Evaluate(_snapshot, DateTime.UtcNow);
            UpdateHealthCard(_snapshot, decision.Subtitle);
            if (!_previewActive)
            {
                ApplyExpression(decision.Expression, true);
                if (decision.Message is not null && !_chatOpen)
                {
                    ShowMessage(decision.Message, decision.Expression);
                }
            }
        }
        finally
        {
            _telemetryBusy = false;
        }
    }

    private async Task PollNetworkAsync()
    {
        if (_networkBusy)
        {
            return;
        }
        _networkBusy = true;
        try
        {
            var root = await _telemetry.RunJsonAsync("network --json", TimeSpan.FromSeconds(4));
            if (root is null)
            {
                return;
            }
            _snapshot.NetworkUp = TelemetrySnapshot.NetworkUpFromJson(root);
        }
        finally
        {
            _networkBusy = false;
        }
    }

    private void UpdateHealthCard(TelemetrySnapshot s, string subtitle)
    {
        _cardSubtitleText.Text = subtitle;

        if (s.CpuPercent.HasValue)
        {
            _cpuMetric.Update($"{s.CpuPercent.Value:F0}%", s.CpuPercent.Value < 85);
        }
        else
        {
            _cpuMetric.UpdateUnknown();
        }

        if (s.MemoryLoadPercent.HasValue)
        {
            _memoryMetric.Update($"{s.MemoryLoadPercent.Value:F0}%", s.MemoryLoadPercent.Value < 90);
        }
        else
        {
            _memoryMetric.UpdateUnknown();
        }

        if (s.ThermalAvailable && s.ThermalCelsius.HasValue)
        {
            _tempMetric.Update($"{s.ThermalCelsius.Value:F0}°C", s.ThermalCelsius.Value < 80);
        }
        else
        {
            _tempMetric.UpdateUnknown("n/a");
        }

        if (s.BatteryPresent && s.BatteryPercent.HasValue)
        {
            _batteryMetric.Update($"{s.BatteryPercent.Value:F0}%", s.OnAcPower || s.BatteryPercent.Value >= 20);
        }
        else
        {
            _batteryMetric.UpdateUnknown("n/a");
        }

        if (s.DiskTotalBytes is > 0 && s.DiskFreeBytes.HasValue)
        {
            var usedPercent = 100.0 * (s.DiskTotalBytes.Value - s.DiskFreeBytes.Value) / s.DiskTotalBytes.Value;
            _diskMetric.Update($"{usedPercent:F0}%", usedPercent < 90);
        }
        else
        {
            _diskMetric.UpdateUnknown();
        }
    }

    private static Dictionary<string, PreviewState> BuildStates() => new(StringComparer.OrdinalIgnoreCase)
    {
        ["Calm"] = new("Everything feels good.", Blue, ExpressionKind.Calm),
        ["Curious"] = new("Hm? What’s that?", Blue, ExpressionKind.Curious),
        ["Working hard"] = new("I’m working pretty hard right now.", Color.FromRgb(126, 113, 255), ExpressionKind.Working),
        ["Warm"] = new("Um… I’m getting a little warm.", Color.FromRgb(255, 161, 92), ExpressionKind.Warm),
        ["Needs attention"] = new("Something feels a little off.", Color.FromRgb(255, 128, 109), ExpressionKind.Worried),
        ["Low battery"] = new("I’m getting sleepy…", Color.FromRgb(147, 135, 197), ExpressionKind.Sleepy),
        ["Charging"] = new("Ahh, thank you.", Color.FromRgb(72, 196, 160), ExpressionKind.Happy),
        ["No internet"] = new("Did the internet disappear?", Color.FromRgb(116, 142, 190), ExpressionKind.Offline)
    };

    private FrameworkElement BuildCharacter()
    {
        _haloA = new GradientStop(Color.FromRgb(166, 243, 255), 0.17);
        _haloB = new GradientStop(Color.FromRgb(145, 177, 255), 0.36);
        _haloCoreStop = new GradientStop(Color.FromRgb(251, 253, 255), 0.50);
        _haloC = new GradientStop(Color.FromRgb(201, 168, 255), 0.66);
        _haloD = new GradientStop(Color.FromRgb(239, 176, 210), 0.83);
        var transparentLeft = new GradientStop(Colors.Transparent, 0.02);
        var transparentRight = new GradientStop(Colors.Transparent, 0.98);
        var gradient = new LinearGradientBrush
        {
            StartPoint = new Point(0, 0.5),
            EndPoint = new Point(1, 0.5),
            GradientStops = { transparentLeft, _haloA, _haloB, _haloCoreStop, _haloC, _haloD, transparentRight }
        };

        _haloBloom = new Border
        {
            Width = 190,
            Height = 24,
            CornerRadius = new CornerRadius(0, 0, 90, 90),
            Background = gradient,
            Opacity = 0.56,
            Effect = new System.Windows.Media.Effects.BlurEffect { Radius = 8 },
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Top,
            Margin = new Thickness(0, -3, 0, 0)
        };

        var coreGradient = new LinearGradientBrush
        {
            StartPoint = new Point(0, 0.5), EndPoint = new Point(1, 0.5),
            GradientStops =
            {
                new GradientStop(Colors.Transparent, 0),
                new GradientStop(Color.FromRgb(214, 242, 255), 0.23),
                new GradientStop(Colors.White, 0.50),
                new GradientStop(Color.FromRgb(245, 218, 250), 0.77),
                new GradientStop(Colors.Transparent, 1)
            }
        };
        _haloCore = new Border
        {
            Width = 112, Height = 3,
            CornerRadius = new CornerRadius(0, 0, 8, 8),
            Background = coreGradient,
            Opacity = 0.86,
            Effect = new System.Windows.Media.Effects.DropShadowEffect
            {
                Color = Color.FromRgb(145, 177, 255), BlurRadius = 17, ShadowDepth = 0, Opacity = 0.75
            },
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Top
        };

        _haloWisp = new Border
        {
            Width = 64, Height = 11,
            CornerRadius = new CornerRadius(50),
            Background = gradient,
            Opacity = 0.20,
            Effect = new System.Windows.Media.Effects.BlurEffect { Radius = 7 },
            RenderTransform = _haloWispOffset,
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Top,
            Margin = new Thickness(-62, 5, 0, 0)
        };

        var halo = new Grid
        {
            Width = 230,
            Height = 62,
            Background = Brushes.Transparent,
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Top,
            RenderTransformOrigin = new Point(0.5, 0),
            RenderTransform = _breathScale,
            Cursor = Cursors.Hand
        };
        halo.Children.Add(_haloBloom);
        halo.Children.Add(_haloWisp);
        halo.Children.Add(_haloCore);
        ApplyExpression(ExpressionKind.Calm, false);
        return halo;
    }

    private static EyeVisual BuildEye(double left, double top)
    {
        var pupilOffset = new TranslateTransform();
        var pupil = new Ellipse
        {
            Width = 10,
            Height = 12.5,
            Fill = new RadialGradientBrush(Color.FromRgb(40, 57, 93), Color.FromRgb(10, 18, 37)),
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center,
            RenderTransform = pupilOffset
        };
        var highlight = new Ellipse
        {
            Width = 2.8,
            Height = 2.8,
            Fill = Brushes.White,
            HorizontalAlignment = HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Top,
            Margin = new Thickness(3.5, 2.5, 0, 0),
            IsHitTestVisible = false
        };
        var eyeContents = new Grid();
        eyeContents.Children.Add(pupil);
        eyeContents.Children.Add(highlight);
        eyeContents.Children.Add(new Ellipse
        {
            Width = 1.2, Height = 1.2, Fill = Brushes.White, Opacity = 0.65,
            HorizontalAlignment = HorizontalAlignment.Right,
            VerticalAlignment = VerticalAlignment.Bottom,
            Margin = new Thickness(0, 0, 3, 3)
        });
        eyeContents.ClipToBounds = true;

        var blinkScale = new ScaleTransform(1, 1);
        var rotation = new RotateTransform();
        var transforms = new TransformGroup();
        transforms.Children.Add(blinkScale);
        transforms.Children.Add(rotation);

        var eye = new Border
        {
            Width = 14,
            Height = 16,
            CornerRadius = new CornerRadius(8),
            Background = new RadialGradientBrush(
                Color.FromRgb(255, 255, 255),
                Color.FromRgb(172, 206, 255)),
            BorderBrush = new SolidColorBrush(Color.FromRgb(205, 225, 255)),
            BorderThickness = new Thickness(0.7),
            Effect = new System.Windows.Media.Effects.DropShadowEffect
            {
                Color = Color.FromRgb(92, 137, 255),
                BlurRadius = 2.5,
                ShadowDepth = 0,
                Opacity = 0.45
            },
            RenderTransformOrigin = new Point(0.5, 0.5),
            RenderTransform = transforms,
            Child = eyeContents
        };
        Canvas.SetLeft(eye, left);
        Canvas.SetTop(eye, top);
        return new EyeVisual(eye, pupil, blinkScale, rotation, pupilOffset);
    }

    private static Border BuildFaceMark(
        double width,
        double height,
        double left,
        double top,
        double opacity)
    {
        var mark = new Border
        {
            Width = width,
            Height = height,
            CornerRadius = new CornerRadius(Math.Max(1, height / 2)),
            Background = new SolidColorBrush(Color.FromRgb(185, 216, 255)),
            Opacity = opacity,
            Effect = new System.Windows.Media.Effects.DropShadowEffect
            {
                Color = Color.FromRgb(92, 137, 255),
                BlurRadius = 5,
                ShadowDepth = 0,
                Opacity = 0.8
            }
        };
        Canvas.SetLeft(mark, left);
        Canvas.SetTop(mark, top);
        return mark;
    }

    private static Border BuildBubble(TextBlock text)
    {
        var panel = new Grid();
        panel.Children.Add(text);

        return new Border
        {
            Visibility = Visibility.Collapsed,
            Width = 270,
            MinHeight = 72,
            Padding = new Thickness(18, 14, 18, 14),
            CornerRadius = new CornerRadius(22, 22, 6, 22),
            Background = new SolidColorBrush(Color.FromArgb(182, 251, 253, 255)),
            BorderBrush = new SolidColorBrush(Color.FromArgb(48, 151, 178, 235)),
            BorderThickness = new Thickness(1),
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Top,
            Margin = new Thickness(0, 54, 0, 0),
            Child = panel
        };
    }

    private Border BuildHealthCard()
    {
        var status = new StackPanel
        {
            Width = 135,
            VerticalAlignment = VerticalAlignment.Center
        };
        _cardSubtitleText = new TextBlock
        {
            Text = "Everything feels good.",
            FontFamily = new FontFamily("Segoe UI Variable Text, Segoe UI"),
            FontSize = 12,
            FontWeight = FontWeights.SemiBold,
            Foreground = new SolidColorBrush(Ink),
            TextTrimming = TextTrimming.CharacterEllipsis
        };
        status.Children.Add(_cardSubtitleText);

        _cpuMetric = BuildMetric("CPU", "—");
        _memoryMetric = BuildMetric("MEM", "—");
        _tempMetric = BuildMetric("TEMP", "—");
        _batteryMetric = BuildMetric("BATT", "—");
        _diskMetric = BuildMetric("DISK", "—");

        var metrics = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            VerticalAlignment = VerticalAlignment.Center
        };
        metrics.Children.Add(_cpuMetric.Element);
        metrics.Children.Add(_memoryMetric.Element);
        metrics.Children.Add(_tempMetric.Element);
        metrics.Children.Add(_batteryMetric.Element);
        metrics.Children.Add(_diskMetric.Element);

        var row = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            VerticalAlignment = VerticalAlignment.Center
        };
        row.Children.Add(status);
        row.Children.Add(metrics);

        return new Border
        {
            Visibility = Visibility.Collapsed,
            Width = 510,
            Height = 48,
            Padding = new Thickness(14, 5, 10, 5),
            CornerRadius = new CornerRadius(18),
            Background = new SolidColorBrush(Color.FromArgb(168, 251, 253, 255)),
            BorderBrush = new SolidColorBrush(Color.FromArgb(44, 153, 178, 230)),
            BorderThickness = new Thickness(1),
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Top,
            Margin = new Thickness(0, 50, 0, 0),
            Child = row
        };
    }

    private static HealthMetricRow BuildMetric(string label, string value)
    {
        var grid = new Grid();
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

        var name = new TextBlock
        {
            Text = label,
            FontSize = 9,
            FontWeight = FontWeights.SemiBold,
            Foreground = new SolidColorBrush(Color.FromRgb(103, 115, 148)),
            VerticalAlignment = VerticalAlignment.Center,
            Margin = new Thickness(0, 0, 5, 0)
        };
        var reading = new TextBlock
        {
            Text = value,
            FontSize = 12,
            FontWeight = FontWeights.SemiBold,
            Foreground = new SolidColorBrush(Ink),
            VerticalAlignment = VerticalAlignment.Center
        };
        Grid.SetColumn(name, 0);
        Grid.SetColumn(reading, 1);
        grid.Children.Add(name);
        grid.Children.Add(reading);

        var element = new Border
        {
            Width = 70,
            Background = Brushes.Transparent,
            Padding = new Thickness(5, 4, 2, 4),
            Child = grid
        };
        return new HealthMetricRow(element, reading);
    }

    private ContextMenu BuildContextMenu()
    {
        var menu = new ContextMenu
        {
            FontFamily = new FontFamily("Segoe UI Variable Text, Segoe UI"),
            FontSize = 13
        };

        var heading = new MenuItem { Header = "Preview system state", IsEnabled = false };
        menu.Items.Add(heading);
        menu.Items.Add(new Separator());
        foreach (var state in _states)
        {
            var item = new MenuItem { Header = state.Key, Tag = state.Key };
            item.Click += (_, _) => ShowState((string)item.Tag);
            menu.Items.Add(item);
        }

        menu.Items.Add(new Separator());
        var voiceToggle = new MenuItem { Header = "Enable voice (“Hey Pixel”)" };
        voiceToggle.Click += (_, _) => ToggleVoice(voiceToggle);
        menu.Items.Add(voiceToggle);

        var ask = new MenuItem { Header = "Ask the system…" };
        ask.Click += (_, _) => OpenChatPanel();
        menu.Items.Add(ask);

        menu.Items.Add(new Separator());
        var exit = new MenuItem { Header = "Quit System Intelligence" };
        exit.Click += (_, _) => Close();
        menu.Items.Add(exit);
        return menu;
    }

    // Legacy chat entry point retained for the panel lifecycle, but kept
    // visually collapsed now that the formless halo uses its context menu.
    private Border BuildChatIcon()
    {
        var dots = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center };
        for (var i = 0; i < 3; i++)
        {
            dots.Children.Add(new Ellipse
            {
                Width = 2.4,
                Height = 2.4,
                Margin = new Thickness(1, 0, 1, 0),
                Fill = new SolidColorBrush(Color.FromRgb(120, 155, 235))
            });
        }

        var icon = new Border
        {
            Width = 0,
            Height = 0,
            Visibility = Visibility.Collapsed,
            CornerRadius = new CornerRadius(7, 7, 7, 2),
            Background = new SolidColorBrush(Color.FromArgb(235, 250, 253, 255)),
            BorderBrush = new SolidColorBrush(Color.FromArgb(120, 151, 178, 235)),
            BorderThickness = new Thickness(1),
            Effect = new System.Windows.Media.Effects.DropShadowEffect
            {
                BlurRadius = 6, ShadowDepth = 2, Opacity = 0.18, Color = Color.FromRgb(42, 63, 112)
            },
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Top,
            Margin = new Thickness(0),
            Cursor = Cursors.Hand,
            ToolTip = "Ask the system",
            Child = dots
        };
        icon.MouseLeftButtonUp += (_, e) =>
        {
            e.Handled = true;
            ToggleChatPanel();
        };
        return icon;
    }

    private (Border Panel, StackPanel Messages, ScrollViewer Scroll, TextBox Input,
        Border Composer, Border ResultSurface, TextBlock Hint) BuildChatPanel()
    {
        var input = new TextBox
        {
            FontFamily = new FontFamily("Segoe UI Variable Text, Segoe UI"),
            FontSize = 15,
            FontWeight = FontWeights.Medium,
            Foreground = new SolidColorBrush(Color.FromRgb(38, 49, 82)),
            CaretBrush = new SolidColorBrush(Color.FromRgb(103, 132, 235)),
            Padding = new Thickness(0),
            BorderThickness = new Thickness(0),
            Background = Brushes.Transparent,
            AcceptsReturn = false,
            TextWrapping = TextWrapping.NoWrap,
            VerticalContentAlignment = VerticalAlignment.Center
        };

        var hint = new TextBlock
        {
            Text = "Ask your computer…",
            FontFamily = new FontFamily("Segoe UI Variable Text, Segoe UI"),
            FontSize = 15,
            Foreground = new SolidColorBrush(Color.FromArgb(155, 88, 100, 132)),
            IsHitTestVisible = false,
            VerticalAlignment = VerticalAlignment.Center
        };
        input.TextChanged += (_, _) =>
        {
            hint.Visibility = input.Text.Length == 0
                ? Visibility.Visible
                : Visibility.Collapsed;
            HandleChatInputActivity(input.Text);
        };
        input.PreviewKeyDown += (_, e) =>
        {
            if (e.Key == Key.Enter)
            {
                e.Handled = true;
                _ = SendChatMessageAsync();
            }
            else if (e.Key == Key.Escape)
            {
                e.Handled = true;
                CloseChatPanel();
            }
        };

        var inputLayer = new Grid { VerticalAlignment = VerticalAlignment.Center };
        inputLayer.Children.Add(hint);
        inputLayer.Children.Add(input);

        var send = new Border
        {
            Width = 32,
            Height = 32,
            Background = Brushes.Transparent,
            Cursor = Cursors.Hand,
            ToolTip = "Send",
            Child = new TextBlock
            {
                Text = "→",
                FontSize = 23,
                FontWeight = FontWeights.SemiBold,
                Foreground = new LinearGradientBrush(
                    Color.FromRgb(82, 146, 238),
                    Color.FromRgb(145, 108, 226),
                    0),
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center
            }
        };
        send.MouseLeftButtonUp += (_, e) => { e.Handled = true; _ = SendChatMessageAsync(); };

        var composerGrid = new Grid();
        composerGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        composerGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(42) });
        Grid.SetColumn(inputLayer, 0);
        Grid.SetColumn(send, 1);
        composerGrid.Children.Add(inputLayer);
        composerGrid.Children.Add(send);

        var glass = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 1) };
        glass.GradientStops.Add(new GradientStop(Color.FromArgb(184, 255, 255, 255), 0));
        glass.GradientStops.Add(new GradientStop(Color.FromArgb(158, 242, 247, 255), 0.55));
        glass.GradientStops.Add(new GradientStop(Color.FromArgb(176, 251, 248, 255), 1));

        var composer = new Border
        {
            Height = 48,
            Padding = new Thickness(20, 0, 12, 0),
            CornerRadius = new CornerRadius(24),
            Background = glass,
            BorderBrush = new SolidColorBrush(Color.FromArgb(48, 167, 196, 255)),
            BorderThickness = new Thickness(1),
            RenderTransform = new TranslateTransform(),
            Child = composerGrid
        };

        var messages = new StackPanel();
        var scroll = new ScrollViewer
        {
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            MaxHeight = 190,
            Content = messages
        };
        var resultGrid = new Grid();
        resultGrid.Children.Add(scroll);

        var resultSurface = new Border
        {
            Visibility = Visibility.Collapsed,
            Margin = new Thickness(10, 0, 10, 8),
            Padding = new Thickness(20, 17, 16, 17),
            CornerRadius = new CornerRadius(20),
            Background = new SolidColorBrush(Color.FromArgb(176, 251, 253, 255)),
            BorderBrush = new SolidColorBrush(Color.FromArgb(44, 164, 196, 255)),
            BorderThickness = new Thickness(1),
            RenderTransform = new TranslateTransform(0, -8),
            Child = resultGrid
        };

        var stack = new StackPanel();
        stack.Children.Add(resultSurface);
        stack.Children.Add(composer);

        var panel = new Border
        {
            Visibility = Visibility.Collapsed,
            Width = ChatWidth,
            Padding = new Thickness(10, 0, 10, 12),
            Background = Brushes.Transparent,
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Top,
            Margin = new Thickness(0, 46, 0, 0),
            Child = stack
        };
        return (panel, messages, scroll, input, composer, resultSurface, hint);
    }

    private static Border BuildChatRow(string text, bool fromUser)
    {
        var block = new TextBlock
        {
            Text = text,
            FontFamily = new FontFamily("Segoe UI Variable Text, Segoe UI"),
            FontSize = fromUser ? 13 : 15,
            LineHeight = fromUser ? double.NaN : 22,
            TextWrapping = TextWrapping.Wrap,
            Foreground = new SolidColorBrush(fromUser ? Colors.White : Ink)
        };
        return new Border
        {
            Background = new SolidColorBrush(fromUser ? Color.FromRgb(100, 145, 255) : Colors.Transparent),
            CornerRadius = fromUser ? new CornerRadius(12, 12, 3, 12) : new CornerRadius(12, 12, 12, 3),
            Padding = fromUser ? new Thickness(10, 7, 10, 7) : new Thickness(0, 0, 28, 0),
            Margin = fromUser ? new Thickness(40, 4, 0, 4) : new Thickness(0),
            HorizontalAlignment = fromUser ? HorizontalAlignment.Right : HorizontalAlignment.Left,
            Child = block
        };
    }

    private void ToggleChatPanel()
    {
        if (_chatOpen)
        {
            CloseChatPanel();
        }
        else
        {
            OpenChatPanel();
        }
    }

    private void OpenChatPanel()
    {
        _lastNearby = DateTime.UtcNow;
        if (_isPeeking)
        {
            WakeFromPeek();
        }
        _cardOpen = false;
        _healthCard.Visibility = Visibility.Collapsed;
        _bubbleTimer.Stop();
        _bubble.Visibility = Visibility.Collapsed;
        _chatOpen = true;
        _chatPanel.Visibility = Visibility.Visible;
        _chatIcon.Visibility = Visibility.Collapsed;
        ResizeFromCorner(ChatWidth, ChatHeight);
        _chatMessages.Children.Clear();
        _chatResultSurface.Visibility = Visibility.Collapsed;
        _chatResultSurface.Opacity = 0;
        _chatComposer.Visibility = Visibility.Visible;
        _chatComposer.Opacity = 1;
        if (_chatComposer.RenderTransform is TranslateTransform composerOffset)
        {
            composerOffset.Y = 0;
        }
        _chatInput.IsEnabled = true;
        _chatInput.Text = string.Empty;
        _chatHint.Text = _chat.IsAvailable
            ? "Ask your computer…"
            : "Ask a question — limited reasoning is available";
        _chatInput.Focus();
        RestartChatIdleTimer();
    }

    private void CloseChatPanel()
    {
        _chatIdleTimer.Stop();
        _chatOpen = false;
        _chatPanel.Visibility = Visibility.Collapsed;
        _chatIcon.Visibility = Visibility.Visible;
        ResizeFromCorner(CompactWidth, CompactHeight);
    }

    private Task SendChatMessageAsync() => SendChatMessageAsync(_chatInput.Text.Trim(), speakReply: false);

    private async Task SendChatMessageAsync(string question, bool speakReply)
    {
        if (question.Length == 0 || _chatBusy)
        {
            return;
        }

        _chatHistory.Add(new ChatTurn(true, question));
        _chatIdleTimer.Stop();
        _chatMessages.Children.Clear();
        _chatResultSurface.Visibility = Visibility.Collapsed;
        _chatInput.IsEnabled = false;
        AnimatePromptAway();
        ApplyExpression(ExpressionKind.Working, true);
        _chatBusy = true;
        try
        {
            var reply = await _chat.AskAsync(question, _chatHistory, TimeSpan.FromSeconds(20));
            var answer = reply?.Answer ?? "Sorry, I couldn't quite gather my thoughts just then -- could you try again?";

            _chatMessages.Children.Add(BuildChatRow(answer, fromUser: false));
            _chatHistory.Add(new ChatTurn(false, answer));
            if (speakReply)
            {
                _voice.Speak(answer);
            }

            if (reply?.Action is { } action && IsActionWellFormed(action))
            {
                _chatMessages.Children.Add(BuildActionProposalRow(action));
            }
            ShowChatResult();
        }
        finally
        {
            _chatBusy = false;
            _chatInput.IsEnabled = true;
            _chatInput.Text = string.Empty;
            _chatHint.Text = "Ask a follow-up…";
            if (!_previewActive)
            {
                ApplyExpression(ExpressionKind.Calm, true);
            }
            if (_chatOpen)
            {
                ShowFollowUpComposer();
            }
            ScrollChatToEnd();
        }
    }

    private void AnimatePromptAway()
    {
        var duration = TimeSpan.FromMilliseconds(220);
        var easing = new CubicEase { EasingMode = EasingMode.EaseIn };
        var fade = new DoubleAnimation(1, 0, duration) { EasingFunction = easing };
        var lift = new DoubleAnimation(0, -18, duration) { EasingFunction = easing };
        fade.Completed += (_, _) =>
        {
            _chatComposer.Visibility = Visibility.Collapsed;
            _chatComposer.BeginAnimation(OpacityProperty, null);
            _chatComposer.Opacity = 1;
            if (_chatComposer.RenderTransform is TranslateTransform offset)
            {
                offset.BeginAnimation(TranslateTransform.YProperty, null);
                offset.Y = 0;
            }
        };
        _chatComposer.BeginAnimation(OpacityProperty, fade);
        if (_chatComposer.RenderTransform is TranslateTransform offset)
        {
            offset.BeginAnimation(TranslateTransform.YProperty, lift);
        }
    }

    private void ShowChatResult()
    {
        _chatResultSurface.Visibility = Visibility.Visible;
        _chatResultSurface.Opacity = 0;
        var duration = TimeSpan.FromMilliseconds(280);
        var easing = new CubicEase { EasingMode = EasingMode.EaseOut };
        _chatResultSurface.BeginAnimation(OpacityProperty,
            new DoubleAnimation(0, 1, duration) { EasingFunction = easing });
        if (_chatResultSurface.RenderTransform is TranslateTransform offset)
        {
            offset.BeginAnimation(TranslateTransform.YProperty,
                new DoubleAnimation(-8, 0, duration) { EasingFunction = easing });
        }
    }

    private void ShowFollowUpComposer()
    {
        _chatComposer.BeginAnimation(OpacityProperty, null);
        _chatComposer.Visibility = Visibility.Visible;
        _chatComposer.Opacity = 0;

        var offset = _chatComposer.RenderTransform as TranslateTransform;
        if (offset is not null)
        {
            offset.BeginAnimation(TranslateTransform.YProperty, null);
            offset.Y = 6;
        }

        var delay = TimeSpan.FromMilliseconds(150);
        var duration = TimeSpan.FromMilliseconds(240);
        var easing = new CubicEase { EasingMode = EasingMode.EaseOut };
        _chatComposer.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, duration)
        {
            BeginTime = delay,
            EasingFunction = easing
        });
        if (offset is not null)
        {
            offset.BeginAnimation(TranslateTransform.YProperty, new DoubleAnimation(6, 0, duration)
            {
                BeginTime = delay,
                EasingFunction = easing
            });
        }

        _chatInput.Focus();
        Keyboard.Focus(_chatInput);
        RestartChatIdleTimer();
    }

    private void HandleChatInputActivity(string text)
    {
        if (!_chatOpen || _chatBusy)
        {
            return;
        }

        if (text.Length > 0)
        {
            _chatIdleTimer.Stop();
            _chatComposer.BeginAnimation(OpacityProperty, null);
            _chatComposer.Visibility = Visibility.Visible;
            _chatComposer.Opacity = 1;
            return;
        }

        RestartChatIdleTimer();
    }

    private void RestartChatIdleTimer()
    {
        _chatIdleTimer.Stop();
        if (_chatOpen && !_chatBusy && _chatInput.Text.Length == 0 && !_voice.IsListeningForQuestion)
        {
            _chatIdleTimer.Start();
        }
    }

    private void HideIdleChatComposer()
    {
        _chatIdleTimer.Stop();
        if (!_chatOpen || _chatBusy || _chatInput.Text.Length > 0 || _voice.IsListeningForQuestion)
        {
            return;
        }

        var fade = new DoubleAnimation(_chatComposer.Opacity, 0, TimeSpan.FromMilliseconds(200))
        {
            EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseOut }
        };
        fade.Completed += (_, _) =>
        {
            _chatComposer.Visibility = Visibility.Collapsed;
            _chatComposer.BeginAnimation(OpacityProperty, null);
            _chatComposer.Opacity = 1;
            if (_chatResultSurface.Visibility != Visibility.Visible)
            {
                CloseChatPanel();
            }
        };
        _chatComposer.BeginAnimation(OpacityProperty, fade);
    }

    private void ScrollChatToEnd() => _chatScroll.ScrollToEnd();

    // Defense in depth: chat.py already validates a proposed action against
    // a fixed action-type set and (for suspend_process) real pids before it
    // ever leaves the process, but the UI never trusts a subprocess's JSON
    // blindly either -- a malformed or missing field here just means no
    // confirm button appears, never a bad `sysintel act` invocation.
    private static bool IsActionWellFormed(ChatAction action) => action.Type switch
    {
        "suspend_process" => action.Pid is > 0,
        "change_power_mode" => action.Level is "best_power_efficiency" or "best_performance",
        _ => false
    };

    private static string DescribeAction(ChatAction action) => action.Type switch
    {
        "suspend_process" => string.IsNullOrEmpty(action.TargetName)
            ? $"Pause process {action.Pid} -- it can be resumed later, this doesn't close it."
            : $"Pause {action.TargetName} (pid {action.Pid})? It can be resumed later -- this doesn't close it.",
        "change_power_mode" => action.Level == "best_power_efficiency"
            ? "Switch to Best Power Efficiency mode?"
            : "Switch to Best Performance mode?",
        _ => "Do this?"
    };

    private Border BuildActionProposalRow(ChatAction action)
    {
        var text = new TextBlock
        {
            Text = DescribeAction(action),
            FontFamily = new FontFamily("Segoe UI Variable Text, Segoe UI"),
            FontSize = 12.5,
            TextWrapping = TextWrapping.Wrap,
            Foreground = new SolidColorBrush(Ink)
        };

        var confirm = new TextBlock
        {
            Text = "Confirm", FontSize = 12, FontWeight = FontWeights.SemiBold,
            Foreground = new SolidColorBrush(Color.FromRgb(59, 183, 149)),
            Cursor = Cursors.Hand, Margin = new Thickness(0, 6, 14, 0)
        };
        var dismiss = new TextBlock
        {
            Text = "Dismiss", FontSize = 12,
            Foreground = new SolidColorBrush(Color.FromRgb(132, 142, 169)),
            Cursor = Cursors.Hand, Margin = new Thickness(0, 6, 0, 0)
        };
        var buttonRow = new StackPanel { Orientation = Orientation.Horizontal };
        buttonRow.Children.Add(confirm);
        buttonRow.Children.Add(dismiss);

        var stack = new StackPanel();
        stack.Children.Add(text);
        stack.Children.Add(buttonRow);

        var row = new Border
        {
            Background = new SolidColorBrush(Color.FromArgb(170, 255, 240, 210)),
            CornerRadius = new CornerRadius(10),
            Padding = new Thickness(10, 8, 10, 8),
            Margin = new Thickness(0, 2, 40, 8),
            HorizontalAlignment = HorizontalAlignment.Left,
            Child = stack
        };

        confirm.MouseLeftButtonUp += async (_, e) =>
        {
            e.Handled = true;
            buttonRow.Children.Clear();
            text.Text = "Working on it…";
            var (message, actionId) = await ExecuteChatActionAsync(action);
            text.Text = message;
            if (actionId is not null)
            {
                stack.Children.Add(BuildUndoLink(actionId, text));
            }
            ScrollChatToEnd();
        };
        dismiss.MouseLeftButtonUp += (_, e) =>
        {
            e.Handled = true;
            row.Visibility = Visibility.Collapsed;
        };

        return row;
    }

    private TextBlock BuildUndoLink(string actionId, TextBlock outcomeText)
    {
        var undo = new TextBlock
        {
            Text = "Undo", FontSize = 12, FontWeight = FontWeights.SemiBold,
            Foreground = new SolidColorBrush(Blue), Cursor = Cursors.Hand,
            Margin = new Thickness(0, 6, 0, 0)
        };
        undo.MouseLeftButtonUp += async (_, e) =>
        {
            e.Handled = true;
            undo.Text = "Undoing…";
            var (message, _) = await ExecuteRollbackAsync(actionId);
            outcomeText.Text = message;
            ((Panel)undo.Parent).Children.Remove(undo);
            ScrollChatToEnd();
        };
        return undo;
    }

    // Both of these shell out to the exact same sysintel.exe `act` command
    // the CLI itself uses (core/cli/main.cpp), via TelemetryClient's
    // existing JSON runner -- nothing chat-specific about execution, only
    // about getting the human's confirm click before it happens.
    private async Task<(string Message, string? ActionId)> ExecuteChatActionAsync(ChatAction action)
    {
        var reason = string.IsNullOrEmpty(action.Reason) ? "requested via chat" : action.Reason;
        var args = action.Type switch
        {
            "suspend_process" when action.Pid is int pid =>
                $"act suspend-process {pid} --reason \"{PixelChatClient.EscapeArg(reason)}\" --db \"{PixelChatClient.EscapeArg(_dbPath)}\" --yes --json",
            "change_power_mode" when action.Level is not null =>
                $"act change-power-mode --level {action.Level} --reason \"{PixelChatClient.EscapeArg(reason)}\" --db \"{PixelChatClient.EscapeArg(_dbPath)}\" --yes --json",
            _ => null
        };
        if (args is null)
        {
            return ("I couldn't work out how to do that safely, so I didn't.", null);
        }

        var root = await _telemetry.RunJsonAsync(args, TimeSpan.FromSeconds(10));
        if (root is null)
        {
            return ("Something went wrong trying to do that -- nothing changed.", null);
        }

        var executed = root["executed"].AsBool();
        var success = root["success"].AsBool();
        var message = root["message"].AsString();
        var actionId = root["action_id"].AsString();

        if (!executed)
        {
            // Always calling with --yes means the only way to land here is
            // a genuine no-op (already in the requested state) -- not a
            // declined preview, so this isn't a failure.
            return (success ? $"Already the case -- {message}" : $"Couldn't do that: {message}", null);
        }
        return (success ? $"Done -- {message}" : $"That failed: {message}", success && actionId.Length > 0 ? actionId : null);
    }

    private async Task<(string Message, string? ActionId)> ExecuteRollbackAsync(string actionId)
    {
        var args = $"act rollback {actionId} --db \"{PixelChatClient.EscapeArg(_dbPath)}\" --yes --json";
        var root = await _telemetry.RunJsonAsync(args, TimeSpan.FromSeconds(10));
        if (root is null)
        {
            return ("Couldn't undo that -- nothing changed.", null);
        }
        var success = root["success"].AsBool();
        var message = root["message"].AsString();
        return (success ? $"Undone -- {message}" : $"Couldn't undo: {message}", null);
    }

    private void StartBreathing()
    {
        var breathe = new DoubleAnimation
        {
            From = 0.97,
            To = 1.035,
            Duration = TimeSpan.FromSeconds(2.7),
            AutoReverse = true,
            RepeatBehavior = RepeatBehavior.Forever,
            EasingFunction = new SineEase { EasingMode = EasingMode.EaseInOut }
        };
        _breathScale.BeginAnimation(ScaleTransform.ScaleXProperty, breathe);

        _haloBloom.BeginAnimation(OpacityProperty, new DoubleAnimation
        {
            From = 0.42, To = 0.62, Duration = TimeSpan.FromSeconds(2.7),
            AutoReverse = true, RepeatBehavior = RepeatBehavior.Forever,
            EasingFunction = new SineEase { EasingMode = EasingMode.EaseInOut }
        });
    }

    private void ScheduleNextBlink()
    {
        _blinkTimer.Stop();
        _blinkTimer.Interval = TimeSpan.FromMilliseconds(_random.Next(3200, 7200));
        _blinkTimer.Start();
    }

    private void Blink()
    {
        _haloCore.BeginAnimation(OpacityProperty, new DoubleAnimationUsingKeyFrames
        {
            FillBehavior = FillBehavior.Stop,
            KeyFrames =
            {
                new LinearDoubleKeyFrame(0.72, KeyTime.FromTimeSpan(TimeSpan.Zero)),
                new SplineDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(170)), new KeySpline(0.2, 0, 0.2, 1)),
                new SplineDoubleKeyFrame(0.82, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(900)), new KeySpline(0.2, 0, 0.2, 1))
            }
        });
        _haloWispOffset.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation
        {
            From = -18, To = 58, Duration = TimeSpan.FromSeconds(1.4),
            EasingFunction = new SineEase { EasingMode = EasingMode.EaseInOut },
            FillBehavior = FillBehavior.Stop
        });
        ScheduleNextBlink();
    }

    private void FollowCursor()
    {
        if (_layoutSmokeTest) return;
        if (!GetCursorPos(out var cursor))
        {
            return;
        }

        var source = PresentationSource.FromVisual(this);
        if (source?.CompositionTarget == null) return;
        var pointer = source.CompositionTarget.TransformFromDevice.Transform(new Point(cursor.X, cursor.Y));
        var centerX = SystemParameters.PrimaryScreenWidth / 2;
        var dx = pointer.X - centerX;
        var dy = pointer.Y;
        var distance = Math.Sqrt((dx * dx) + (dy * dy));
        var nearby = distance < 155 || IsMouseOver || _character.ContextMenu?.IsOpen == true;
        var wakeZone = IsWakeZone(pointer, SystemParameters.PrimaryScreenWidth);
        if (_isPeeking)
        {
            if (wakeZone)
            {
                if (!_cornerEntered.HasValue) _cornerEntered = DateTime.UtcNow;
                if ((DateTime.UtcNow - _cornerEntered.Value).TotalMilliseconds >= 180)
                {
                    _lastNearby = DateTime.UtcNow;
                    WakeFromPeek();
                }
            }
            else _cornerEntered = null;
            return;
        }
        _cornerEntered = null;

        if (nearby)
        {
            _lastNearby = DateTime.UtcNow;
        }
        else if (!_cardOpen && !_chatOpen && _bubble.Visibility != Visibility.Visible &&
                 !_isPeeking && DateTime.UtcNow - _lastNearby >= PeekAfter)
        {
            HideToPeek();
        }
    }

    private void ToggleHealthCard()
    {
        _lastNearby = DateTime.UtcNow;
        if (_isPeeking)
        {
            _isPeeking = false;
        }
        if (_chatOpen)
        {
            _chatOpen = false;
            _chatPanel.Visibility = Visibility.Collapsed;
        }
        _cardOpen = !_cardOpen;
        _bubbleTimer.Stop();
        _bubble.Visibility = Visibility.Collapsed;
        _healthCard.Visibility = _cardOpen ? Visibility.Visible : Visibility.Collapsed;
        _chatIcon.Visibility = _cardOpen ? Visibility.Collapsed : Visibility.Visible;
        ResizeFromCorner(_cardOpen ? ExpandedWidth : CompactWidth, _cardOpen ? ExpandedHeight : CompactHeight);
    }

    private void ShowState(string name)
    {
        if (!_states.TryGetValue(name, out var state))
        {
            return;
        }

        _previewActive = true;
        ShowBubble(state.Message, state.Accent, state.Expression);
    }

    private void ShowMessage(string message, ExpressionKind expression) =>
        ShowBubble(message, AccentFor(expression), expression);

    private static Color AccentFor(ExpressionKind expression) => expression switch
    {
        ExpressionKind.Working => Color.FromRgb(126, 113, 255),
        ExpressionKind.Warm => Color.FromRgb(255, 161, 92),
        ExpressionKind.Worried => Color.FromRgb(255, 128, 109),
        ExpressionKind.Sleepy => Color.FromRgb(147, 135, 197),
        ExpressionKind.Happy => Color.FromRgb(72, 196, 160),
        ExpressionKind.Offline => Color.FromRgb(116, 142, 190),
        _ => Blue
    };

    private void ShowBubble(string message, Color accent, ExpressionKind expression)
    {
        _cardOpen = false;
        _isPeeking = false;
        _lastNearby = DateTime.UtcNow;
        _healthCard.Visibility = Visibility.Collapsed;
        _chatOpen = false;
        _chatPanel.Visibility = Visibility.Collapsed;
        _chatIcon.Visibility = Visibility.Collapsed;
        _bubbleText.Text = message;
        _bubble.BorderBrush = new SolidColorBrush(Color.FromArgb(120, accent.R, accent.G, accent.B));
        _bubble.Visibility = Visibility.Visible;
        ApplyExpression(expression, true);
        ResizeFromCorner(BubbleWidth, BubbleHeight);
        _bubbleTimer.Stop();
        _bubbleTimer.Start();

        var hop = new DoubleAnimation
        {
            From = 0,
            To = expression == ExpressionKind.Happy ? -2 : -0.5,
            Duration = TimeSpan.FromMilliseconds(280),
            AutoReverse = true,
            FillBehavior = FillBehavior.Stop,
            EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseOut }
        };
        _perkOffset.BeginAnimation(TranslateTransform.YProperty, hop);
    }

    private void HideBubble()
    {
        _previewActive = false;
        _bubbleTimer.Stop();
        _bubble.Visibility = Visibility.Collapsed;
        _chatIcon.Visibility = Visibility.Visible;
        ResizeFromCorner(CompactWidth, CompactHeight);
    }

    private void ApplyExpression(ExpressionKind expression, bool animate)
    {
        _expression = expression;
        var palette = expression switch
        {
            ExpressionKind.Curious => new HaloPalette("82F2FF", "76BDFF", "FFFFFF", "A88FFF", "DDA4E4", 202, 124),
            ExpressionKind.Working => new HaloPalette("66E9FF", "789FFF", "F7F5FF", "A878F1", "E98BC5", 214, 132),
            ExpressionKind.Warm => new HaloPalette("FFE0A0", "FFB378", "FFF9ED", "FF8C91", "D989BC", 208, 122),
            ExpressionKind.Worried => new HaloPalette("FFD28A", "FF9F70", "FFF5EA", "FF777F", "D26B9E", 220, 136),
            ExpressionKind.Sleepy => new HaloPalette("B9D7E5", "929FCB", "F5F7FF", "A99AC8", "C3A5C0", 158, 96),
            ExpressionKind.Happy => new HaloPalette("AAFFDF", "78E8CA", "F5FFFD", "7FCFFF", "B6AEED", 204, 126),
            ExpressionKind.Offline => new HaloPalette("AFC5D7", "8499BB", "EEF3F8", "9897B6", "B49AAA", 168, 98),
            _ => new HaloPalette("A6F3FF", "91B1FF", "FBFDFF", "C9A8FF", "EFB0D2", 190, 112)
        };

        TransitionColor(_haloA, palette.A, animate);
        TransitionColor(_haloB, palette.B, animate);
        TransitionColor(_haloCoreStop, palette.Core, animate);
        TransitionColor(_haloC, palette.C, animate);
        TransitionColor(_haloD, palette.D, animate);
        Transition(_haloBloom, WidthProperty, palette.BloomWidth, animate);
        Transition(_haloCore, WidthProperty, palette.CoreWidth, animate);
    }

    private static void TransitionColor(GradientStop stop, Color value, bool animate)
    {
        var current = stop.Color;
        stop.BeginAnimation(GradientStop.ColorProperty, null);
        stop.Color = value;
        if (!animate) return;
        stop.BeginAnimation(GradientStop.ColorProperty, new ColorAnimation
        {
            From = current, To = value, Duration = TimeSpan.FromMilliseconds(520),
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut },
            FillBehavior = FillBehavior.Stop
        });
    }

    private void SetEyes(EyePose left, EyePose right, bool animate)
    {
        SetEye(_leftEye, left, animate);
        SetEye(_rightEye, right, animate);
    }

    private static void SetEye(EyeVisual eye, EyePose pose, bool animate)
    {
        Transition(eye.Root, WidthProperty, pose.Width, animate);
        Transition(eye.Root, HeightProperty, pose.Height, animate);
        Transition(eye.Root, Canvas.LeftProperty, pose.Left, animate);
        Transition(eye.Root, Canvas.TopProperty, pose.Top, animate);
        Transition(eye.Rotation, RotateTransform.AngleProperty, pose.Angle, animate);
        Transition(eye.Pupil, OpacityProperty, pose.PupilOpacity, animate);
    }

    private void SetMarks(
        double mouthWidth,
        double mouthHeight,
        double mouthLeft,
        double mouthTop,
        double mouthOpacity,
        double cheekOpacity,
        double sweatOpacity,
        bool animate)
    {
        Transition(_mouth, WidthProperty, mouthWidth, animate);
        Transition(_mouth, HeightProperty, mouthHeight, animate);
        Transition(_mouth, Canvas.LeftProperty, mouthLeft, animate);
        Transition(_mouth, Canvas.TopProperty, mouthTop, animate);
        Transition(_mouth, OpacityProperty, mouthOpacity, animate);
        Transition(_leftCheek, OpacityProperty, cheekOpacity, animate);
        Transition(_rightCheek, OpacityProperty, cheekOpacity, animate);
        Transition(_sweatDrop, OpacityProperty, sweatOpacity, animate);
    }

    private void SetBrows(double opacity, double leftAngle, double rightAngle, bool animate)
    {
        EnsureRotation(_leftBrow);
        EnsureRotation(_rightBrow);
        Transition(_leftBrow, OpacityProperty, opacity, animate);
        Transition(_rightBrow, OpacityProperty, opacity, animate);
        Transition((RotateTransform)_leftBrow.RenderTransform, RotateTransform.AngleProperty, leftAngle, animate);
        Transition((RotateTransform)_rightBrow.RenderTransform, RotateTransform.AngleProperty, rightAngle, animate);
    }

    private static void EnsureRotation(FrameworkElement element)
    {
        if (element.RenderTransform is RotateTransform)
        {
            return;
        }
        element.RenderTransformOrigin = new Point(0.5, 0.5);
        element.RenderTransform = new RotateTransform();
    }

    private static void Transition(
        DependencyObject target,
        DependencyProperty property,
        double value,
        bool animate)
    {
        var current = (double)target.GetValue(property);
        BeginAnimation(target, property, null);
        target.SetValue(property, value);
        if (!animate || double.IsNaN(current))
        {
            return;
        }

        BeginAnimation(target, property, new DoubleAnimation
        {
            From = current,
            To = value,
            Duration = TimeSpan.FromMilliseconds(220),
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut },
            FillBehavior = FillBehavior.Stop
        });
    }

    private static void BeginAnimation(
        DependencyObject target,
        DependencyProperty property,
        AnimationTimeline? animation)
    {
        if (target is Animatable animatable)
        {
            animatable.BeginAnimation(property, animation);
        }
        else if (target is UIElement element)
        {
            element.BeginAnimation(property, animation);
        }
    }

    private void HideToPeek()
    {
        _isPeeking = true;
        AnimateTop(PeekTop());
    }

    private async Task RunLayoutSmokeTest()
    {
        _cursorTimer.Stop();
        if (!IsWakeZone(new Point(768, 1), 1536) ||
            IsWakeZone(new Point(700, 1), 1536) ||
            IsWakeZone(new Point(768, 20), 1536)) Environment.Exit(4);
        HideToPeek();
        await Task.Delay(1000);
        WakeFromPeek();
        await Task.Delay(100);
        ToggleHealthCard();
        await Task.Delay(800);

        var correctTop = Math.Abs(Top - ActiveTop(ExpandedHeight)) < 0.5;
        var correctSize = Math.Abs(Width - ExpandedWidth) < 0.5 &&
                          Math.Abs(Height - ExpandedHeight) < 0.5;
        var correctCenter = Math.Abs((Left + Width / 2) - SystemParameters.PrimaryScreenWidth / 2) < 0.5;
        Environment.Exit(correctTop && correctSize && correctCenter && _cardOpen ? 0 : 3);
    }

    private void WakeFromPeek()
    {
        _isPeeking = false;
        _lastNearby = DateTime.UtcNow;
        AnimateTop(ActiveTop(Height));
        _haloCore.BeginAnimation(OpacityProperty, new DoubleAnimation
        {
            From = 0.35, To = 0.94, Duration = TimeSpan.FromMilliseconds(720),
            EasingFunction = new SineEase { EasingMode = EasingMode.EaseInOut },
            FillBehavior = FillBehavior.Stop
        });
    }

    private void AnimateTop(double target)
    {
        var revision = ++_positionAnimationRevision;
        var slide = new DoubleAnimation
        {
            From = Top,
            To = target,
            Duration = TimeSpan.FromMilliseconds(_isPeeking ? 900 : 720),
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseInOut },
            FillBehavior = FillBehavior.Stop
        };
        slide.Completed += (_, _) =>
        {
            if (revision != _positionAnimationRevision)
            {
                return;
            }
            BeginAnimation(TopProperty, null);
            Top = target;
        };
        BeginAnimation(TopProperty, slide);
    }

    private void ResizeFromCorner(double width, double height)
    {
        _positionAnimationRevision++;
        BeginAnimation(TopProperty, null);
        Width = width;
        Height = height;
        Left = (SystemParameters.PrimaryScreenWidth - width) / 2;
        Top = _isPeeking ? PeekTop() : ActiveTop(height);
    }

    private void DockToCorner() => ResizeFromCorner(Width, Height);

    private static double ActiveTop(double height)
        => 0;

    private static double PeekTop()
    {
        return -CompactHeight;
    }

    private static double Clamp(double value, double minimum, double maximum)
        => Math.Max(minimum, Math.Min(maximum, value));

    private static bool IsWakeZone(Point pointer, double screenWidth)
        => Math.Abs(pointer.X - screenWidth / 2) <= 42 && pointer.Y >= 0 && pointer.Y <= 6;

    [DllImport("user32.dll")]
    private static extern bool GetCursorPos(out NativePoint point);

    [StructLayout(LayoutKind.Sequential)]
    private struct NativePoint
    {
        public int X;
        public int Y;
    }

    private sealed class HaloPalette
    {
        public HaloPalette(string a, string b, string core, string c, string d, double bloomWidth, double coreWidth)
        {
            A = Parse(a); B = Parse(b); Core = Parse(core); C = Parse(c); D = Parse(d);
            BloomWidth = bloomWidth; CoreWidth = coreWidth;
        }

        public Color A { get; }
        public Color B { get; }
        public Color Core { get; }
        public Color C { get; }
        public Color D { get; }
        public double BloomWidth { get; }
        public double CoreWidth { get; }

        private static Color Parse(string hex) => Color.FromRgb(
            Convert.ToByte(hex.Substring(0, 2), 16),
            Convert.ToByte(hex.Substring(2, 2), 16),
            Convert.ToByte(hex.Substring(4, 2), 16));
    }

    private sealed class HealthMetricRow
    {
        private static readonly SolidColorBrush NormalBrush = new(Ink);
        private static readonly SolidColorBrush AttentionBrush = new(Color.FromRgb(224, 105, 92));
        private static readonly SolidColorBrush UnknownBrush = new(Color.FromRgb(163, 173, 199));

        public HealthMetricRow(Border element, TextBlock valueText)
        {
            Element = element;
            ValueText = valueText;
        }

        public Border Element { get; }
        private TextBlock ValueText { get; }

        public void Update(string value, bool healthy)
        {
            ValueText.Text = value;
            ValueText.Foreground = healthy ? NormalBrush : AttentionBrush;
        }

        public void UpdateUnknown(string placeholder = "—")
        {
            ValueText.Text = placeholder;
            ValueText.Foreground = UnknownBrush;
        }
    }

    private sealed class PreviewState
    {
        public PreviewState(string message, Color accent, ExpressionKind expression)
        {
            Message = message;
            Accent = accent;
            Expression = expression;
        }

        public string Message { get; }
        public Color Accent { get; }
        public ExpressionKind Expression { get; }
    }

    private sealed class EyeVisual
    {
        public EyeVisual(
            Border root,
            Ellipse pupil,
            ScaleTransform blinkScale,
            RotateTransform rotation,
            TranslateTransform pupilOffset)
        {
            Root = root;
            Pupil = pupil;
            BlinkScale = blinkScale;
            Rotation = rotation;
            PupilOffset = pupilOffset;
        }

        public Border Root { get; }
        public Ellipse Pupil { get; }
        public ScaleTransform BlinkScale { get; }
        public RotateTransform Rotation { get; }
        public TranslateTransform PupilOffset { get; }
    }

    private sealed class EyePose
    {
        public EyePose(double width, double height, double left, double top, double angle, double pupilOpacity)
        {
            Width = width;
            Height = height;
            Left = left;
            Top = top;
            Angle = angle;
            PupilOpacity = pupilOpacity;
        }

        public double Width { get; }
        public double Height { get; }
        public double Left { get; }
        public double Top { get; }
        public double Angle { get; }
        public double PupilOpacity { get; }
    }

}
