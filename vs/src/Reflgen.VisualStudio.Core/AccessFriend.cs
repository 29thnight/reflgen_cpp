using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace Reflgen.VisualStudio
{
    // 클래스 본문 맨 앞(Position)에 넣을 friend 선언.
    public sealed record FriendInsertion(string ClassName, int Position, string Text);

    // [[reflgen::reflect]] 클래스가 공개되지 않은 멤버를 반영하는데 `friend struct reflgen::access;` 가 없으면 넣을
    // 자리를 찾는다. 생성 코드는 클래스 밖(access::describe<T>)에서 멤버를 가리키므로 그 friend 가 있어야 한다.
    // 생성기의 RG0002 와 같은 규칙에 하나를 더한다:
    //   - 비정적 데이터 멤버([[reflgen::ignore]]·bit-field·참조는 반영되지 않으므로 뺀다)
    //   - [[reflgen::reflect]] 를 단 비정적 메서드
    //   - attribute 인자에서 쓰는 static 멤버(RG0002 는 잡지 않지만 생성 코드가 클래스 밖에서 쓰므로 컴파일이 깨진다)
    // 저장 직전에 돌므로 libclang 없이 텍스트로 가린다 — 놓친 것은 생성기가 RG0002 로 알린다.
    // friend 없이 private 멤버 포인터를 상수로 얻는 C++20 방법(명시적 인스턴스화의 접근 예외)은 MSVC 19.51 이
    // 상수 평가에서 받지 않는다(실측) — 그래서 이 한 줄은 남는다.
    public static class AccessFriend
    {
        private const string Declaration = "friend struct reflgen::access;";

        // class-key 와 그 뒤의 attribute 들, 이름. enum class 는 클래스가 아니다.
        private static readonly Regex ClassHead = new Regex(
            @"(?<!\benum\s+)\b(?<key>class|struct)\b(?<attributes>(?:\s*(?:\[\[.*?\]\]|alignas\s*\([^()]*\)))+)\s*" +
                @"(?<name>[A-Za-z_]\w*)",
            RegexOptions.Singleline | RegexOptions.CultureInvariant);
        private static readonly Regex AccessLabel =
            new Regex(@"\G\s*(public|protected|private)\b\s*:(?!:)", RegexOptions.CultureInvariant);
        private static readonly Regex ExistingFriend =
            new Regex(@"^friend\s+(?:(?:struct|class)\s+)?(?:::\s*)?reflgen\s*::\s*access$", RegexOptions.CultureInvariant);
        // 멤버가 아닌 선언 — 중첩 타입, 별칭, template, friend 등.
        private static readonly Regex NotAMember = new Regex(
            @"^(?:using|typedef|enum|template|static_assert|class|struct|union|friend)\b", RegexOptions.CultureInvariant);
        private static readonly Regex Static = new Regex(@"\bstatic\b", RegexOptions.CultureInvariant);
        private static readonly Regex LastIdentifier = new Regex(@"([A-Za-z_]\w*)\W*$", RegexOptions.CultureInvariant);
        private static readonly Regex OperatorName = new Regex(@"\boperator\b", RegexOptions.CultureInvariant);
        // 이 이름 뒤의 '(' 는 함수 매개변수가 아니다.
        private static readonly HashSet<string> NotFunctionNames =
            new HashSet<string>(StringComparer.Ordinal) { "decltype", "alignas", "noexcept", "sizeof", "alignof", "__declspec" };

        public static IReadOnlyList<FriendInsertion> Insertions(string text)
        {
            string code = CodeText.Blank(text, blankLiterals: true);
            var insertions = new List<FriendInsertion>();
            foreach (Match head in ClassHead.Matches(code))
            {
                if (!ReflectedHeader.HasDirective(head.Groups["attributes"].Value, "reflect"))
                {
                    continue;
                }
                int open = BodyStart(code, head.Index + head.Length);
                int close = open < 0 ? -1 : MatchingBrace(code, open, code.Length);
                if (close >= 0 && NeedsFriend(code, open + 1, close, isStruct: head.Groups["key"].Value == "struct"))
                {
                    insertions.Add(new FriendInsertion(head.Groups["name"].Value, open + 1,
                                                       InsertionText(text, head.Index, open)));
                }
            }
            return insertions;
        }

        // 본문을 여는 '{'. 선언만 있으면(';' 가 먼저) -1.
        private static int BodyStart(string code, int from)
        {
            int index = code.IndexOfAny(new[] { '{', ';' }, from);
            return index >= 0 && code[index] == '{' ? index : -1;
        }

        private static int MatchingBrace(string code, int open, int end)
        {
            int depth = 0;
            for (int i = open; i < end; ++i)
            {
                if (code[i] == '{')
                {
                    ++depth;
                }
                else if (code[i] == '}' && --depth == 0)
                {
                    return i;
                }
            }
            return -1;
        }

        // 본문 [start, end) 의 멤버를 차례로 읽으며 접근 구획을 따라간다.
        private static bool NeedsFriend(string code, int start, int end, bool isStruct)
        {
            bool isPublic = isStruct;
            bool hasFriend = false;
            bool needsFriend = false;
            var hiddenStatics = new List<string>();
            var attributes = new StringBuilder();
            int i = start;
            while (i < end)
            {
                Match label = AccessLabel.Match(code, i);
                if (label.Success && label.Index + label.Length <= end)
                {
                    isPublic = label.Groups[1].Value == "public";
                    i = label.Index + label.Length;
                    continue;
                }
                if (char.IsWhiteSpace(code[i]))
                {
                    ++i;
                    continue;
                }
                Member member = ReadMember(code, i, end);
                i = member.Next;
                attributes.Append(member.Attributes).Append(' ');
                hasFriend |= ExistingFriend.IsMatch(member.Head);
                if (isPublic || member.Head.Length == 0 || NotAMember.IsMatch(member.Head))
                {
                    continue;
                }
                needsFriend |= NeedsAccess(member, hiddenStatics);
            }
            string attributeText = attributes.ToString();
            needsFriend |= hiddenStatics.Any(name => Regex.IsMatch(attributeText, $@"(?<![A-Za-z0-9_]){name}(?![A-Za-z0-9_])"));
            return needsFriend && !hasFriend;
        }

        // 공개되지 않은 멤버 하나가 생성 코드에서 쓰이는가. static 데이터 멤버는 attribute 인자에서 쓰일 때만
        // 필요하므로 이름을 모아 두었다가 본문을 다 읽은 뒤에 본다.
        private static bool NeedsAccess(Member member, List<string> hiddenStatics)
        {
            bool isStatic = Static.IsMatch(member.Head);
            if (member.IsFunction)
            {
                return !isStatic && ReflectedHeader.HasDirective(member.Attributes, "reflect");
            }
            if (isStatic)
            {
                Match name = LastIdentifier.Match(member.Head);
                if (name.Success)
                {
                    hiddenStatics.Add(name.Groups[1].Value);
                }
                return false;
            }
            return !member.IsBitField && !member.IsReference && !ReflectedHeader.HasDirective(member.Attributes, "ignore");
        }

        // 멤버 선언 하나. Head 는 attribute 와 초기화 식·본문을 뺀 선언부다.
        private sealed record Member(int Next, string Head, string Attributes, bool IsFunction, bool IsBitField,
                                     bool IsReference);

        // start 에서 멤버 선언 하나를 읽는다 — ';' 에서, 또는 함수 본문의 '}' 에서 끝난다.
        private static Member ReadMember(string code, int start, int end)
        {
            var reader = new MemberReader(code, end);
            return reader.Read(start);
        }

        private sealed class MemberReader
        {
            private readonly string _code;
            private readonly int _end;
            private readonly StringBuilder _head = new StringBuilder();
            private readonly StringBuilder _attributes = new StringBuilder();
            private int _parentheses;
            private int _angles;
            private bool _initializer;
            private bool _function;
            private bool _bitField;
            private bool _reference;

            public MemberReader(string code, int end)
            {
                _code = code;
                _end = end;
            }

            public Member Read(int i)
            {
                while (i < _end)
                {
                    char c = _code[i];
                    if (c == '[' && i + 1 < _end && _code[i + 1] == '[')
                    {
                        i = TakeAttribute(i);
                    }
                    else if (c == ';' && _parentheses == 0)
                    {
                        return Finish(i + 1);
                    }
                    else if (c == '{' && _parentheses == 0)
                    {
                        int close = MatchingBrace(_code, i, _end);
                        i = close < 0 ? _end : close + 1;
                        // 함수 본문으로 끝난다. 선언 없이 시작한 블록(생성자 초기화 목록 `y{2} {}` 의 뒤쪽)도
                        // 여기서 끊어야 다음 멤버를 삼키지 않는다.
                        if (_function || (_head.Length == 0 && _attributes.Length == 0))
                        {
                            return Finish(i);
                        }
                        _initializer |= !NotAMember.IsMatch(_head.ToString().Trim()); // 중괄호 초기화
                    }
                    else
                    {
                        Track(c, i);
                        ++i;
                    }
                }
                return Finish(_end);
            }

            private int TakeAttribute(int i)
            {
                int close = _code.IndexOf("]]", i + 2, StringComparison.Ordinal);
                int stop = close < 0 || close + 2 > _end ? _end : close + 2;
                _attributes.Append(_code, i, stop - i).Append(' ');
                return stop;
            }

            // 선언부(초기화 식 앞)에서만 모양을 가린다: 함수 괄호, template 꺾쇠, bit-field ':', 참조 '&'.
            private void Track(char c, int i)
            {
                // operator==, operator<, operator() — 이름 속 기호는 모양이 아니다. 다음 '(' 가 매개변수다.
                if (!_initializer && _parentheses == 0 && !_function && OperatorName.IsMatch(_head.ToString()))
                {
                    if (c == '(')
                    {
                        _function = true;
                        ++_parentheses;
                    }
                    else
                    {
                        _head.Append(c);
                    }
                    return;
                }
                if (c == '(')
                {
                    if (_parentheses == 0 && _angles == 0 && !_initializer && !NotFunctionNames.Contains(PrecedingWord()))
                    {
                        _function = true;
                    }
                    ++_parentheses;
                }
                else if (c == ')')
                {
                    _parentheses = Math.Max(0, _parentheses - 1);
                }
                if (_initializer || _parentheses > 0)
                {
                    return;
                }
                if (c == '<')
                {
                    ++_angles;
                }
                else if (c == '>')
                {
                    _angles = Math.Max(0, _angles - 1);
                }
                else if (c == '=' && _angles == 0)
                {
                    _initializer = true;
                    return;
                }
                else if (c == ':' && _angles == 0 && !IsScope(i))
                {
                    _bitField = true;
                }
                else if (c == '&' && _angles == 0)
                {
                    _reference = true;
                }
                _head.Append(c);
            }

            private bool IsScope(int i) =>
                (i + 1 < _end && _code[i + 1] == ':') || (i > 0 && _code[i - 1] == ':');

            private string PrecedingWord()
            {
                Match word = LastIdentifier.Match(_head.ToString());
                return word.Success && word.Index + word.Length == _head.Length ? word.Groups[1].Value : string.Empty;
            }

            private Member Finish(int next) =>
                new Member(next, _head.ToString().Trim(), _attributes.ToString(), _function, _bitField && !_function,
                           _reference && !_function);
        }

        // 넣을 텍스트. '{' 가 줄 끝이면 다음 줄에 멤버 들여쓰기로 넣고 빈 줄 하나를 둔다. 한 줄짜리 클래스면
        // 그 줄 안에 넣는다. 개행은 파일이 쓰는 것을 따른다.
        private static string InsertionText(string text, int classStart, int open)
        {
            string newline = text.Contains("\r\n") ? "\r\n" : "\n";
            int lineEnd = text.IndexOf('\n', open);
            lineEnd = lineEnd < 0 ? text.Length : lineEnd;
            if (text.Substring(open + 1, lineEnd - open - 1).Trim().Length != 0)
            {
                return " " + Declaration;
            }
            string classIndent = IndentAt(text, text.LastIndexOf('\n', classStart) + 1);
            string? memberIndent = NextMemberIndent(text, lineEnd + 1);
            string indent = memberIndent != null && memberIndent.Length > classIndent.Length
                                ? memberIndent
                                : classIndent + (classIndent.Contains("\t") ? "\t" : "    ");
            // '{' 다음 줄이 이미 비어 있으면 그 빈 줄을 friend 뒤의 빈 줄로 쓴다.
            return newline + indent + Declaration + (IsBlankLine(text, lineEnd + 1) ? string.Empty : newline);
        }

        private static bool IsBlankLine(string text, int lineStart)
        {
            int first = lineStart + IndentAt(text, lineStart).Length;
            return lineStart < text.Length && (first >= text.Length || text[first] == '\r' || text[first] == '\n');
        }

        private static string IndentAt(string text, int lineStart)
        {
            int i = lineStart;
            while (i < text.Length && (text[i] == ' ' || text[i] == '\t'))
            {
                ++i;
            }
            return text.Substring(lineStart, i - lineStart);
        }

        // 다음 비어 있지 않은 줄의 들여쓰기. 그 줄이 '}' 로 시작하면(빈 본문) null.
        private static string? NextMemberIndent(string text, int lineStart)
        {
            while (lineStart < text.Length)
            {
                string indent = IndentAt(text, lineStart);
                int first = lineStart + indent.Length;
                if (first < text.Length && text[first] != '\r' && text[first] != '\n')
                {
                    return text[first] == '}' ? null : indent;
                }
                int next = text.IndexOf('\n', lineStart);
                if (next < 0)
                {
                    break;
                }
                lineStart = next + 1;
            }
            return null;
        }
    }
}
