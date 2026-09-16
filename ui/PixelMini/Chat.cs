using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading.Tasks;

namespace SystemIntelligence.PixelMini;

internal readonly struct ChatTurn
{
    public ChatTurn(bool fromUser, string text)
    {
        FromUser = fromUser;
        Text = text;
    }

    public bool FromUser { get; }
    public string Text { get; }
}

// A single action Pixel proposed in response to a chat message -- already
// validated server-side (chat.py's _sanitize_action) against a fixed,
// closed set of action types and, for suspend_process, against the real
// pids handed to the model. Never executes itself; PixelWindow shows a
// confirm control and only calls sysintel.exe's own `act` command -- the
// same one the CLI uses -- once the user clicks it.
internal sealed class ChatAction
{
    public ChatAction(string type, int? pid, string? level, string? targetName, string reason)
    {
        Type = type;
        Pid = pid;
        Level = level;
        TargetName = targetName;
        Reason = reason;
    }

    public string Type { get; }
    public int? Pid { get; }
    public string? Level { get; }
    public string? TargetName { get; }
    public string Reason { get; }
}

internal sealed class ChatReply
{
    public ChatReply(string answer, ChatAction? action)
    {
        Answer = answer;
        Action = action;
    }

    public string Answer { get; }
    public ChatAction? Action { get; }
}

// Bridges the WPF UI to `intelligence/main.py ask` -- the one place a user
// can type a free-form question and get an answer in Pixel's voice, grounded
// in a live sysintel.exe snapshot. Shells out per question (same pattern as
// TelemetryClient), rather than keeping a Python process resident, since a
// chat message is rare enough that a ~1-2s interpreter start doesn't matter.
internal sealed class PixelChatClient
{
    public string? PythonExePath { get; }
    public string? RepoRoot { get; }
    private readonly string? _sysIntelExePath;

    public bool IsAvailable => PythonExePath is not null && _sysIntelExePath is not null;

    // repoRoot and sysIntelExePath both come from wherever TelemetryClient
    // already found build/sysintel.exe, so chat and telemetry always agree
    // on which checkout they're talking to.
    public PixelChatClient(string? repoRoot, string? sysIntelExePath)
    {
        RepoRoot = repoRoot;
        _sysIntelExePath = sysIntelExePath;
        PythonExePath = repoRoot is null
            ? null
            : FileIfExists(Path.Combine(repoRoot, "intelligence", ".venv", "Scripts", "python.exe"));
    }

    private static string? FileIfExists(string path) => File.Exists(path) ? path : null;

    public async Task<ChatReply?> AskAsync(string question, IReadOnlyList<ChatTurn> history, TimeSpan timeout)
    {
        if (!IsAvailable)
        {
            return null;
        }

        var payloadBytes = Encoding.UTF8.GetBytes(BuildRequestJson(question, history));

        try
        {
            var startInfo = new ProcessStartInfo
            {
                FileName = PythonExePath,
                Arguments = $"-m intelligence.main ask --sysintel-exe \"{EscapeArg(_sysIntelExePath!)}\"",
                WorkingDirectory = RepoRoot,
                RedirectStandardInput = true,
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

            // Write/read raw UTF-8 bytes over the pipes' BaseStream rather
            // than through StandardInput/StandardOutput's StreamWriter --
            // under the csc.exe fallback build (true .NET Framework, no
            // StandardInputEncoding/StandardOutputEncoding available) those
            // default to the system codepage, which would mangle the curly
            // quotes and em dashes Pixel's voice actually uses.
            await process.StandardInput.BaseStream.WriteAsync(payloadBytes, 0, payloadBytes.Length);
            await process.StandardInput.BaseStream.FlushAsync();
            process.StandardInput.Close();

            using var stdoutBuffer = new MemoryStream();
            var copyTask = process.StandardOutput.BaseStream.CopyToAsync(stdoutBuffer);
            var stderrTask = process.StandardError.ReadToEndAsync();

            var exited = await Task.Run(() => process.WaitForExit((int)timeout.TotalMilliseconds)).ConfigureAwait(true);
            if (!exited)
            {
                TryKill(process);
                return null;
            }
            await copyTask.ConfigureAwait(true);
            await stderrTask.ConfigureAwait(true); // drained so a chatty child can't block on a full pipe buffer

            var output = Encoding.UTF8.GetString(stdoutBuffer.ToArray()).Trim();
            if (output.Length == 0)
            {
                return null;
            }

            var root = JsonValue.Parse(output);
            var answer = root["answer"].AsString();
            if (answer.Length == 0)
            {
                return null;
            }

            return new ChatReply(answer, ParseAction(root["proposed_action"]));
        }
        catch
        {
            // Missing python/venv, a killed process, JSON we didn't expect
            // -- all collapse to "no answer this time", handled by the
            // caller with an in-character apology, never a crash.
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
        }
    }

    private static ChatAction? ParseAction(JsonValue node)
    {
        if (node.IsNull)
        {
            return null;
        }
        var type = node["action_type"].AsString();
        if (type.Length == 0)
        {
            return null;
        }
        var pidNode = node["pid"];
        int? pid = pidNode.Kind == JsonKind.Number ? (int)pidNode.AsDouble() : null;
        var level = node["level"].AsString();
        var targetName = node["target_name"].AsString();
        var reason = node["reason"].AsString();
        return new ChatAction(type, pid, level.Length > 0 ? level : null, targetName.Length > 0 ? targetName : null, reason);
    }

    // internal (not private) -- PixelWindow.cs reuses this to escape the
    // reason/db-path arguments it builds when executing a confirmed action
    // via sysintel.exe's own `act` command, the exact same quoting rule.
    internal static string EscapeArg(string value) => value.Replace("\"", "\\\"");

    private static string BuildRequestJson(string question, IReadOnlyList<ChatTurn> history)
    {
        var sb = new StringBuilder();
        sb.Append("{\"question\":").Append(JsonString(question)).Append(",\"history\":[");
        var start = Math.Max(0, history.Count - 8);
        for (var i = start; i < history.Count; i++)
        {
            if (i > start)
            {
                sb.Append(',');
            }
            var turn = history[i];
            sb.Append("{\"role\":\"").Append(turn.FromUser ? "user" : "pixel").Append("\",\"text\":")
              .Append(JsonString(turn.Text)).Append('}');
        }
        sb.Append("]}");
        return sb.ToString();
    }

    // A small hand-rolled JSON string writer -- Json.cs only reads (that's
    // all sysintel.exe/intelligence's *replies* ever needed); this is the
    // one spot the UI constructs outbound JSON, and the shape is fixed and
    // tiny enough that pulling in a writer would be pure machinery.
    private static string JsonString(string value)
    {
        var sb = new StringBuilder(value.Length + 2);
        sb.Append('"');
        foreach (var c in value)
        {
            switch (c)
            {
                case '"': sb.Append("\\\""); break;
                case '\\': sb.Append("\\\\"); break;
                case '\n': sb.Append("\\n"); break;
                case '\r': sb.Append("\\r"); break;
                case '\t': sb.Append("\\t"); break;
                default:
                    if (c < 0x20)
                    {
                        sb.Append("\\u").Append(((int)c).ToString("x4"));
                    }
                    else
                    {
                        sb.Append(c);
                    }
                    break;
            }
        }
        sb.Append('"');
        return sb.ToString();
    }
}
