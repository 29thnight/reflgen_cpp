using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Xunit;

namespace Reflgen.VisualStudio.Tests
{
    // 실제 프로세스를 띄운다(Windows 전용 — 확장이 도는 곳이다).
    public sealed class ProcessRunnerTests : IDisposable
    {
        private readonly string _directory = Path.Combine(Path.GetTempPath(), "reflgen-runner-" + Guid.NewGuid().ToString("N"));

        public ProcessRunnerTests()
        {
            Directory.CreateDirectory(_directory);
        }

        public void Dispose()
        {
            Directory.Delete(_directory, recursive: true);
        }

        [Fact]
        public async Task RunAsync_ReturnsTheExitCode()
        {
            if (!OperatingSystem.IsWindows())
            {
                return;
            }

            int exitCode = await ProcessRunner.RunAsync(Command("exit 3"), TimeSpan.FromSeconds(30), CancellationToken.None);

            Assert.Equal(3, exitCode);
        }

        // MSBuild → cmd → reflgen 처럼 손자가 있는 트리. 맨 위만 끝내면 손자가 살아남아 표시 파일을 남긴다.
        [Fact]
        public async Task RunAsync_KillsTheWholeTreeOnTimeout()
        {
            if (!OperatingSystem.IsWindows())
            {
                return;
            }
            string marker = Path.Combine(_directory, "grandchild-finished");

            int exitCode = await ProcessRunner.RunAsync(Command(Grandchild(marker)), TimeSpan.FromMilliseconds(500),
                                                        CancellationToken.None);

            Assert.Equal(ProcessRunner.TimedOut, exitCode);
            await Task.Delay(TimeSpan.FromSeconds(4));
            Assert.False(File.Exists(marker), "a grandchild process survived the timeout");
        }

        [Fact]
        public async Task RunAsync_KillsTheWholeTreeWhenCanceled()
        {
            if (!OperatingSystem.IsWindows())
            {
                return;
            }
            string marker = Path.Combine(_directory, "grandchild-finished");
            using var cancellation = new CancellationTokenSource(TimeSpan.FromMilliseconds(500));

            await Assert.ThrowsAnyAsync<OperationCanceledException>(
                () => ProcessRunner.RunAsync(Command(Grandchild(marker)), TimeSpan.FromMinutes(1), cancellation.Token));

            await Task.Delay(TimeSpan.FromSeconds(4));
            Assert.False(File.Exists(marker), "a grandchild process survived the cancellation");
        }

        // 약 2초 뒤 표시 파일을 쓰는 손자 cmd 를 띄운다.
        private static string Grandchild(string marker) =>
            $"cmd /c \"ping -n 3 127.0.0.1 >nul & echo done>\"{marker}\"\"";

        private static ProcessStartInfo Command(string command) =>
            new ProcessStartInfo(Path.Combine(Environment.SystemDirectory, "cmd.exe"), "/c " + command)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
            };
    }
}
