using System;
using System.Collections.Generic;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;

namespace Reflgen.VisualStudio
{
    // Error List 와 Output 창의 "reflgen" 창. 프로젝트마다 마지막 생성 결과만 남긴다 — 다시 생성하면 그
    // 프로젝트의 이전 항목을 지운다. UI 스레드에서 부른다.
    internal sealed class DiagnosticsReporter : IDisposable
    {
        // 확장 자신의 진단 코드(생성기 코드 RG0001~ 과 겹치지 않게 RG09xx 를 쓴다).
        public const string MissingImportCode = "RG0901";
        public const string MSBuildFailedCode = "RG0902";

        private static readonly Guid PaneGuid = new Guid("953af35b-3c15-4b52-b7cf-77d93529382f");

        private readonly IServiceProvider _services;
        private readonly ErrorListProvider _errors;
        private readonly Dictionary<string, List<ErrorTask>> _tasksByProject =
            new Dictionary<string, List<ErrorTask>>(StringComparer.OrdinalIgnoreCase);
        private IVsOutputWindowPane? _pane;

        public DiagnosticsReporter(IServiceProvider services)
        {
            _services = services;
            _errors = new ErrorListProvider(services)
            {
                ProviderName = "reflgen",
                ProviderGuid = new Guid("76845a00-8550-4f54-b24f-491934ccb71f"),
            };
        }

        public void Replace(ProjectTarget target, IReadOnlyList<CompilerDiagnostic> diagnostics)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            Clear(target.ProjectFile);
            var tasks = new List<ErrorTask>();
            foreach (CompilerDiagnostic diagnostic in diagnostics)
            {
                tasks.Add(CreateTask(diagnostic, target));
            }
            _errors.SuspendRefresh();
            try
            {
                foreach (ErrorTask task in tasks)
                {
                    _errors.Tasks.Add(task);
                }
            }
            finally
            {
                _errors.ResumeRefresh();
            }
            _tasksByProject[target.ProjectFile] = tasks;
        }

        public void Clear(string projectFile)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (_tasksByProject.TryGetValue(projectFile, out List<ErrorTask> previous))
            {
                foreach (ErrorTask task in previous)
                {
                    _errors.Tasks.Remove(task);
                }
                _tasksByProject.Remove(projectFile);
            }
        }

        public void ClearAll()
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            foreach (string projectFile in new List<string>(_tasksByProject.Keys))
            {
                Clear(projectFile);
            }
        }

        public void Log(string message)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (_pane == null)
            {
                var window = (IVsOutputWindow)_services.GetService(typeof(SVsOutputWindow));
                Guid guid = PaneGuid;
                if (window == null || window.CreatePane(ref guid, "reflgen", 1, 1) != VSConstants.S_OK ||
                    window.GetPane(ref guid, out _pane) != VSConstants.S_OK)
                {
                    return;
                }
                // 만들 때 한 번만 Output 창의 선택을 이 창으로 돌린다 — 그러지 않으면 드롭다운에서 골라야 보인다.
                // 이후 로그는 사용자가 고른 창(빌드 등)을 빼앗지 않는다. Output 창 자체를 열지는 않는다.
                _pane.Activate();
            }
            _pane.OutputStringThreadSafe(message + Environment.NewLine);
        }

        public void Dispose()
        {
            _errors.Dispose();
        }

        private ErrorTask CreateTask(CompilerDiagnostic diagnostic, ProjectTarget target)
        {
            var task = new ErrorTask
            {
                Category = TaskCategory.BuildCompile,
                ErrorCategory = diagnostic.Severity == DiagnosticSeverity.Error ? TaskErrorCategory.Error
                                                                                 : TaskErrorCategory.Warning,
                Text = diagnostic.Code + ": " + diagnostic.Message,
                Document = diagnostic.File ?? target.ProjectFile,
                Line = Math.Max(0, diagnostic.Line - 1), // Error List 는 0부터 센다
                Column = Math.Max(0, diagnostic.Column - 1),
                HierarchyItem = target.Hierarchy,
            };
            task.Navigate += (sender, _) =>
            {
                ThreadHelper.ThrowIfNotOnUIThread();
                // ErrorListProvider.Navigate 는 줄을 1부터로 보고 하나를 뺀다 — 잠시 더해 맞춘다.
                var item = (ErrorTask)sender;
                ++item.Line;
                _errors.Navigate(item, VSConstants.LOGVIEWID_Code);
                --item.Line;
            };
            return task;
        }
    }
}
