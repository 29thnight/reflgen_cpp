using System;
using System.Text;

namespace Reflgen.VisualStudio
{
    // CodeText.Scan 의 결과. Text 는 원문과 길이가 같다.
    public sealed record ScannedCode(string Text, bool EndsInCode);

    // 주석(과 선택하면 문자열·문자 리터럴의 내용)을 같은 길이의 공백으로 바꾼다 — 텍스트 검색이 주석이나
    // 문자열 안의 표기에 걸리지 않게 하면서 위치는 그대로 두려는 것이다. 개행은 남긴다.
    // C++ 파서가 아니라 편집 중의 덜 쓴 코드에서도 돌아야 하는 작은 lexer 다. 다루는 것: //, /* */,
    // "…", '…'(접두 L·u·U·u8), raw 문자열 R"delim(…)delim", 숫자 자릿수 구분자(1'000).
    public static class CodeText
    {
        private const int MaxRawDelimiter = 16; // 표준의 한도

        public static string Blank(string text, bool blankLiterals) => Scan(text, blankLiterals).Text;

        // EndsInCode 는 텍스트 끝이 주석·리터럴 밖인지다 — 편집기 커서가 코드 위에 있는지 가를 때 쓴다.
        public static ScannedCode Scan(string text, bool blankLiterals)
        {
            var result = new StringBuilder(text);
            bool endsInCode = true;
            int i = 0;
            while (i < text.Length)
            {
                char c = text[i];
                char next = i + 1 < text.Length ? text[i + 1] : '\0';
                int end;
                if (c == '/' && next == '/')
                {
                    int newline = text.IndexOf('\n', i);
                    end = newline < 0 ? text.Length : newline;
                    endsInCode = newline >= 0;
                    Fill(result, i, end);
                }
                else if (c == '/' && next == '*')
                {
                    int close = text.IndexOf("*/", i + 2, StringComparison.Ordinal);
                    end = close < 0 ? text.Length : close + 2;
                    endsInCode = close >= 0;
                    Fill(result, i, end);
                }
                else if (c == '"' && IsRawStringStart(text, i))
                {
                    end = RawStringEnd(text, i, out bool closed);
                    endsInCode = closed;
                    if (blankLiterals)
                    {
                        Fill(result, i + 1, closed ? end - 1 : end);
                    }
                }
                else if (c == '"' || (c == '\'' && !IsDigitSeparator(text, i)))
                {
                    end = QuotedEnd(text, i, c, out int contentEnd, out bool closed);
                    endsInCode = closed;
                    if (blankLiterals)
                    {
                        Fill(result, i + 1, contentEnd);
                    }
                }
                else
                {
                    end = i + 1;
                    endsInCode = true;
                }
                i = end;
            }
            return new ScannedCode(result.ToString(), endsInCode);
        }

        // 닫는 따옴표 바로 뒤. 줄이 끝나면 닫힌 것으로 본다(덜 쓴 리터럴이 파일 끝까지 번지지 않게).
        // contentEnd 는 내용의 끝(닫는 따옴표나 개행의 자리)이다.
        private static int QuotedEnd(string text, int open, char quote, out int contentEnd, out bool closed)
        {
            int i = open + 1;
            while (i < text.Length && text[i] != quote && text[i] != '\n')
            {
                i += text[i] == '\\' ? 2 : 1;
            }
            contentEnd = Math.Min(i, text.Length);
            closed = i < text.Length;
            return closed && text[i] == quote ? i + 1 : contentEnd;
        }

        // R"delim( … )delim" — 안의 따옴표·역슬래시·개행은 모두 내용이다.
        private static int RawStringEnd(string text, int quote, out bool closed)
        {
            int open = text.IndexOf('(', quote + 1);
            string delimiter = text.Substring(quote + 1, open - quote - 1);
            int close = text.IndexOf(")" + delimiter + "\"", open + 1, StringComparison.Ordinal);
            closed = close >= 0;
            return closed ? close + delimiter.Length + 2 : text.Length;
        }

        // 따옴표 앞이 R, u8R, uR, UR, LR 이고, 따옴표 뒤에 괄호 전까지 올바른 구분자가 있는가.
        private static bool IsRawStringStart(string text, int quote)
        {
            string prefix = PrecedingWord(text, quote);
            if (prefix != "R" && prefix != "u8R" && prefix != "uR" && prefix != "UR" && prefix != "LR")
            {
                return false;
            }
            for (int i = quote + 1; i < text.Length && i <= quote + 1 + MaxRawDelimiter; ++i)
            {
                char c = text[i];
                if (c == '(')
                {
                    return true;
                }
                if (char.IsWhiteSpace(c) || c == ')' || c == '\\' || c == '"')
                {
                    return false;
                }
            }
            return false;
        }

        // 1'000, 0xFF'FF 의 ' 는 숫자 안의 구분자다. L'x', u8'x' 같은 접두 문자 리터럴과 가른다.
        private static bool IsDigitSeparator(string text, int quote)
        {
            string word = PrecedingWord(text, quote);
            return word.Length > 0 && char.IsDigit(word[0]);
        }

        // position 바로 앞에 붙은 식별자·숫자 글자들.
        private static string PrecedingWord(string text, int position)
        {
            int start = position;
            while (start > 0 && (char.IsLetterOrDigit(text[start - 1]) || text[start - 1] == '_'))
            {
                --start;
            }
            return text.Substring(start, position - start);
        }

        private static void Fill(StringBuilder text, int begin, int end)
        {
            for (int i = begin; i < end && i < text.Length; ++i)
            {
                if (text[i] != '\n' && text[i] != '\r')
                {
                    text[i] = ' ';
                }
            }
        }
    }
}
