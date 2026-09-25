using System.Linq;
using Xunit;

namespace Reflgen.VisualStudio.Tests
{
    public sealed class AccessFriendTests
    {
        private const string Friend = "friend struct reflgen::access;";

        private static string Apply(string text) =>
            AccessFriend.Insertions(text)
                        .OrderByDescending(insertion => insertion.Position)
                        .Aggregate(text, (current, insertion) => current.Insert(insertion.Position, insertion.Text));

        [Fact]
        public void Insertions_AddTheFriendFirstInAClassWithAPrivateField()
        {
            string text = "namespace game\n{\n    class [[reflgen::reflect(\"game.player\")]] player\n    {\n" +
                          "        int secret_ = 7;\n\n      public:\n        int hp = 100;\n    };\n}\n";

            string result = Apply(text);

            Assert.Contains("    {\n        " + Friend + "\n\n        int secret_ = 7;", result);
            Assert.Equal("player", AccessFriend.Insertions(text).Single().ClassName);
        }

        [Fact]
        public void Insertions_SeesPrivateAndProtectedSectionsOfAStruct()
        {
            Assert.Single(AccessFriend.Insertions("struct [[reflgen::reflect]] a\n{\n  private:\n    int x;\n};\n"));
            Assert.Single(AccessFriend.Insertions("struct [[reflgen::reflect]] a\n{\n  protected:\n    int x;\n};\n"));
        }

        [Theory]
        [InlineData("struct [[reflgen::reflect]] a\n{\n    int x;\n  private:\n    void helper();\n};\n")]
        [InlineData("class [[reflgen::reflect]] a\n{\n  public:\n    int x;\n};\n")]
        [InlineData("class [[reflgen::reflect]] a\n{\n    [[reflgen::ignore]] int cache;\n  public:\n    int x;\n};\n")]
        [InlineData("class [[reflgen::reflect]] a\n{\n    int bits : 3;\n    int& reference;\n  public:\n    int x;\n};\n")]
        [InlineData("class [[reflgen::reflect]] a\n{\n    static constexpr int limit = 3;\n  public:\n    int x;\n};\n")]
        [InlineData("class [[reflgen::reflect]] a\n{\n    using alias = int;\n    enum class kind { one };\n    a() = default;\n" +
                    "  public:\n    int x;\n};\n")]
        [InlineData("class plain\n{\n    int x;\n};\n")]
        [InlineData("enum class [[reflgen::reflect]] element\n{\n    fire,\n    water\n};\n")]
        [InlineData("class [[reflgen::reflect]] a;\n")]
        public void Insertions_LeaveClassesThatReflectNothingPrivate(string text)
        {
            Assert.Empty(AccessFriend.Insertions(text));
        }

        [Theory]
        [InlineData("friend struct reflgen::access;")]
        [InlineData("friend class ::reflgen::access;")]
        [InlineData("friend reflgen :: access;")]
        public void Insertions_RespectAnExistingFriend(string friendLine)
        {
            string text = "class [[reflgen::reflect]] a\n{\n    " + friendLine + "\n    int secret_;\n};\n";

            Assert.Empty(AccessFriend.Insertions(text));
        }

        [Fact]
        public void Insertions_CountOnlyReflectedPrivateMethods()
        {
            string reflected = "struct [[reflgen::reflect]] a\n{\n  private:\n    [[reflgen::reflect]] int bump(int by) { return by; }\n};\n";
            string plain = "struct [[reflgen::reflect]] a\n{\n  private:\n    int bump(int by) { return by; }\n};\n";

            Assert.Single(AccessFriend.Insertions(reflected));
            Assert.Empty(AccessFriend.Insertions(plain));
        }

