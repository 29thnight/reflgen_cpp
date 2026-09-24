using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Threading;
using Task = System.Threading.Tasks.Task;

namespace Reflgen.VisualStudio
{
    // 저장한 header 의 프로젝트에서 ReflgenGenerate target 만 따로 돌린다. VS 빌드와 섞이지 않게 별도 MSBuild
    // 프로세스로 돌린다. 같은 프로젝트를 다시 저장하면 기다리던 것이든 이미 돌던 것이든 이전 생성은 버리고
    // 최신 내용으로 한 번 돈다. MSBuild 는 한 번에 하나만 돈다.
    internal sealed class GenerationRunner
    {
        private static readonly TimeSpan Settle = TimeSpan.FromMilliseconds(400);
        private static readonly TimeSpan MaximumDuration = TimeSpan.FromMinutes(5);

        private readonly JoinableTaskFactory _joinableTasks;
        private readonly ProjectBridge _projects;
        private readonly DiagnosticsReporter _reporter;
        private readonly ReflgenOptionsPage _options;
        // UI 스레드에서만 만진다. 끝난 생성은 자기 항목을 지우고 CancellationTokenSource 를 버린다.
        private readonly Dictionary<string, CancellationTokenSource> _pending =
            new Dictionary<string, CancellationTokenSource>(StringComparer.OrdinalIgnoreCase);
        private readonly SemaphoreSlim _oneAtATime = new SemaphoreSlim(1, 1);
        // UI 스레드에서만 만진다. 생성 코드가 바뀐 생성이 있었으면, 기다리는 생성이 모두 끝난 뒤 한 번만 새로 고친다
        // (솔루션을 열 때 여러 프로젝트가 잇달아 생성된다).
        private bool _generatedCodeChanged;

        public GenerationRunner(JoinableTaskFactory joinableTasks, ProjectBridge projects, DiagnosticsReporter reporter,
                                ReflgenOptionsPage options)
        {
            _joinableTasks = joinableTasks;
            _projects = projects;
            _reporter = reporter;
            _options = options;
        }

        public void Schedule(ProjectTarget target)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (_pending.TryGetValue(target.ProjectFile, out CancellationTokenSource previous))
            {
                previous.Cancel();
            }
            var cancellation = new CancellationTokenSource();
            _pending[target.ProjectFile] = cancellation;
            _joinableTasks.RunAsync(() => RunAsync(target, cancellation)).FileAndForget("reflgen/generate");
        }

        private async Task RunAsync(ProjectTarget target, CancellationTokenSource cancellation)
        {
            CancellationToken token = cancellation.Token;
            bool entered = false;
            try
            {
                await Task.Delay(Settle, token);
                await _oneAtATime.WaitAsync(token);
                entered = true;
                await GenerateAsync(target, token);
            }
            catch (OperationCanceledException) when (token.IsCancellationRequested)
            {
                // 더 나중의 저장이 대신 돈다.
            }
            catch (Exception exception) when (!(exception is OperationCanceledException))
            {
                await _joinableTasks.SwitchToMainThreadAsync();
                ReportFailure(target, "generating reflection failed: " + exception.Message);
            }
            finally
            {
                if (entered)
                {
                    _oneAtATime.Release();
                }
                await _joinableTasks.SwitchToMainThreadAsync();
                if (_pending.TryGetValue(target.ProjectFile, out CancellationTokenSource current) && current == cancellation)
                {
                    _pending.Remove(target.ProjectFile);
                }
                cancellation.Dispose(); // Schedule 은 _pending 에 있는 것만 취소하므로 이제 아무도 쓰지 않는다
                if (_pending.Count == 0 && _generatedCodeChanged)
                {
                    _generatedCodeChanged = false;
                    RefreshIntelliSense();
                }
            }
        }

