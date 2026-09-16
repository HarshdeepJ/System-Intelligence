using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace SystemIntelligence.PixelMini;

internal enum JsonKind
{
    Null,
    Bool,
    Number,
    String,
    Array,
    Object
}

// A minimal hand-rolled JSON reader mirroring core/util/json.hpp on the C++
// side: sysintel.exe's output has a small, fixed shape, so a real JSON
// package (unreachable anyway from the csc.exe-only fallback build, which
// has no System.Text.Json) is machinery this doesn't need.
internal sealed class JsonValue
{
    public static readonly JsonValue Null = new();

    private readonly bool _bool;
    private readonly double _number;
    private readonly string? _string;
    private readonly List<JsonValue>? _array;
    private readonly Dictionary<string, JsonValue>? _object;

    private JsonValue() { Kind = JsonKind.Null; }
    private JsonValue(bool value) { Kind = JsonKind.Bool; _bool = value; }
    private JsonValue(double value) { Kind = JsonKind.Number; _number = value; }
    private JsonValue(string value) { Kind = JsonKind.String; _string = value; }
    private JsonValue(List<JsonValue> value) { Kind = JsonKind.Array; _array = value; }
    private JsonValue(Dictionary<string, JsonValue> value) { Kind = JsonKind.Object; _object = value; }

    public JsonKind Kind { get; }
    public bool IsNull => Kind == JsonKind.Null;

    public JsonValue this[string key] =>
        Kind == JsonKind.Object && _object!.TryGetValue(key, out var value) ? value : Null;

    public IReadOnlyList<JsonValue> Items => (IReadOnlyList<JsonValue>?)_array ?? Array.Empty<JsonValue>();

    public double AsDouble(double fallback = 0) => Kind == JsonKind.Number ? _number : fallback;
    public ulong AsUInt64(ulong fallback = 0) => Kind == JsonKind.Number ? (ulong)_number : fallback;
    public bool AsBool(bool fallback = false) => Kind == JsonKind.Bool ? _bool : fallback;
    public string AsString(string fallback = "") => Kind == JsonKind.String ? _string! : fallback;

    // Every Reading<T> the C++ side emits crosses the wire as
    // {"value": ... | null, "availability": "ok"|"unavailable"|"unsupported"|"error"}.
    // Only "ok" ever carries a usable value -- everything else must stay
    // distinguishable from a real zero, never collapsed into one.
    public bool ReadingOk => Kind == JsonKind.Object && this["availability"].AsString() == "ok" && !this["value"].IsNull;
    public double? ReadingDoubleOrNull() => ReadingOk ? this["value"].AsDouble() : null;

    public static JsonValue Parse(string text)
    {
        var i = 0;
        var value = ParseValue(text, ref i);
        return value;
    }

    private static JsonValue ParseValue(string s, ref int i)
    {
        SkipWhitespace(s, ref i);
        if (i >= s.Length)
        {
            throw new FormatException("Unexpected end of JSON input.");
        }

        return s[i] switch
        {
            '{' => ParseObject(s, ref i),
            '[' => ParseArray(s, ref i),
            '"' => new JsonValue(ParseString(s, ref i)),
            't' => ParseLiteral(s, ref i, "true", new JsonValue(true)),
            'f' => ParseLiteral(s, ref i, "false", new JsonValue(false)),
            'n' => ParseLiteral(s, ref i, "null", Null),
            _ => ParseNumber(s, ref i)
        };
    }

    private static JsonValue ParseObject(string s, ref int i)
    {
        var result = new Dictionary<string, JsonValue>(StringComparer.Ordinal);
        i++; // consume '{'
        SkipWhitespace(s, ref i);
        if (i < s.Length && s[i] == '}')
        {
            i++;
            return new JsonValue(result);
        }
        while (true)
        {
            SkipWhitespace(s, ref i);
            var key = ParseString(s, ref i);
            SkipWhitespace(s, ref i);
            Expect(s, ref i, ':');
            var value = ParseValue(s, ref i);
            result[key] = value;
            SkipWhitespace(s, ref i);
            if (i < s.Length && s[i] == ',')
            {
                i++;
                continue;
            }
            Expect(s, ref i, '}');
            break;
        }
        return new JsonValue(result);
    }

    private static JsonValue ParseArray(string s, ref int i)
    {
        var result = new List<JsonValue>();
        i++; // consume '['
        SkipWhitespace(s, ref i);
        if (i < s.Length && s[i] == ']')
        {
            i++;
            return new JsonValue(result);
        }
        while (true)
        {
            var value = ParseValue(s, ref i);
            result.Add(value);
            SkipWhitespace(s, ref i);
            if (i < s.Length && s[i] == ',')
            {
                i++;
                continue;
            }
            Expect(s, ref i, ']');
            break;
        }
        return new JsonValue(result);
    }

    private static string ParseString(string s, ref int i)
    {
        Expect(s, ref i, '"');
        var sb = new StringBuilder();
        while (true)
        {
            if (i >= s.Length)
            {
                throw new FormatException("Unterminated JSON string.");
            }
            var c = s[i++];
            if (c == '"')
            {
                break;
            }
            if (c == '\\')
            {
                if (i >= s.Length)
                {
                    throw new FormatException("Unterminated JSON escape.");
                }
                var escape = s[i++];
                sb.Append(escape switch
                {
                    '"' => '"',
                    '\\' => '\\',
                    '/' => '/',
                    'n' => '\n',
                    'r' => '\r',
                    't' => '\t',
                    'b' => '\b',
                    'f' => '\f',
                    'u' => ParseUnicodeEscape(s, ref i),
                    _ => throw new FormatException($"Unknown JSON escape '\\{escape}'.")
                });
                continue;
            }
            sb.Append(c);
        }
        return sb.ToString();
    }

    private static char ParseUnicodeEscape(string s, ref int i)
    {
        if (i + 4 > s.Length)
        {
            throw new FormatException("Truncated \\u escape.");
        }
        var hex = s.Substring(i, 4);
        i += 4;
        return (char)ushort.Parse(hex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture);
    }

    private static JsonValue ParseNumber(string s, ref int i)
    {
        var start = i;
        if (i < s.Length && (s[i] == '-' || s[i] == '+'))
        {
            i++;
        }
        while (i < s.Length && (char.IsDigit(s[i]) || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-'))
        {
            i++;
        }
        var slice = s.Substring(start, i - start);
        if (slice.Length == 0)
        {
            throw new FormatException($"Invalid JSON number at position {start}.");
        }
        return new JsonValue(double.Parse(slice, CultureInfo.InvariantCulture));
    }

    private static JsonValue ParseLiteral(string s, ref int i, string literal, JsonValue value)
    {
        if (i + literal.Length > s.Length || s.Substring(i, literal.Length) != literal)
        {
            throw new FormatException($"Expected '{literal}' at position {i}.");
        }
        i += literal.Length;
        return value;
    }

    private static void Expect(string s, ref int i, char expected)
    {
        if (i >= s.Length || s[i] != expected)
        {
            throw new FormatException($"Expected '{expected}' at position {i}.");
        }
        i++;
    }

    private static void SkipWhitespace(string s, ref int i)
    {
        while (i < s.Length && char.IsWhiteSpace(s[i]))
        {
            i++;
        }
    }
}
