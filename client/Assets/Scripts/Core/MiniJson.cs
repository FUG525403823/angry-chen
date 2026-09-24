using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Ac.Core
{
    // C02 §3：受限 JSON 读取器。只覆盖 ADR-010 的 fixture 子集与 §5.6 的角度表：
    // 对象、数组、字符串、数字、true/false/null。
    // 明确不支持：注释、尾随内容、前导零、原始控制字符、落单的 surrogate 转义。
    public enum JsonKind
    {
        Null = 0,
        Bool = 1,
        Number = 2,
        String = 3,
        Array = 4,
        Object = 5,
    }

    public sealed class JsonException : Exception
    {
        public JsonException(string message) : base(message) { }
    }

    public sealed class JsonValue
    {
        private readonly JsonKind _kind;
        private readonly bool _bool;
        private readonly double _number;
        private readonly string _text;
        private readonly List<JsonValue> _items;
        private readonly List<KeyValuePair<string, JsonValue>> _members;

        private JsonValue(JsonKind kind, bool boolValue, double number, string text, List<JsonValue> items,
            List<KeyValuePair<string, JsonValue>> members)
        {
            _kind = kind;
            _bool = boolValue;
            _number = number;
            _text = text;
            _items = items;
            _members = members;
        }

        internal static JsonValue FromBool(bool value) { return new JsonValue(JsonKind.Bool, value, 0.0, null, null, null); }
        internal static JsonValue FromNumber(double value) { return new JsonValue(JsonKind.Number, false, value, null, null, null); }
        internal static JsonValue FromString(string value) { return new JsonValue(JsonKind.String, false, 0.0, value, null, null); }
        internal static JsonValue FromArray(List<JsonValue> items) { return new JsonValue(JsonKind.Array, false, 0.0, null, items, null); }
        internal static JsonValue FromObject(List<KeyValuePair<string, JsonValue>> members) { return new JsonValue(JsonKind.Object, false, 0.0, null, null, members); }
        internal static JsonValue Null() { return new JsonValue(JsonKind.Null, false, 0.0, null, null, null); }

        public JsonKind Kind { get { return _kind; } }

        public int Count { get { return _kind == JsonKind.Array ? _items.Count : (_members == null ? 0 : _members.Count); } }

        public JsonValue this[int index]
        {
            get
            {
                Require(JsonKind.Array, "数组");
                return _items[index];
            }
        }

        // 对象成员保持插入序（ADR-010 §4：禁止依赖哈希容器迭代序）。
        public KeyValuePair<string, JsonValue> MemberAt(int index)
        {
            Require(JsonKind.Object, "对象");
            return _members[index];
        }

        public bool TryGet(string key, out JsonValue value)
        {
            Require(JsonKind.Object, "对象");
            for (var i = 0; i < _members.Count; i++)
            {
                if (_members[i].Key == key)
                {
                    value = _members[i].Value;
                    return true;
                }
            }
            value = null;
            return false;
        }

        public JsonValue Get(string key)
        {
            JsonValue value;
            if (!TryGet(key, out value)) throw new JsonException("对象里缺少键：" + key);
            return value;
        }

        public bool AsBool()
        {
            Require(JsonKind.Bool, "布尔");
            return _bool;
        }

        public string AsString()
        {
            Require(JsonKind.String, "字符串");
            return _text;
        }

        public double AsDouble()
        {
            Require(JsonKind.Number, "数字");
            return _number;
        }

        public int AsInt()
        {
            var value = AsDouble();
            if (value != Math.Floor(value) || value < int.MinValue || value > int.MaxValue)
            {
                throw new JsonException("不是 int32：" + value.ToString("R", CultureInfo.InvariantCulture));
            }
            return (int)value;
        }

        public long AsLong()
        {
            var value = AsDouble();
            if (value != Math.Floor(value) || value < -9007199254740992.0 || value > 9007199254740992.0)
            {
                throw new JsonException("不是整数：" + value.ToString("R", CultureInfo.InvariantCulture));
            }
            return (long)value;
        }

        private void Require(JsonKind kind, string what)
        {
            if (_kind != kind) throw new JsonException("期望" + what + "，实际 " + _kind);
        }
    }

    public static class MiniJson
    {
        public static JsonValue Parse(string text)
        {
            if (text == null) throw new JsonException("输入为 null");
            var parser = new Parser(text);
            var value = parser.ParseValue();
            parser.SkipWhitespace();
            if (!parser.AtEnd) throw parser.Error("JSON 结束后仍有内容");
            return value;
        }

        private sealed class Parser
        {
            private readonly string _text;
            private int _index;

            internal Parser(string text) { _text = text; }

            internal bool AtEnd { get { return _index >= _text.Length; } }

            internal JsonException Error(string message)
            {
                return new JsonException(message + "（偏移 " + _index + "）");
            }

            internal void SkipWhitespace()
            {
                while (_index < _text.Length)
                {
                    var c = _text[_index];
                    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { _index++; continue; }
                    break;
                }
            }

            internal JsonValue ParseValue()
            {
                SkipWhitespace();
                if (AtEnd) throw Error("意外结束");
                var c = _text[_index];
                switch (c)
                {
                    case '{': return ParseObject();
                    case '[': return ParseArray();
                    case '"': return JsonValue.FromString(ParseString());
                    case 't': Expect("true"); return JsonValue.FromBool(true);
                    case 'f': Expect("false"); return JsonValue.FromBool(false);
                    case 'n': Expect("null"); return JsonValue.Null();
                    default:
                        if (c == '-' || (c >= '0' && c <= '9')) return JsonValue.FromNumber(ParseNumber());
                        throw Error("非法字符 '" + c + "'（本读取器不支持注释）");
                }
            }

            private void Expect(string literal)
            {
                if (_index + literal.Length > _text.Length || string.CompareOrdinal(_text, _index, literal, 0, literal.Length) != 0)
                {
                    throw Error("期望字面量 " + literal);
                }
                _index += literal.Length;
            }

            private JsonValue ParseObject()
            {
                _index++;
                var members = new List<KeyValuePair<string, JsonValue>>();
                SkipWhitespace();
                if (!AtEnd && _text[_index] == '}') { _index++; return JsonValue.FromObject(members); }
                while (true)
                {
                    SkipWhitespace();
                    if (AtEnd || _text[_index] != '"') throw Error("对象键必须是字符串");
                    var key = ParseString();
                    SkipWhitespace();
                    if (AtEnd || _text[_index] != ':') throw Error("对象键后缺少 ':'");
                    _index++;
                    members.Add(new KeyValuePair<string, JsonValue>(key, ParseValue()));
                    SkipWhitespace();
                    if (AtEnd) throw Error("对象没有闭合");
                    if (_text[_index] == ',') { _index++; continue; }
                    if (_text[_index] == '}') { _index++; return JsonValue.FromObject(members); }
                    throw Error("对象里出现非法字符 '" + _text[_index] + "'");
                }
            }

            private JsonValue ParseArray()
            {
                _index++;
                var items = new List<JsonValue>();
                SkipWhitespace();
                if (!AtEnd && _text[_index] == ']') { _index++; return JsonValue.FromArray(items); }
                while (true)
                {
                    items.Add(ParseValue());
                    SkipWhitespace();
                    if (AtEnd) throw Error("数组没有闭合");
                    if (_text[_index] == ',') { _index++; continue; }
                    if (_text[_index] == ']') { _index++; return JsonValue.FromArray(items); }
                    throw Error("数组里出现非法字符 '" + _text[_index] + "'");
                }
            }

            private string ParseString()
            {
                _index++;
                var builder = new StringBuilder();
                while (true)
                {
                    if (AtEnd) throw Error("字符串没有闭合");
                    var c = _text[_index];
                    if (c == '"') { _index++; return builder.ToString(); }
                    if (c < ' ') throw Error("字符串里有未转义的控制字符");
                    if (c != '\\') { builder.Append(c); _index++; continue; }

                    _index++;
                    if (AtEnd) throw Error("转义符后意外结束");
                    var escape = _text[_index];
                    _index++;
                    switch (escape)
                    {
                        case '"': builder.Append('"'); break;
                        case '\\': builder.Append('\\'); break;
                        case '/': builder.Append('/'); break;
                        case 'b': builder.Append('\b'); break;
                        case 'f': builder.Append('\f'); break;
                        case 'n': builder.Append('\n'); break;
                        case 'r': builder.Append('\r'); break;
                        case 't': builder.Append('\t'); break;
                        case 'u': builder.Append(ParseUnicodeEscape()); break;
                        default: throw Error("非法转义 \\" + escape);
                    }
                }
            }

            // 只接受成对的 surrogate：落单的一律判错（C02 §3「无 surrogate」）。
            private string ParseUnicodeEscape()
            {
                var first = ReadHex4();
                if (first >= 0xDC00 && first <= 0xDFFF) throw Error("落单的低位 surrogate");
                if (first < 0xD800 || first > 0xDBFF) return ((char)first).ToString();
                if (_index + 1 >= _text.Length || _text[_index] != '\\' || _text[_index + 1] != 'u')
                {
                    throw Error("高位 surrogate 后必须是配对的 \\uXXXX");
                }
                _index += 2;
                var second = ReadHex4();
                if (second < 0xDC00 || second > 0xDFFF) throw Error("高位 surrogate 后的低位 surrogate 非法");
                return new string(new[] { (char)first, (char)second });
            }

            private int ReadHex4()
            {
                if (_index + 4 > _text.Length) throw Error("\\u 转义不足 4 位");
                var value = 0;
                for (var i = 0; i < 4; i++)
                {
                    var c = _text[_index + i];
                    int digit;
                    if (c >= '0' && c <= '9') digit = c - '0';
                    else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                    else throw Error("\\u 转义里有非十六进制字符 '" + c + "'");
                    value = value * 16 + digit;
                }
                _index += 4;
                return value;
            }

            private double ParseNumber()
            {
                var start = _index;
                if (!AtEnd && _text[_index] == '-') _index++;
                if (AtEnd) throw Error("数字不完整");
                if (_text[_index] == '0')
                {
                    _index++;
                }
                else if (_text[_index] >= '1' && _text[_index] <= '9')
                {
                    while (!AtEnd && _text[_index] >= '0' && _text[_index] <= '9') _index++;
                }
                else
                {
                    throw Error("数字前导零或缺少整数位");
                }

                if (!AtEnd && _text[_index] == '.')
                {
                    _index++;
                    if (AtEnd || _text[_index] < '0' || _text[_index] > '9') throw Error("小数点后必须有数字");
                    while (!AtEnd && _text[_index] >= '0' && _text[_index] <= '9') _index++;
                }

                if (!AtEnd && (_text[_index] == 'e' || _text[_index] == 'E'))
                {
                    _index++;
                    if (!AtEnd && (_text[_index] == '+' || _text[_index] == '-')) _index++;
                    if (AtEnd || _text[_index] < '0' || _text[_index] > '9') throw Error("指数必须有数字");
                    while (!AtEnd && _text[_index] >= '0' && _text[_index] <= '9') _index++;
                }

                var literal = _text.Substring(start, _index - start);
                double value;
                if (!double.TryParse(literal, NumberStyles.Float, CultureInfo.InvariantCulture, out value))
                {
                    throw Error("数字无法解析：" + literal);
                }
                return value;
            }
        }
    }
}
