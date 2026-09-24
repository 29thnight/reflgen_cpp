using System;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Editor;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.TextManager.Interop;

namespace Reflgen.VisualStudio
{
    // 저장을 지켜본다.
    //   저장 직전: [[reflgen::reflect]] 가 있는데 생성 파일 include 가 없으면 파일 끝에 넣는다 — 저장되는
    //              내용에 함께 들어가도록 버퍼를 고친다.
    //   저장 직후: header 를 프로젝트에 등록(ReflgenGenerate=true)하고 그 프로젝트의 생성을 예약한다.
    internal sealed class SaveListener : IVsRunningDocTableEvents3
    {
        private readonly IVsRunningDocumentTable _documents;
        private readonly IVsEditorAdaptersFactoryService _adapters;
        private readonly ProjectBridge _projects;
        private readonly GenerationRunner _runner;
        private readonly DiagnosticsReporter _reporter;
        private readonly ReflgenOptionsPage _options;

        public SaveListener(IVsRunningDocumentTable documents, IVsEditorAdaptersFactoryService adapters,
                            ProjectBridge projects, GenerationRunner runner, DiagnosticsReporter reporter,
                            ReflgenOptionsPage options)
        {
            _documents = documents;
            _adapters = adapters;
            _projects = projects;
            _runner = runner;
            _reporter = reporter;
            _options = options;
        }

        public int OnBeforeSave(uint docCookie)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (!_options.AddIncludeOnSave)
            {
                return VSConstants.S_OK;
            }
            IntPtr documentData = IntPtr.Zero;
            try
            {
                if (_documents.GetDocumentInfo(docCookie, out _, out _, out _, out string path, out _, out _,
                                               out documentData) != VSConstants.S_OK ||
                    !ReflectedHeader.IsHeaderPath(path) || documentData == IntPtr.Zero ||
                    Marshal.GetObjectForIUnknown(documentData) is not IVsTextBuffer textBuffer)
                {
                    return VSConstants.S_OK;
                }
                ITextBuffer? buffer = _adapters.GetDataBuffer(textBuffer);
                TextInsertion? insertion =
                    buffer == null ? null : ReflectedHeader.IncludeInsertion(buffer.CurrentSnapshot.GetText(), path);
                if (buffer != null && insertion != null)
                {
                    using ITextEdit edit = buffer.CreateEdit();
                    edit.Insert(insertion.Position, insertion.Text);
                    edit.Apply();
                    _reporter.Log($"{Path.GetFileName(path)}: added #include \"{ReflectedHeader.GeneratedHeaderName(path)}\".");
                }
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

        public int OnAfterSave(uint docCookie)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            IntPtr documentData = IntPtr.Zero;
            try
            {
                if (_documents.GetDocumentInfo(docCookie, out _, out _, out _, out string path,
                                               out IVsHierarchy hierarchy, out uint itemId, out documentData) ==
                    VSConstants.S_OK)
                {
                    OnHeaderSaved(path, hierarchy, itemId);
                }
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

        private void OnHeaderSaved(string path, IVsHierarchy? hierarchy, uint itemId)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (hierarchy == null || !ReflectedHeader.IsHeaderPath(path) || !File.Exists(path))
            {
                return;
            }
            bool declares = ReflectedHeader.DeclaresReflection(File.ReadAllText(path));
            bool registered = _projects.IsRegistered(hierarchy, itemId);
            if (!declares && !registered)
            {
                return;
            }
            ProjectTarget? target = _projects.TargetOf(hierarchy);
            if (target == null)
            {
                return;
            }
            if (!_projects.UsesReflgen(hierarchy))
            {
                ReportMissingImport(target, path);
                return;
            }
            if (declares && !registered && _options.RegisterOnSave && _projects.Register(hierarchy, itemId))
            {
                registered = true;
                _reporter.Log($"{Path.GetFileName(path)}: registered in {target.DisplayName} (ReflgenGenerate=true).");
            }
            // 등록된 header 는 반영을 모두 지웠어도 다시 생성해야 옛 서술이 남지 않는다.
            if (registered && _options.GenerateOnSave)
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
