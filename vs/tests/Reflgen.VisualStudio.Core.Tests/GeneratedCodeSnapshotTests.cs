using System;
using System.IO;
using Xunit;

namespace Reflgen.VisualStudio.Tests
{
    public sealed class GeneratedCodeSnapshotTests : IDisposable
    {
        private readonly string _directory = Path.Combine(Path.GetTempPath(), "reflgen-snapshot-" + Guid.NewGuid().ToString("N"));

        public GeneratedCodeSnapshotTests()
        {
            Directory.CreateDirectory(_directory);
            Write("reflgen_game.h", "#pragma once");
            Write("player.reflgen.h", "// player");
        }

        public void Dispose()
        {
            Directory.Delete(_directory, recursive: true);
        }

        [Fact]
        public void Differs_IsFalseWhenNothingChanged()
        {
            GeneratedCodeSnapshot before = GeneratedCodeSnapshot.Take(_directory);

            Assert.False(GeneratedCodeSnapshot.Take(_directory).Differs(before));
        }

        [Fact]
        public void Differs_SeesARewrittenHeader()
        {
            GeneratedCodeSnapshot before = GeneratedCodeSnapshot.Take(_directory);

            File.SetLastWriteTimeUtc(Path.Combine(_directory, "player.reflgen.h"), DateTime.UtcNow.AddMinutes(1));

            Assert.True(GeneratedCodeSnapshot.Take(_directory).Differs(before));
        }

        [Fact]
        public void Differs_SeesAddedAndRemovedHeaders()
        {
            GeneratedCodeSnapshot before = GeneratedCodeSnapshot.Take(_directory);

            Write("item.reflgen.h", "// item");
            GeneratedCodeSnapshot added = GeneratedCodeSnapshot.Take(_directory);
            File.Delete(Path.Combine(_directory, "player.reflgen.h"));

            Assert.True(added.Differs(before));
            Assert.True(GeneratedCodeSnapshot.Take(_directory).Differs(added));
        }

        // 생성할 때마다 바뀌는 stamp·응답 파일은 코드가 아니다 — 그것만 바뀌면 IntelliSense 를 새로 고칠 까닭이 없다.
        [Fact]
        public void Differs_IgnoresFilesThatAreNotHeaders()
        {
            GeneratedCodeSnapshot before = GeneratedCodeSnapshot.Take(_directory);

            Write("reflgen_game.stamp", string.Empty);
            Write("reflgen_game.rsp", "--module\ngame");

            Assert.False(GeneratedCodeSnapshot.Take(_directory).Differs(before));
        }

        [Fact]
        public void Take_TreatsAMissingDirectoryAsEmpty()
        {
            GeneratedCodeSnapshot missing = GeneratedCodeSnapshot.Take(Path.Combine(_directory, "absent"));

            Assert.False(missing.Differs(GeneratedCodeSnapshot.Take(null)));
            Assert.True(GeneratedCodeSnapshot.Take(_directory).Differs(missing));
        }

        private void Write(string name, string text) => File.WriteAllText(Path.Combine(_directory, name), text);
    }
}
