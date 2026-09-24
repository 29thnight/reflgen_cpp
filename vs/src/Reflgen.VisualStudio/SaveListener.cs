using System;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;

namespace Reflgen.VisualStudio
{
    // 저장을 지켜본다. [[reflgen::reflect]] 가 있는(또는 있었던) header 가 저장되면 그 프로젝트의 생성을
    // 예약한다 — IntelliSense 와 Error List 가 빌드를 기다리지 않고 새 서술을 본다. header 의 내용과
    // 프로젝트 파일은 건드리지 않는다.
    internal sealed class SaveListener : IVsRunningDocTableEvents3
    {
        private readonly IVsRunningDocumentTable _documents;
        private readonly ProjectBridge _projects;
        private readonly GenerationRunner _runner;
        private readonly DiagnosticsReporter _reporter;
        private readonly ReflgenOptionsPage _options;

        public SaveListener(IVsRunningDocumentTable documents, ProjectBridge projects, GenerationRunner runner,
                            DiagnosticsReporter reporter, ReflgenOptionsPage options)
        {
            _documents = documents;
            _projects = projects;
            _runner = runner;
            _reporter = reporter;
            _options = options;
        }

        public int OnBeforeSave(uint docCookie) => VSConstants.S_OK;

        public int OnAfterSave(uint docCookie)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            IntPtr documentData = IntPtr.Zero;
            try
            {
                if (_documents.GetDocumentInfo(docCookie, out _, out _, out _, out string path,
                                               out IVsHierarchy hierarchy, out _, out documentData) ==
                    VSConstants.S_OK)
                {
                    OnHeaderSaved(path, hierarchy);
                }
            }
            // RDT 이벤트에서 새어 나간 예외는 VS 가 말없이 버린다 — 여기가 경계이므로 무엇이든 Output 창에 남긴다.
            catch (Exception exception)
            {
                _reporter.Log($"save handling failed: {exception.GetType().Name}: {exception.Message}");
            }
            finally
            {
                if (documentData != IntPtr.Zero)
                {
                    Marshal.Release(documentData);
                }
            }
            return VSConstants.S_OK;
        }

        private void OnHeaderSaved(string path, IVsHierarchy? hierarchy)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (hierarchy == null || !ReflectedHeader.IsHeaderPath(path) || !File.Exists(path))
            {
                return;
            }
            // 반영을 모두 지운 header 도 다시 생성해야 옛 서술이 주입 header 에서 빠진다.
            bool declares = ReflectedHeader.DeclaresReflection(File.ReadAllText(path));
            if (!declares && !_projects.WasGenerated(hierarchy, path))
            {
                return;
            }
            // 여기서부터는 반영하는 header 다 — 멈추는 곳마다 Output 창에 이유를 남긴다(조용히 넘기지 않는다).
            string name = Path.GetFileName(path);
            ProjectTarget? target = _projects.TargetOf(hierarchy, out string problem);
            if (target == null)
            {
                _reporter.Log($"{name}: skipped; {problem}.");
                return;
            }
            if (!_projects.UsesReflgen(hierarchy))
            {
                _reporter.Log($"{name}: skipped; {target.DisplayName} does not import reflgen.targets.");
                ReportMissingImport(target, path);
                return;
            }
            if (_options.GenerateOnSave)
            {
                _runner.Schedule(target);
            }
        }

        private void ReportMissingImport(ProjectTarget target, string header)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            _reporter.Replace(target, new[]
            {
                new CompilerDiagnostic(header, 0, 0, DiagnosticSeverity.Warning, DiagnosticsReporter.MissingImportCode,
                                       $"'{target.DisplayName}' does not import reflgen.targets, so this header is " +
                                           "not generated; add <Import Project=\"…\\reflgen\\msbuild\\reflgen.targets\" /> " +
                                           "after Microsoft.Cpp.targets (or in Directory.Build.targets)"),
            });
        }

        public int OnAfterFirstDocumentLock(uint docCookie, uint dwRDTLockType, uint dwReadLocksRemaining,
                                            uint dwEditLocksRemaining) => VSConstants.S_OK;

        public int OnBeforeLastDocumentUnlock(uint docCookie, uint dwRDTLockType, uint dwReadLocksRemaining,
                                              uint dwEditLocksRemaining) => VSConstants.S_OK;

        public int OnAfterAttributeChange(uint docCookie, uint grfAttribs) => VSConstants.S_OK;

        public int OnBeforeDocumentWindowShow(uint docCookie, int fFirstShow, IVsWindowFrame pFrame) => VSConstants.S_OK;

        public int OnAfterDocumentWindowHide(uint docCookie, IVsWindowFrame pFrame) => VSConstants.S_OK;

        public int OnAfterAttributeChangeEx(uint docCookie, uint grfAttribs, IVsHierarchy pHierOld, uint itemidOld,
                                            string pszMkDocumentOld, IVsHierarchy pHierNew, uint itemidNew,
                                            string pszMkDocumentNew) => VSConstants.S_OK;
    }
}