        // RG0002 는 잡지 않지만 컴파일이 깨지는 경우 — 생성 코드가 attribute 인자를 클래스 밖에서 쓴다.
        [Fact]
        public void Insertions_CountPrivateStaticsUsedInAttributeArguments()
        {
            string text = "struct [[reflgen::reflect]] a\n{\n    [[reflgen::range(0, max_hp)]] int hp = 1;\n\n" +
                          "  private:\n    static constexpr int max_hp = 999;\n};\n";

            Assert.Single(AccessFriend.Insertions(text));
        }

        [Fact]
        public void Insertions_TreatTrickyDeclarationsAsDataMembers()
        {
            Assert.Single(AccessFriend.Insertions("class [[reflgen::reflect]] a\n{\n    decltype(0) value;\n};\n"));
            Assert.Single(AccessFriend.Insertions("class [[reflgen::reflect]] a\n{\n    std::function<void(int)> callback;\n};\n"));
            Assert.Single(AccessFriend.Insertions("class [[reflgen::reflect]] a\n{\n    int value = compute(1);\n};\n"));
        }

        [Fact]
        public void Insertions_ReadOperatorsAndConstructorInitializersAsFunctions()
        {
            // operator== 의 두 번째 '=' 는 초기화 식이 아니다.
            Assert.Empty(AccessFriend.Insertions(
                "class [[reflgen::reflect]] a\n{\n    bool operator==(const a&) const;\n  public:\n    int x;\n};\n"));
            // 초기화 목록의 y{2} 뒤 본문이 다음 멤버(hidden_)를 삼키지 않는다.
            Assert.Single(AccessFriend.Insertions(
                "struct [[reflgen::reflect]] a\n{\n    a() : y{2} {}\n    int y;\n\n  private:\n    int hidden_;\n};\n"));
        }

        [Fact]
        public void Insertions_IgnoreCommentsAndStrings()
        {
            string text = "struct [[reflgen::reflect]] a\n{\n    // private: int x;\n    const char* note = \"private: int y;\";\n};\n" +
                          "// class [[reflgen::reflect]] b { int z; };\n";

            Assert.Empty(AccessFriend.Insertions(text));
        }

        [Fact]
        public void Insertions_HandleNestedReflectedClassesSeparately()
        {
            string text = "struct outer\n{\n    int visible;\n\n    class [[reflgen::reflect]] inner\n    {\n        int secret_;\n    };\n\n" +
                          "  private:\n    int hidden_;\n};\n";

            string result = Apply(text);

            Assert.Equal("inner", AccessFriend.Insertions(text).Single().ClassName);
            Assert.Contains("    {\n        " + Friend + "\n\n        int secret_;", result);
        }

        [Fact]
        public void Insertions_KeepWindowsLineEndingsAndTabs()
        {
            string text = "class [[reflgen::reflect]] a\r\n{\r\n\tint secret_;\r\n};\r\n";

            Assert.Equal("class [[reflgen::reflect]] a\r\n{\r\n\t" + Friend + "\r\n\r\n\tint secret_;\r\n};\r\n", Apply(text));
        }

        [Fact]
        public void Insertions_ReuseABlankLineAfterTheBrace()
        {
            string text = "class [[reflgen::reflect]] a\n{\n\n    int secret_;\n};\n";

            Assert.Equal("class [[reflgen::reflect]] a\n{\n    " + Friend + "\n\n    int secret_;\n};\n", Apply(text));
        }

        [Fact]
        public void Insertions_StayOnTheLineOfAOneLineClass()
        {
            Assert.Equal("class [[reflgen::reflect]] a { " + Friend + " int secret_; };\n",
                         Apply("class [[reflgen::reflect]] a { int secret_; };\n"));
        }

        [Fact]
        public void Insertions_CoverEveryClassThatNeedsIt()
        {
            string text = "class [[reflgen::reflect]] a\n{\n    int x;\n};\n\nclass [[reflgen::reflect]] b\n{\n    int y;\n};\n";

            Assert.Equal(new[] { "a", "b" }, AccessFriend.Insertions(text).Select(insertion => insertion.ClassName));
        }
    }
}
