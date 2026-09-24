using System.Linq;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;

namespace Reflgen.VisualStudio
{
    // 솔루션이 바뀌면 자동완성 카탈로그를 다시 모은다. 패키지는 VS 가 떠 있는 동안 살아 있으므로, 이것이
    // 없으면 솔루션을 바꿔 연 뒤에도 앞 솔루션의 attribute 가 제안된다. 닫을 때는 Error List 항목도 치운다.
    internal sealed class SolutionListener : IVsSolutionEvents
    {
        private readonly ProjectBridge _projects;
        private readonly DiagnosticsReporter _reporter;

        public SolutionListener(ProjectBridge projects, DiagnosticsReporter reporter)
        {
            _projects = projects;
            _reporter = reporter;
        }

        public int OnAfterOpenSolution(object pUnkReserved, int fNewSolution)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            CatalogStore.Instance.SetFiles(_projects.CatalogFiles());
            return VSConstants.S_OK;
        }

        public int OnAfterOpenProject(IVsHierarchy pHierarchy, int fAdded)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            CatalogStore.Instance.SetFiles(_projects.CatalogFiles());
            return VSConstants.S_OK;
        }

        public int OnAfterLoadProject(IVsHierarchy pStubHierarchy, IVsHierarchy pRealHierarchy)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            CatalogStore.Instance.SetFiles(_projects.CatalogFiles());
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
