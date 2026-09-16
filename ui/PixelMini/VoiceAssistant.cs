using System;
using System.Speech.Recognition;
using System.Speech.Synthesis;
using System.Windows.Threading;

namespace SystemIntelligence.PixelMini;

// "Hey Pixel" wake-word listening plus a spoken reply, entirely local --
// System.Speech (SAPI under the hood) rather than a cloud STT, so nothing
// about your voice ever leaves the machine. Opt-in only: the caller decides
// when to Start()/Stop() this (see PixelWindow's context-menu toggle), it
// never listens unless explicitly turned on.
//
// State machine: idle (only the wake-word grammar active) -> wake word
// heard -> the free-dictation grammar takes over for one utterance (with a
// hard timeout backstop, since natural speech doesn't always trip SAPI's
// own end-of-speech detection promptly) -> back to idle, whether or not
// anything usable was heard.
internal sealed class VoiceAssistant : IDisposable
{
    private static readonly TimeSpan ListenTimeout = TimeSpan.FromSeconds(8);
    private const float WakeWordConfidenceThreshold = 0.6f;

    private SpeechRecognitionEngine? _recognizer;
    private Grammar? _wakeGrammar;
    private DictationGrammar? _dictationGrammar;
    private readonly SpeechSynthesizer _synth = new();
    private readonly DispatcherTimer _listenTimeoutTimer;
    private bool _awaitingQuestion;

    public event Action? WakeWordDetected;
    public event Action<string>? TranscriptionChanged;
    public event Action<string>? QuestionCaptured;
    public event Action? ListenTimedOut;

    public bool IsAvailable { get; private set; }
    public bool IsListeningForQuestion { get; private set; }

    public VoiceAssistant()
    {
        _listenTimeoutTimer = new DispatcherTimer { Interval = ListenTimeout };
        _listenTimeoutTimer.Tick += (_, _) =>
        {
            _listenTimeoutTimer.Stop();
            RevertToWakeWord();
            ListenTimedOut?.Invoke();
        };
    }

    // Returns false (never throws) if there's no microphone, no speech
    // recognizer installed for this Windows locale, or permission is
    // denied -- voice just silently isn't available, same "degrade, don't
    // crash" contract as the telemetry/chat adapters.
    public bool Start()
    {
        if (_recognizer is not null)
        {
            return true;
        }

        try
        {
            var recognizer = new SpeechRecognitionEngine();
            recognizer.SetInputToDefaultAudioDevice();
            recognizer.InitialSilenceTimeout = TimeSpan.FromSeconds(5);
            recognizer.EndSilenceTimeout = TimeSpan.FromSeconds(1.5);

            var wakeGrammar = new Grammar(new GrammarBuilder(new Choices("Pixel", "Hey Pixel"))) { Name = "wake" };
            recognizer.LoadGrammar(wakeGrammar);

            var dictationGrammar = new DictationGrammar { Name = "dictation", Enabled = false };
            recognizer.LoadGrammar(dictationGrammar);

            recognizer.SpeechRecognized += OnSpeechRecognized;
            recognizer.SpeechHypothesized += OnSpeechHypothesized;
            recognizer.RecognizeAsync(RecognizeMode.Multiple);

            _recognizer = recognizer;
            _wakeGrammar = wakeGrammar;
            _dictationGrammar = dictationGrammar;
            IsAvailable = true;
            return true;
        }
        catch
        {
            IsAvailable = false;
            _recognizer = null;
            return false;
        }
    }

    public void Stop()
    {
        _listenTimeoutTimer.Stop();
        _awaitingQuestion = false;
        IsListeningForQuestion = false;
        try
        {
            _synth.SpeakAsyncCancelAll();
        }
        catch
        {
        }

        if (_recognizer is { } recognizer)
        {
            recognizer.SpeechRecognized -= OnSpeechRecognized;
            recognizer.SpeechHypothesized -= OnSpeechHypothesized;
            try
            {
                recognizer.RecognizeAsyncCancel();
            }
            catch
            {
            }
            recognizer.Dispose();
        }
        _recognizer = null;
        _wakeGrammar = null;
        _dictationGrammar = null;
        IsAvailable = false;
    }

    public void Speak(string text)
    {
        if (!IsAvailable || string.IsNullOrWhiteSpace(text))
        {
            return;
        }
        try
        {
            _synth.SpeakAsyncCancelAll();
            _synth.SpeakAsync(text);
        }
        catch
        {
            // A synthesis failure shouldn't take the rest of the app down
            // with it -- the answer is still visible in the chat panel
            // either way.
        }
    }

    private void OnSpeechRecognized(object? sender, SpeechRecognizedEventArgs e)
    {
        var grammarName = e.Result?.Grammar?.Name;

        if (grammarName == "wake" && !_awaitingQuestion && e.Result!.Confidence >= WakeWordConfidenceThreshold)
        {
            BeginListeningForQuestion();
            return;
        }

        if (grammarName == "dictation" && _awaitingQuestion)
        {
            var text = e.Result?.Text?.Trim();
            RevertToWakeWord();
            if (!string.IsNullOrEmpty(text))
            {
                QuestionCaptured?.Invoke(text!);
            }
        }
    }

    private void OnSpeechHypothesized(object? sender, SpeechHypothesizedEventArgs e)
    {
        if (!_awaitingQuestion || e.Result?.Grammar?.Name != "dictation")
        {
            return;
        }

        var text = e.Result.Text?.Trim();
        if (!string.IsNullOrEmpty(text))
        {
            TranscriptionChanged?.Invoke(text!);
        }
    }

    private void BeginListeningForQuestion()
    {
        if (_wakeGrammar is null || _dictationGrammar is null)
        {
            return;
        }
        _awaitingQuestion = true;
        IsListeningForQuestion = true;
        _wakeGrammar.Enabled = false;
        _dictationGrammar.Enabled = true;
        _listenTimeoutTimer.Stop();
        _listenTimeoutTimer.Start();
        WakeWordDetected?.Invoke();
    }

    private void RevertToWakeWord()
    {
        _listenTimeoutTimer.Stop();
        _awaitingQuestion = false;
        IsListeningForQuestion = false;
        if (_wakeGrammar is not null)
        {
            _wakeGrammar.Enabled = true;
        }
        if (_dictationGrammar is not null)
        {
            _dictationGrammar.Enabled = false;
        }
    }

    public void Dispose() => Stop();
}
