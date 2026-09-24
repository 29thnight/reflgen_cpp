using System;
using System.Linq;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;

namespace Reflgen.VisualStudio
{
    // 솔루션이 바뀌면 자동완성 카탈로그를 다시 모은다. 패키지는 VS 가 떠 있는 동안 살아 있으므로, 이것이
    // 없으면 솔루션을 바꿔 연 뒤에도 앞 솔루션의 attribute 가 제안된다. 닫을 때는 Error List 항목도 치운다.
    // 한 번도 생성하지 않은 프로젝트는 열 때 생성한다 — 그러지 않으면 IntelliSense 가 빈 주입 header 만 본다.
    internal sealed class SolutionListener : IVsSolutionEvents
    {
        private readonly ProjectBridge _projects;
        private readonly GenerationRunner _runner;
        private readonly DiagnosticsReporter _reporter;
        private readonly ReflgenOptionsPage _options;

        public SolutionListener(ProjectBridge projects, GenerationRunner runner, DiagnosticsReporter reporter,
                                ReflgenOptionsPage options)
        {
            _projects = projects;
            _runner = runner;
            _reporter = reporter;
            _options = options;
        }

        // 패키지는 솔루션이 열린 뒤에 올라올 수 있다 — 그때 이미 열린 프로젝트는 패키지가 직접 부른다.
        public void GenerateNeverGenerated()
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (!_options.GenerateOnSave)
            {
                return;
            }
            // 솔루션 이벤트에서 새어 나간 예외는 VS 가 말없이 버리고 다른 수신자까지 흔든다 — 여기가 경계다.
            // 주입 header 를 생성기가 쓰는 중이면(동시에 도는 빌드) 읽기가 실패할 수 있다.
            try
            {
                foreach (IVsHierarchy project in _projects.ReflgenProjects())
                {
                    if (!_projects.NeedsFirstGeneration(project))
                    {
                        continue;
                    }
                    ProjectTarget? target = _projects.TargetOf(project, out string problem);
                    if (target == null)
                    {
                        _reporter.Log($"first generation skipped; {problem}.");
                        continue;
                    }
                    _runner.Schedule(target);
                }
            }
            catch (Exception exception)
            {
                _reporter.Log($"first generation failed: {exception.GetType().Name}: {exception.Message}");
            }
        }

        public int OnAfterOpenSolution(object pUnkReserved, int fNewSolution)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            CatalogStore.Instance.SetFiles(_projects.CatalogFiles());
            GenerateNeverGenerated();
            return VSConstants.S_OK;
        }

        public int OnAfterOpenProject(IVsHierarchy pHierarchy, int fAdded)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            CatalogStore.Instance.SetFiles(_projects.CatalogFiles());
            if (fAdded != 0) // 솔루션을 여는 중이면 OnAfterOpenSolution 이 한꺼번에 한다
            {
                GenerateNeverGenerated();
            }
            return VSConstants.S_OK;
        }

        public int OnAfterLoadProject(IVsHierarchy pStubHierarchy, IVsHierarchy pRealHierarchy)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            CatalogStore.Instance.SetFiles(_projects.CatalogFiles());
            GenerateNeverGenerated();
            return VSConstants.S_OK;
        }

        public int OnAfterCloseSolution(object pUnkReserved)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            CatalogStore.Instance.SetFiles(Enumerable.Empty<string>());
            _reporter.ClearAll();
            return VSConstants.S_OK;
        }

        public int OnQueryCloseProject(IVsHierarchy pHierarchy, int fRemoving, ref int pfCancel) => VSConstants.S_OK;

        public int OnBeforeCloseProject(IVsHierarchy pHierarchy, int fRemoved) => VSConstants.S_OK;

        public int OnQueryUnloadProject(IVsHierarchy pRealHierarchy, ref int pfCancel) => VSConstants.S_OK;

        public int OnBeforeUnloadProject(IVsHierarchy pRealHierarchy, IVsHierarchy pStubHierarchy) => VSConstants.S_OK;

        public int OnQueryCloseSolution(object pUnkReserved, ref int pfCancel) => VSConstants.S_OK;

        public int OnBeforeCloseSolution(object pUnkReserved) => VSConstants.S_OK;
    }
}
