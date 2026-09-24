using System;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.ComponentModelHost;
using Microsoft.VisualStudio.Editor;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Task = System.Threading.Tasks.Task;

namespace Reflgen.VisualStudio
{
    // 솔루션이 열리면 뒤에서 올라와 저장을 지켜본다 — 저장하면 include 를 채우고, 프로젝트에 등록하고,
    // 생성기를 돌려 진단을 Error List 에 올린다. 자동완성은 MEF 쪽(AttributeCompletionSource)이 맡는다.
    [PackageRegistration(UseManagedResourcesOnly = true, AllowsBackgroundLoading = true)]
    [Guid(PackageGuidString)]
    [ProvideAutoLoad(VSConstants.UICONTEXT.SolutionExistsAndFullyLoaded_string, PackageAutoLoadFlags.BackgroundLoad)]
    [ProvideOptionPage(typeof(ReflgenOptionsPage), "reflgen", "General", 0, 0, true)]
    [ProvideBindingPath] // Reflgen.VisualStudio.Core.dll 을 확장 폴더에서 찾게 한다
    public sealed class ReflgenPackage : AsyncPackage
    {
        public const string PackageGuidString = "04027391-8415-404a-91b1-d4fee16a7657";

        private IVsRunningDocumentTable? _documents;
        private uint _documentEventsCookie;
        private IVsSolution? _solution;
        private uint _solutionEventsCookie;
        private DiagnosticsReporter? _reporter;

        protected override async Task InitializeAsync(CancellationToken cancellationToken,
                                                      IProgress<ServiceProgressData> progress)
        {
            await JoinableTaskFactory.SwitchToMainThreadAsync(cancellationToken);

            var options = (ReflgenOptionsPage)GetDialogPage(typeof(ReflgenOptionsPage));
            var componentModel = await GetServiceAsync(typeof(SComponentModel)) as IComponentModel;
            var documents = await GetServiceAsync(typeof(SVsRunningDocumentTable)) as IVsRunningDocumentTable;
            var solution = await GetServiceAsync(typeof(SVsSolution)) as IVsSolution;
            if (componentModel == null || documents == null || solution == null)
            {
                return; // 편집기가 없는 환경(명령줄 devenv 등)에서는 할 일이 없다
            }

            var projects = new ProjectBridge(this);
            _reporter = new DiagnosticsReporter(this);
            var runner = new GenerationRunner(JoinableTaskFactory, projects, _reporter, options);
            var listener = new SaveListener(documents, componentModel.GetService<IVsEditorAdaptersFactoryService>(),
                                            projects, runner, _reporter, options);
            ErrorHandler.ThrowOnFailure(documents.AdviseRunningDocTableEvents(listener, out _documentEventsCookie));
            _documents = documents;
            ErrorHandler.ThrowOnFailure(
                solution.AdviseSolutionEvents(new SolutionListener(projects, _reporter), out _solutionEventsCookie));
            _solution = solution;

            CatalogStore.Instance.SetFiles(projects.CatalogFiles());
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                // VS 는 종료할 때 UI 스레드에서 패키지를 버리므로 전환은 곧바로 끝난다.
                JoinableTaskFactory.Run(async () =>
                {
                    await JoinableTaskFactory.SwitchToMainThreadAsync();
                    if (_documents != null && _documentEventsCookie != 0)
                    {
                        _documents.UnadviseRunningDocTableEvents(_documentEventsCookie);
                        _documentEventsCookie = 0;
                    }
                    if (_solution != null && _solutionEventsCookie != 0)
                    {
                        _solution.UnadviseSolutionEvents(_solutionEventsCookie);
                        _solutionEventsCookie = 0;
                    }
                    _reporter?.Dispose();
                });
            }
            base.Dispose(disposing);
        }
    }
}
