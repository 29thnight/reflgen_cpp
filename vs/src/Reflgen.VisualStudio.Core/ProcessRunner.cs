using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace Reflgen.VisualStudio
{
    // 외부 프로세스를 기다린다. 시간을 넘기거나 취소되면 자식까지 트리째 끝낸다 — MSBuild 의 Exec 는 cmd.exe 를
    // 거쳐 reflgen.exe 를 띄우므로 맨 위 프로세스만 끝내면 자식이 남아 생성 파일을 붙잡는다(.NET Framework 의
    // Process.Kill 은 트리를 모른다).
    public static class ProcessRunner
    {
        public const int TimedOut = -1;

        // 끝나면 종료 코드, maximumDuration 을 넘기면 TimedOut. 취소되면 OperationCanceledException.
        public static async Task<int> RunAsync(ProcessStartInfo start, TimeSpan maximumDuration, CancellationToken token)
        {
            using var process = new Process { StartInfo = start, EnableRaisingEvents = true };
            var exited = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
            process.Exited += (_, _) => exited.TrySetResult(0);
            process.Start();
            using var deadline = CancellationTokenSource.CreateLinkedTokenSource(token);
            deadline.CancelAfter(maximumDuration);
            Task finished = await Task.WhenAny(exited.Task, Task.Delay(Timeout.Infinite, deadline.Token)).ConfigureAwait(false);
            if (finished == exited.Task)
            {
                return process.ExitCode;
            }
            KillTree(process);
            token.ThrowIfCancellationRequested();
            return TimedOut;
        }

        // 이미 끝난 프로세스와의 경합에서 나는 예외는 무시한다 — 끝내려던 것이 이미 끝났다.
        public static void KillTree(Process process)
        {
            try
            {
                var start = new ProcessStartInfo(Path.Combine(Environment.SystemDirectory, "taskkill.exe"),
                                                 $"/T /F /PID {process.Id}")
                {
                    UseShellExecute = false,
                    CreateNoWindow = true,
                };
                using Process? taskkill = Process.Start(start);
                taskkill?.WaitForExit(10_000);
            }
            catch (Exception exception) when (exception is InvalidOperationException || exception is Win32Exception)
            {
                // 이미 끝났거나 taskkill 을 띄울 수 없다 — 아래에서 한 번 더 시도한다.
            }
            try
            {
                if (!process.HasExited)
                {
                    process.Kill();
                }
            }
            catch (Exception exception) when (exception is InvalidOperationException || exception is Win32Exception)
            {
                // 그사이 끝났다.
            }
        }
    }
}
