using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace Reflgen.VisualStudio
{
    // 솔루션의 reflgen 프로젝트들이 내놓은 attribute 카탈로그를 합쳐 둔다. 자동완성은 백그라운드 스레드에서도
    // 읽으므로 잠금으로 지킨다. 파일 시각이 바뀌면 다시 읽는다(생성이 끝나면 새 카탈로그가 생긴다).
    internal sealed class CatalogStore
    {
        public static CatalogStore Instance { get; } = new CatalogStore();

        private readonly object _lock = new object();
        private readonly Dictionary<string, (DateTime Written, AttributeCatalog Catalog)> _loaded =
            new Dictionary<string, (DateTime, AttributeCatalog)>(StringComparer.OrdinalIgnoreCase);
        private IReadOnlyList<string> _files = Array.Empty<string>();
        private AttributeCatalog _merged = AttributeCatalog.BuiltIn;

        public AttributeCatalog Current
        {
            get
            {
                lock (_lock)
                {
                    if (_files.Any(file => Stamp(file) != (_loaded.TryGetValue(file, out var entry) ? entry.Written : DateTime.MinValue)))
                    {
                        Rebuild();
                    }
                    return _merged;
                }
            }
        }

        public void SetFiles(IEnumerable<string> files)
        {
            lock (_lock)
            {
                _files = files.Distinct(StringComparer.OrdinalIgnoreCase).ToList();
                Rebuild();
            }
        }

        // 프로젝트 카탈로그가 앞에 온다 — 내장 목록은 한 번도 생성하지 않은 경우를 채울 뿐이다.
        private void Rebuild()
        {
            var catalogs = new List<AttributeCatalog>();
            foreach (string file in _files)
            {
                DateTime written = Stamp(file);
                if (written == DateTime.MinValue)
                {
                    continue;
                }
                if (!_loaded.TryGetValue(file, out var entry) || entry.Written != written)
                {
                    entry = (written, Read(file));
                    _loaded[file] = entry;
                }
                catalogs.Add(entry.Catalog);
            }
            catalogs.Add(AttributeCatalog.BuiltIn);
            _merged = AttributeCatalog.Merge(catalogs);
        }

        private static DateTime Stamp(string file) => File.Exists(file) ? File.GetLastWriteTimeUtc(file) : DateTime.MinValue;

        private static AttributeCatalog Read(string file)
        {
            try
            {
                return AttributeCatalog.Parse(File.ReadAllText(file, Encoding.UTF8));
            }
            catch (IOException)
            {
                return AttributeCatalog.Empty; // 생성기가 쓰는 중이면 다음 요청에 다시 읽는다
            }
        }
    }
}
