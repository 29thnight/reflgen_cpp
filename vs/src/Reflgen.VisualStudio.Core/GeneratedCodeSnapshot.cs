using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace Reflgen.VisualStudio
{
    // 생성 코드(출력 디렉터리의 .h)의 이름과 마지막 수정 시각. 생성 전후를 견주어 IntelliSense 를 새로 고칠지
    // 정한다 — IntelliSense 는 프로젝트 밖에 있는 강제 include 파일이 바뀐 것을 스스로 알아채지 못한다.
    // 생성기는 내용이 같으면 파일을 다시 쓰지 않으므로 시각이 그대로면 코드도 그대로다.
    public sealed class GeneratedCodeSnapshot
    {
        private readonly IReadOnlyDictionary<string, DateTime> _headers;

        private GeneratedCodeSnapshot(IReadOnlyDictionary<string, DateTime> headers)
        {
            _headers = headers;
        }

        // 디렉터리가 없으면(아직 생성하지 않았으면) 빈 스냅숏이다.
        public static GeneratedCodeSnapshot Take(string? directory)
        {
            if (string.IsNullOrEmpty(directory) || !Directory.Exists(directory))
            {
                return new GeneratedCodeSnapshot(new Dictionary<string, DateTime>());
            }
            return new GeneratedCodeSnapshot(
                Directory.EnumerateFiles(directory!, "*.h")
                         .ToDictionary(path => Path.GetFileName(path), File.GetLastWriteTimeUtc,
                                       StringComparer.OrdinalIgnoreCase));
        }

        public bool Differs(GeneratedCodeSnapshot other) =>
            _headers.Count != other._headers.Count ||
            _headers.Any(entry => !other._headers.TryGetValue(entry.Key, out DateTime time) || time != entry.Value);
    }
}