        private void RefreshIntelliSense()
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (!_options.RefreshIntelliSense)
            {
                return;
            }
            _reporter.Log(_projects.RefreshIntelliSense(out string problem)
                              ? "The generated code changed; refreshing IntelliSense (Project > Rescan Solution)."
                              : $"The generated code changed but IntelliSense could not be refreshed; {problem}. " +
                                    "Use Project > Rescan Solution.");
        }

        private async Task GenerateAsync(ProjectTarget target, CancellationToken token)
        {
            await _joinableTasks.SwitchToMainThreadAsync(token);
            if (_projects.IsBuildRunning())
            {
                _reporter.Log($"{target.DisplayName}: skipped; a build is running and will generate instead.");
                return;
            }
            string? msbuild = string.IsNullOrWhiteSpace(_options.MSBuildPath) ? _projects.BundledMSBuild()
                                                                                : _options.MSBuildPath;
            if (msbuild == null || !File.Exists(msbuild))
            {
                ReportFailure(target, $"MSBuild.exe was not found ('{msbuild}'); set it in Tools > Options > reflgen");
                return;
            }
            string? solution = _projects.SolutionFile();
            string? outputDirectory = _projects.OutputDirectory(target.Hierarchy);
            _reporter.Log($"{target.DisplayName} ({target.Configuration}|{target.Platform}): generating...");

            await TaskScheduler.Default;
            GeneratedCodeSnapshot before = GeneratedCodeSnapshot.Take(outputDirectory);
            MSBuildResult result = await RunMSBuildAsync(msbuild, target, solution, token);
            bool changed = GeneratedCodeSnapshot.Take(outputDirectory).Differs(before);

            await _joinableTasks.SwitchToMainThreadAsync(token);
            ReportResult(target, result);
            CatalogStore.Instance.SetFiles(_projects.CatalogFiles());
            _generatedCodeChanged |= changed;
        }

        private void ReportResult(ProjectTarget target, MSBuildResult result)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            var diagnostics = DiagnosticParser.Parse(result.Log).ToList();
            if (result.ExitCode != 0 && !diagnostics.Any(item => item.Severity == DiagnosticSeverity.Error))
            {
                // 형식을 알아볼 수 없는 실패(MSBuild 자체 오류, 시간 초과 등)도 조용히 넘기지 않는다.
                diagnostics.Add(FailureDiagnostic(
                    target, result.ExitCode == ProcessRunner.TimedOut
                                ? $"generating reflection took longer than {MaximumDuration.TotalMinutes:0} minutes and was stopped"
                                : $"generating reflection failed (MSBuild exit code {result.ExitCode}); see the reflgen output pane"));
            }
            _reporter.Replace(target, diagnostics);
            foreach (string line in result.Log.Where(line => line.Trim().Length > 0))
            {
                _reporter.Log("  " + line.Trim());
            }
            int errors = diagnostics.Count(item => item.Severity == DiagnosticSeverity.Error);
            int warnings = diagnostics.Count - errors;
            _reporter.Log($"{target.DisplayName}: {(errors == 0 ? "done" : "failed")}, {errors} error(s), {warnings} warning(s).");
        }

        private void ReportFailure(ProjectTarget target, string message)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            _reporter.Replace(target, new[] { FailureDiagnostic(target, message) });
            _reporter.Log($"{target.DisplayName}: {message}");
        }

        private static CompilerDiagnostic FailureDiagnostic(ProjectTarget target, string message) =>
            new CompilerDiagnostic(target.ProjectFile, 0, 0, DiagnosticSeverity.Error, DiagnosticsReporter.MSBuildFailedCode,
                                   message);

        private static async Task<MSBuildResult> RunMSBuildAsync(string msbuild, ProjectTarget target, string? solution,
                                                                 CancellationToken token)
        {
            string log = Path.Combine(Path.GetTempPath(), "reflgen-" + Guid.NewGuid().ToString("N") + ".log");
            var start = new ProcessStartInfo(msbuild, MSBuildCommand.Arguments(target.ProjectFile, target.Configuration,
                                                                                target.Platform, solution, log))
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                WorkingDirectory = Path.GetDirectoryName(target.ProjectFile),
            };
            try
            {
                int exitCode = await ProcessRunner.RunAsync(start, MaximumDuration, token);
                string[] lines = File.Exists(log) ? File.ReadAllLines(log, Encoding.UTF8) : Array.Empty<string>();
                return new MSBuildResult(exitCode, lines);
            }
            catch (Exception exception) when (exception is IOException || exception is Win32Exception)
            {
                return new MSBuildResult(-1, new[] { "reflgen : error RG0902: could not run MSBuild: " + exception.Message });
            }
            finally
            {
                try
                {
                    File.Delete(log);
                }
                catch (IOException)
                {
                    // 임시 파일이라 남아도 다음 실행에 영향이 없다.
                }
            }
        }

        private sealed record MSBuildResult(int ExitCode, IReadOnlyList<string> Log);
    }
}
